/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "soc/soc_caps.h"
#include "esp_h264_efuse.h"
#include "h264_nal.h"

#define LOG_MAX_FRAME_NUM 8
#define SLICE_I7          7
#define SLICE_P5          5
#define LEVL_IDC_MAX      (51)
#define BS_EOF(bs)        (((bs)->p) >= ((bs)->end) ? 1 : 0)

/**
 * @brief  Value is in [0, 255] needing bit numbers
 *
 */
static const uint8_t i_size0_255[256] = {
    1,                                              /* value: 0*/
    1,                                              /* value: 1*/
    2, 2,                                           /* value: 2, 3*/
    3, 3, 3, 3,                                     /* value: 4 - 7*/
    4, 4, 4, 4, 4, 4, 4, 4,                         /* value: 8 - 15*/
    5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, /* value: 16 - 31*/
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, /* value: 32 - 63*/
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, /* value: 32 - 63*/
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, /* value: 64 - 127*/
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, /* value: 64 - 127*/
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, /* value: 64 - 127*/
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, /* value: 64 - 127*/
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, /* value: 128 - 255*/
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, /* value: 128 - 255*/
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, /* value: 128 - 255*/
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, /* value: 128 - 255*/
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, /* value: 128 - 255*/
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, /* value: 128 - 255*/
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, /* value: 128 - 255*/
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, /* value: 128 - 255*/
};

/**
 * H.264 Annex A Table A-1 subset: MaxMBPS, MaxFS (macroblocks), level_idc.
 * Must be sorted by increasing MaxMBPS / MaxFS for the linear scan below.
 */
static const int level_idc_table[][3] = {
    { 11880, 396, 20 },
    { 19800, 792, 21 },
    { 20250, 1620, 22 },
    { 40500, 1620, 30 },
    { 108000, 3600, 31 },
    { 216000, 5120, 32 },
    { 245760, 8192, 41 },
    { 522240, 8704, 42 },
    { 589824, 22080, 50 },
};

typedef struct {
    uint8_t *start;
    uint8_t *p;
    uint8_t *end;
    int      bits_left;
    int      zero_run;  /*<! Count (capped at 2) of consecutive 0x00 bytes just emitted,
                             used to insert emulation_prevention_three_byte. */
} bs_t;

static int level_idcl(int width, int height, int fps)
{
    int mb_w = (width + 15) / 16;
    int mb_h = (height + 15) / 16;
    int frame_mbs = mb_w * mb_h;
    int mb_per_s = fps * frame_mbs;

    for (int i = 0; i < (int)(sizeof(level_idc_table) / sizeof(level_idc_table[0])); i++) {
        if (mb_per_s <= level_idc_table[i][0] && frame_mbs <= level_idc_table[i][1]) {
            return level_idc_table[i][2];
        }
    }
    return LEVL_IDC_MAX;
}

/**
 * @brief  Emit one completed RBSP byte into the EBSP output, inserting an
 *         `emulation_prevention_three_byte` (0x03) right before it when the
 *         previous two emitted bytes were 0x00 0x00 and this byte is 0x00-0x03.
 *
 * @note  Per H.264 7.4.1.1, within a NAL unit the byte sequences 0x000000,
 *        0x000001 and 0x000002 must never occur; 0x03 must be inserted to
 *        break them up. Decoders then strip that 0x03 back out.
 */
static void bs_emit_byte(bs_t *b, uint8_t byte)
{
    if (b->zero_run >= 2 && byte <= 3) {
        if (!BS_EOF(b)) {
            b->p[0] = 0x03;
        }
        b->p++;
        b->zero_run = 0;
    }
    if (!BS_EOF(b)) {
        b->p[0] = byte;
    }
    b->p++;
    b->zero_run = (byte == 0) ? (b->zero_run + 1) : 0;
}

static void bs_write_u1(bs_t *b, uint32_t v)
{
    b->bits_left--;
    if (!BS_EOF(b)) {
        (*(b->p)) |= ((v & 0x01) << b->bits_left);
    }
    if (b->bits_left == 0) {
        bs_emit_byte(b, b->p[0]);
        if (!BS_EOF(b)) {
            b->p[0] = 0;
        }
        b->bits_left = 8;
    }
}

static void bs_write_u(bs_t *b, uint32_t v, uint32_t n)
{
    for (int i = 0; i < n; i++) {
        bs_write_u1(b, (v >> (n - i - 1)) & 0x01);
    }
}

static void bs_write_ue(bs_t *b, unsigned int val)
{
    int i_size = 0;
    if (val == 0) {
        bs_write_u1(b, 1);
    } else {
        unsigned int tmp = ++val;
        if (tmp >= 0x00010000) {
            i_size += 16;
            tmp >>= 16;
        }
        if (tmp >= 0x100) {
            i_size += 8;
            tmp >>= 8;
        }
        i_size += i_size0_255[tmp];
        bs_write_u(b, val, 2 * i_size - 1);
    }
}

static void bs_write_se(bs_t *b, int32_t v)
{
    if (v <= 0) {
        bs_write_ue(b, -v * 2);
    } else {
        bs_write_ue(b, v * 2 - 1);
    }
}

static void bs_rbsp_trailing(bs_t *b)
{
    bs_write_u1(b, 1);
    bs_write_u(b, 0, b->bits_left & 7);
}

static int nal_bs_size(bs_t *b)
{
    int bits_len = (8 - b->bits_left) + (b->p - b->start) * 8;
    return bits_len;
}

uint16_t esp_h264_enc_set_sps(uint8_t *buffer, uint16_t len, uint16_t height, uint16_t width, uint8_t fps)
{
    uint32_t *start_code = (uint32_t *)buffer;
    start_code[0] = 0x01000000;
    bs_t bs = {
        .bits_left = 8,
        .end = buffer + len - 4,
        .p = buffer + 4,
        .start = buffer + 4,
    };
    bs.p[0] = 0;
    uint8_t forbidden_zero_bit = 0;
    uint8_t nal_ref_idc = 3;
    uint8_t nal_unit_type = 7;
    uint8_t profile_idc = 66;
    uint8_t constraint_set0_flag = 1;
    uint8_t constraint_set1_flag = 1;
    uint8_t constraint_set2_flag = 0;
    uint8_t constraint_set3_flag = 0;
    uint8_t constraint_set4_flag = 0;
    uint8_t constraint_set5_flag = 0;
    uint8_t reserved_zero_2bits = 0;
    uint8_t level_idc = level_idcl(width, height, fps);
    uint8_t seq_parameter_set_id = 0;
    uint8_t log2_max_frame_num_minus4 = LOG_MAX_FRAME_NUM - 4;
    uint8_t pic_order_cnt_type = 2;
    uint8_t num_ref_frames = 1;
    int gaps_in_frame_num_value_allowed_flag = 0;
    int xsize = (width + 15) & (~0xf);
    int ysize = (height + 15) & (~0xf);
    int pic_width_in_mbs_minus1 = (xsize >> 4) - 1;
    int pic_height_in_map_units_minus1 = (ysize >> 4) - 1;
    uint8_t frame_mbs_only_flag = 1;
    uint8_t direct_8x8_inference_flag = 0;
    uint8_t frame_cropping_flag = (width % 16 != 0) || (height % 16 != 0);
    /* Signal fps in VUI timing_info so decoders can recover:
     * fps = time_scale / (2 * num_units_in_tick)  (progressive, fixed frame rate)
     */
    uint8_t vui_parameters_present_flag = 1;

    bs_write_u(&bs, forbidden_zero_bit, 1);
    bs_write_u(&bs, nal_ref_idc, 2);
    bs_write_u(&bs, nal_unit_type, 5);
    bs_write_u(&bs, profile_idc, 8);
    bs_write_u(&bs, constraint_set0_flag, 1);
    bs_write_u(&bs, constraint_set1_flag, 1);
    bs_write_u(&bs, constraint_set2_flag, 1);
    bs_write_u(&bs, constraint_set3_flag, 1);
    bs_write_u(&bs, constraint_set4_flag, 1);
    bs_write_u(&bs, constraint_set5_flag, 1);
    bs_write_u(&bs, reserved_zero_2bits, 2);
    bs_write_u(&bs, level_idc, 8);
    bs_write_ue(&bs, seq_parameter_set_id);
    bs_write_ue(&bs, log2_max_frame_num_minus4);
    bs_write_ue(&bs, pic_order_cnt_type);
    bs_write_ue(&bs, num_ref_frames);
    bs_write_u(&bs, gaps_in_frame_num_value_allowed_flag, 1);
    bs_write_ue(&bs, pic_width_in_mbs_minus1);
    bs_write_ue(&bs, pic_height_in_map_units_minus1);
    bs_write_u(&bs, frame_mbs_only_flag, 1);
    bs_write_u(&bs, direct_8x8_inference_flag, 1);
    bs_write_u(&bs, frame_cropping_flag, 1);
    if (frame_cropping_flag) {
        int frame_crop_left_offset = 0;
        int frame_crop_right_offset = 0;
        int frame_crop_bottom_offset = 0;
        int frame_crop_top_offset = 0;
        int cropx = 2;
        int cropy = 2 * (2 - frame_mbs_only_flag);
        frame_crop_right_offset = (((pic_width_in_mbs_minus1 + 1) * 16) - width) / cropx - frame_crop_left_offset;
        frame_crop_bottom_offset = (((pic_height_in_map_units_minus1 + 1) * 16) - height) / cropy - frame_crop_top_offset;

        bs_write_ue(&bs, frame_crop_left_offset);
        bs_write_ue(&bs, frame_crop_right_offset);
        bs_write_ue(&bs, frame_crop_top_offset);
        bs_write_ue(&bs, frame_crop_bottom_offset);
    }
    bs_write_u(&bs, vui_parameters_present_flag, 1);
    if (vui_parameters_present_flag) {
        uint8_t aspect_ratio_info_present_flag = 0;
        uint8_t overscan_info_present_flag = 0;
        uint8_t video_signal_type_present_flag = 0;
        uint8_t chroma_loc_info_present_flag = 0;
        uint8_t timing_info_present_flag = 1;
        uint32_t num_units_in_tick = 1;
        uint32_t time_scale = (uint32_t)fps * 2;
        uint8_t fixed_frame_rate_flag = 1;
        uint8_t nal_hrd_parameters_present_flag = 0;
        uint8_t vcl_hrd_parameters_present_flag = 0;
        uint8_t pic_struct_present_flag = 0;
        uint8_t bitstream_restriction_flag = 0;

        bs_write_u(&bs, aspect_ratio_info_present_flag, 1);
        bs_write_u(&bs, overscan_info_present_flag, 1);
        bs_write_u(&bs, video_signal_type_present_flag, 1);
        bs_write_u(&bs, chroma_loc_info_present_flag, 1);
        bs_write_u(&bs, timing_info_present_flag, 1);
        if (timing_info_present_flag) {
            bs_write_u(&bs, num_units_in_tick, 32);
            bs_write_u(&bs, time_scale, 32);
            bs_write_u(&bs, fixed_frame_rate_flag, 1);
        }
        bs_write_u(&bs, nal_hrd_parameters_present_flag, 1);
        bs_write_u(&bs, vcl_hrd_parameters_present_flag, 1);
        bs_write_u(&bs, pic_struct_present_flag, 1);
        bs_write_u(&bs, bitstream_restriction_flag, 1);
    }
    bs_rbsp_trailing(&bs);
    return nal_bs_size(&bs) + 32;
}

uint16_t esp_h264_enc_set_pps(uint8_t *buffer, uint16_t len, uint8_t qp, bool db_ena)
{
    uint32_t *start_code = (uint32_t *)buffer;
    start_code[0] = 0x01000000;
    bs_t bs = {
        .bits_left = 8,
        .end = buffer + len - 4,
        .p = buffer + 4,
        .start = buffer + 4,
    };
    bs.p[0] = 0;
    uint8_t forbidden_zero_bit = 0;
    uint8_t nal_ref_idc = 3;
    uint8_t nal_unit_type = 8;
    uint8_t pic_parameter_set_id = 0;
    uint8_t seq_parameter_set_id = 0;
    uint8_t entropy_coding_mode_flag = 0;
    uint8_t bottom_field_pic_order_in_frame_present_flag = 0;
    uint8_t num_slice_groups_minus1 = 0; // all slices of the picture belong to the same slice group
    int8_t num_ref_idx_l0_default_active_minus1 = 0;
    int8_t num_ref_idx_l1_default_active_minus1 = 0;
    uint8_t weighted_pred_flag = 0;
    uint8_t weighted_bipred_idc = 0;
    int8_t pic_init_qp_minus26 = qp - 26;
    /* pic_init_qs (= 26 + pic_init_qs_minus26) is only meaningful for SP/SI slices, which this
     * baseline-profile (profile_idc 66) encoder never emits, so its value has no effect on
     * decoding P/I slices. It must still be spec-valid (0..51) though: mirroring pic_init_qp
     * guarantees that, since `qp` itself is already validated to be within 0..51. An earlier
     * "qp - 2" offset had no functional purpose and went out of range (negative) for qp < 2,
     * producing a non-conformant PPS that compliant decoders reject. */
    int8_t pic_init_qs_minus26 = pic_init_qp_minus26;
    int8_t chroma_qp_index_offset = 0;
    int8_t deblocking_filter_control_present_flag = db_ena;
    uint8_t constrained_intra_pred_flag = 0;
    uint8_t redundant_pic_cnt_present_flag = 0;

    bs_write_u(&bs, forbidden_zero_bit, 1);
    bs_write_u(&bs, nal_ref_idc, 2);
    bs_write_u(&bs, nal_unit_type, 5);
    bs_write_ue(&bs, pic_parameter_set_id);
    bs_write_ue(&bs, seq_parameter_set_id);
    bs_write_u(&bs, entropy_coding_mode_flag, 1);
    bs_write_u(&bs, bottom_field_pic_order_in_frame_present_flag, 1);
    bs_write_ue(&bs, num_slice_groups_minus1);
    bs_write_ue(&bs, num_ref_idx_l0_default_active_minus1);
    bs_write_ue(&bs, num_ref_idx_l1_default_active_minus1);
    bs_write_u(&bs, weighted_pred_flag, 1);
    bs_write_u(&bs, weighted_bipred_idc, 2);
    bs_write_se(&bs, pic_init_qp_minus26);
    bs_write_se(&bs, pic_init_qs_minus26);
    bs_write_se(&bs, chroma_qp_index_offset);
    bs_write_u(&bs, deblocking_filter_control_present_flag, 1);
    bs_write_u(&bs, constrained_intra_pred_flag, 1);
    bs_write_u(&bs, redundant_pic_cnt_present_flag, 1);
    bs_rbsp_trailing(&bs);
    uint16_t nal_size = nal_bs_size(&bs) + 32;
    return nal_size;
}

uint16_t esp_h264_enc_hw_set_slice(uint8_t *buffer, uint32_t len, bool is_iframe, uint32_t frame_num, int8_t qp_delta, bool db_ena)
{
    uint32_t *start_code = (uint32_t *)buffer;
    /* HW placeholder; caller rewrites Annex-B start code (0x00000001 LE) after encode */
    start_code[0] = 0xffffffff;
    bs_t bs = {
        .bits_left = 8,
        .end = buffer + len - 4,
        .p = buffer + 4,
        .start = buffer + 4,
    };
    bs.p[0] = 0;
    uint8_t forbidden_zero_bit = 0;
    uint8_t nal_ref_idc = is_iframe ? 3 : 2;
    uint8_t nal_unit_type = is_iframe ? 5 : 1;
    uint8_t first_mb_in_slice = 0;
    uint8_t slice_type = is_iframe ? SLICE_I7 : SLICE_P5;
    uint8_t pic_parameter_set_id = 0;
    uint8_t idrpicflag = is_iframe; // This design I frame is IDR frame.
    uint8_t deblocking_filter_control_present_flag = db_ena;

    bs_write_u(&bs, forbidden_zero_bit, 1);
    bs_write_u(&bs, nal_ref_idc, 2);
    bs_write_u(&bs, nal_unit_type, 5);
    bs_write_ue(&bs, first_mb_in_slice);
    bs_write_ue(&bs, slice_type);
    bs_write_ue(&bs, pic_parameter_set_id);
    bs_write_u(&bs, frame_num, LOG_MAX_FRAME_NUM);
    if (idrpicflag) {
        bs_write_ue(&bs, 0);
    }
    if (!is_iframe) {
        uint8_t num_ref_idx_active_override_flag = 0;
        bs_write_u(&bs, num_ref_idx_active_override_flag, 1);
    }
    if (slice_type % 5 != 2 && slice_type % 5 != 4) {
        uint8_t ref_pic_list_modification_flag_l0 = 0;
        bs_write_u(&bs, ref_pic_list_modification_flag_l0, 1);
    }
    if (idrpicflag) {
        uint8_t no_output_of_prior_pics_flag = 0;
        uint8_t long_term_reference_flag = 0;
        bs_write_u(&bs, no_output_of_prior_pics_flag, 1);
        bs_write_u(&bs, long_term_reference_flag, 1);
    } else {
        uint8_t adaptive_ref_pic_marking_mode_flag = 0;
        bs_write_u(&bs, adaptive_ref_pic_marking_mode_flag, 1);
    }
    bs_write_se(&bs, qp_delta);
    if (deblocking_filter_control_present_flag) {
        uint8_t disable_deblocking_filter_idc = !db_ena;
        bs_write_se(&bs, disable_deblocking_filter_idc);
        if (disable_deblocking_filter_idc != 1) {
            uint8_t slice_alpha_c0_offset_div2 = 0;
            uint8_t slice_beta_offset_div2 = 0;
            bs_write_se(&bs, slice_alpha_c0_offset_div2);
            bs_write_se(&bs, slice_beta_offset_div2);
        }
    }
    uint16_t nal_size = nal_bs_size(&bs) + 32;
    /*
     * Flash encryption on, 16-byte phase: when the byte offset implied by (nal_size >> 3) has
     * (offset % 16) >= 8, prepend an AUD (NAL type 9) before the slice RBSP (starts at buffer+4
     * after the slice start-code word) so CABAC/DMA sees an 8-byte shifted layout. nal_size += 64
     * is eight extra bytes in bits, same length accounting style as the +32 reserve elsewhere.
     */
#if SOC_PSRAM_DMA_CAPABLE || SOC_DMA_CAN_ACCESS_FLASH
    if (ESP_H264_IS_FLASH_ENCRYPTION_ENABLED()) {
        int32_t nal_bytes = nal_size >> 3;
        uint32_t buffer_copy = (uint32_t)(buffer + nal_bytes);
        buffer_copy &= 0xf;
        /* Need room for 8-byte AUD insert after the 4-byte start code (overlap-safe move). */
        if (buffer_copy >= 0x8 && (uint32_t)(nal_bytes + 13) <= len) {
            static const uint8_t aud_nalu_8B[] = {
                0x09,                    /* nal header (AUD) */
                0xF0,                    /* primary_pic_type + RBSP trailing */
                0x00, 0x00,              /* padding */
                0x00, 0x00, 0x00, 0x01,  /* start code for next NAL (slice) */
            };
            memmove(buffer + 12, buffer + 4, (size_t)nal_bytes + 1);
            memcpy(buffer + 4, aud_nalu_8B, 8);
            nal_size += 64;
        }
    }
#endif
    return nal_size;
}
