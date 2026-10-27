/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "esp_heap_caps.h"
#include "esp_h264_alloc.h"
#include "esp_h264_enc_single_hw.h"
#include "esp_h264_enc_single.h"
#include "esp_h264_enc_dual_hw.h"
#include "soc/h264_dma_struct.h"

#if CONFIG_IDF_TARGET_ESP32P4

/* Same formulas as max_refame_buffer_size() / esp_h264_enc_hw_max_db_tmp_buffer_size(). */
#define DMA_RTC_DB4_ROW          (16 * 4 + 8 * 4 * 2)
#define DMA_RTC_REF_PER_MB       (3 * 16 * (16 + 8))
#define DMA_RTC_ALIGN_SLACK      (7)
#define DMA_RTC_DB_TMP_EXTRA     (7 + 8 + 64)
#define DMA_RTC_CACHE_ALIGN      (64)
/* Heap header is not part of get_largest_free_block(); slack must cover it
 * and the small NAL buffer, and stay below db_tmp so that buffer cannot. */
#define DMA_RTC_HOLE_SLACK       (4096)
#define DMA_RTC_SWALLOW_MIN      (1024)
#define DMA_RTC_HOLD_MAX         (512)
#define DMA_RTC_CAPS             (MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA)

/* h264_dma_ll.h: H264_DMA_LL_GET_HW() */
#define DMA_RTC_H264_DMA         ((h264_dma_dev_t *)0x500A7000)

static size_t dma_rtc_align_up(size_t n)
{
    return (n + (DMA_RTC_CACHE_ALIGN - 1)) & ~(size_t)(DMA_RTC_CACHE_ALIGN - 1);
}

static int dma_rtc_hold_rest(void **hold, size_t *held)
{
    size_t chunk = 64 * 1024;
    while (chunk >= DMA_RTC_SWALLOW_MIN) {
        for (;;) {
            if (*held >= DMA_RTC_HOLD_MAX) {
                return -1;
            }
            void *p = heap_caps_malloc(chunk, DMA_RTC_CAPS);
            if (p == NULL) {
                break;
            }
            hold[(*held)++] = p;
        }
        chunk /= 2;
    }
    return 0;
}

static void dma_rtc_free_hold(void **hold, size_t held)
{
    for (size_t i = 0; i < held; i++) {
        heap_caps_free(hold[i]);
    }
    heap_caps_free(hold);
}

/*
 * Leave one DMA-capable L2 hole large enough for the reference window and
 * nothing else big enough for db_tmp. RTC RAM / SPM stay free. On the
 * unfixed encoder, db_tmp is requested as MALLOC_CAP_INTERNAL only and the
 * heap serves it from RTC RAM (0x50108000), which the H.264 DMA cannot write.
 * The fixed encoder must fail esp_h264_enc_hw_new() with ESP_H264_ERR_MEM
 * and leave RTC RAM unused.
 */
TEST_CASE("hw_enc_internal_dma_not_rtc", "[esp_h264][dma_rtc]")
{
    const uint16_t width = 1280;
    const uint16_t height = 720;
    const uint16_t mb_width = (width + 15) >> 4;
    const size_t ref_raw = (size_t)DMA_RTC_REF_PER_MB * mb_width + DMA_RTC_ALIGN_SLACK;
    const size_t db_tmp_raw = (size_t)mb_width * DMA_RTC_DB4_ROW + DMA_RTC_DB_TMP_EXTRA;
    const size_t ref_aligned = dma_rtc_align_up(ref_raw);
    const size_t db_tmp_aligned = dma_rtc_align_up(db_tmp_raw);
    const size_t hole = ref_aligned + DMA_RTC_HOLE_SLACK;

    void **hold = heap_caps_calloc(DMA_RTC_HOLD_MAX, sizeof(void *), MALLOC_CAP_SPIRAM);
    TEST_ASSERT_NOT_NULL(hold);
    size_t held = 0;
    void *keep = heap_caps_aligned_alloc(DMA_RTC_CACHE_ALIGN, hole, DMA_RTC_CAPS);
    int swallow = -1;
    if (keep != NULL) {
        swallow = dma_rtc_hold_rest(hold, &held);
        heap_caps_free(keep);
        keep = NULL;
    }

    const size_t largest_dma = heap_caps_get_largest_free_block(DMA_RTC_CAPS);
    const size_t rtc_before = heap_caps_get_free_size(MALLOC_CAP_RTCRAM);
    const size_t spm_before = heap_caps_get_free_size(MALLOC_CAP_SPM);
    const bool hole_ok = (swallow == 0) && (largest_dma >= ref_aligned) &&
                         (largest_dma < ref_aligned + db_tmp_aligned);

    esp_h264_enc_handle_t enc = NULL;
    esp_h264_err_t ret = ESP_H264_ERR_FAIL;
    uint32_t idr_len = 0;
    uint32_t p_len = 0;
    uint32_t axi_err = 0;
    esp_h264_enc_in_frame_t in_frame = { 0 };
    esp_h264_enc_out_frame_t out_frame = { 0 };
    uint32_t out_cap = 0;

    if (hole_ok) {
        esp_h264_enc_cfg_hw_t cfg = { 0 };
        cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        cfg.gop = 30;
        cfg.fps = 30;
        cfg.res.width = width;
        cfg.res.height = height;
        cfg.rc.bitrate = (uint32_t)width * height * cfg.fps / 20;
        cfg.rc.qp_min = 26;
        cfg.rc.qp_max = 26;

        ret = esp_h264_enc_hw_new(&cfg, &enc);
    }

    const size_t rtc_after = heap_caps_get_free_size(MALLOC_CAP_RTCRAM);
    const size_t spm_after = heap_caps_get_free_size(MALLOC_CAP_SPM);
    const size_t rtc_used = (rtc_before > rtc_after) ? (rtc_before - rtc_after) : 0;
    const size_t spm_used = (spm_before > spm_after) ? (spm_before - spm_after) : 0;

    if (ret == ESP_H264_ERR_OK && enc != NULL) {
        const esp_h264_raw_format_t pic = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        in_frame.raw_data.len = (uint32_t)((float)width * height * ESP_H264_GET_BPP_BY_PIC_TYPE(pic));
        in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame.raw_data.len,
                                   &in_frame.raw_data.len, ESP_H264_MEM_SPIRAM);
        out_cap = in_frame.raw_data.len;
        out_frame.raw_data.len = out_cap;
        out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_cap, &out_frame.raw_data.len,
                                    ESP_H264_MEM_SPIRAM);
        if (in_frame.raw_data.buffer != NULL && out_frame.raw_data.buffer != NULL &&
                esp_h264_enc_open(enc) == ESP_H264_ERR_OK) {
            for (uint32_t i = 0; i < in_frame.raw_data.len; i++) {
                in_frame.raw_data.buffer[i] = (uint8_t)(16 + (i & 0x7f));
            }
            out_frame.raw_data.len = out_cap;
            if (esp_h264_enc_process(enc, &in_frame, &out_frame) == ESP_H264_ERR_OK) {
                idr_len = out_frame.length;
            }
            out_frame.raw_data.len = out_cap;
            if (esp_h264_enc_process(enc, &in_frame, &out_frame) == ESP_H264_ERR_OK) {
                p_len = out_frame.length;
            }
            axi_err = DMA_RTC_H264_DMA->inter_axi_err.val;
            esp_h264_enc_close(enc);
        }
    }

    if (enc != NULL) {
        esp_h264_enc_del(enc);
    }
    if (in_frame.raw_data.buffer != NULL) {
        esp_h264_free(in_frame.raw_data.buffer);
    }
    if (out_frame.raw_data.buffer != NULL) {
        esp_h264_free(out_frame.raw_data.buffer);
    }
    dma_rtc_free_hold(hold, held);

    printf("hw_enc_dma_rtc: hole_ok=%d swallow=%d largest_dma=%u ref=%u db_tmp=%u "
           "rtc %u -> %u (used %u) spm %u -> %u (used %u) ret=%d idr=%u p=%u axi=0x%08x\n",
           hole_ok, swallow, (unsigned)largest_dma, (unsigned)ref_aligned, (unsigned)db_tmp_aligned,
           (unsigned)rtc_before, (unsigned)rtc_after, (unsigned)rtc_used,
           (unsigned)spm_before, (unsigned)spm_after, (unsigned)spm_used,
           (int)ret, (unsigned)idr_len, (unsigned)p_len, (unsigned)axi_err);

    char msg[192];
    snprintf(msg, sizeof(msg),
             "DMA hole %u, RTC used %u, SPM used %u, ret %d, P frame %u, axi 0x%08x",
             (unsigned)largest_dma, (unsigned)rtc_used, (unsigned)spm_used,
             (int)ret, (unsigned)p_len, (unsigned)axi_err);

    TEST_ASSERT_MESSAGE(hole_ok, msg);
    TEST_ASSERT_EQUAL_MESSAGE(ESP_H264_ERR_MEM, ret, msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)rtc_used, msg);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, (uint32_t)spm_used, msg);
}

/* 640-wide db_tmp is about 5 KB and fits in the 8 KB SPM. 720p does not. */
TEST_CASE("hw_enc_internal_dma_not_spm", "[esp_h264][dma_rtc]")
{
    const uint16_t width = 640;
    const uint16_t height = 480;
    const uint16_t mb_width = (width + 15) >> 4;
    const size_t ref_raw = (size_t)DMA_RTC_REF_PER_MB * mb_width + DMA_RTC_ALIGN_SLACK;
    const size_t db_tmp_raw = (size_t)mb_width * DMA_RTC_DB4_ROW + DMA_RTC_DB_TMP_EXTRA;
    const size_t ref_aligned = dma_rtc_align_up(ref_raw);
    const size_t db_tmp_aligned = dma_rtc_align_up(db_tmp_raw);
    const size_t hole = ref_aligned + DMA_RTC_HOLE_SLACK;

    void **hold = heap_caps_calloc(DMA_RTC_HOLD_MAX, sizeof(void *), MALLOC_CAP_SPIRAM);
    TEST_ASSERT_NOT_NULL(hold);
    size_t held = 0;
    void *keep = heap_caps_aligned_alloc(DMA_RTC_CACHE_ALIGN, hole, DMA_RTC_CAPS);
    int swallow = -1;
    if (keep != NULL) {
        swallow = dma_rtc_hold_rest(hold, &held);
        heap_caps_free(keep);
    }
    const size_t largest_dma = heap_caps_get_largest_free_block(DMA_RTC_CAPS);
    const bool hole_ok = (swallow == 0) && (largest_dma >= ref_aligned) &&
                         (largest_dma < ref_aligned + db_tmp_aligned) &&
                         (db_tmp_aligned <= 8192);
    const size_t rtc_before = heap_caps_get_free_size(MALLOC_CAP_RTCRAM);
    const size_t spm_before = heap_caps_get_free_size(MALLOC_CAP_SPM);
    esp_h264_enc_handle_t enc = NULL;
    esp_h264_err_t ret = ESP_H264_ERR_FAIL;
    if (hole_ok) {
        esp_h264_enc_cfg_hw_t cfg = { 0 };
        cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        cfg.gop = 30;
        cfg.fps = 30;
        cfg.res.width = width;
        cfg.res.height = height;
        cfg.rc.bitrate = (uint32_t)width * height * cfg.fps / 20;
        cfg.rc.qp_min = 26;
        cfg.rc.qp_max = 26;
        ret = esp_h264_enc_hw_new(&cfg, &enc);
    }
    if (enc != NULL) {
        esp_h264_enc_del(enc);
    }
    const size_t rtc_used = heap_caps_get_free_size(MALLOC_CAP_RTCRAM);
    const size_t spm_used = heap_caps_get_free_size(MALLOC_CAP_SPM);
    dma_rtc_free_hold(hold, held);
    TEST_ASSERT_MESSAGE(hole_ok, "SPM-sized hole was not created");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_H264_ERR_MEM, ret, "640x480 encoder should fail closed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)rtc_before, (uint32_t)rtc_used, "RTC changed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)spm_before, (uint32_t)spm_used, "SPM changed");
}

TEST_CASE("hw_enc_dual_dma_fail_no_leak", "[esp_h264][dma_rtc]")
{
    const uint16_t width = 1280;
    const uint16_t height = 720;
    const uint16_t mb_width = (width + 15) >> 4;
    const size_t ref_aligned = dma_rtc_align_up((size_t)DMA_RTC_REF_PER_MB * mb_width + DMA_RTC_ALIGN_SLACK);
    const size_t db_tmp_aligned = dma_rtc_align_up((size_t)mb_width * DMA_RTC_DB4_ROW + DMA_RTC_DB_TMP_EXTRA);
    const size_t hole = ref_aligned + DMA_RTC_HOLE_SLACK;

    void **hold = heap_caps_calloc(DMA_RTC_HOLD_MAX, sizeof(void *), MALLOC_CAP_SPIRAM);
    TEST_ASSERT_NOT_NULL(hold);
    size_t held = 0;
    void *keep = heap_caps_aligned_alloc(DMA_RTC_CACHE_ALIGN, hole, DMA_RTC_CAPS);
    int swallow = -1;
    if (keep != NULL) {
        swallow = dma_rtc_hold_rest(hold, &held);
        heap_caps_free(keep);
    }
    const size_t largest_dma = heap_caps_get_largest_free_block(DMA_RTC_CAPS);
    const bool hole_ok = (swallow == 0) && (largest_dma >= ref_aligned) &&
                         (largest_dma < ref_aligned + db_tmp_aligned);
    const size_t dma_before = heap_caps_get_free_size(DMA_RTC_CAPS);
    const size_t spiram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    esp_h264_enc_dual_handle_t enc = NULL;
    esp_h264_err_t ret = ESP_H264_ERR_FAIL;
    if (hole_ok) {
        esp_h264_enc_cfg_dual_hw_t cfg = { 0 };
        cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        cfg.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        cfg.cfg0.gop = 30;
        cfg.cfg1.gop = 30;
        cfg.cfg0.fps = 30;
        cfg.cfg1.fps = 30;
        cfg.cfg0.res.width = width;
        cfg.cfg0.res.height = height;
        cfg.cfg1.res.width = width;
        cfg.cfg1.res.height = height;
        cfg.cfg0.rc.bitrate = (uint32_t)width * height * cfg.cfg0.fps / 20;
        cfg.cfg1.rc.bitrate = cfg.cfg0.rc.bitrate;
        cfg.cfg0.rc.qp_min = 26;
        cfg.cfg0.rc.qp_max = 26;
        cfg.cfg1.rc.qp_min = 26;
        cfg.cfg1.rc.qp_max = 26;
        ret = esp_h264_enc_dual_hw_new(&cfg, &enc);
    }
    if (enc != NULL) {
        esp_h264_enc_dual_del(enc);
    }
    const size_t dma_after = heap_caps_get_free_size(DMA_RTC_CAPS);
    const size_t spiram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    dma_rtc_free_hold(hold, held);

    TEST_ASSERT_MESSAGE(hole_ok, "dual DMA hole was not created");
    TEST_ASSERT_EQUAL_MESSAGE(ESP_H264_ERR_MEM, ret, "dual encoder should fail closed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)dma_before, (uint32_t)dma_after, "DMA heap leaked");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)spiram_before, (uint32_t)spiram_after, "SPIRAM leaked");
}

#endif
