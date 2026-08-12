/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_h264_hw_enc_test.h"
#include "esp_h264_alloc.h"
#include "h264_io.h"

#define HW_ENC_OPEN_TIME_ROUNDS 10

static int write_mvm(esp_h264_enc_mvm_pkt_t *mv_pkt, uint32_t length)
{
    return 1;
}

typedef struct {
    const uint8_t *data;
    size_t         size;
    size_t         bit_pos;
} sps_bs_t;

static int sps_bs_eof(const sps_bs_t *bs, uint32_t nbits)
{
    return (bs->bit_pos + nbits) > (bs->size * 8);
}

static uint32_t sps_bs_read_u(sps_bs_t *bs, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) {
        if (sps_bs_eof(bs, 1)) {
            return 0;
        }
        size_t byte_i = bs->bit_pos / 8;
        int bit_i = 7 - (int)(bs->bit_pos % 8);
        v = (v << 1) | ((bs->data[byte_i] >> bit_i) & 0x1);
        bs->bit_pos++;
    }
    return v;
}

static uint32_t sps_bs_read_ue(sps_bs_t *bs)
{
    int zeros = 0;
    while (!sps_bs_eof(bs, 1) && sps_bs_read_u(bs, 1) == 0) {
        zeros++;
        if (zeros > 31) {
            return 0;
        }
    }
    if (zeros == 0) {
        return 0;
    }
    return ((1U << zeros) - 1U) + sps_bs_read_u(bs, (uint32_t)zeros);
}

/**
 * @brief  Strip Annex-B `emulation_prevention_three_byte` (0x03) values, i.e.
 *         convert EBSP back to RBSP, exactly as a conformant decoder does
 *         before parsing any syntax element (H.264 7.3.1 / 7.4.1.1).
 */
static size_t strip_emulation_prevention(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_cap)
{
    size_t out_len = 0;
    int zero_run = 0;
    for (size_t i = 0; i < src_len && out_len < dst_cap; i++) {
        uint8_t b = src[i];
        if (zero_run >= 2 && b == 0x03) {
            zero_run = 0;
            continue;
        }
        dst[out_len++] = b;
        zero_run = (b == 0) ? (zero_run + 1) : 0;
    }
    return out_len;
}

/**
 * Parse fps from Baseline SPS VUI timing_info:
 * fps = time_scale / (2 * num_units_in_tick)
 *
 * @return parsed fps on success, 0 on failure
 */
static uint8_t parse_sps_vui_timing_fps(const uint8_t *nal, size_t nal_bytes)
{
    if (nal == NULL || nal_bytes < 5) {
        return 0;
    }

    size_t off = 0;
    if (nal_bytes >= 4 && nal[0] == 0x00 && nal[1] == 0x00 && nal[2] == 0x00 && nal[3] == 0x01) {
        off = 4;
    } else if (nal_bytes >= 3 && nal[0] == 0x00 && nal[1] == 0x00 && nal[2] == 0x01) {
        off = 3;
    }

    /* De-escape into a scratch RBSP buffer before parsing; the SPS RBSP itself
     * is always small (well under 256 bytes even with VUI timing info). */
    uint8_t rbsp[256];
    size_t rbsp_len = strip_emulation_prevention(nal + off, nal_bytes - off, rbsp, sizeof(rbsp));

    sps_bs_t bs = {
        .data = rbsp,
        .size = rbsp_len,
        .bit_pos = 0,
    };

    (void)sps_bs_read_u(&bs, 1); /* forbidden_zero_bit */
    (void)sps_bs_read_u(&bs, 2); /* nal_ref_idc */
    if (sps_bs_read_u(&bs, 5) != 7) {
        return 0;
    }

    uint32_t profile_idc = sps_bs_read_u(&bs, 8);
    (void)sps_bs_read_u(&bs, 8); /* constraint_set + reserved */
    (void)sps_bs_read_u(&bs, 8); /* level_idc */
    (void)sps_bs_read_ue(&bs);   /* seq_parameter_set_id */

    /* Encoder writes Baseline(66); High profile extensions are unsupported here */
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 || profile_idc == 244 ||
            profile_idc == 44 || profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
            profile_idc == 128 || profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
            profile_idc == 135) {
        return 0;
    }

    (void)sps_bs_read_ue(&bs); /* log2_max_frame_num_minus4 */
    uint32_t pic_order_cnt_type = sps_bs_read_ue(&bs);
    if (pic_order_cnt_type == 0) {
        (void)sps_bs_read_ue(&bs);
    } else if (pic_order_cnt_type == 1) {
        return 0;
    }
    (void)sps_bs_read_ue(&bs); /* num_ref_frames */
    (void)sps_bs_read_u(&bs, 1);
    (void)sps_bs_read_ue(&bs); /* pic_width_in_mbs_minus1 */
    (void)sps_bs_read_ue(&bs); /* pic_height_in_map_units_minus1 */
    if (!sps_bs_read_u(&bs, 1)) {
        (void)sps_bs_read_u(&bs, 1);
    }
    (void)sps_bs_read_u(&bs, 1); /* direct_8x8_inference_flag */
    if (sps_bs_read_u(&bs, 1)) {
        (void)sps_bs_read_ue(&bs);
        (void)sps_bs_read_ue(&bs);
        (void)sps_bs_read_ue(&bs);
        (void)sps_bs_read_ue(&bs);
    }

    if (!sps_bs_read_u(&bs, 1)) {
        return 0; /* vui_parameters_present_flag */
    }
    if (sps_bs_read_u(&bs, 1)) { /* aspect_ratio_info_present_flag */
        if (sps_bs_read_u(&bs, 8) == 255) {
            (void)sps_bs_read_u(&bs, 16);
            (void)sps_bs_read_u(&bs, 16);
        }
    }
    if (sps_bs_read_u(&bs, 1)) {
        (void)sps_bs_read_u(&bs, 1);
    }
    if (sps_bs_read_u(&bs, 1)) {
        (void)sps_bs_read_u(&bs, 3);
        if (sps_bs_read_u(&bs, 1)) {
            (void)sps_bs_read_u(&bs, 8);
            (void)sps_bs_read_u(&bs, 8);
            (void)sps_bs_read_u(&bs, 8);
        }
    }
    if (sps_bs_read_u(&bs, 1)) {
        (void)sps_bs_read_ue(&bs);
        (void)sps_bs_read_ue(&bs);
    }

    if (!sps_bs_read_u(&bs, 1)) {
        return 0; /* timing_info_present_flag */
    }
    uint32_t num_units_in_tick = sps_bs_read_u(&bs, 32);
    uint32_t time_scale = sps_bs_read_u(&bs, 32);
    (void)sps_bs_read_u(&bs, 1); /* fixed_frame_rate_flag */
    if (num_units_in_tick == 0) {
        return 0;
    }
    uint32_t fps = time_scale / (2U * num_units_in_tick);
    if (fps == 0 || fps > 255) {
        return 0;
    }
    return (uint8_t)fps;
}

uint8_t esp_h264_test_parse_sps_vui_fps(const uint8_t *nal, size_t nal_bytes)
{
    return parse_sps_vui_timing_fps(nal, nal_bytes);
}

bool esp_h264_test_annexb_has_forbidden_sequence(const uint8_t *buf, size_t len, size_t *out_offset)
{
    size_t i = 0;
    while (i < len) {
        size_t sc_len = 0;
        if (i + 4 <= len && buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 0 && buf[i + 3] == 1) {
            sc_len = 4;
        } else if (i + 3 <= len && buf[i] == 0 && buf[i + 1] == 0 && buf[i + 2] == 1) {
            sc_len = 3;
        } else {
            /* Not aligned on a start code: the stream itself is malformed. */
            if (out_offset) {
                *out_offset = i;
            }
            return true;
        }
        size_t nal_start = i + sc_len;
        size_t nal_end = len;
        for (size_t j = nal_start; j + 2 < len; j++) {
            bool is_3byte_sc = buf[j] == 0 && buf[j + 1] == 0 && buf[j + 2] == 1;
            bool is_4byte_sc = (j + 3 < len) && buf[j] == 0 && buf[j + 1] == 0 && buf[j + 2] == 0 && buf[j + 3] == 1;
            if (is_3byte_sc || is_4byte_sc) {
                nal_end = j;
                break;
            }
        }
        /* Mirror a real decoder's de-escaping: after two zero bytes, a 0x03 is the
         * legitimate emulation_prevention_three_byte and is consumed (not a violation);
         * 0x00-0x02 in that position means the encoder failed to escape it. */
        int zero_run = 0;
        for (size_t k = nal_start; k < nal_end; k++) {
            uint8_t b = buf[k];
            if (zero_run >= 2) {
                if (b <= 2) {
                    if (out_offset) {
                        *out_offset = k;
                    }
                    return true;
                } else if (b == 3) {
                    zero_run = 0;
                    continue;
                }
            }
            zero_run = (b == 0) ? (zero_run + 1) : 0;
        }
        i = nal_end;
    }
    return false;
}

static esp_h264_err_t check_out_frame_sps_fps(const esp_h264_enc_out_frame_t *out_frame, uint8_t expect_fps)
{
    if (out_frame == NULL || out_frame->raw_data.buffer == NULL || out_frame->length < 5) {
        printf("SPS fps check failed: empty frame. line %d\n", __LINE__);
        return ESP_H264_ERR_FAIL;
    }
    uint8_t sps_fps = parse_sps_vui_timing_fps(out_frame->raw_data.buffer, out_frame->length);
    if (sps_fps != expect_fps) {
        printf("SPS fps mismatch: expect %u, got %u. line %d\n", expect_fps, sps_fps, __LINE__);
        return ESP_H264_ERR_FAIL;
    }
    return ESP_H264_ERR_OK;
}

esp_h264_err_t single_hw_enc_process(esp_h264_enc_cfg_hw_t cfg)
{
    esp_h264_enc_in_frame_t in_frame = {0};
    esp_h264_enc_out_frame_t out_frame = {0};
    esp_h264_err_t ret = ESP_H264_ERR_OK;
    esp_h264_enc_handle_t enc = NULL;
    bool first_frame = true;
    uint16_t width = ((cfg.res.width + 15) >> 4 << 4);
    uint16_t height = ((cfg.res.height + 15) >> 4 << 4);
    in_frame.raw_data.len = (int)((float)width * height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame.raw_data.len, &in_frame.raw_data.len, ESP_H264_MEM_INTERNAL);
    if (!in_frame.raw_data.buffer) {
        printf("mem allocation failed.line %d %d %d %d\n", width,  height, (int)in_frame.raw_data.len, __LINE__);
        goto _exit_;
    }
    out_frame.raw_data.len = in_frame.raw_data.len;
    out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame.raw_data.len, &out_frame.raw_data.len, ESP_H264_MEM_INTERNAL);
    if (!out_frame.raw_data.buffer) {
        printf("mem allocation failed.line %d \n", __LINE__);
        goto _exit_;
    }
    ret = esp_h264_enc_hw_new(&cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("new failed. line %d \n", __LINE__);
        goto _exit_;
    }
    ret = esp_h264_enc_open(enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("open failed .line %d \n", __LINE__);
        goto _exit_;
    }
    while (1) {
        int ret_w = read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type);
        if (ret_w <= 0) {
            break;
        }
        ret = esp_h264_enc_process(enc, &in_frame, &out_frame);
        if (ret != ESP_H264_ERR_OK) {
            printf("process failed. line %d \n", __LINE__);
            goto _exit_;
        }
        if (first_frame) {
            ret = check_out_frame_sps_fps(&out_frame, cfg.fps);
            if (ret != ESP_H264_ERR_OK) {
                goto _exit_;
            }
            first_frame = false;
        }
        write_enc_cb(&out_frame);
    }
_exit_:
    if (enc) {
        esp_h264_err_t close_ret = esp_h264_enc_close(enc);
        esp_h264_err_t del_ret = esp_h264_enc_del(enc);
        if (ret == ESP_H264_ERR_OK) {
            ret = (close_ret != ESP_H264_ERR_OK) ? close_ret : del_ret;
        }
    }
    if (in_frame.raw_data.buffer) {
        esp_h264_free(in_frame.raw_data.buffer);
    }
    if (out_frame.raw_data.buffer) {
        esp_h264_free(out_frame.raw_data.buffer);
    }
    return ret;
}

esp_h264_err_t dual_hw_enc_process(esp_h264_enc_cfg_dual_hw_t cfg)
{
    esp_h264_enc_in_frame_t *in_frame[2] = {NULL, NULL};
    esp_h264_enc_out_frame_t *out_frame[2] = {NULL, NULL};
    esp_h264_err_t ret = ESP_H264_ERR_OK;
    esp_h264_enc_dual_handle_t enc = NULL;
    bool first_frame = true;
    int32_t out_length[2];
    int16_t width[2] = { ((cfg.cfg0.res.width + 15) >> 4 << 4), ((cfg.cfg1.res.width + 15) >> 4 << 4)};
    int16_t height[2] = { ((cfg.cfg0.res.height + 15) >> 4 << 4), ((cfg.cfg1.res.height + 15) >> 4 << 4)};
    uint8_t expect_fps[2] = { cfg.cfg0.fps, cfg.cfg1.fps };
    out_length[0] = width[0] * height[0] * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg0.pic_type);
    out_length[1] = width[1] * height[1] * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg1.pic_type);
    for (int16_t i = 0; i < 2; i++) {
        in_frame[i] = heap_caps_calloc(1, sizeof(esp_h264_enc_in_frame_t), ESP_H264_MEM_INTERNAL);
        if (!in_frame[i]) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_frame[i] = heap_caps_calloc(1, sizeof(esp_h264_enc_out_frame_t), ESP_H264_MEM_INTERNAL);
        if (!out_frame[i]) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        in_frame[i]->raw_data.len = out_length[i];
        in_frame[i]->raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame[i]->raw_data.len, &in_frame[i]->raw_data.len, ESP_H264_MEM_INTERNAL);
        if (!in_frame[i]->raw_data.buffer) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_frame[i]->raw_data.len = out_length[i];
        out_frame[i]->raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame[i]->raw_data.len, &out_frame[i]->raw_data.len, ESP_H264_MEM_INTERNAL);
        if (!out_frame[i]->raw_data.buffer) {
            printf("mem allocation failed. line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_length[i] = out_frame[i]->raw_data.len;
    }

    ret = esp_h264_enc_dual_hw_new(&cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("new failed. line %d \n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_open(enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("open failed .line %d \n", __LINE__);
        goto _exit_dual_;
    }
    while (1) {
        for (int16_t i = 0; i < 2; i++) {
            esp_h264_enc_cfg_hw_t cfg_tmp = i == 0 ? cfg.cfg0 : cfg.cfg1;
            int ret_w = read_enc_cb(in_frame[i], width[i], height[i], cfg_tmp.pic_type);
            if (ret_w <= 0) {
                goto _exit_dual_;
            }
        }
        ret = esp_h264_enc_dual_process(enc, in_frame, out_frame);
        if (ret != ESP_H264_ERR_OK) {
            printf("process failed. line %d \n", __LINE__);
            goto _exit_dual_;
        }
        if (first_frame) {
            for (int16_t i = 0; i < 2; i++) {
                ret = check_out_frame_sps_fps(out_frame[i], expect_fps[i]);
                if (ret != ESP_H264_ERR_OK) {
                    printf("dual stream%d SPS fps check failed. line %d\n", i, __LINE__);
                    goto _exit_dual_;
                }
            }
            first_frame = false;
        }
        for (int16_t i = 0; i < 2; i++) {
            write_enc_cb(out_frame[i]);
        }
    }
_exit_dual_:
    if (enc) {
        esp_h264_err_t close_ret = esp_h264_enc_dual_close(enc);
        esp_h264_err_t del_ret = esp_h264_enc_dual_del(enc);
        if (ret == ESP_H264_ERR_OK) {
            ret = (close_ret != ESP_H264_ERR_OK) ? close_ret : del_ret;
        }
    }
    for (int16_t i = 0; i < 2; i++) {
        if (in_frame[i]) {
            if (in_frame[i]->raw_data.buffer) {
                esp_h264_free(in_frame[i]->raw_data.buffer);
                esp_h264_free(in_frame[i]);
            }
        }
        if (out_frame[i]) {
            if (out_frame[i]->raw_data.buffer) {
                esp_h264_free(out_frame[i]->raw_data.buffer);
                esp_h264_free(out_frame[i]);
            }
        }
    }
    return ret;
}

esp_h264_err_t single_hw_enc_open_time_test(esp_h264_enc_cfg_hw_t cfg)
{
    esp_h264_err_t ret = ESP_H264_ERR_OK;
    int64_t new_sum_us = 0;
    int64_t open_sum_us = 0;
    int64_t new_max_us = 0;
    int64_t open_max_us = 0;
    int64_t new_min_us = INT64_MAX;
    int64_t open_min_us = INT64_MAX;

    for (int i = 0; i < HW_ENC_OPEN_TIME_ROUNDS; i++) {
        esp_h264_enc_handle_t enc = NULL;
        int64_t t0 = esp_timer_get_time();
        ret = esp_h264_enc_hw_new(&cfg, &enc);
        int64_t t1 = esp_timer_get_time();
        if (ret != ESP_H264_ERR_OK) {
            printf("new failed. round %d line %d\n", i, __LINE__);
            return ret;
        }

        int64_t t2 = esp_timer_get_time();
        ret = esp_h264_enc_open(enc);
        int64_t t3 = esp_timer_get_time();
        if (ret != ESP_H264_ERR_OK) {
            printf("open failed. round %d line %d\n", i, __LINE__);
            esp_h264_enc_del(enc);
            return ret;
        }

        int64_t new_us = t1 - t0;
        int64_t open_us = t3 - t2;
        new_sum_us += new_us;
        open_sum_us += open_us;
        if (new_us > new_max_us) {
            new_max_us = new_us;
        }
        if (open_us > open_max_us) {
            open_max_us = open_us;
        }
        if (new_us < new_min_us) {
            new_min_us = new_us;
        }
        if (open_us < open_min_us) {
            open_min_us = open_us;
        }

        ret = esp_h264_enc_close(enc);
        if (ret != ESP_H264_ERR_OK) {
            printf("close failed. round %d line %d\n", i, __LINE__);
            esp_h264_enc_del(enc);
            return ret;
        }
        ret = esp_h264_enc_del(enc);
        if (ret != ESP_H264_ERR_OK) {
            printf("del failed. round %d line %d\n", i, __LINE__);
            return ret;
        }
    }

    printf("single_hw_enc open time: res=%dx%d rounds=%d "
           "new(avg/min/max)=%lld/%lld/%lld us "
           "open(avg/min/max)=%lld/%lld/%lld us "
           "new+open_avg=%lld us\n",
           cfg.res.width, cfg.res.height, HW_ENC_OPEN_TIME_ROUNDS,
           (long long)(new_sum_us / HW_ENC_OPEN_TIME_ROUNDS),
           (long long)new_min_us, (long long)new_max_us,
           (long long)(open_sum_us / HW_ENC_OPEN_TIME_ROUNDS),
           (long long)open_min_us, (long long)open_max_us,
           (long long)((new_sum_us + open_sum_us) / HW_ENC_OPEN_TIME_ROUNDS));
    return ESP_H264_ERR_OK;
}

esp_h264_err_t dual_hw_enc_open_time_test(esp_h264_enc_cfg_dual_hw_t cfg)
{
    esp_h264_err_t ret = ESP_H264_ERR_OK;
    int64_t new_sum_us = 0;
    int64_t open_sum_us = 0;
    int64_t new_max_us = 0;
    int64_t open_max_us = 0;
    int64_t new_min_us = INT64_MAX;
    int64_t open_min_us = INT64_MAX;

    for (int i = 0; i < HW_ENC_OPEN_TIME_ROUNDS; i++) {
        esp_h264_enc_dual_handle_t enc = NULL;
        int64_t t0 = esp_timer_get_time();
        ret = esp_h264_enc_dual_hw_new(&cfg, &enc);
        int64_t t1 = esp_timer_get_time();
        if (ret != ESP_H264_ERR_OK) {
            printf("dual new failed. round %d line %d\n", i, __LINE__);
            return ret;
        }

        int64_t t2 = esp_timer_get_time();
        ret = esp_h264_enc_dual_open(enc);
        int64_t t3 = esp_timer_get_time();
        if (ret != ESP_H264_ERR_OK) {
            printf("dual open failed. round %d line %d\n", i, __LINE__);
            esp_h264_enc_dual_del(enc);
            return ret;
        }

        int64_t new_us = t1 - t0;
        int64_t open_us = t3 - t2;
        new_sum_us += new_us;
        open_sum_us += open_us;
        if (new_us > new_max_us) {
            new_max_us = new_us;
        }
        if (open_us > open_max_us) {
            open_max_us = open_us;
        }
        if (new_us < new_min_us) {
            new_min_us = new_us;
        }
        if (open_us < open_min_us) {
            open_min_us = open_us;
        }

        ret = esp_h264_enc_dual_close(enc);
        if (ret != ESP_H264_ERR_OK) {
            printf("dual close failed. round %d line %d\n", i, __LINE__);
            esp_h264_enc_dual_del(enc);
            return ret;
        }
        ret = esp_h264_enc_dual_del(enc);
        if (ret != ESP_H264_ERR_OK) {
            printf("dual del failed. round %d line %d\n", i, __LINE__);
            return ret;
        }
    }

    printf("dual_hw_enc open time: res0=%dx%d res1=%dx%d rounds=%d "
           "new(avg/min/max)=%lld/%lld/%lld us "
           "open(avg/min/max)=%lld/%lld/%lld us "
           "new+open_avg=%lld us\n",
           cfg.cfg0.res.width, cfg.cfg0.res.height,
           cfg.cfg1.res.width, cfg.cfg1.res.height,
           HW_ENC_OPEN_TIME_ROUNDS,
           (long long)(new_sum_us / HW_ENC_OPEN_TIME_ROUNDS),
           (long long)new_min_us, (long long)new_max_us,
           (long long)(open_sum_us / HW_ENC_OPEN_TIME_ROUNDS),
           (long long)open_min_us, (long long)open_max_us,
           (long long)((new_sum_us + open_sum_us) / HW_ENC_OPEN_TIME_ROUNDS));
    return ESP_H264_ERR_OK;
}

esp_h264_err_t single_hw_enc_force_idr_test(esp_h264_enc_cfg_hw_t cfg)
{
    esp_h264_enc_in_frame_t in_frame = {0};
    esp_h264_enc_out_frame_t out_frame = {0};
    esp_h264_err_t ret = ESP_H264_ERR_OK;
    esp_h264_enc_handle_t enc = NULL;
    esp_h264_enc_param_hw_handle_t param_hd = NULL;
    uint16_t width = ((cfg.res.width + 15) >> 4 << 4);
    uint16_t height = ((cfg.res.height + 15) >> 4 << 4);

    in_frame.raw_data.len = (int)((float)width * height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame.raw_data.len, &in_frame.raw_data.len, ESP_H264_MEM_INTERNAL);
    if (!in_frame.raw_data.buffer) {
        printf("mem allocation failed. line %d\n", __LINE__);
        goto _exit_;
    }
    out_frame.raw_data.len = in_frame.raw_data.len;
    out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame.raw_data.len, &out_frame.raw_data.len, ESP_H264_MEM_INTERNAL);
    if (!out_frame.raw_data.buffer) {
        printf("mem allocation failed. line %d\n", __LINE__);
        goto _exit_;
    }

    ret = esp_h264_enc_hw_new(&cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("new failed. line %d\n", __LINE__);
        goto _exit_;
    }
    ret = esp_h264_enc_hw_get_param_hd(enc, &param_hd);
    if (ret != ESP_H264_ERR_OK) {
        printf("get_param_hd failed. line %d\n", __LINE__);
        goto _exit_;
    }
    ret = esp_h264_enc_open(enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("open failed. line %d\n", __LINE__);
        goto _exit_;
    }

    /* Frame 0: natural GOP IDR */
    if (read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type) <= 0) {
        ret = ESP_H264_ERR_FAIL;
        goto _exit_;
    }
    ret = esp_h264_enc_process(enc, &in_frame, &out_frame);
    if (ret != ESP_H264_ERR_OK || out_frame.frame_type != ESP_H264_FRAME_TYPE_IDR) {
        printf("frame0 expect IDR got %d ret %d. line %d\n", out_frame.frame_type, ret, __LINE__);
        ret = ESP_H264_ERR_FAIL;
        goto _exit_;
    }

    /* Frame 1: P within GOP */
    if (read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type) <= 0) {
        ret = ESP_H264_ERR_FAIL;
        goto _exit_;
    }
    ret = esp_h264_enc_process(enc, &in_frame, &out_frame);
    if (ret != ESP_H264_ERR_OK || out_frame.frame_type != ESP_H264_FRAME_TYPE_P) {
        printf("frame1 expect P got %d ret %d. line %d\n", out_frame.frame_type, ret, __LINE__);
        ret = ESP_H264_ERR_FAIL;
        goto _exit_;
    }

    /* Force IDR mid-GOP */
    ret = esp_h264_enc_force_idr(&param_hd->base);
    if (ret != ESP_H264_ERR_OK) {
        printf("force_idr failed. line %d\n", __LINE__);
        goto _exit_;
    }
    if (read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type) <= 0) {
        ret = ESP_H264_ERR_FAIL;
        goto _exit_;
    }
    ret = esp_h264_enc_process(enc, &in_frame, &out_frame);
    if (ret != ESP_H264_ERR_OK || out_frame.frame_type != ESP_H264_FRAME_TYPE_IDR) {
        printf("forced frame expect IDR got %d ret %d. line %d\n", out_frame.frame_type, ret, __LINE__);
        ret = ESP_H264_ERR_FAIL;
        goto _exit_;
    }

    /* Next frame after forced IDR should be P again */
    if (read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type) <= 0) {
        ret = ESP_H264_ERR_FAIL;
        goto _exit_;
    }
    ret = esp_h264_enc_process(enc, &in_frame, &out_frame);
    if (ret != ESP_H264_ERR_OK || out_frame.frame_type != ESP_H264_FRAME_TYPE_P) {
        printf("post-force frame expect P got %d ret %d. line %d\n", out_frame.frame_type, ret, __LINE__);
        ret = ESP_H264_ERR_FAIL;
        goto _exit_;
    }

_exit_:
    if (enc) {
        esp_h264_err_t close_ret = esp_h264_enc_close(enc);
        esp_h264_err_t del_ret = esp_h264_enc_del(enc);
        if (ret == ESP_H264_ERR_OK) {
            ret = (close_ret != ESP_H264_ERR_OK) ? close_ret : del_ret;
        }
    }
    if (in_frame.raw_data.buffer) {
        esp_h264_free(in_frame.raw_data.buffer);
    }
    if (out_frame.raw_data.buffer) {
        esp_h264_free(out_frame.raw_data.buffer);
    }
    return ret;
}

esp_h264_err_t dual_hw_enc_force_idr_test(esp_h264_enc_cfg_dual_hw_t cfg)
{
    esp_h264_enc_in_frame_t *in_frame[2] = {NULL, NULL};
    esp_h264_enc_out_frame_t *out_frame[2] = {NULL, NULL};
    esp_h264_err_t ret = ESP_H264_ERR_OK;
    esp_h264_enc_dual_handle_t enc = NULL;
    esp_h264_enc_param_hw_handle_t param_hd0 = NULL;
    esp_h264_enc_param_hw_handle_t param_hd1 = NULL;
    int16_t width[2] = { ((cfg.cfg0.res.width + 15) >> 4 << 4), ((cfg.cfg1.res.width + 15) >> 4 << 4)};
    int16_t height[2] = { ((cfg.cfg0.res.height + 15) >> 4 << 4), ((cfg.cfg1.res.height + 15) >> 4 << 4)};
    int32_t out_length[2];

    out_length[0] = width[0] * height[0] * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg0.pic_type);
    out_length[1] = width[1] * height[1] * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg1.pic_type);
    for (int16_t i = 0; i < 2; i++) {
        in_frame[i] = heap_caps_calloc(1, sizeof(esp_h264_enc_in_frame_t), ESP_H264_MEM_INTERNAL);
        out_frame[i] = heap_caps_calloc(1, sizeof(esp_h264_enc_out_frame_t), ESP_H264_MEM_INTERNAL);
        if (!in_frame[i] || !out_frame[i]) {
            printf("mem allocation failed. line %d\n", __LINE__);
            ret = ESP_H264_ERR_MEM;
            goto _exit_dual_;
        }
        in_frame[i]->raw_data.len = out_length[i];
        in_frame[i]->raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame[i]->raw_data.len, &in_frame[i]->raw_data.len, ESP_H264_MEM_INTERNAL);
        out_frame[i]->raw_data.len = out_length[i];
        out_frame[i]->raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame[i]->raw_data.len, &out_frame[i]->raw_data.len, ESP_H264_MEM_INTERNAL);
        if (!in_frame[i]->raw_data.buffer || !out_frame[i]->raw_data.buffer) {
            printf("mem allocation failed. line %d\n", __LINE__);
            ret = ESP_H264_ERR_MEM;
            goto _exit_dual_;
        }
    }

    ret = esp_h264_enc_dual_hw_new(&cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("dual new failed. line %d\n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_hw_get_param_hd0(enc, &param_hd0);
    if (ret != ESP_H264_ERR_OK) {
        printf("get_param_hd0 failed. line %d\n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_hw_get_param_hd1(enc, &param_hd1);
    if (ret != ESP_H264_ERR_OK) {
        printf("get_param_hd1 failed. line %d\n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_open(enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("dual open failed. line %d\n", __LINE__);
        goto _exit_dual_;
    }

    for (int16_t i = 0; i < 2; i++) {
        esp_h264_enc_cfg_hw_t cfg_tmp = (i == 0) ? cfg.cfg0 : cfg.cfg1;
        if (read_enc_cb(in_frame[i], width[i], height[i], cfg_tmp.pic_type) <= 0) {
            ret = ESP_H264_ERR_FAIL;
            goto _exit_dual_;
        }
    }
    ret = esp_h264_enc_dual_process(enc, in_frame, out_frame);
    if (ret != ESP_H264_ERR_OK
            || out_frame[0]->frame_type != ESP_H264_FRAME_TYPE_IDR
            || out_frame[1]->frame_type != ESP_H264_FRAME_TYPE_IDR) {
        printf("dual frame0 expect IDR got %d/%d. line %d\n",
               out_frame[0]->frame_type, out_frame[1]->frame_type, __LINE__);
        ret = ESP_H264_ERR_FAIL;
        goto _exit_dual_;
    }

    for (int16_t i = 0; i < 2; i++) {
        esp_h264_enc_cfg_hw_t cfg_tmp = (i == 0) ? cfg.cfg0 : cfg.cfg1;
        if (read_enc_cb(in_frame[i], width[i], height[i], cfg_tmp.pic_type) <= 0) {
            ret = ESP_H264_ERR_FAIL;
            goto _exit_dual_;
        }
    }
    ret = esp_h264_enc_dual_process(enc, in_frame, out_frame);
    if (ret != ESP_H264_ERR_OK
            || out_frame[0]->frame_type != ESP_H264_FRAME_TYPE_P
            || out_frame[1]->frame_type != ESP_H264_FRAME_TYPE_P) {
        printf("dual frame1 expect P got %d/%d. line %d\n",
               out_frame[0]->frame_type, out_frame[1]->frame_type, __LINE__);
        ret = ESP_H264_ERR_FAIL;
        goto _exit_dual_;
    }

    ret = esp_h264_enc_force_idr(&param_hd0->base);
    if (ret != ESP_H264_ERR_OK) {
        printf("dual force_idr stream0 failed. line %d\n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_force_idr(&param_hd1->base);
    if (ret != ESP_H264_ERR_OK) {
        printf("dual force_idr stream1 failed. line %d\n", __LINE__);
        goto _exit_dual_;
    }
    for (int16_t i = 0; i < 2; i++) {
        esp_h264_enc_cfg_hw_t cfg_tmp = (i == 0) ? cfg.cfg0 : cfg.cfg1;
        if (read_enc_cb(in_frame[i], width[i], height[i], cfg_tmp.pic_type) <= 0) {
            ret = ESP_H264_ERR_FAIL;
            goto _exit_dual_;
        }
    }
    ret = esp_h264_enc_dual_process(enc, in_frame, out_frame);
    if (ret != ESP_H264_ERR_OK
            || out_frame[0]->frame_type != ESP_H264_FRAME_TYPE_IDR
            || out_frame[1]->frame_type != ESP_H264_FRAME_TYPE_IDR) {
        printf("dual forced frame expect IDR got %d/%d. line %d\n",
               out_frame[0]->frame_type, out_frame[1]->frame_type, __LINE__);
        ret = ESP_H264_ERR_FAIL;
        goto _exit_dual_;
    }

    /* Both pending requests must be consumed by the same dual-process call. This catches
     * short-circuit evaluation that leaves stream1's request pending for one extra IDR. */
    for (int16_t i = 0; i < 2; i++) {
        esp_h264_enc_cfg_hw_t cfg_tmp = (i == 0) ? cfg.cfg0 : cfg.cfg1;
        if (read_enc_cb(in_frame[i], width[i], height[i], cfg_tmp.pic_type) <= 0) {
            ret = ESP_H264_ERR_FAIL;
            goto _exit_dual_;
        }
    }
    ret = esp_h264_enc_dual_process(enc, in_frame, out_frame);
    if (ret != ESP_H264_ERR_OK
            || out_frame[0]->frame_type != ESP_H264_FRAME_TYPE_P
            || out_frame[1]->frame_type != ESP_H264_FRAME_TYPE_P) {
        printf("dual post-force frame expect P got %d/%d. line %d\n",
               out_frame[0]->frame_type, out_frame[1]->frame_type, __LINE__);
        ret = ESP_H264_ERR_FAIL;
        goto _exit_dual_;
    }

_exit_dual_:
    if (enc) {
        esp_h264_err_t close_ret = esp_h264_enc_dual_close(enc);
        esp_h264_err_t del_ret = esp_h264_enc_dual_del(enc);
        if (ret == ESP_H264_ERR_OK) {
            ret = (close_ret != ESP_H264_ERR_OK) ? close_ret : del_ret;
        }
    }
    for (int16_t i = 0; i < 2; i++) {
        if (in_frame[i]) {
            if (in_frame[i]->raw_data.buffer) {
                esp_h264_free(in_frame[i]->raw_data.buffer);
            }
            esp_h264_free(in_frame[i]);
        }
        if (out_frame[i]) {
            if (out_frame[i]->raw_data.buffer) {
                esp_h264_free(out_frame[i]->raw_data.buffer);
            }
            esp_h264_free(out_frame[i]);
        }
    }
    return ret;
}

/** GOP FPS RC */
esp_h264_err_t single_hw_enc_thread_test(esp_h264_enc_cfg_hw_t cfg)
{
    esp_h264_enc_in_frame_t in_frame = {0};
    esp_h264_enc_out_frame_t out_frame = {0};
    esp_h264_err_t ret = ESP_H264_ERR_FAIL;
    esp_h264_enc_handle_t enc = NULL;
    esp_h264_resolution_t res;
    esp_h264_enc_rc_t rc;
    uint32_t frame_count = 0;
    uint8_t gop;
    uint8_t fps;
    esp_h264_enc_param_hw_handle_t param_hd;
    int index_c = 0;

    in_frame.raw_data.len = (int)((float)cfg.res.width * cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame.raw_data.len, &in_frame.raw_data.len, ESP_H264_MEM_SPIRAM);
    if (!in_frame.raw_data.buffer) {
        printf("mem allocation failed.line %d \n", __LINE__);
        goto _exit_;
    }
    out_frame.raw_data.len = (int)((float)cfg.res.width * cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type)) / 10;
    out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame.raw_data.len, &out_frame.raw_data.len, ESP_H264_MEM_SPIRAM);
    if (!out_frame.raw_data.buffer) {
        printf("mem allocation failed.line %d \n", __LINE__);
        goto _exit_;
    }

    ret = esp_h264_enc_hw_new(&cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("new failed. line %d \n", __LINE__);
        goto _exit_;
    }

    ret = esp_h264_enc_hw_get_param_hd(enc, &param_hd);
    if (ret != ESP_H264_ERR_OK) {
        printf("esp_h264_enc_hw_get_param_hd error. line %d \n", __LINE__);
        goto _exit_;
    }

    ret = esp_h264_enc_get_resolution(&param_hd->base, &res);
    if ((ret != ESP_H264_ERR_OK)
            || (res.width != cfg.res.width)
            || (res.height != cfg.res.height)) {
        printf("esp_h264_enc_get_resolution failed .line %d \n", __LINE__);
        goto _exit_;
    }

    ret = esp_h264_enc_get_fps(&param_hd->base, &fps);
    if ((ret != ESP_H264_ERR_OK)
            || (fps != cfg.fps)) {
        printf("esp_h264_enc_get_fps failed .line %d \n", __LINE__);
        goto _exit_;
    }

    ret = esp_h264_enc_get_gop(&param_hd->base, &gop);
    if ((ret != ESP_H264_ERR_OK)
            || (gop != cfg.gop)) {
        printf("esp_h264_enc_get_gop failed .line %d \n", __LINE__);
        goto _exit_;
    }

    ret = esp_h264_enc_get_bitrate(&param_hd->base, &rc.bitrate);
    if (ret != ESP_H264_ERR_OK
            || (rc.bitrate != cfg.rc.bitrate)) {
        printf("esp_h264_enc_get_bitrate failed .line %d \n", __LINE__);
        goto _exit_;
    }

    ret = esp_h264_enc_open(enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("open failed .line %d \n", __LINE__);
        goto _exit_;
    }
    while (1) {
        index_c++;
        int ret_w = read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type);
        if (ret_w <= 0) {
            break;
        }
        ret |= esp_h264_enc_get_resolution(&param_hd->base, &res);
        if ((ret != ESP_H264_ERR_OK)
                || (res.width != cfg.res.width)
                || (res.height != cfg.res.height)) {
            printf("esp_h264_enc_get_resolution failed .line %d \n", __LINE__);
            goto _exit_;
        }

        ret |= esp_h264_enc_get_fps(&param_hd->base, &fps);
        if ((ret != ESP_H264_ERR_OK)
                || (fps != cfg.fps)) {
            printf("esp_h264_enc_get_fps failed .line %d \n", __LINE__);
            goto _exit_;
        }

        ret |= esp_h264_enc_get_gop(&param_hd->base, &gop);
        if ((ret != ESP_H264_ERR_OK)
                || (gop != cfg.gop)) {
            printf("esp_h264_enc_get_gop failed .line %d \n", __LINE__);
            goto _exit_;
        }

        ret |= esp_h264_enc_get_bitrate(&param_hd->base, &rc.bitrate);
        if (ret != ESP_H264_ERR_OK
                || (rc.bitrate != cfg.rc.bitrate)) {
            printf("esp_h264_enc_get_bitrate failed .line %d \n", __LINE__);
            goto _exit_;
        }

        cfg.fps = index_c + 4;
        ret |= esp_h264_enc_set_fps(&param_hd->base, cfg.fps);
        if (ret != ESP_H264_ERR_OK) {
            printf("esp_h264_enc_set_fps failed .line %d \n", __LINE__);
            goto _exit_;
        }
        cfg.gop = index_c + 3;
        ret |= esp_h264_enc_set_gop(&param_hd->base, cfg.gop);
        if (ret != ESP_H264_ERR_OK) {
            printf("esp_h264_enc_set_gop failed .line %d \n", __LINE__);
            goto _exit_;
        }
        frame_count = 0;
        cfg.rc.qp_min = index_c + 3;
        cfg.rc.qp_max = index_c + 10;
        ret |= esp_h264_enc_set_bitrate(&param_hd->base, cfg.rc.bitrate);
        if (ret != ESP_H264_ERR_OK) {
            printf("esp_h264_enc_set_bitrate failed .line %d \n", __LINE__);
            goto _exit_;
        }
        ret |= esp_h264_enc_process(enc, &in_frame, &out_frame);
        if (ret != ESP_H264_ERR_OK) {
            printf("process failed. line %d \n", __LINE__);
            goto _exit_;
        }
        write_enc_cb(&out_frame);
        if (frame_count % cfg.gop == 0) {
            if (out_frame.frame_type != ESP_H264_FRAME_TYPE_I
                    && out_frame.frame_type != ESP_H264_FRAME_TYPE_IDR) {
                printf("frame type error. frame type %d GOP %d line %d \n", out_frame.frame_type, cfg.gop, __LINE__);
                ret = ESP_H264_ERR_FAIL;
                goto _exit_;
            }
        } else {
            if (out_frame.frame_type != ESP_H264_FRAME_TYPE_P) {
                printf("frame type error. frame type %d GOP %d frame count %ld line %d \n", out_frame.frame_type, cfg.gop, frame_count, __LINE__);
                ret = ESP_H264_ERR_FAIL;
                goto _exit_;
            }
        }
        frame_count++;
    }
_exit_:
    ret |= esp_h264_enc_close(enc);
    ret |= esp_h264_enc_del(enc);
    if (in_frame.raw_data.buffer) {
        esp_h264_free(in_frame.raw_data.buffer);
    }
    if (out_frame.raw_data.buffer) {
        esp_h264_free(out_frame.raw_data.buffer);
    }
    return ret;
}

esp_h264_err_t dual_hw_enc_thread_test(esp_h264_enc_cfg_dual_hw_t cfg)
{
    esp_h264_err_t ret = ESP_H264_ERR_FAIL;
    int index_c = 0;
    esp_h264_resolution_t res;
    esp_h264_enc_rc_t rc;
    uint8_t gop[2] = { cfg.cfg0.gop, cfg.cfg1.gop };
    uint8_t gop_tmp;
    uint8_t fps;
    uint32_t frame_count = 0;
    esp_h264_enc_param_hw_handle_t param_hd;
    esp_h264_enc_param_hw_handle_t param_hd0;
    esp_h264_enc_param_hw_handle_t param_hd1;
    esp_h264_enc_in_frame_t *in_frame[2] = {NULL, NULL};
    esp_h264_enc_out_frame_t *out_frame[2] = {NULL, NULL};
    esp_h264_enc_dual_handle_t enc = NULL;
    int16_t out_length[2];
    esp_h264_enc_cfg_t base_cfg;
    int16_t width[2] = { cfg.cfg0.res.width, cfg.cfg1.res.width };
    int16_t height[2] = { cfg.cfg0.res.height, cfg.cfg1.res.height };
    out_length[0] = (int)((float)cfg.cfg0.res.width * cfg.cfg0.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg0.pic_type));
    out_length[1] = (int)((float)cfg.cfg1.res.width * cfg.cfg1.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg1.pic_type));

    for (int16_t i = 0; i < 2; i++) {
        in_frame[i] = heap_caps_calloc(1, sizeof(esp_h264_enc_in_frame_t), MALLOC_CAP_INTERNAL);
        if (!in_frame[i]) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_frame[i] = heap_caps_calloc(1, sizeof(esp_h264_enc_out_frame_t), MALLOC_CAP_INTERNAL);
        if (!out_frame[i]) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        in_frame[i]->raw_data.len = out_length[i];
        in_frame[i]->raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame[i]->raw_data.len, &in_frame[i]->raw_data.len, ESP_H264_MEM_INTERNAL);
        if (!in_frame[i]->raw_data.buffer) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_frame[i]->raw_data.len = out_length[i];
        out_frame[i]->raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame[i]->raw_data.len, &out_frame[i]->raw_data.len, ESP_H264_MEM_INTERNAL);
        if (!out_frame[i]->raw_data.buffer) {
            printf("mem allocation failed. line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_length[i] = out_frame[i]->raw_data.len;
    }

    ret = esp_h264_enc_dual_hw_new(&cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("new failed. line %d \n", __LINE__);
        goto _exit_dual_;
    }

    ret = esp_h264_enc_dual_hw_get_param_hd0(enc, &param_hd0);
    if (ret != ESP_H264_ERR_OK) {
        printf("esp_h264_enc_hw_get_param_hd error. line %d \n", __LINE__);
        goto _exit_dual_;
    }

    ret = esp_h264_enc_dual_hw_get_param_hd1(enc, &param_hd1);
    if (ret != ESP_H264_ERR_OK) {
        printf("esp_h264_enc_hw_get_param_hd error. line %d \n", __LINE__);
        goto _exit_dual_;
    }

    param_hd = param_hd0;
    ret = esp_h264_enc_dual_open(enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("open failed .line %d \n", __LINE__);
        goto _exit_dual_;
    }
    while (1) {
        index_c++;
        param_hd = index_c % 2 ? param_hd0 : param_hd1;
        memcpy(&base_cfg, index_c % 2 ? &cfg.cfg0 : &cfg.cfg1, sizeof(esp_h264_enc_cfg_t));
        for (int16_t i = 0; i < 2; i++) {
            esp_h264_enc_cfg_hw_t cfg_tmp = i == 0 ? cfg.cfg0 : cfg.cfg1;
            int ret_w = read_enc_cb(in_frame[i], width[i], height[i], cfg_tmp.pic_type);
            if (ret_w <= 0) {
                goto _exit_dual_;
            }
        }

        base_cfg.fps = index_c + 4;
        ret = esp_h264_enc_set_fps(&param_hd->base, base_cfg.fps);
        if (ret != ESP_H264_ERR_OK) {
            printf("esp_h264_enc_set_fps failed .line %d \n", __LINE__);
            goto _exit_dual_;
        }
        gop[index_c % 2] = (index_c + 3);
        ret = esp_h264_enc_set_gop(&param_hd->base, gop[index_c % 2]);
        if (ret != ESP_H264_ERR_OK) {
            printf("esp_h264_enc_set_gop failed .line %d \n", __LINE__);
            goto _exit_dual_;
        }
        frame_count = 0;

        base_cfg.rc.qp_min = index_c + 3;
        base_cfg.rc.qp_max = index_c + 10;
        base_cfg.rc.bitrate = base_cfg.res.width * base_cfg.res.height / 20;
        ret = esp_h264_enc_set_bitrate(&param_hd->base, base_cfg.rc.bitrate);
        if (ret != ESP_H264_ERR_OK) {
            printf("esp_h264_enc_set_bitrate failed .line %d \n", __LINE__);
            goto _exit_dual_;
        }
        ret = esp_h264_enc_dual_process(enc, in_frame, out_frame);
        if (ret != ESP_H264_ERR_OK) {
            printf("process failed. line %d \n", __LINE__);
            goto _exit_dual_;
        }
        for (int16_t i = 0; i < 2; i++) {
            write_enc_cb(out_frame[i]);
            for (int16_t i = 0; i < 2; i++) {
                if (frame_count % base_cfg.gop == 0) {
                    if (out_frame[i]->frame_type != ESP_H264_FRAME_TYPE_I) {
                        printf("frame type error. frame type %d GOP %d line %d \n", out_frame[i]->frame_type, base_cfg.gop, __LINE__);
                        goto _exit_dual_;
                    }
                } else {
                    if (out_frame[i]->frame_type != ESP_H264_FRAME_TYPE_P) {
                        printf("frame type error. frame type %d GOP %d line %d \n", out_frame[i]->frame_type, base_cfg.gop, __LINE__);
                        goto _exit_dual_;
                    }
                }
            }
        }
        frame_count++;
        ret = esp_h264_enc_get_resolution(&param_hd->base, &res);
        if ((ret != ESP_H264_ERR_OK)
                || (res.width != base_cfg.res.width)
                || (res.height != base_cfg.res.height)) {
            printf("esp_h264_enc_get_resolution failed .line %d \n", __LINE__);
            goto _exit_dual_;
        }

        ret = esp_h264_enc_get_fps(&param_hd->base, &fps);
        if ((ret != ESP_H264_ERR_OK)
                || (fps != base_cfg.fps)) {
            printf("esp_h264_enc_get_fps failed .line %d \n", __LINE__);
            goto _exit_dual_;
        }

        ret = esp_h264_enc_get_gop(&param_hd->base, &gop_tmp);

        if ((ret != ESP_H264_ERR_OK)
                || (gop_tmp != ((gop[0] + gop[1]) >> 1)
                    && gop_tmp != gop[index_c % 2])) {
            printf("esp_h264_enc_get_gop failed . %d %d %d %d line %d \n", index_c, gop[0], gop[1], gop_tmp, __LINE__);
            goto _exit_dual_;
        }
        ret = esp_h264_enc_get_bitrate(&param_hd->base, &rc.bitrate);
        if (ret != ESP_H264_ERR_OK
                || (rc.bitrate != base_cfg.rc.bitrate)) {
            printf("esp_h264_enc_get_bitrate failed .line %d \n", __LINE__);
            printf("bitrate %d %d \n", (int)rc.bitrate, (int)base_cfg.rc.bitrate);
            goto _exit_dual_;
        }
    }
_exit_dual_:
    ret |= esp_h264_enc_dual_close(enc);
    ret |= esp_h264_enc_dual_del(enc);
    for (int16_t i = 0; i < 2; i++) {
        if (in_frame[i]) {
            if (in_frame[i]->raw_data.buffer) {
                esp_h264_free(in_frame[i]->raw_data.buffer);
                esp_h264_free(in_frame[i]);
            }
        }
        if (out_frame[i]) {
            if (out_frame[i]->raw_data.buffer) {
                esp_h264_free(out_frame[i]->raw_data.buffer);
                esp_h264_free(out_frame[i]);
            }
        }
    }
    return ret;
}

/** ROI component*/
esp_h264_err_t single_hw_enc_roi_cfg_test(esp_h264_enc_cfg_hw_t cfg)
{
    esp_h264_enc_in_frame_t in_frame = {0};
    esp_h264_enc_out_frame_t out_frame = {0};
    esp_h264_err_t ret = ESP_H264_ERR_FAIL;
    esp_h264_enc_handle_t enc = NULL;
    esp_h264_enc_param_hw_handle_t param_hd;
    esp_h264_enc_roi_cfg_t roi_cfg = {
        .roi_mode = ESP_H264_ROI_MODE_DELTA_QP,
        .none_roi_delta_qp = 2,
    };
    esp_h264_enc_roi_cfg_t cfg_get = {
        .roi_mode = ESP_H264_ROI_MODE_DELTA_QP,
        .none_roi_delta_qp = -51,
    };
    in_frame.raw_data.len = (int)((float)cfg.res.width * cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame.raw_data.len, &in_frame.raw_data.len, ESP_H264_MEM_INTERNAL);
    if (!in_frame.raw_data.buffer) {
        printf("mem allocation failed.line %d \n", __LINE__);
        goto _exit_;
    }
    out_frame.raw_data.len = (int)((float)cfg.res.width * cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type)) / 10;
    out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame.raw_data.len, &out_frame.raw_data.len, ESP_H264_MEM_INTERNAL);
    if (!out_frame.raw_data.buffer) {
        printf("mem allocation failed.line %d \n", __LINE__);
        goto _exit_;
    }

    ret = esp_h264_enc_hw_new(&cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("new failed. line %d \n", __LINE__);
        goto _exit_;
    }

    ret = esp_h264_enc_hw_get_param_hd(enc, &param_hd);
    if (ret != ESP_H264_ERR_OK) {
        ret = ESP_H264_ERR_FAIL;
        printf("esp_h264_enc_hw_get_param_hd error. line %d \n", __LINE__);
        goto _exit_;
    }
    ret = esp_h264_enc_open(enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("open failed .line %d \n", __LINE__);
        goto _exit_;
    }
    for (int8_t i = ESP_H264_ROI_MODE_DISABLE; i < ESP_H264_ROI_MODE_INVALID; i++) {
        roi_cfg.roi_mode = i;
        roi_cfg.none_roi_delta_qp = -26;
        while (1) {
            roi_cfg.none_roi_delta_qp = (roi_cfg.none_roi_delta_qp + 1) % 51;
            ret = esp_h264_enc_hw_cfg_roi(param_hd, roi_cfg);
            if (ret != ESP_H264_ERR_OK) {
                printf("ROI configure error. line %d \n", __LINE__);
                goto _exit_;
            }
            int ret_w = read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type);
            if (ret_w <= 0) {
                break;
            }
            ret = esp_h264_enc_process(enc, &in_frame, &out_frame);
            if (ret != ESP_H264_ERR_OK) {
                printf("process failed. line %d \n", __LINE__);
                goto _exit_;
            }
            write_enc_cb(&out_frame);
            ret = esp_h264_enc_hw_get_roi_cfg_info(param_hd, &cfg_get);
            if (ret != ESP_H264_ERR_OK
                    || cfg_get.roi_mode != roi_cfg.roi_mode
                    || ((cfg_get.roi_mode != ESP_H264_ROI_MODE_DISABLE)
                        && (cfg_get.none_roi_delta_qp != roi_cfg.none_roi_delta_qp))) {
                printf("ROI process error. %d %d %d line %d \n", cfg_get.roi_mode, cfg_get.none_roi_delta_qp, roi_cfg.none_roi_delta_qp, __LINE__);
                goto _exit_;
            }
        }
    }
_exit_:
    ret |= esp_h264_enc_close(enc);
    ret |= esp_h264_enc_del(enc);
    if (in_frame.raw_data.buffer) {
        esp_h264_free(in_frame.raw_data.buffer);
    }
    if (out_frame.raw_data.buffer) {
        esp_h264_free(out_frame.raw_data.buffer);
    }
    return ret;
}

esp_h264_err_t dual_hw_enc_roi_cfg_test(esp_h264_enc_cfg_dual_hw_t cfg)
{
    esp_h264_enc_in_frame_t *in_frame[2] = {NULL, NULL};
    int index_c = 0;
    esp_h264_enc_out_frame_t *out_frame[2] = {NULL, NULL};
    esp_h264_err_t ret = ESP_H264_ERR_FAIL;
    esp_h264_enc_dual_handle_t enc = NULL;
    int16_t out_length[2];
    esp_h264_enc_param_hw_handle_t param_hd0;
    esp_h264_enc_param_hw_handle_t param_hd1;
    esp_h264_enc_param_hw_handle_t param_hd;
    int16_t width[2] = { cfg.cfg0.res.width, cfg.cfg1.res.width };
    int16_t height[2] = { cfg.cfg0.res.height, cfg.cfg1.res.height };
    out_length[0] = (int)((float)cfg.cfg0.res.width * cfg.cfg0.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg0.pic_type));
    out_length[1] = (int)((float)cfg.cfg1.res.width * cfg.cfg1.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg1.pic_type));
    esp_h264_enc_roi_cfg_t roi_cfg = {
        .roi_mode = ESP_H264_ROI_MODE_DELTA_QP,
        .none_roi_delta_qp = 2,
    };
    esp_h264_enc_roi_cfg_t cfg_get = {
        .roi_mode = ESP_H264_ROI_MODE_DELTA_QP,
        .none_roi_delta_qp = -51,
    };
    for (int16_t i = 0; i < 2; i++) {
        in_frame[i] = heap_caps_calloc(1, sizeof(esp_h264_enc_in_frame_t), MALLOC_CAP_INTERNAL);
        if (!in_frame[i]) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_frame[i] = heap_caps_calloc(1, sizeof(esp_h264_enc_out_frame_t), MALLOC_CAP_INTERNAL);
        if (!out_frame[i]) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        in_frame[i]->raw_data.len = out_length[i];
        in_frame[i]->raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame[i]->raw_data.len, &in_frame[i]->raw_data.len, ESP_H264_MEM_INTERNAL);
        if (!in_frame[i]->raw_data.buffer) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_frame[i]->raw_data.len = out_length[i];
        out_frame[i]->raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame[i]->raw_data.len, &out_frame[i]->raw_data.len, ESP_H264_MEM_INTERNAL);
        if (!out_frame[i]->raw_data.buffer) {
            printf("mem allocation failed. line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_length[i] = out_frame[i]->raw_data.len;
    }

    ret = esp_h264_enc_dual_hw_new(&cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("new failed. line %d \n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_open(enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("open failed .line %d \n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_hw_get_param_hd0(enc, &param_hd0);
    if (ret != ESP_H264_ERR_OK) {
        printf("esp_h264_enc_hw_get_param_hd error. line %d \n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_hw_get_param_hd1(enc, &param_hd1);
    if (ret != ESP_H264_ERR_OK) {
        printf("esp_h264_enc_hw_get_param_hd error. line %d \n", __LINE__);
        goto _exit_dual_;
    }
    for (int8_t i = ESP_H264_ROI_MODE_DISABLE; i < ESP_H264_ROI_MODE_INVALID; i++) {
        roi_cfg.roi_mode = i;
        roi_cfg.none_roi_delta_qp = -26;
        while (1) {
            index_c++;
            param_hd = index_c % 2 ? param_hd0 : param_hd1;
            roi_cfg.none_roi_delta_qp = (roi_cfg.none_roi_delta_qp + 1) % 51;
            ret = esp_h264_enc_hw_cfg_roi(param_hd, roi_cfg);
            if (ret != ESP_H264_ERR_OK) {
                printf("ROI configure error. line %d \n", __LINE__);
                goto _exit_dual_;
            }
            for (int16_t i = 0; i < 2; i++) {
                esp_h264_enc_cfg_hw_t cfg_tmp = i == 0 ? cfg.cfg0 : cfg.cfg1;
                int ret_w = read_enc_cb(in_frame[i], width[i], height[i], cfg_tmp.pic_type);
                if (ret_w <= 0) {
                    goto _exit_dual_;
                }
                out_frame[i]->raw_data.len = out_length[i];
            }

            ret = esp_h264_enc_dual_process(enc, in_frame, out_frame);
            if (ret != ESP_H264_ERR_OK) {
                printf("process failed. line %d \n", __LINE__);
                goto _exit_dual_;
            }
            for (int16_t i = 0; i < 2; i++) {
                write_enc_cb(out_frame[i]);
            }
            ret = esp_h264_enc_hw_get_roi_cfg_info(param_hd, &cfg_get);
            if (ret != ESP_H264_ERR_OK
                    || cfg_get.roi_mode != roi_cfg.roi_mode
                    || ((cfg_get.roi_mode != ESP_H264_ROI_MODE_DISABLE)
                        && (cfg_get.none_roi_delta_qp != roi_cfg.none_roi_delta_qp))) {
                printf("ROI process error. %d %d %d line %d \n", cfg_get.roi_mode, cfg_get.none_roi_delta_qp, roi_cfg.none_roi_delta_qp, __LINE__);
                goto _exit_dual_;
            }
        }
    }
_exit_dual_:
    ret |= esp_h264_enc_dual_close(enc);
    ret |= esp_h264_enc_dual_del(enc);
    for (int16_t i = 0; i < 2; i++) {
        if (in_frame[i]) {
            if (in_frame[i]->raw_data.buffer) {
                esp_h264_free(in_frame[i]->raw_data.buffer);
                esp_h264_free(in_frame[i]);
            }
        }
        if (out_frame[i]) {
            if (out_frame[i]->raw_data.buffer) {
                esp_h264_free(out_frame[i]->raw_data.buffer);
                esp_h264_free(out_frame[i]);
            }
        }
    }
    return ret;
}

esp_h264_err_t single_hw_enc_roi_reg_test(esp_h264_enc_cfg_hw_t cfg)
{
    esp_h264_enc_in_frame_t in_frame = {0};
    esp_h264_enc_out_frame_t out_frame = {0};
    esp_h264_err_t ret = ESP_H264_ERR_FAIL;
    esp_h264_enc_handle_t enc = NULL;
    esp_h264_enc_param_hw_handle_t param_hd;
    esp_h264_enc_roi_cfg_t roi_cfg = {
        .roi_mode = ESP_H264_ROI_MODE_DELTA_QP,
        .none_roi_delta_qp = 2,
    };
    int index_c = 0;
    esp_h264_enc_roi_reg_t roi_reg[8] = { 0 };
    esp_h264_enc_roi_reg_t roi_reg1[8] = { 0 };
    in_frame.raw_data.len = (int)((float)cfg.res.width * cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame.raw_data.len, &in_frame.raw_data.len, ESP_H264_MEM_INTERNAL);
    if (!in_frame.raw_data.buffer) {
        printf("mem allocation failed.line %d \n", __LINE__);
        goto _exit_;
    }
    out_frame.raw_data.len = (int)((float)cfg.res.width * cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type)) / 10;
    out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame.raw_data.len, &out_frame.raw_data.len, ESP_H264_MEM_INTERNAL);
    if (!out_frame.raw_data.buffer) {
        printf("mem allocation failed.line %d \n", __LINE__);
        goto _exit_;
    }

    ret = esp_h264_enc_hw_new(&cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("new failed. line %d \n", __LINE__);
        goto _exit_;
    }

    ret = esp_h264_enc_hw_get_param_hd(enc, &param_hd);
    if (ret != ESP_H264_ERR_OK) {
        printf("esp_h264_enc_hw_get_param_hd error. line %d \n", __LINE__);
        goto _exit_;
    }
    ret = esp_h264_enc_open(enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("open failed .line %d \n", __LINE__);
        goto _exit_;
    }

    for (int8_t idx = ESP_H264_ROI_MODE_DISABLE; idx < ESP_H264_ROI_MODE_INVALID; idx++) {
        roi_cfg.roi_mode = idx;
        roi_cfg.none_roi_delta_qp = (roi_cfg.none_roi_delta_qp + 1) % 51;
        ret = esp_h264_enc_hw_cfg_roi(param_hd, roi_cfg);
        if (ret != ESP_H264_ERR_OK) {
            printf("ROI configure error. line %d \n", __LINE__);
            goto _exit_;
        }
        while (1) {
            index_c ++;
            roi_reg[0].x = 0;
            roi_reg[0].y = 0;
            roi_reg[0].len_x = 0;
            roi_reg[0].len_y = 0;
            roi_reg[1].x = 0;
            roi_reg[1].y = 0;
            roi_reg[1].len_x = (cfg.res.width + 15) >> 4;
            roi_reg[1].len_y = ((cfg.res.height + 15) >> 4) >> 3;
            roi_reg[2].x = 0;
            roi_reg[2].y = 0;
            roi_reg[2].len_x = ((cfg.res.width + 15) >> 4) >> 3;
            roi_reg[2].len_y = (cfg.res.height + 15) >> 4;
            roi_reg[3].x = 0;
            roi_reg[3].y = ((cfg.res.height + 15) >> 4) * 7 >> 3;
            roi_reg[3].len_x = ((cfg.res.width + 15) >> 4);
            roi_reg[3].len_y = ((cfg.res.height + 15) >> 4) >> 3;
            roi_reg[4].x = ((cfg.res.width + 15) >> 4) * 7 >> 3;
            roi_reg[4].y = 0;
            roi_reg[4].len_x = ((cfg.res.width + 15) >> 4) >> 3;
            roi_reg[4].len_y = (cfg.res.height + 15) >> 4;
            roi_reg[5].x = ((cfg.res.width + 15) >> 4) >> 3;
            roi_reg[5].y = ((cfg.res.height + 15) >> 4) >> 3;
            roi_reg[5].len_x = ((cfg.res.width + 15) >> 4) >> 3;
            roi_reg[5].len_y = ((cfg.res.height + 15) >> 4) >> 3;
            roi_reg[6].x = ((cfg.res.width + 15) >> 4) * 2 >> 3;
            roi_reg[6].y = ((cfg.res.height + 15) >> 4) * 2 >> 3;
            roi_reg[6].len_x = ((cfg.res.width + 15) >> 4) >> 3;
            roi_reg[6].len_y = ((cfg.res.height + 15) >> 4) >> 3;
            roi_reg[7].x = ((cfg.res.width + 15) >> 4) * 3 >> 3;
            roi_reg[7].y = ((cfg.res.height + 15) >> 4) * 3 >> 3;
            roi_reg[7].len_x = ((cfg.res.width + 15) >> 4) * 2 >> 3;
            roi_reg[7].len_y = ((cfg.res.height + 15) >> 4) * 2 >> 3;
            if (idx > ESP_H264_ROI_MODE_DISABLE) {
                for (uint8_t i = 0; i < 8; i++) {
                    roi_reg[i].reg_idx = i;
                    if (roi_cfg.roi_mode == ESP_H264_ROI_MODE_FIX_QP) {
                        roi_reg[i].qp = ((i + index_c - 51) % 51) < 0 ? 0 : ((i + index_c - 51) % 51);
                    } else {
                        roi_reg[i].qp = (i + index_c - 51) % 51;
                    }
                    ret = esp_h264_enc_hw_set_roi_region(param_hd, roi_reg[i]);
                    if (ret != ESP_H264_ERR_OK) {
                        printf("ROI region error. line %d \n", __LINE__);
                        goto _exit_;
                    }
                }
            }

            roi_reg[0].qp = 0;
            int ret_w = read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type);
            if (ret_w <= 0) {
                break;
            }
            ret = esp_h264_enc_process(enc, &in_frame, &out_frame);
            if (ret != ESP_H264_ERR_OK) {
                printf("process failed. line %d \n", __LINE__);
                goto _exit_;
            }
            write_enc_cb(&out_frame);
            for (size_t i = 0; i < 8; i++) {
                roi_reg1[i].reg_idx = i;
                ret = esp_h264_enc_hw_get_roi_region(param_hd, &roi_reg1[i]);
                if (roi_cfg.roi_mode == ESP_H264_ROI_MODE_DISABLE) {
                    if (ret != ESP_H264_ERR_OK
                            || roi_reg1[i].x != 0
                            || roi_reg1[i].y != 0
                            || roi_reg1[i].len_x != 0
                            || roi_reg1[i].len_y != 0
                            || roi_reg1[i].qp != 0) {
                        goto _exit_;
                    }
                } else {
                    if (ret != ESP_H264_ERR_OK
                            || roi_reg1[i].x != roi_reg[i].x
                            || roi_reg1[i].y != roi_reg[i].y
                            || roi_reg1[i].len_x != roi_reg[i].len_x
                            || roi_reg1[i].len_y != roi_reg[i].len_y
                            || roi_reg1[i].qp != roi_reg[i].qp) {
                        goto _exit_;
                    }
                }
            }
        }
    }
_exit_:
    ret |= esp_h264_enc_close(enc);
    ret |= esp_h264_enc_del(enc);
    if (in_frame.raw_data.buffer) {
        esp_h264_free(in_frame.raw_data.buffer);
    }
    if (out_frame.raw_data.buffer) {
        esp_h264_free(out_frame.raw_data.buffer);
    }
    return ret;
}

esp_h264_err_t dual_hw_enc_roi_reg_test(esp_h264_enc_cfg_dual_hw_t cfg)
{
    esp_h264_enc_in_frame_t *in_frame[2] = {NULL, NULL};
    esp_h264_enc_out_frame_t *out_frame[2] = {NULL, NULL};
    esp_h264_err_t ret = ESP_H264_ERR_FAIL;
    esp_h264_enc_dual_handle_t enc = NULL;
    int16_t out_length[2];
    esp_h264_enc_param_hw_handle_t param_hd0;
    esp_h264_enc_param_hw_handle_t param_hd1;
    esp_h264_enc_param_hw_handle_t param_hd;
    int16_t width[2] = { cfg.cfg0.res.width, cfg.cfg1.res.width };
    int16_t height[2] = { cfg.cfg0.res.height, cfg.cfg1.res.height };
    out_length[0] = (int)((float)cfg.cfg0.res.width * cfg.cfg0.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg0.pic_type));
    out_length[1] = (int)((float)cfg.cfg1.res.width * cfg.cfg1.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg1.pic_type));
    esp_h264_enc_roi_cfg_t roi_cfg = {
        .roi_mode = ESP_H264_ROI_MODE_DELTA_QP,
        .none_roi_delta_qp = 2,
    };
    esp_h264_enc_roi_reg_t roi_reg[8] = { 0 };
    esp_h264_enc_roi_reg_t roi_reg1[8] = { 0 };
    esp_h264_enc_cfg_t base_cfg;
    int index_c = 0;
    for (int16_t i = 0; i < 2; i++) {
        in_frame[i] = heap_caps_calloc(1, sizeof(esp_h264_enc_in_frame_t), MALLOC_CAP_INTERNAL);
        if (!in_frame[i]) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_frame[i] = heap_caps_calloc(1, sizeof(esp_h264_enc_out_frame_t), MALLOC_CAP_INTERNAL);
        if (!out_frame[i]) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        in_frame[i]->raw_data.len = out_length[i];
        in_frame[i]->raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame[i]->raw_data.len, &in_frame[i]->raw_data.len, ESP_H264_MEM_INTERNAL);
        if (!in_frame[i]->raw_data.buffer) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_frame[i]->raw_data.len = out_length[i];
        out_frame[i]->raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame[i]->raw_data.len, &out_frame[i]->raw_data.len, ESP_H264_MEM_INTERNAL);
        if (!out_frame[i]->raw_data.buffer) {
            printf("mem allocation failed. line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_length[i] = out_frame[i]->raw_data.len;
    }

    ret = esp_h264_enc_dual_hw_new(&cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("new failed. line %d \n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_open(enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("open failed .line %d \n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_hw_get_param_hd0(enc, &param_hd0);
    if (ret != ESP_H264_ERR_OK) {
        printf("esp_h264_enc_hw_get_param_hd error. line %d \n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_hw_get_param_hd1(enc, &param_hd1);
    if (ret != ESP_H264_ERR_OK) {
        printf("esp_h264_enc_hw_get_param_hd error. line %d \n", __LINE__);
        goto _exit_dual_;
    }
    for (int8_t idx = ESP_H264_ROI_MODE_DISABLE; idx < ESP_H264_ROI_MODE_INVALID; idx++) {
        roi_cfg.roi_mode = idx;
        roi_cfg.none_roi_delta_qp = -26;
        while (1) {
            index_c ++;
            param_hd = index_c % 2 ? param_hd0 : param_hd1;
            memcpy(&base_cfg, index_c % 2 ? &cfg.cfg0 : &cfg.cfg1, sizeof(esp_h264_enc_cfg_t));
            roi_cfg.none_roi_delta_qp = (roi_cfg.none_roi_delta_qp + 1) % 51;
            ret = esp_h264_enc_hw_cfg_roi(param_hd, roi_cfg);
            if (ret != ESP_H264_ERR_OK) {
                printf("ROI configure error. line %d \n", __LINE__);
                goto _exit_dual_;
            }

            roi_reg[0].x = 0;
            roi_reg[0].y = 0;
            roi_reg[0].len_x = 0;
            roi_reg[0].len_y = 0;
            roi_reg[1].x = 0;
            roi_reg[1].y = 0;
            roi_reg[1].len_x = (base_cfg.res.width + 15) >> 4;
            roi_reg[1].len_y = ((base_cfg.res.height + 15) >> 4) >> 3;
            roi_reg[2].x = 0;
            roi_reg[2].y = 0;
            roi_reg[2].len_x = ((base_cfg.res.width + 15) >> 4) >> 3;
            roi_reg[2].len_y = (base_cfg.res.height + 15) >> 4;
            roi_reg[3].x = 0;
            roi_reg[3].y = ((base_cfg.res.height + 15) >> 4) * 7 >> 3;
            roi_reg[3].len_x = ((base_cfg.res.width + 15) >> 4);
            roi_reg[3].len_y = ((base_cfg.res.height + 15) >> 4) >> 3;
            roi_reg[4].x = ((base_cfg.res.width + 15) >> 4) * 7 >> 3;
            roi_reg[4].y = 0;
            roi_reg[4].len_x = ((base_cfg.res.width + 15) >> 4) >> 3;
            roi_reg[4].len_y = (base_cfg.res.height + 15) >> 4;
            roi_reg[5].x = ((base_cfg.res.width + 15) >> 4) >> 3;
            roi_reg[5].y = ((base_cfg.res.height + 15) >> 4) >> 3;
            roi_reg[5].len_x = ((base_cfg.res.width + 15) >> 4) >> 3;
            roi_reg[5].len_y = ((base_cfg.res.height + 15) >> 4) >> 3;
            roi_reg[6].x = ((base_cfg.res.width + 15) >> 4) * 2 >> 3;
            roi_reg[6].y = ((base_cfg.res.height + 15) >> 4) * 2 >> 3;
            roi_reg[6].len_x = ((base_cfg.res.width + 15) >> 4) >> 3;
            roi_reg[6].len_y = ((base_cfg.res.height + 15) >> 4) >> 3;
            roi_reg[7].x = ((base_cfg.res.width + 15) >> 4) * 3 >> 3;
            roi_reg[7].y = ((base_cfg.res.height + 15) >> 4) * 3 >> 3;
            roi_reg[7].len_x = ((base_cfg.res.width + 15) >> 4) * 2 >> 3;
            roi_reg[7].len_y = ((base_cfg.res.height + 15) >> 4) * 2 >> 3;
            if (idx > ESP_H264_ROI_MODE_DISABLE) {
                for (uint8_t i = 0; i < 8; i++) {
                    roi_reg[i].reg_idx = i;
                    if (roi_cfg.roi_mode == ESP_H264_ROI_MODE_FIX_QP) {
                        roi_reg[i].qp = ((i + index_c - 51) % 51) < 0 ? 0 : ((i + index_c - 51) % 51);
                    } else {
                        roi_reg[i].qp = (i + index_c - 51) % 51;
                    }
                    ret = esp_h264_enc_hw_set_roi_region(param_hd, roi_reg[i]);
                    if (ret != ESP_H264_ERR_OK) {
                        printf("ROI region error. line %d \n", __LINE__);
                        goto _exit_dual_;
                    }
                }
            }

            roi_reg[0].qp = 0;
            for (int16_t i = 0; i < 2; i++) {
                esp_h264_enc_cfg_hw_t cfg_tmp = i == 0 ? cfg.cfg0 : cfg.cfg1;
                int ret_w = read_enc_cb(in_frame[i], width[i], height[i], cfg_tmp.pic_type);
                if (ret_w <= 0) {
                    goto _exit_dual_;
                }
            }

            ret = esp_h264_enc_dual_process(enc, in_frame, out_frame);
            if (ret != ESP_H264_ERR_OK) {
                printf("process failed. line %d \n", __LINE__);
                goto _exit_dual_;
            }
            for (int16_t i = 0; i < 2; i++) {
                write_enc_cb(out_frame[i]);
            }
            for (size_t i = 0; i < 8; i++) {
                roi_reg1[i].reg_idx = i;
                ret = esp_h264_enc_hw_get_roi_region(param_hd, &roi_reg1[i]);
                if (roi_cfg.roi_mode == ESP_H264_ROI_MODE_DISABLE) {
                    if (ret != ESP_H264_ERR_OK
                            || roi_reg1[i].x != 0
                            || roi_reg1[i].y != 0
                            || roi_reg1[i].len_x != 0
                            || roi_reg1[i].len_y != 0
                            || roi_reg1[i].qp != 0) {
                        goto _exit_dual_;
                    }
                } else {
                    if (ret != ESP_H264_ERR_OK
                            || roi_reg1[i].x != roi_reg[i].x
                            || roi_reg1[i].y != roi_reg[i].y
                            || roi_reg1[i].len_x != roi_reg[i].len_x
                            || roi_reg1[i].len_y != roi_reg[i].len_y
                            || roi_reg1[i].qp != roi_reg[i].qp) {
                        goto _exit_dual_;
                    }
                }
            }
        }
    }
_exit_dual_:
    ret |= esp_h264_enc_dual_close(enc);
    ret |= esp_h264_enc_dual_del(enc);
    for (int16_t i = 0; i < 2; i++) {
        if (in_frame[i]) {
            if (in_frame[i]->raw_data.buffer) {
                esp_h264_free(in_frame[i]->raw_data.buffer);
                esp_h264_free(in_frame[i]);
            }
        }
        if (out_frame[i]) {
            if (out_frame[i]->raw_data.buffer) {
                esp_h264_free(out_frame[i]->raw_data.buffer);
                esp_h264_free(out_frame[i]);
            }
        }
    }
    return ret;
}
/** MV component*/
esp_h264_err_t single_hw_enc_mv_pkt_test(esp_h264_enc_cfg_hw_t cfg)
{
    esp_h264_enc_in_frame_t in_frame = {0};
    esp_h264_enc_out_frame_t out_frame = {0};
    esp_h264_err_t ret = ESP_H264_ERR_FAIL;
    esp_h264_enc_handle_t enc = NULL;
    esp_h264_enc_param_hw_handle_t param_hd  = NULL;
    esp_h264_enc_mvm_pkt_t mv_pkt = {0};
    uint32_t length = 0;

    mv_pkt.len = ((cfg.res.width + 15) >> 4) * ((cfg.res.height + 15) >> 4);
    mv_pkt.len *= sizeof(*mv_pkt.data);
    mv_pkt.data = esp_h264_aligned_calloc(16, 1, mv_pkt.len, &mv_pkt.len, ESP_H264_MEM_INTERNAL);
    if (!mv_pkt.data) {
        printf("mem allocation failed.line %d \n", __LINE__);
        goto _mv_pkt_exit_;
    }

    in_frame.raw_data.len = (int)((float)cfg.res.width * cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame.raw_data.len, &in_frame.raw_data.len, ESP_H264_MEM_INTERNAL);
    if (!in_frame.raw_data.buffer) {
        printf("mem allocation failed.line %d \n", __LINE__);
        goto _mv_pkt_exit_;
    }
    out_frame.raw_data.len = (int)((float)cfg.res.width * cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type)) / 10;
    out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame.raw_data.len, &out_frame.raw_data.len, ESP_H264_MEM_INTERNAL);
    if (!out_frame.raw_data.buffer) {
        printf("mem allocation failed.line %d \n", __LINE__);
        goto _mv_pkt_exit_;
    }

    ret = esp_h264_enc_hw_new(&cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("new failed. line %d \n", __LINE__);
        goto _mv_pkt_exit_;
    }

    ret = esp_h264_enc_hw_get_param_hd(enc, &param_hd);
    if (ret != ESP_H264_ERR_OK) {
        printf("esp_h264_enc_hw_get_param_hd error. line %d \n", __LINE__);
        goto _mv_pkt_exit_;
    }
    ret = esp_h264_enc_open(enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("open failed .line %d \n", __LINE__);
        goto _mv_pkt_exit_;
    }
    esp_h264_enc_mv_cfg_t mv_cfg = {
        .mv_mode = ESP_H264_MVM_MODE_DISABLE,
        .mv_fmt = ESP_H264_MVM_FMT_ALL,
    };
    for (int8_t mode = ESP_H264_MVM_MODE_DISABLE; mode < ESP_H264_MVM_MODE_INVALID; mode++) {
        for (int8_t fmt = ESP_H264_MVM_FMT_ALL; fmt < ESP_H264_MVM_FMT_INVALID; fmt++) {
            mv_cfg.mv_mode = mode;
            mv_cfg.mv_fmt = fmt;
            ret = esp_h264_enc_hw_cfg_mv(param_hd, mv_cfg);
            if (ret != ESP_H264_ERR_OK) {
                printf("open failed .line %d \n", __LINE__);
                goto _mv_pkt_exit_;
            }
            while (1) {
                int ret_w = read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type);
                if (ret_w <= 0) {
                    break;
                }
                ret = esp_h264_enc_hw_set_mv_pkt(param_hd, mv_pkt);
                if (ret != ESP_H264_ERR_OK) {
                    printf("esp_h264_enc_hw_set_mv_pkt failed. line %d \n", __LINE__);
                    goto _mv_pkt_exit_;
                }
                ret = esp_h264_enc_process(enc, &in_frame, &out_frame);
                if (ret != ESP_H264_ERR_OK) {
                    printf("process failed. line %d \n", __LINE__);
                    goto _mv_pkt_exit_;
                }
                write_enc_cb(&out_frame);
                ret = esp_h264_enc_hw_get_mv_data_len(param_hd, &length);
                if (ret != ESP_H264_ERR_OK) {
                    printf("esp_h264_enc_hw_get_mv_data_len failed. line %d \n", __LINE__);
                    goto _mv_pkt_exit_;
                }
                if (length > 0) {
                    write_mvm(&mv_pkt, length);
                }
            }
        }
    }
_mv_pkt_exit_:
    ret |= esp_h264_enc_close(enc);
    ret |= esp_h264_enc_del(enc);
    if (in_frame.raw_data.buffer) {
        esp_h264_free(in_frame.raw_data.buffer);
    }
    if (out_frame.raw_data.buffer) {
        esp_h264_free(out_frame.raw_data.buffer);
    }
    if (mv_pkt.data) {
        esp_h264_free(mv_pkt.data);
        mv_pkt.data = NULL;
    }
    return ret;
}

esp_h264_err_t dual_hw_enc_mv_pkt_test(esp_h264_enc_cfg_dual_hw_t cfg)
{
    esp_h264_enc_in_frame_t *in_frame[2] = {NULL, NULL};
    esp_h264_enc_out_frame_t *out_frame[2] = {NULL, NULL};
    esp_h264_err_t ret = ESP_H264_ERR_FAIL;
    esp_h264_enc_dual_handle_t enc = NULL;
    int16_t out_length[2];
    esp_h264_enc_param_hw_handle_t param_hd0 = NULL;
    esp_h264_enc_param_hw_handle_t param_hd1 = NULL;
    esp_h264_enc_param_hw_handle_t param_hd = NULL;
    int16_t width[2] = { cfg.cfg0.res.width, cfg.cfg1.res.width };
    int16_t height[2] = { cfg.cfg0.res.height, cfg.cfg1.res.height };
    out_length[0] = (int)((float)cfg.cfg0.res.width * cfg.cfg0.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg0.pic_type));
    out_length[1] = (int)((float)cfg.cfg1.res.width * cfg.cfg1.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg1.pic_type));
    esp_h264_enc_mv_cfg_t mv_cfg = {
        .mv_mode = ESP_H264_MVM_MODE_DISABLE,
        .mv_fmt = ESP_H264_MVM_FMT_ALL,
    };
    int index_c = 0;
    esp_h264_enc_mvm_pkt_t mv_pkt[2];
    mv_pkt[0].data = mv_pkt[1].data = NULL;
    uint32_t length = 0;
    esp_h264_enc_cfg_t base_cfg;
    esp_h264_enc_mvm_pkt_t mv_pkt_tmp;
    for (int16_t i = 0; i < 2; i++) {
        mv_pkt[i].len = ((width[i] + 15) >> 4) * ((height[i] + 15) >> 4);
        mv_pkt[i].len *= sizeof(*mv_pkt[i].data);
        mv_pkt[i].data = esp_h264_aligned_calloc(16, 1, mv_pkt[i].len, &mv_pkt[i].len, ESP_H264_MEM_INTERNAL);
        if (!mv_pkt[i].data) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        in_frame[i] = heap_caps_calloc(1, sizeof(esp_h264_enc_in_frame_t), MALLOC_CAP_INTERNAL);
        if (!in_frame[i]) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_frame[i] = heap_caps_calloc(1, sizeof(esp_h264_enc_out_frame_t), MALLOC_CAP_INTERNAL);
        if (!out_frame[i]) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        in_frame[i]->raw_data.len = out_length[i];
        in_frame[i]->raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame[i]->raw_data.len, &in_frame[i]->raw_data.len, ESP_H264_MEM_INTERNAL);
        if (!in_frame[i]->raw_data.buffer) {
            printf("mem allocation failed.line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_frame[i]->raw_data.len = out_length[i];
        out_frame[i]->raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_frame[i]->raw_data.len, &out_frame[i]->raw_data.len, ESP_H264_MEM_INTERNAL);
        if (!out_frame[i]->raw_data.buffer) {
            printf("mem allocation failed. line %d \n", __LINE__);
            goto _exit_dual_;
        }
        out_length[i] = out_frame[i]->raw_data.len;
    }

    ret = esp_h264_enc_dual_hw_new(&cfg, &enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("new failed. line %d \n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_open(enc);
    if (ret != ESP_H264_ERR_OK) {
        printf("open failed .line %d \n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_hw_get_param_hd0(enc, &param_hd0);
    if (ret != ESP_H264_ERR_OK) {
        printf("esp_h264_enc_hw_get_param_hd error. line %d \n", __LINE__);
        goto _exit_dual_;
    }
    ret = esp_h264_enc_dual_hw_get_param_hd1(enc, &param_hd1);
    if (ret != ESP_H264_ERR_OK) {
        printf("esp_h264_enc_hw_get_param_hd error. line %d \n", __LINE__);
        goto _exit_dual_;
    }
    for (int8_t mode = ESP_H264_MVM_MODE_DISABLE; mode < ESP_H264_MVM_MODE_INVALID; mode++) {
        for (int8_t fmt = ESP_H264_MVM_FMT_ALL; fmt < ESP_H264_MVM_FMT_INVALID; fmt++) {
            mv_cfg.mv_mode = mode;
            mv_cfg.mv_fmt = fmt;
            param_hd = index_c % 2 ? param_hd0 : param_hd1;
            memcpy(&mv_pkt_tmp, index_c % 2 ? &mv_pkt[0] : &mv_pkt[1], sizeof(esp_h264_enc_mvm_pkt_t));
            memcpy(&base_cfg, index_c % 2 ? &cfg.cfg0 : &cfg.cfg1, sizeof(esp_h264_enc_cfg_t));
            ret = esp_h264_enc_hw_cfg_mv(param_hd, mv_cfg);
            if (ret != ESP_H264_ERR_OK) {
                printf("open failed .line %d \n", __LINE__);
                goto _exit_dual_;
            }
            while (1) {
                index_c ++;
                for (int16_t i = 0; i < 2; i++) {
                    esp_h264_enc_cfg_hw_t cfg_tmp = i == 0 ? cfg.cfg0 : cfg.cfg1;
                    int ret_w = read_enc_cb(in_frame[i], width[i], height[i], cfg_tmp.pic_type);
                    if (ret_w <= 0) {
                        goto _exit_dual_;
                    }
                }
                ret = esp_h264_enc_hw_set_mv_pkt(param_hd, mv_pkt_tmp);
                if (ret != ESP_H264_ERR_OK) {
                    printf("esp_h264_enc_hw_set_mv_pkt failed. line %d \n", __LINE__);
                    goto _exit_dual_;
                }
                ret = esp_h264_enc_dual_process(enc, in_frame, out_frame);
                if (ret != ESP_H264_ERR_OK) {
                    printf("process failed. line %d \n", __LINE__);
                    goto _exit_dual_;
                }
                for (int16_t i = 0; i < 2; i++) {
                    write_enc_cb(out_frame[i]);
                }
                ret = esp_h264_enc_hw_get_mv_data_len(param_hd, &length);
                if (ret != ESP_H264_ERR_OK) {
                    printf("esp_h264_enc_hw_get_mv_data_len failed. line %d \n", __LINE__);
                    goto _exit_dual_;
                }

                if (length > 0) {
                    write_mvm(&mv_pkt_tmp, length);
                }
            }
        }
    }
_exit_dual_:
    ret |= esp_h264_enc_dual_close(enc);
    ret |= esp_h264_enc_dual_del(enc);
    for (int16_t i = 0; i < 2; i++) {
        if (in_frame[i]) {
            if (in_frame[i]->raw_data.buffer) {
                esp_h264_free(in_frame[i]->raw_data.buffer);
                esp_h264_free(in_frame[i]);
            }
        }
        if (out_frame[i]) {
            if (out_frame[i]->raw_data.buffer) {
                esp_h264_free(out_frame[i]->raw_data.buffer);
                esp_h264_free(out_frame[i]);
            }
        }
        if (mv_pkt[i].data) {
            esp_h264_free(mv_pkt[i].data);
            mv_pkt[i].data = NULL;
        }
    }
    return ret;
}
