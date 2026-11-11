/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The second encoder create idles a DMA whose clocks are already on,
 * then pulses the module reset. Both frames must encode.
 */

#include "unity.h"
#include "esp_h264_alloc.h"
#include "esp_h264_enc_single_hw.h"
#include "h264_io.h"

static void h264_fill_frame(esp_h264_enc_in_frame_t *in_frame, int16_t width, int16_t height, esp_h264_raw_format_t pic_type)
{
    if (read_enc_cb(in_frame, width, height, pic_type) <= 0) {
        TEST_ASSERT_GREATER_THAN(0, read_enc_cb(in_frame, width, height, pic_type));
    }
}

static esp_h264_err_t h264_encode_one(esp_h264_enc_cfg_hw_t *cfg, esp_h264_enc_in_frame_t *in_frame, esp_h264_enc_out_frame_t *out_frame)
{
    esp_h264_enc_handle_t enc = NULL;
    esp_h264_err_t ret = esp_h264_enc_hw_new(cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        return ret;
    }
    ret = esp_h264_enc_open(enc);
    if (ret == ESP_H264_ERR_OK) {
        h264_fill_frame(in_frame, cfg->res.width, cfg->res.height, cfg->pic_type);
        ret = esp_h264_enc_process(enc, in_frame, out_frame);
    }
    esp_h264_enc_close(enc);
    esp_h264_enc_del(enc);
    return ret;
}

TEST_CASE("hw_enc_dma_idle_before_reset_test", "[esp_h264]")
{
    const int16_t width = 128;
    const int16_t height = 128;
    esp_h264_enc_cfg_hw_t cfg = {
        .pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY,
        .gop = 30,
        .fps = 30,
        .res = {
            .width = width,
            .height = height,
        },
        .rc = {
            .bitrate = width * height * 30 / 20,
            .qp_min = 26,
            .qp_max = 26,
        },
    };
    esp_h264_enc_in_frame_t in_frame = { 0 };
    esp_h264_enc_out_frame_t out_frame = { 0 };
    uint16_t mb_w = (width + 15) & ~15;
    uint16_t mb_h = (height + 15) & ~15;
    in_frame.raw_data.len = (int)((float)mb_w * mb_h * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame.raw_data.len, &in_frame.raw_data.len, ESP_H264_MEM_INTERNAL_DMA);
    TEST_ASSERT_NOT_NULL(in_frame.raw_data.buffer);
    out_frame.raw_data.len = in_frame.raw_data.len;
    out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame.raw_data.len, &out_frame.raw_data.len, ESP_H264_MEM_INTERNAL_DMA);
    TEST_ASSERT_NOT_NULL(out_frame.raw_data.buffer);

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, h264_encode_one(&cfg, &in_frame, &out_frame));
    TEST_ASSERT_GREATER_THAN(0, out_frame.length);
    out_frame.length = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, h264_encode_one(&cfg, &in_frame, &out_frame));
    TEST_ASSERT_GREATER_THAN(0, out_frame.length);

    esp_h264_free(in_frame.raw_data.buffer);
    esp_h264_free(out_frame.raw_data.buffer);
}
