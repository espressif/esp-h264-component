/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_h264_alloc.h"
#include "esp_h264_enc_dual.h"
#include "esp_h264_enc_dual_hw.h"
#include "driver/ppa.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pattern_input.h"

static const char *TAG = "dual_ppa";

#define SRC_W          CONFIG_EXAMPLE_SRC_WIDTH
#define SRC_H          CONFIG_EXAMPLE_SRC_HEIGHT
#define SCALE_NUM      CONFIG_EXAMPLE_PPA_SCALE_NUM
#define SCALE_DEN      CONFIG_EXAMPLE_PPA_SCALE_DEN
#define DST_W          ((SRC_W) * (SCALE_NUM) / (SCALE_DEN))
#define DST_H          ((SRC_H) * (SCALE_NUM) / (SCALE_DEN))
#define GEN_SLOTS      2
#ifndef EXAMPLE_GEN_REALTIME
#if CONFIG_EXAMPLE_GEN_REALTIME
#define EXAMPLE_GEN_REALTIME 1
#else
#define EXAMPLE_GEN_REALTIME 0
#endif
#endif
#ifndef EXAMPLE_PPA_PARALLEL
#if CONFIG_EXAMPLE_PPA_PARALLEL
#define EXAMPLE_PPA_PARALLEL 1
#else
#define EXAMPLE_PPA_PARALLEL 0
#endif
#endif
#define GEN_CORE       1
#define PPA_CORE       1
#define ENC_CORE       0
#define GEN_TASK_STACK 4096
#define PPA_TASK_STACK 4096
#define ENC_TASK_STACK 8192
#define GEN_TASK_PRIO  5
#define PPA_TASK_PRIO  6
#define ENC_TASK_PRIO  6
/* PPA SRM fractional scale is 1/16. YUV420 output forces an even fragment.
 * Same truncation as ppa_do_scale_rotate_mirror() in esp_driver_ppa. */
#define PPA_SRM_SCALE_FRAG_MAX 16

static uint8_t *s_src[GEN_SLOTS];
static uint8_t *s_dst[GEN_SLOTS];
static uint32_t s_src_len;
static uint32_t s_dst_len;
static QueueHandle_t s_free_q;
static QueueHandle_t s_ppa_q;
static QueueHandle_t s_enc_q;
static volatile bool s_gen_run;
static volatile bool s_ppa_run;
static TaskHandle_t s_gen_task;
static TaskHandle_t s_ppa_task;
static TaskHandle_t s_enc_task;
static volatile bool s_enc_done;

static ppa_client_handle_t s_ppa;
static uint8_t *s_out0;
static uint8_t *s_out1;
static uint32_t s_out0_len;
static uint32_t s_out1_len;
static esp_h264_enc_dual_handle_t s_enc;
static ppa_srm_oper_config_t s_srm;

static uint8_t *s_alloc_frame(uint32_t need, uint32_t *out_len)
{
    uint32_t actual = 0;
    uint8_t *buf = esp_h264_aligned_calloc(16, 1, need, &actual, ESP_H264_MEM_SPIRAM);
    if (buf == NULL) {
        buf = esp_h264_aligned_calloc(16, 1, need, &actual, ESP_H264_MEM_INTERNAL);
    }
    if (out_len) {
        *out_len = actual;
    }
    return buf;
}

static void fill_enc_cfg(esp_h264_enc_cfg_t *cfg, uint16_t w, uint16_t h)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    cfg->gop = CONFIG_EXAMPLE_ENC_GOP;
    cfg->fps = CONFIG_EXAMPLE_ENC_FPS;
    cfg->res.width = w;
    cfg->res.height = h;
    cfg->rc.bitrate = (uint32_t)w * h * CONFIG_EXAMPLE_ENC_FPS / 20;
    cfg->rc.qp_min = 26;
    cfg->rc.qp_max = 36;
}

static BaseType_t create_pinned(TaskFunction_t fn, const char *name, uint32_t stack, UBaseType_t prio,
                                TaskHandle_t *out, BaseType_t core)
{
#if CONFIG_FREERTOS_UNICORE
    (void)core;
    return xTaskCreate(fn, name, stack, NULL, prio, out);
#else
    return xTaskCreatePinnedToCore(fn, name, stack, NULL, prio, out, core);
#endif
}

static void gen_task(void *arg)
{
    (void)arg;
    uint16_t phase = 0;
    uint8_t filled = 0;
    QueueHandle_t out_q = EXAMPLE_PPA_PARALLEL ? s_ppa_q : s_enc_q;
    ESP_LOGI(TAG, "generator task on core %d realtime=%d parallel=%d", xPortGetCoreID(),
             EXAMPLE_GEN_REALTIME, EXAMPLE_PPA_PARALLEL);
    while (s_gen_run) {
        uint8_t slot = 0;
        if (xQueueReceive(s_free_q, &slot, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue;
        }
        if (!s_gen_run) {
            xQueueSend(s_free_q, &slot, 0);
            break;
        }
        bool do_fill = EXAMPLE_GEN_REALTIME || (filled < GEN_SLOTS);
        if (do_fill) {
            pattern_fill_ouev_colorbar(s_src[slot], SRC_W, SRC_H, phase, 0);
            phase = (uint16_t)((phase + 2) % SRC_W);
            if (pattern_cache_writeback(s_src[slot], s_src_len) != ESP_OK) {
                ESP_LOGE(TAG, "cache writeback failed slot=%u", slot);
                xQueueSend(s_free_q, &slot, 0);
                continue;
            }
            if (!EXAMPLE_GEN_REALTIME) {
                filled++;
                if (filled == GEN_SLOTS) {
                    ESP_LOGI(TAG, "filled %d frames, recycle without refill", GEN_SLOTS);
                }
            }
        }
        xQueueSend(out_q, &slot, portMAX_DELAY);
    }
    ESP_LOGI(TAG, "generator task exit");
    s_gen_task = NULL;
    vTaskDelete(NULL);
}

static void gen_stop(void)
{
    s_gen_run = false;
    uint8_t slot = 0;
    if (s_ppa_q) {
        while (xQueueReceive(s_ppa_q, &slot, 0) == pdTRUE) {
            xQueueSend(s_free_q, &slot, 0);
        }
    }
    if (s_enc_q) {
        while (xQueueReceive(s_enc_q, &slot, 0) == pdTRUE) {
            xQueueSend(s_free_q, &slot, 0);
        }
    }
    xQueueSend(s_free_q, &slot, 0);
    while (s_gen_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void ppa_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "PPA task on core %d", xPortGetCoreID());

    int64_t win_t0 = esp_timer_get_time();
    uint32_t win_frames = 0;
    uint32_t total_frames = 0;

    while (s_ppa_run) {
        uint8_t slot = 0;
        if (xQueueReceive(s_ppa_q, &slot, pdMS_TO_TICKS(200)) != pdTRUE) {
            if (!s_ppa_run) {
                break;
            }
            ESP_LOGW(TAG, "PPA wait generator timeout");
            continue;
        }
        if (!s_ppa_run) {
            xQueueSend(s_free_q, &slot, portMAX_DELAY);
            break;
        }

        s_srm.in.buffer = s_src[slot];
        s_srm.out.buffer = s_dst[slot];
        if (ppa_do_scale_rotate_mirror(s_ppa, &s_srm) != ESP_OK) {
            ESP_LOGE(TAG, "PPA SRM failed slot=%u", slot);
            xQueueSend(s_free_q, &slot, portMAX_DELAY);
            break;
        }
        if (pattern_cache_invalidate(s_dst[slot], s_dst_len) != ESP_OK) {
            ESP_LOGE(TAG, "PPA cache invalidate failed slot=%u", slot);
            xQueueSend(s_free_q, &slot, portMAX_DELAY);
            break;
        }

        xQueueSend(s_enc_q, &slot, portMAX_DELAY);
        total_frames++;
        win_frames++;

        const int64_t now = esp_timer_get_time();
        const int64_t win = now - win_t0;
        if (win >= 1000000) {
            const float fps = (float)win_frames * 1000000.0f / (float)win;
            ESP_LOGI(TAG, "PPA scale %dx%d -> %dx%d fps=%.2f", SRC_W, SRC_H, DST_W, DST_H, fps);
            win_t0 = now;
            win_frames = 0;
        }
    }

    ESP_LOGI(TAG, "PPA task exit frames=%" PRIu32, total_frames);
    s_ppa_task = NULL;
    vTaskDelete(NULL);
}

static void ppa_stop(void)
{
    s_ppa_run = false;
    uint8_t slot = 0;
    xQueueSend(s_ppa_q, &slot, 0);
    while (s_ppa_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void enc_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "encode task on core %d parallel=%d", xPortGetCoreID(), EXAMPLE_PPA_PARALLEL);

    esp_h264_enc_in_frame_t in0 = { .raw_data = { .len = s_dst_len } };
    esp_h264_enc_in_frame_t in1 = { .raw_data = { .len = s_src_len } };
    esp_h264_enc_out_frame_t bitstream0 = { .raw_data = { .buffer = s_out0, .len = s_out0_len } };
    esp_h264_enc_out_frame_t bitstream1 = { .raw_data = { .buffer = s_out1, .len = s_out1_len } };
    esp_h264_enc_in_frame_t *in_dual[2] = { &in0, &in1 };
    esp_h264_enc_out_frame_t *out_dual[2] = { &bitstream0, &bitstream1 };

    const int64_t run_us = (int64_t)CONFIG_EXAMPLE_RUN_SECONDS * 1000000LL;
    const int64_t t0 = esp_timer_get_time();
    int64_t win_t0 = t0;
    uint32_t win_frames = 0;
    uint32_t total_frames = 0;

    while ((esp_timer_get_time() - t0) < run_us) {
        uint8_t slot = 0;
        if (xQueueReceive(s_enc_q, &slot, pdMS_TO_TICKS(200)) != pdTRUE) {
            ESP_LOGW(TAG, "wait frame timeout");
            continue;
        }

        if (!EXAMPLE_PPA_PARALLEL) {
            s_srm.in.buffer = s_src[slot];
            s_srm.out.buffer = s_dst[slot];
            if (ppa_do_scale_rotate_mirror(s_ppa, &s_srm) != ESP_OK) {
                ESP_LOGE(TAG, "PPA SRM failed");
                xQueueSend(s_free_q, &slot, portMAX_DELAY);
                break;
            }
            if (pattern_cache_invalidate(s_dst[slot], s_dst_len) != ESP_OK) {
                ESP_LOGE(TAG, "PPA cache invalidate failed");
                xQueueSend(s_free_q, &slot, portMAX_DELAY);
                break;
            }
        }

        in0.raw_data.buffer = s_dst[slot];
        in1.raw_data.buffer = s_src[slot];
        bitstream0.raw_data.len = s_out0_len;
        bitstream1.raw_data.len = s_out1_len;
        esp_h264_err_t ret = esp_h264_enc_dual_process(s_enc, in_dual, out_dual);
        xQueueSend(s_free_q, &slot, portMAX_DELAY);
        if (ret != ESP_H264_ERR_OK) {
            ESP_LOGE(TAG, "enc_dual_process failed ret=%d", (int)ret);
            break;
        }
        total_frames++;
        win_frames++;

        const int64_t now = esp_timer_get_time();
        const int64_t win = now - win_t0;
        if (win >= 1000000) {
            const float fps = (float)win_frames * 1000000.0f / (float)win;
            ESP_LOGI(TAG, "stream0 (PPA %dx%d) fps=%.2f", DST_W, DST_H, fps);
            ESP_LOGI(TAG, "stream1 (src %dx%d) fps=%.2f", SRC_W, SRC_H, fps);
            win_t0 = now;
            win_frames = 0;
        }
    }

    const int64_t elapsed = esp_timer_get_time() - t0;
    const float avg = (elapsed > 0) ? ((float)total_frames * 1000000.0f / (float)elapsed) : 0.0f;
    ESP_LOGI(TAG, "average stream0 (PPA %dx%d) fps=%.2f frames=%" PRIu32, DST_W, DST_H, avg, total_frames);
    ESP_LOGI(TAG, "average stream1 (src %dx%d) fps=%.2f frames=%" PRIu32, SRC_W, SRC_H, avg, total_frames);
    ESP_LOGI(TAG, "demo finished");
    s_enc_done = true;
    s_enc_task = NULL;
    vTaskDelete(NULL);
}

static uint32_t ppa_yuv420_scaled_len(uint32_t src_len, uint32_t scale_num, uint32_t scale_den)
{
    const float scale = (float)scale_num / (float)scale_den;
    const uint32_t scale_int = (uint32_t)scale;
    uint32_t scale_frag = ((uint32_t)(scale * (float)PPA_SRM_SCALE_FRAG_MAX)) & (PPA_SRM_SCALE_FRAG_MAX - 1);

    scale_frag &= ~1U;
    return src_len * scale_int + (src_len * scale_frag) / PPA_SRM_SCALE_FRAG_MAX;
}

void app_main(void)
{
    const uint32_t ppa_w = ppa_yuv420_scaled_len(SRC_W, SCALE_NUM, SCALE_DEN);
    const uint32_t ppa_h = ppa_yuv420_scaled_len(SRC_H, SCALE_NUM, SCALE_DEN);

    if ((SRC_W & 0xF) || (SRC_H & 0xF) || (ppa_w & 0xF) || (ppa_h & 0xF) ||
            ppa_w < 80 || ppa_h < 80 || ppa_w != (uint32_t)DST_W || ppa_h != (uint32_t)DST_H) {
        ESP_LOGE(TAG, "Invalid size src=%dx%d integer=%dx%d ppa=%" PRIu32 "x%" PRIu32
                 " (need multiple of 16, dst>=80, PPA 1/16 size equal to SRC * NUM / DEN)",
                 SRC_W, SRC_H, DST_W, DST_H, ppa_w, ppa_h);
        return;
    }

    ESP_LOGI(TAG, "Demo: gen core%d, PPA core%d, enc core%d realtime=%d parallel=%d",
             GEN_CORE, PPA_CORE, ENC_CORE, EXAMPLE_GEN_REALTIME, EXAMPLE_PPA_PARALLEL);
    ESP_LOGI(TAG, "stream0 PPA %dx%d, stream1 src %dx%d", DST_W, DST_H, SRC_W, SRC_H);

    for (uint8_t i = 0; i < GEN_SLOTS; i++) {
        s_src[i] = s_alloc_frame(pattern_ouev_frame_len(SRC_W, SRC_H), i == 0 ? &s_src_len : NULL);
        s_dst[i] = s_alloc_frame(pattern_ouev_frame_len((uint16_t)ppa_w, (uint16_t)ppa_h), i == 0 ? &s_dst_len : NULL);
    }
    s_out0 = s_alloc_frame(pattern_ouev_frame_len((uint16_t)ppa_w, (uint16_t)ppa_h), &s_out0_len);
    s_out1 = s_alloc_frame(pattern_ouev_frame_len(SRC_W, SRC_H), &s_out1_len);
    if (!s_src[0] || !s_src[1] || !s_dst[0] || !s_dst[1] || !s_out0 || !s_out1) {
        ESP_LOGE(TAG, "Alloc failed");
        goto exit_free;
    }

    s_free_q = xQueueCreate(GEN_SLOTS, sizeof(uint8_t));
    s_ppa_q = xQueueCreate(GEN_SLOTS, sizeof(uint8_t));
    s_enc_q = xQueueCreate(GEN_SLOTS, sizeof(uint8_t));
    if (!s_free_q || !s_ppa_q || !s_enc_q) {
        ESP_LOGE(TAG, "Queue create failed");
        goto exit_free;
    }
    for (uint8_t i = 0; i < GEN_SLOTS; i++) {
        xQueueSend(s_free_q, &i, 0);
    }

    ppa_client_config_t ppa_cfg = {
        .oper_type = PPA_OPERATION_SRM,
    };
    ESP_ERROR_CHECK(ppa_register_client(&ppa_cfg, &s_ppa));

    const float scale_x = (float)SCALE_NUM / (float)SCALE_DEN;
    const float scale_y = (float)SCALE_NUM / (float)SCALE_DEN;
    s_srm = (ppa_srm_oper_config_t) {
        .in = {
            .pic_w = SRC_W,
            .pic_h = SRC_H,
            .block_w = SRC_W,
            .block_h = SRC_H,
            .srm_cm = PPA_SRM_COLOR_MODE_YUV420,
            .yuv_range = PPA_COLOR_RANGE_LIMIT,
            .yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601,
        },
        .out = {
            .buffer_size = s_dst_len,
            .pic_w = ppa_w,
            .pic_h = ppa_h,
            .srm_cm = PPA_SRM_COLOR_MODE_YUV420,
            .yuv_range = PPA_COLOR_RANGE_LIMIT,
            .yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = scale_x,
        .scale_y = scale_y,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };

    esp_h264_enc_cfg_dual_hw_t enc_cfg = { 0 };
    fill_enc_cfg(&enc_cfg.cfg0, (uint16_t)ppa_w, (uint16_t)ppa_h);
    fill_enc_cfg(&enc_cfg.cfg1, SRC_W, SRC_H);
    if (esp_h264_enc_dual_hw_new(&enc_cfg, &s_enc) != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "Create dual encoder failed");
        goto exit_ppa;
    }
    if (esp_h264_enc_dual_open(s_enc) != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "Open dual encoder failed");
        esp_h264_enc_dual_del(s_enc);
        s_enc = NULL;
        goto exit_ppa;
    }

    s_gen_run = true;
    if (create_pinned(gen_task, "h264_gen", GEN_TASK_STACK, GEN_TASK_PRIO, &s_gen_task, GEN_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Create generator task failed");
        s_gen_run = false;
        goto exit_enc;
    }
    if (EXAMPLE_PPA_PARALLEL) {
        s_ppa_run = true;
        if (create_pinned(ppa_task, "h264_ppa", PPA_TASK_STACK, PPA_TASK_PRIO, &s_ppa_task, PPA_CORE) != pdPASS) {
            ESP_LOGE(TAG, "Create PPA task failed");
            s_ppa_run = false;
            gen_stop();
            goto exit_enc;
        }
    }
    if (create_pinned(enc_task, "h264_enc", ENC_TASK_STACK, ENC_TASK_PRIO, &s_enc_task, ENC_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Create encode task failed");
        if (EXAMPLE_PPA_PARALLEL) {
            ppa_stop();
        }
        gen_stop();
        goto exit_enc;
    }

    while (!s_enc_done) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    gen_stop();
    if (EXAMPLE_PPA_PARALLEL) {
        ppa_stop();
    }

exit_enc:
    if (s_enc) {
        esp_h264_enc_dual_close(s_enc);
        esp_h264_enc_dual_del(s_enc);
        s_enc = NULL;
    }
exit_ppa:
    if (s_ppa) {
        ppa_unregister_client(s_ppa);
        s_ppa = NULL;
    }
exit_free:
    if (s_free_q) {
        vQueueDelete(s_free_q);
        s_free_q = NULL;
    }
    if (s_ppa_q) {
        vQueueDelete(s_ppa_q);
        s_ppa_q = NULL;
    }
    if (s_enc_q) {
        vQueueDelete(s_enc_q);
        s_enc_q = NULL;
    }
    for (uint8_t i = 0; i < GEN_SLOTS; i++) {
        if (s_src[i]) {
            esp_h264_free(s_src[i]);
        }
        if (s_dst[i]) {
            esp_h264_free(s_dst[i]);
        }
    }
    if (s_out0) {
        esp_h264_free(s_out0);
    }
    if (s_out1) {
        esp_h264_free(s_out1);
    }
}
