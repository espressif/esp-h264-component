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
#include "esp_h264_enc_single.h"
#include "esp_h264_enc_single_hw.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pattern_input.h"

static const char *TAG = "hw_roi";

#define SRC_W          CONFIG_EXAMPLE_SRC_WIDTH
#define SRC_H          CONFIG_EXAMPLE_SRC_HEIGHT
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

#if CONFIG_EXAMPLE_ROI_MODE_DISABLE
#define EXAMPLE_ROI_MODE ESP_H264_ROI_MODE_DISABLE
#elif CONFIG_EXAMPLE_ROI_MODE_DELTA_QP
#define EXAMPLE_ROI_MODE ESP_H264_ROI_MODE_DELTA_QP
#else
#define EXAMPLE_ROI_MODE ESP_H264_ROI_MODE_FIX_QP
#endif

static uint8_t *s_src[GEN_SLOTS];
static uint32_t s_src_len;
static QueueHandle_t s_free_q;
static QueueHandle_t s_ready_q;
static volatile bool s_gen_run;
static TaskHandle_t s_gen_task;
static TaskHandle_t s_enc_task;
static volatile bool s_enc_done;
static uint8_t *s_out;
static uint32_t s_out_len;
static esp_h264_enc_handle_t s_enc;
static esp_h264_enc_param_hw_handle_t s_param;

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

static void fill_enc_cfg(esp_h264_enc_cfg_hw_t *cfg, uint16_t w, uint16_t h)
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

static const char *roi_mode_str(esp_h264_roi_mode_t mode)
{
    switch (mode) {
    case ESP_H264_ROI_MODE_DISABLE:
        return "DISABLE";
    case ESP_H264_ROI_MODE_FIX_QP:
        return "FIX_QP";
    case ESP_H264_ROI_MODE_DELTA_QP:
        return "DELTA_QP";
    default:
        return "INVALID";
    }
}

static esp_h264_err_t setup_roi(esp_h264_enc_param_hw_handle_t param, uint16_t width, uint16_t height)
{
    esp_h264_enc_roi_cfg_t roi_cfg = {
        .roi_mode = EXAMPLE_ROI_MODE,
        .none_roi_delta_qp = CONFIG_EXAMPLE_NONE_ROI_DELTA_QP,
    };
    esp_h264_err_t ret = esp_h264_enc_hw_cfg_roi(param, roi_cfg);
    if (ret != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "cfg_roi failed ret=%d", (int)ret);
        return ret;
    }
    ESP_LOGI(TAG, "ROI mode=%s none_roi_delta_qp=%d", roi_mode_str(EXAMPLE_ROI_MODE),
             (int)roi_cfg.none_roi_delta_qp);

    if (EXAMPLE_ROI_MODE == ESP_H264_ROI_MODE_DISABLE) {
        return ESP_H264_ERR_OK;
    }

    const uint8_t mb_w = (uint8_t)((width + 15) >> 4);
    const uint8_t mb_h = (uint8_t)((height + 15) >> 4);
    esp_h264_enc_roi_reg_t roi = {
        .x = (uint8_t)(mb_w / 4),
        .y = (uint8_t)(mb_h / 4),
        .len_x = (uint8_t)(mb_w / 2),
        .len_y = (uint8_t)(mb_h / 2),
        .qp = (int8_t)CONFIG_EXAMPLE_ROI_QP,
        .reg_idx = 0,
    };
    if (roi.len_x == 0) {
        roi.len_x = 1;
    }
    if (roi.len_y == 0) {
        roi.len_y = 1;
    }
    ret = esp_h264_enc_hw_set_roi_region(param, roi);
    if (ret != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "set_roi_region failed ret=%d", (int)ret);
        return ret;
    }
    ESP_LOGI(TAG, "ROI region mb x=%u y=%u len_x=%u len_y=%u qp=%d (frame %dx%d, mb %ux%u)",
             roi.x, roi.y, roi.len_x, roi.len_y, (int)roi.qp, width, height, mb_w, mb_h);
    return ESP_H264_ERR_OK;
}

static void gen_task(void *arg)
{
    (void)arg;
    uint16_t phase = 0;
    uint8_t filled = 0;
    ESP_LOGI(TAG, "generator task on core %d realtime=%d", xPortGetCoreID(), EXAMPLE_GEN_REALTIME);
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
        xQueueSend(s_ready_q, &slot, portMAX_DELAY);
    }
    ESP_LOGI(TAG, "generator task exit");
    s_gen_task = NULL;
    vTaskDelete(NULL);
}

static void gen_stop(void)
{
    s_gen_run = false;
    uint8_t slot = 0;
    while (xQueueReceive(s_ready_q, &slot, 0) == pdTRUE) {
        xQueueSend(s_free_q, &slot, 0);
    }
    xQueueSend(s_free_q, &slot, 0);
    while (s_gen_task != NULL) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void enc_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "encode task on core %d", xPortGetCoreID());

    esp_h264_enc_in_frame_t in_frame = { .raw_data = { .len = s_src_len } };
    esp_h264_enc_out_frame_t out_frame = { .raw_data = { .buffer = s_out, .len = s_out_len } };

    const int64_t run_us = (int64_t)CONFIG_EXAMPLE_RUN_SECONDS * 1000000LL;
    const int64_t t0 = esp_timer_get_time();
    int64_t win_t0 = t0;
    uint32_t win_frames = 0;
    uint32_t win_bytes = 0;
    uint32_t total_frames = 0;
    uint64_t total_bytes = 0;

    while ((esp_timer_get_time() - t0) < run_us) {
        uint8_t slot = 0;
        if (xQueueReceive(s_ready_q, &slot, pdMS_TO_TICKS(200)) != pdTRUE) {
            ESP_LOGW(TAG, "wait generator timeout");
            continue;
        }
        in_frame.raw_data.buffer = s_src[slot];
        in_frame.raw_data.len = s_src_len;
        out_frame.raw_data.len = s_out_len;
        esp_h264_err_t ret = esp_h264_enc_process(s_enc, &in_frame, &out_frame);
        xQueueSend(s_free_q, &slot, portMAX_DELAY);
        if (ret != ESP_H264_ERR_OK) {
            ESP_LOGE(TAG, "enc_process failed ret=%d", (int)ret);
            break;
        }
        total_frames++;
        win_frames++;
        total_bytes += out_frame.length;
        win_bytes += out_frame.length;

        const int64_t now = esp_timer_get_time();
        const int64_t win = now - win_t0;
        if (win >= 1000000) {
            const float fps = (float)win_frames * 1000000.0f / (float)win;
            const float kbps = (float)win_bytes * 8.0f / (float)win * 1000.0f;
            ESP_LOGI(TAG, "stream0 (src %dx%d) fps=%.2f bitrate=%.0f kbps avg_nal=%" PRIu32,
                     SRC_W, SRC_H, fps, kbps, win_frames ? (win_bytes / win_frames) : 0);
            win_t0 = now;
            win_frames = 0;
            win_bytes = 0;
        }
    }

    const int64_t elapsed = esp_timer_get_time() - t0;
    const float avg = (elapsed > 0) ? ((float)total_frames * 1000000.0f / (float)elapsed) : 0.0f;
    const float avg_kbps = (elapsed > 0) ? ((float)total_bytes * 8.0f / (float)elapsed * 1000.0f) : 0.0f;
    ESP_LOGI(TAG, "average stream0 (src %dx%d) fps=%.2f frames=%" PRIu32 " bitrate=%.0f kbps",
             SRC_W, SRC_H, avg, total_frames, avg_kbps);
    ESP_LOGI(TAG, "demo finished");
    s_enc_done = true;
    s_enc_task = NULL;
    vTaskDelete(NULL);
}

void app_main(void)
{
    if ((SRC_W & 0xF) || (SRC_H & 0xF)) {
        ESP_LOGE(TAG, "Invalid size src=%dx%d (need multiple of 16)", SRC_W, SRC_H);
        return;
    }

    ESP_LOGI(TAG, "Demo: generator core%d, encode core%d realtime=%d", GEN_CORE, ENC_CORE, EXAMPLE_GEN_REALTIME);
    ESP_LOGI(TAG, "stream0 src %dx%d", SRC_W, SRC_H);

    s_out = s_alloc_frame(pattern_ouev_frame_len(SRC_W, SRC_H), &s_out_len);
    s_src[0] = s_alloc_frame(pattern_ouev_frame_len(SRC_W, SRC_H), &s_src_len);
    s_src[1] = s_alloc_frame(pattern_ouev_frame_len(SRC_W, SRC_H), NULL);
    if (!s_src[0] || !s_src[1] || !s_out) {
        ESP_LOGE(TAG, "Alloc failed");
        goto exit_free;
    }

    s_free_q = xQueueCreate(GEN_SLOTS, sizeof(uint8_t));
    s_ready_q = xQueueCreate(GEN_SLOTS, sizeof(uint8_t));
    if (!s_free_q || !s_ready_q) {
        ESP_LOGE(TAG, "Queue create failed");
        goto exit_free;
    }
    for (uint8_t i = 0; i < GEN_SLOTS; i++) {
        xQueueSend(s_free_q, &i, 0);
    }

    esp_h264_enc_cfg_hw_t enc_cfg = { 0 };
    fill_enc_cfg(&enc_cfg, SRC_W, SRC_H);
    if (esp_h264_enc_hw_new(&enc_cfg, &s_enc) != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "Create encoder failed");
        goto exit_free;
    }
    if (esp_h264_enc_hw_get_param_hd(s_enc, &s_param) != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "Get param handle failed");
        goto exit_enc;
    }
    if (esp_h264_enc_open(s_enc) != ESP_H264_ERR_OK) {
        ESP_LOGE(TAG, "Open encoder failed");
        goto exit_enc;
    }
    if (setup_roi(s_param, SRC_W, SRC_H) != ESP_H264_ERR_OK) {
        goto exit_enc;
    }

    s_gen_run = true;
    if (create_pinned(gen_task, "h264_gen", GEN_TASK_STACK, GEN_TASK_PRIO, &s_gen_task, GEN_CORE) != pdPASS) {
        ESP_LOGE(TAG, "Create generator task failed");
        s_gen_run = false;
        goto exit_enc;
    }
    if (create_pinned(enc_task, "h264_enc", ENC_TASK_STACK, ENC_TASK_PRIO, &s_enc_task, ENC_CORE) != pdPASS) {
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
        esp_h264_enc_close(s_enc);
        esp_h264_enc_del(s_enc);
        s_enc = NULL;
    }
exit_free:
    if (s_free_q) {
        vQueueDelete(s_free_q);
        s_free_q = NULL;
    }
    if (s_ready_q) {
        vQueueDelete(s_ready_q);
        s_ready_q = NULL;
    }
    if (s_src[0]) {
        esp_h264_free(s_src[0]);
    }
    if (s_src[1]) {
        esp_h264_free(s_src[1]);
    }
    if (s_out) {
        esp_h264_free(s_out);
    }
}
