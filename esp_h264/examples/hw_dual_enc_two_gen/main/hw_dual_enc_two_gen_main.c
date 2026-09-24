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
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pattern_input.h"

static const char *TAG = "dual_two_gen";

#define SRC0_W         CONFIG_EXAMPLE_SRC0_WIDTH
#define SRC0_H         CONFIG_EXAMPLE_SRC0_HEIGHT
#define SRC1_W         CONFIG_EXAMPLE_SRC1_WIDTH
#define SRC1_H         CONFIG_EXAMPLE_SRC1_HEIGHT
#define GEN_SLOTS      2
#ifndef EXAMPLE_GEN_REALTIME
#if CONFIG_EXAMPLE_GEN_REALTIME
#define EXAMPLE_GEN_REALTIME 1
#else
#define EXAMPLE_GEN_REALTIME 0
#endif
#endif
#define GEN_CORE       1
#define ENC_CORE       0
#define GEN_TASK_STACK 4096
#define ENC_TASK_STACK 8192
#define GEN_TASK_PRIO  5
#define ENC_TASK_PRIO  6

typedef struct {
    const char *name;
    uint8_t *buf[GEN_SLOTS];
    uint32_t len;
    uint16_t width;
    uint16_t height;
    int palette;
    uint16_t phase_step;
    QueueHandle_t free_q;
    QueueHandle_t ready_q;
    TaskHandle_t task;
} gen_ch_t;

static gen_ch_t s_ch[2];
static volatile bool s_gen_run;
static TaskHandle_t s_enc_task;
static volatile bool s_enc_done;
static uint8_t *s_out0;
static uint8_t *s_out1;
static uint32_t s_out0_len;
static uint32_t s_out1_len;
static esp_h264_enc_dual_handle_t s_enc;

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

static BaseType_t create_pinned(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio,
                                TaskHandle_t *out, BaseType_t core)
{
#if CONFIG_FREERTOS_UNICORE
    (void)core;
    return xTaskCreate(fn, name, stack, arg, prio, out);
#else
    return xTaskCreatePinnedToCore(fn, name, stack, arg, prio, out, core);
#endif
}

static void gen_task(void *arg)
{
    gen_ch_t *ch = (gen_ch_t *)arg;
    uint16_t phase = 0;
    uint8_t filled = 0;
    ESP_LOGI(TAG, "%s task on core %d realtime=%d", ch->name, xPortGetCoreID(), EXAMPLE_GEN_REALTIME);
    while (s_gen_run) {
        uint8_t slot = 0;
        if (xQueueReceive(ch->free_q, &slot, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue;
        }
        if (!s_gen_run) {
            xQueueSend(ch->free_q, &slot, 0);
            break;
        }
        bool do_fill = EXAMPLE_GEN_REALTIME || (filled < GEN_SLOTS);
        if (do_fill) {
            pattern_fill_ouev_colorbar(ch->buf[slot], ch->width, ch->height, phase, ch->palette);
            phase = (uint16_t)((phase + ch->phase_step) % ch->width);
            if (pattern_cache_writeback(ch->buf[slot], ch->len) != ESP_OK) {
                ESP_LOGE(TAG, "%s cache writeback failed slot=%u", ch->name, slot);
                xQueueSend(ch->free_q, &slot, 0);
                continue;
            }
            if (!EXAMPLE_GEN_REALTIME) {
                filled++;
                if (filled == GEN_SLOTS) {
                    ESP_LOGI(TAG, "%s filled %d frames, recycle without refill", ch->name, GEN_SLOTS);
                }
            }
        }
        xQueueSend(ch->ready_q, &slot, portMAX_DELAY);
    }
    ESP_LOGI(TAG, "%s task exit", ch->name);
    ch->task = NULL;
    vTaskDelete(NULL);
}

static void gen_stop(void)
{
    s_gen_run = false;
    for (int i = 0; i < 2; i++) {
        uint8_t slot = 0;
        while (xQueueReceive(s_ch[i].ready_q, &slot, 0) == pdTRUE) {
            xQueueSend(s_ch[i].free_q, &slot, 0);
        }
        xQueueSend(s_ch[i].free_q, &slot, 0);
    }
    while (s_ch[0].task != NULL || s_ch[1].task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void enc_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "encode task on core %d", xPortGetCoreID());

    esp_h264_enc_in_frame_t frame0 = { .raw_data = { .len = s_ch[0].len } };
    esp_h264_enc_in_frame_t frame1 = { .raw_data = { .len = s_ch[1].len } };
    esp_h264_enc_out_frame_t bitstream0 = { .raw_data = { .buffer = s_out0, .len = s_out0_len } };
    esp_h264_enc_out_frame_t bitstream1 = { .raw_data = { .buffer = s_out1, .len = s_out1_len } };
    esp_h264_enc_in_frame_t *in_dual[2] = { &frame0, &frame1 };
    esp_h264_enc_out_frame_t *out_dual[2] = { &bitstream0, &bitstream1 };

    const int64_t run_us = (int64_t)CONFIG_EXAMPLE_RUN_SECONDS * 1000000LL;
    const int64_t t0 = esp_timer_get_time();
    int64_t win_t0 = t0;
    uint32_t win_frames = 0;
    uint32_t total_frames = 0;

    while ((esp_timer_get_time() - t0) < run_us) {
        uint8_t slot0 = 0;
        uint8_t slot1 = 0;
        if (xQueueReceive(s_ch[0].ready_q, &slot0, pdMS_TO_TICKS(200)) != pdTRUE) {
            ESP_LOGW(TAG, "wait gen0 timeout");
            continue;
        }
        if (xQueueReceive(s_ch[1].ready_q, &slot1, pdMS_TO_TICKS(200)) != pdTRUE) {
            ESP_LOGW(TAG, "wait gen1 timeout");
            xQueueSend(s_ch[0].free_q, &slot0, portMAX_DELAY);
            continue;
        }
        frame0.raw_data.buffer = s_ch[0].buf[slot0];
        frame1.raw_data.buffer = s_ch[1].buf[slot1];
        bitstream0.raw_data.len = s_out0_len;
        bitstream1.raw_data.len = s_out1_len;
        esp_h264_err_t ret = esp_h264_enc_dual_process(s_enc, in_dual, out_dual);
        xQueueSend(s_ch[0].free_q, &slot0, portMAX_DELAY);
        xQueueSend(s_ch[1].free_q, &slot1, portMAX_DELAY);
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
            ESP_LOGI(TAG, "stream0 (gen0 %dx%d) fps=%.2f", SRC0_W, SRC0_H, fps);
            ESP_LOGI(TAG, "stream1 (gen1 %dx%d) fps=%.2f", SRC1_W, SRC1_H, fps);
            win_t0 = now;
            win_frames = 0;
        }
    }

    const int64_t elapsed = esp_timer_get_time() - t0;
    const float avg = (elapsed > 0) ? ((float)total_frames * 1000000.0f / (float)elapsed) : 0.0f;
    ESP_LOGI(TAG, "average stream0 (gen0 %dx%d) fps=%.2f frames=%" PRIu32, SRC0_W, SRC0_H, avg, total_frames);
    ESP_LOGI(TAG, "average stream1 (gen1 %dx%d) fps=%.2f frames=%" PRIu32, SRC1_W, SRC1_H, avg, total_frames);
    ESP_LOGI(TAG, "demo finished");
    s_enc_done = true;
    s_enc_task = NULL;
    vTaskDelete(NULL);
}

void app_main(void)
{
    if ((SRC0_W & 0xF) || (SRC0_H & 0xF) || (SRC1_W & 0xF) || (SRC1_H & 0xF)) {
        ESP_LOGE(TAG, "Invalid size gen0=%dx%d gen1=%dx%d (need multiple of 16)", SRC0_W, SRC0_H, SRC1_W, SRC1_H);
        return;
    }

    ESP_LOGI(TAG, "Demo: two generator tasks core%d, encode core%d realtime=%d", GEN_CORE, ENC_CORE, EXAMPLE_GEN_REALTIME);
    ESP_LOGI(TAG, "stream0 gen0 %dx%d, stream1 gen1 %dx%d", SRC0_W, SRC0_H, SRC1_W, SRC1_H);

    s_out0 = s_alloc_frame(pattern_ouev_frame_len(SRC0_W, SRC0_H), &s_out0_len);
    s_out1 = s_alloc_frame(pattern_ouev_frame_len(SRC1_W, SRC1_H), &s_out1_len);
    s_ch[0] = (gen_ch_t) {
        .name = "gen0",
        .width = SRC0_W,
        .height = SRC0_H,
        .palette = 0,
        .phase_step = 2,
    };
    s_ch[1] = (gen_ch_t) {
        .name = "gen1",
        .width = SRC1_W,
        .height = SRC1_H,
        .palette = 1,
        .phase_step = 4,
    };
    s_ch[0].buf[0] = s_alloc_frame(pattern_ouev_frame_len(SRC0_W, SRC0_H), &s_ch[0].len);
    s_ch[0].buf[1] = s_alloc_frame(pattern_ouev_frame_len(SRC0_W, SRC0_H), NULL);
    s_ch[1].buf[0] = s_alloc_frame(pattern_ouev_frame_len(SRC1_W, SRC1_H), &s_ch[1].len);
    s_ch[1].buf[1] = s_alloc_frame(pattern_ouev_frame_len(SRC1_W, SRC1_H), NULL);
    if (!s_ch[0].buf[0] || !s_ch[0].buf[1] || !s_ch[1].buf[0] || !s_ch[1].buf[1] || !s_out0 || !s_out1) {
        ESP_LOGE(TAG, "Alloc failed");
        goto exit_free;
    }

    for (int i = 0; i < 2; i++) {
        s_ch[i].free_q = xQueueCreate(GEN_SLOTS, sizeof(uint8_t));
        s_ch[i].ready_q = xQueueCreate(GEN_SLOTS, sizeof(uint8_t));
        if (!s_ch[i].free_q || !s_ch[i].ready_q) {
            ESP_LOGE(TAG, "Queue create failed");
            goto exit_free;
        }
        for (uint8_t slot = 0; slot < GEN_SLOTS; slot++) {
            xQueueSend(s_ch[i].free_q, &slot, 0);
        }
    }

    esp_h264_enc_cfg_dual_hw_t enc_cfg = { 0 };
    fill_enc_cfg(&enc_cfg.cfg0, SRC0_W, SRC0_H);
    fill_enc_cfg(&enc_cfg.cfg1, SRC1_W, SRC1_H);
    if (esp_h264_enc_dual_hw_new(&enc_cfg, &s_enc) != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "Create dual encoder failed");
        goto exit_free;
    }
    if (esp_h264_enc_dual_open(s_enc) != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "Open dual encoder failed");
        esp_h264_enc_dual_del(s_enc);
        s_enc = NULL;
        goto exit_free;
    }

    s_gen_run = true;
    if (create_pinned(gen_task, "h264_gen0", GEN_TASK_STACK, &s_ch[0], GEN_TASK_PRIO, &s_ch[0].task, GEN_CORE) != pdPASS ||
            create_pinned(gen_task, "h264_gen1", GEN_TASK_STACK, &s_ch[1], GEN_TASK_PRIO, &s_ch[1].task, GEN_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Create generator task failed");
        gen_stop();
        goto exit_enc;
    }
    if (create_pinned(enc_task, "h264_enc", ENC_TASK_STACK, NULL, ENC_TASK_PRIO, &s_enc_task, ENC_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Create encode task failed");
        gen_stop();
        goto exit_enc;
    }

    while (!s_enc_done) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    gen_stop();

exit_enc:
    if (s_enc) {
        esp_h264_enc_dual_close(s_enc);
        esp_h264_enc_dual_del(s_enc);
        s_enc = NULL;
    }
exit_free:
    for (int i = 0; i < 2; i++) {
        if (s_ch[i].free_q) {
            vQueueDelete(s_ch[i].free_q);
        }
        if (s_ch[i].ready_q) {
            vQueueDelete(s_ch[i].ready_q);
        }
        if (s_ch[i].buf[0]) {
            esp_h264_free(s_ch[i].buf[0]);
        }
        if (s_ch[i].buf[1]) {
            esp_h264_free(s_ch[i].buf[1]);
        }
    }
    if (s_out0) {
        esp_h264_free(s_out0);
    }
    if (s_out1) {
        esp_h264_free(s_out1);
    }
}
