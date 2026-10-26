/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "sdkconfig.h"
#include "unity.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "esp_sleep.h"
#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define H264_PM_RESULT_MAGIC  0x48323634u

#if CONFIG_PM_ENABLE && CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP && CONFIG_IDF_TARGET_ESP32P4
RTC_NOINIT_ATTR static uint32_t s_h264_pm_magic;
RTC_NOINIT_ATTR static char s_h264_pm_result[40];

void h264_pm_boot_log(void)
{
    if (s_h264_pm_magic == H264_PM_RESULT_MAGIC) {
        ESP_LOGI("h264_pm_test", "last_pm_result=%s", s_h264_pm_result);
    }
}

static void h264_pm_result_set(const char *msg)
{
    s_h264_pm_magic = H264_PM_RESULT_MAGIC;
    snprintf(s_h264_pm_result, sizeof(s_h264_pm_result), "%s", msg ? msg : "unknown");
}
#else
void h264_pm_boot_log(void)
{
}
#endif

#if CONFIG_PM_ENABLE && CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP && CONFIG_IDF_TARGET_ESP32P4

#include "esp_h264_enc_single_hw.h"
#include "esp_h264_enc_dual_hw.h"
#include "esp_h264_enc_dual.h"
#include "esp_h264_types.h"
#include "esp_h264_alloc.h"
#include "h264_io.h"
#include "esp_heap_caps.h"
#include "esp_private/sleep_retention.h"
#include "esp_private/sleep_sys_periph.h"
#include "esp_private/sleep_clock.h"
#if CONFIG_ESP_SLEEP_DEBUG
#include "esp_private/esp_sleep_internal.h"
#endif
#if CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP && CONFIG_ESP_SLEEP_DEBUG
#include "esp_private/esp_pmu.h"
#endif
#if ESP_SLEEP_POWER_DOWN_CPU
#include "esp_private/sleep_cpu.h"
#endif
#include "soc/usb_serial_jtag_reg.h"

/* IDF 5.5 soc headers have no H264 module id; keep in sync with hw/hal/esp32p4/h264_periph.h. */
#ifndef SLEEP_RETENTION_MODULE_H264
#define SLEEP_RETENTION_MODULE_H264 ((sleep_retention_module_t)40)
#endif

static const char *TAG_H264_PM = "h264_pm_test";

static void h264_pm_log_retention(const char *when)
{
    ESP_LOGI(TAG_H264_PM, "%s peri_pd=%d clk_pd=%d h264_inited=%d h264_created=%d heap8=%u",
             when,
             (int)peripheral_domain_pd_allowed(),
             (int)clock_domain_pd_allowed(),
             (int)sleep_retention_is_module_inited(SLEEP_RETENTION_MODULE_H264),
             (int)sleep_retention_is_module_created(SLEEP_RETENTION_MODULE_H264),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT));
    sleep_retention_dump_modules(stdout);
}

static void h264_pm_disable_usb_jtag_chip_reset(void)
{
    /* TOP PD tears down USB-Serial/JTAG. Host cdc_acm reopen would otherwise
     * CHIP_USB_UART_RESET the running Unity case and drop the result. */
    REG_SET_BIT(USB_DEVICE_CHIP_RST_REG, USB_SERIAL_JTAG_USB_UART_CHIP_RST_DIS);
}

static esp_err_t h264_run_light_sleep_core(void)
{
    esp_err_t err = ESP_OK;
    h264_pm_disable_usb_jtag_chip_reset();
#if ESP_SLEEP_POWER_DOWN_CPU
    err = sleep_cpu_configure(true);
    if (err != ESP_OK) {
        return err;
    }
    ESP_LOGI(TAG_H264_PM, "cpu_pd_allowed=%d", (int)cpu_domain_pd_allowed());
#endif
    /* Microseconds; must exceed light-sleep overhead (larger with TOP/REGDMA PD). */
    err = esp_sleep_enable_timer_wakeup(2ULL * 1000 * 1000);
    if (err != ESP_OK) {
        goto cpu_off;
    }
    err = esp_light_sleep_start();
cpu_off:
#if ESP_SLEEP_POWER_DOWN_CPU
    (void)sleep_cpu_configure(false);
#endif
    h264_pm_disable_usb_jtag_chip_reset();
    /* TOP PD tears down USB Serial/JTAG; wait for the host to re-enumerate before logging. */
    vTaskDelay(pdMS_TO_TICKS(2000));
    return err;
}

#if CONFIG_ESP_SLEEP_DEBUG
static esp_err_t h264_run_light_sleep_with_context(esp_sleep_context_t *sleep_ctx)
{
    esp_sleep_set_sleep_context(sleep_ctx);
    esp_err_t err = h264_run_light_sleep_core();
    esp_sleep_set_sleep_context(NULL);
    return err;
}
#else
static esp_err_t h264_run_light_sleep_simple(void)
{
    return h264_run_light_sleep_core();
}
#endif

/* Unity treats ':' in the typed name as a filter separator, so names must not contain ':'. */
TEST_CASE("h264_hw_enc_single_light_sleep_then_encode_again", "[esp_h264][pm]")
{
    const char *fail = NULL;
    ESP_LOGI(TAG_H264_PM, "1: test start");
    h264_pm_result_set("running_single");
    const int16_t width = 128;
    const int16_t height = 128;
    const esp_h264_raw_format_t pic = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    esp_h264_enc_cfg_hw_t cfg = { 0 };

    cfg.gop = 5;
    cfg.fps = 30;
    cfg.res.width = width;
    cfg.res.height = height;
    cfg.rc.bitrate = width * height * cfg.fps / 20;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = pic;

    esp_h264_enc_in_frame_t in_frame = { 0 };
    esp_h264_enc_out_frame_t out_frame = { 0 };
    esp_h264_enc_handle_t enc = NULL;
    in_frame.raw_data.len = (int)((float)width * height * ESP_H264_GET_BPP_BY_PIC_TYPE(pic));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame.raw_data.len, &in_frame.raw_data.len, ESP_H264_MEM_INTERNAL_DMA);
    if (in_frame.raw_data.buffer == NULL) {
        fail = "in_buf";
        goto cleanup;
    }
    /* HW path: out is H.264 ES capacity, not YUV size (see esp_h264_enc_single.h). Extra headroom for IDR + RC after wake. */
    out_frame.raw_data.len = in_frame.raw_data.len * 2;
    out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame.raw_data.len, &out_frame.raw_data.len, ESP_H264_MEM_INTERNAL_DMA);
    if (out_frame.raw_data.buffer == NULL) {
        fail = "out_buf";
        goto cleanup;
    }

    ESP_LOGI(TAG_H264_PM, "2: enc_hw_new");
    if (esp_h264_enc_hw_new(&cfg, &enc) != ESP_H264_ERR_OK) {
        fail = "enc_hw_new";
        goto cleanup;
    }
    ESP_LOGI(TAG_H264_PM, "3: enc_open");
    if (esp_h264_enc_open(enc) != ESP_H264_ERR_OK) {
        fail = "enc_open";
        goto cleanup;
    }

    if (read_enc_cb(&in_frame, width, height, pic) <= 0) {
        fail = "read_enc_cb";
        goto cleanup;
    }
    ESP_LOGI(TAG_H264_PM, "4: enc_process #1");
    if (esp_h264_enc_process(enc, &in_frame, &out_frame) != ESP_H264_ERR_OK) {
        fail = "enc_process_1";
        goto cleanup;
    }
    if (esp_h264_enc_process(enc, &in_frame, &out_frame) != ESP_H264_ERR_OK) {
        fail = "enc_process_2";
        goto cleanup;
    }
    ESP_LOGI(TAG_H264_PM, "5: before light sleep");
    h264_pm_log_retention("before_sleep");
#if CONFIG_ESP_SLEEP_DEBUG
    esp_sleep_context_t sleep_ctx = { 0 };
    if (h264_run_light_sleep_with_context(&sleep_ctx) != ESP_OK) {
        fail = "light_sleep";
        goto cleanup;
    }
    ESP_LOGI(TAG_H264_PM, "sleep_request_result=%d sleep_flags=0x%lx",
             sleep_ctx.sleep_request_result, (unsigned long)sleep_ctx.sleep_flags);
    if (sleep_ctx.sleep_request_result != 0) {
        fail = "sleep_request_result";
        goto cleanup;
    }
#if CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP
    if ((sleep_ctx.sleep_flags & PMU_SLEEP_PD_TOP) == 0) {
        fail = "PMU_SLEEP_PD_TOP";
        goto cleanup;
    }
#endif
#else
    if (h264_run_light_sleep_simple() != ESP_OK) {
        fail = "light_sleep";
        goto cleanup;
    }
#endif

    ESP_LOGI(TAG_H264_PM, "6: after sleep, canary lost then IDR + P");
    for (int i = 0; i < 10; i++) {
        if (esp_h264_enc_process(enc, &in_frame, &out_frame) != ESP_H264_ERR_OK) {
            fail = "enc_process_after_sleep";
            goto cleanup;
        }
        if (i == 0 && out_frame.frame_type != ESP_H264_FRAME_TYPE_IDR) {
            fail = "first_frame_not_idr";
            goto cleanup;
        }
    }

    ESP_LOGI(TAG_H264_PM, "7: done");

cleanup:
    if (enc) {
        (void)esp_h264_enc_close(enc);
        (void)esp_h264_enc_del(enc);
        enc = NULL;
    }
    if (in_frame.raw_data.buffer) {
        esp_h264_free(in_frame.raw_data.buffer);
        in_frame.raw_data.buffer = NULL;
    }
    if (out_frame.raw_data.buffer) {
        esp_h264_free(out_frame.raw_data.buffer);
        out_frame.raw_data.buffer = NULL;
    }
    h264_pm_result_set(fail ? fail : "PASS_single");
    TEST_ASSERT_NULL_MESSAGE(fail, fail);
}

TEST_CASE("h264_hw_enc_dual_light_sleep_then_encode_again", "[esp_h264][pm]")
{
    const char *fail = NULL;
    ESP_LOGI(TAG_H264_PM, "dual 1: test start");
    h264_pm_result_set("running_dual");
    const esp_h264_raw_format_t pic = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    esp_h264_enc_cfg_dual_hw_t cfg = { 0 };

    /* Two streams; GOP must be compatible (HW uses average of cfg0/cfg1 GOP). */
    cfg.cfg0.gop = 5;
    cfg.cfg0.fps = 30;
    cfg.cfg0.res.width = 128;
    cfg.cfg0.res.height = 128;
    cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
    cfg.cfg0.rc.qp_min = 26;
    cfg.cfg0.rc.qp_max = 26;
    cfg.cfg0.pic_type = pic;

    cfg.cfg1.gop = 5;
    cfg.cfg1.fps = 30;
    cfg.cfg1.res.width = 96;
    cfg.cfg1.res.height = 96;
    cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;
    cfg.cfg1.rc.qp_min = 26;
    cfg.cfg1.rc.qp_max = 26;
    cfg.cfg1.pic_type = pic;

    const int16_t aw0 = (int16_t)(((cfg.cfg0.res.width + 15) >> 4) << 4);
    const int16_t ah0 = (int16_t)(((cfg.cfg0.res.height + 15) >> 4) << 4);
    const int16_t aw1 = (int16_t)(((cfg.cfg1.res.width + 15) >> 4) << 4);
    const int16_t ah1 = (int16_t)(((cfg.cfg1.res.height + 15) >> 4) << 4);

    esp_h264_enc_in_frame_t in0 = { 0 };
    esp_h264_enc_in_frame_t in1 = { 0 };
    esp_h264_enc_out_frame_t out0 = { 0 };
    esp_h264_enc_out_frame_t out1 = { 0 };
    esp_h264_enc_dual_handle_t enc = NULL;

    in0.raw_data.len = (int)((float)aw0 * ah0 * ESP_H264_GET_BPP_BY_PIC_TYPE(pic));
    in1.raw_data.len = (int)((float)aw1 * ah1 * ESP_H264_GET_BPP_BY_PIC_TYPE(pic));
    in0.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in0.raw_data.len, &in0.raw_data.len, ESP_H264_MEM_INTERNAL_DMA);
    in1.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in1.raw_data.len, &in1.raw_data.len, ESP_H264_MEM_INTERNAL_DMA);
    if (in0.raw_data.buffer == NULL || in1.raw_data.buffer == NULL) {
        fail = "in_buf";
        goto cleanup;
    }

    out0.raw_data.len = in0.raw_data.len * 2;
    out1.raw_data.len = in1.raw_data.len * 2;
    out0.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out0.raw_data.len, &out0.raw_data.len, ESP_H264_MEM_INTERNAL_DMA);
    out1.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out1.raw_data.len, &out1.raw_data.len, ESP_H264_MEM_INTERNAL_DMA);
    if (out0.raw_data.buffer == NULL || out1.raw_data.buffer == NULL) {
        fail = "out_buf";
        goto cleanup;
    }

    esp_h264_enc_in_frame_t *in_pair[2] = { &in0, &in1 };
    esp_h264_enc_out_frame_t *out_pair[2] = { &out0, &out1 };

    ESP_LOGI(TAG_H264_PM, "dual 2: enc_dual_hw_new");
    if (esp_h264_enc_dual_hw_new(&cfg, &enc) != ESP_H264_ERR_OK) {
        fail = "enc_dual_hw_new";
        goto cleanup;
    }
    ESP_LOGI(TAG_H264_PM, "dual 3: enc_dual_open");
    if (esp_h264_enc_dual_open(enc) != ESP_H264_ERR_OK) {
        fail = "enc_dual_open";
        goto cleanup;
    }

    if (read_enc_cb(&in0, aw0, ah0, pic) <= 0 || read_enc_cb(&in1, aw1, ah1, pic) <= 0) {
        fail = "read_enc_cb";
        goto cleanup;
    }
    ESP_LOGI(TAG_H264_PM, "dual 4: enc_dual_process #1");
    if (esp_h264_enc_dual_process(enc, in_pair, out_pair) != ESP_H264_ERR_OK) {
        fail = "enc_dual_process_1";
        goto cleanup;
    }
    if (read_enc_cb(&in0, aw0, ah0, pic) <= 0 || read_enc_cb(&in1, aw1, ah1, pic) <= 0) {
        fail = "read_enc_cb_2";
        goto cleanup;
    }
    if (esp_h264_enc_dual_process(enc, in_pair, out_pair) != ESP_H264_ERR_OK) {
        fail = "enc_dual_process_2";
        goto cleanup;
    }

    ESP_LOGI(TAG_H264_PM, "dual 5: before light sleep");
    h264_pm_log_retention("dual_before_sleep");
#if CONFIG_ESP_SLEEP_DEBUG
    esp_sleep_context_t sleep_ctx_dual = { 0 };
    if (h264_run_light_sleep_with_context(&sleep_ctx_dual) != ESP_OK) {
        fail = "light_sleep";
        goto cleanup;
    }
    ESP_LOGI(TAG_H264_PM, "dual sleep_request_result=%d sleep_flags=0x%lx",
             sleep_ctx_dual.sleep_request_result, (unsigned long)sleep_ctx_dual.sleep_flags);
    if (sleep_ctx_dual.sleep_request_result != 0) {
        fail = "sleep_request_result";
        goto cleanup;
    }
#if CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP
    if ((sleep_ctx_dual.sleep_flags & PMU_SLEEP_PD_TOP) == 0) {
        fail = "PMU_SLEEP_PD_TOP";
        goto cleanup;
    }
#endif
#else
    if (h264_run_light_sleep_simple() != ESP_OK) {
        fail = "light_sleep";
        goto cleanup;
    }
#endif

    ESP_LOGI(TAG_H264_PM, "dual 6: after sleep, dual process loop");
    for (int i = 0; i < 10; i++) {
        if (read_enc_cb(&in0, aw0, ah0, pic) <= 0 || read_enc_cb(&in1, aw1, ah1, pic) <= 0) {
            fail = "read_enc_cb_loop";
            goto cleanup;
        }
        if (esp_h264_enc_dual_process(enc, in_pair, out_pair) != ESP_H264_ERR_OK) {
            fail = "enc_dual_process_after_sleep";
            goto cleanup;
        }
        if (i == 0 && (out0.frame_type != ESP_H264_FRAME_TYPE_IDR ||
                       out1.frame_type != ESP_H264_FRAME_TYPE_IDR)) {
            fail = "first_frame_not_idr";
            goto cleanup;
        }
    }

    ESP_LOGI(TAG_H264_PM, "dual 7: done");

cleanup:
    if (enc) {
        (void)esp_h264_enc_dual_close(enc);
        (void)esp_h264_enc_dual_del(enc);
        enc = NULL;
    }
    if (in0.raw_data.buffer) {
        esp_h264_free(in0.raw_data.buffer);
    }
    if (in1.raw_data.buffer) {
        esp_h264_free(in1.raw_data.buffer);
    }
    if (out0.raw_data.buffer) {
        esp_h264_free(out0.raw_data.buffer);
    }
    if (out1.raw_data.buffer) {
        esp_h264_free(out1.raw_data.buffer);
    }
    h264_pm_result_set(fail ? fail : "PASS_dual");
    TEST_ASSERT_NULL_MESSAGE(fail, fail);
}

#endif /* CONFIG_PM_ENABLE && CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP && CONFIG_IDF_TARGET_ESP32P4 */
