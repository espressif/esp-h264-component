/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_h264_alloc.h"
#include "h264_rc.h"

typedef struct esp_h264_rc {
    uint8_t  qp_max;
    uint8_t  qp_min;
    int8_t   qpm;
    int8_t   eqp;
    uint8_t  qp_average_frame;
    uint32_t target_frame_bits;
    uint32_t bits_per_frame;
    uint32_t frame_bits_last[4];
    uint32_t frame_bits_last4_average;
    float    mad[4];
    float    mad_last4_average;
    float    mad_frame_pred;
    uint16_t mb_cnt;
    int32_t  ebits;
    float    err_sum;
    uint8_t  frame_num;
} esp_h264_rc_t;

static const int init_mad[] = { 1, 3, 4, 5, 6, 7, 8, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9 };
#define INIT_MAD_MAX_IDX ((int)(sizeof(init_mad) / sizeof(init_mad[0]) - 1))

#define CLIP3(min, max, v) ((v) > (max) ? (max) : ((v) < (min) ? (min) : (v)))

void esp_h264_enc_hw_rc_del(esp_h264_rc_hd_t rc_hd)
{
    if (rc_hd) {
        esp_h264_free(rc_hd);
    }
}

esp_h264_rc_hd_t esp_h264_enc_hw_rc_new(uint8_t qp_max, uint8_t qp_min, uint32_t bitrate, uint8_t fps, uint8_t mb_width, uint8_t mb_height)
{
    uint32_t actual_size;
    uint16_t mb_cnt;
    int mad_idx;
    float mad;

    if (fps == 0 || mb_width == 0 || mb_height == 0 || qp_max < qp_min) {
        return NULL;
    }
    mb_cnt = (uint16_t)mb_width * mb_height;
    if (mb_cnt == 0) {
        return NULL;
    }

    esp_h264_rc_t *prc = esp_h264_calloc_prefer(1, sizeof(esp_h264_rc_t), &actual_size, ESP_H264_MEM_INTERNAL, ESP_H264_MEM_SPIRAM);
    if (prc == NULL) {
        return NULL;
    }
    prc->frame_num = 0;
    prc->qp_max = qp_max;
    prc->qp_min = qp_min;
    prc->qpm = (qp_max + qp_min) >> 1;
    prc->qp_average_frame = (uint8_t)prc->qpm;
    prc->bits_per_frame = bitrate / fps;
    if (prc->bits_per_frame == 0) {
        prc->bits_per_frame = 1;
    }
    prc->mb_cnt = mb_cnt;
    mad_idx = CLIP3(0, INIT_MAD_MAX_IDX, 53 / (prc->qpm + 1));
    mad = 8.0f / init_mad[mad_idx];
    for (uint8_t i = 0; i < 4; i++) {
        prc->frame_bits_last[i] = prc->bits_per_frame;
        prc->mad[i] = mad;
    }
    prc->frame_bits_last4_average = prc->bits_per_frame;
    prc->mad_last4_average = mad;
    return prc;
}

void esp_h264_enc_hw_rc_set_qp(esp_h264_rc_hd_t rc_hd, uint8_t qp_max, uint8_t qp_min)
{
    esp_h264_rc_t *prc = (esp_h264_rc_t *)rc_hd;
    if (prc == NULL || qp_max < qp_min) {
        return;
    }
    prc->qp_max = qp_max;
    prc->qp_min = qp_min;
}

void esp_h264_enc_hw_rc_set_bt_fps(esp_h264_rc_hd_t rc_hd, uint32_t bitrate, uint8_t fps)
{
    esp_h264_rc_t *prc = (esp_h264_rc_t *)rc_hd;
    if (prc == NULL || fps == 0) {
        return;
    }
    prc->bits_per_frame = bitrate / fps;
    if (prc->bits_per_frame == 0) {
        prc->bits_per_frame = 1;
    }
}

void esp_h264_rc_start(esp_h264_rc_hd_t rc_hd, bool is_iframe, uint32_t *rate, uint32_t *pred_mad, uint8_t *qp)
{
    esp_h264_rc_t *prc = (esp_h264_rc_t *)rc_hd;
    float mad_pred;
    int target_frame_bits;
    int target_mb_bits = 0;

    if (prc == NULL || rate == NULL || pred_mad == NULL || qp == NULL) {
        return;
    }

    mad_pred = prc->mad_last4_average;
    target_frame_bits = (int)((prc->bits_per_frame * 10 - 4 * prc->frame_bits_last4_average) / 6);
    if (target_frame_bits < 1) {
        target_frame_bits = 1;
    }
    prc->target_frame_bits = (uint32_t)target_frame_bits;
    if (prc->frame_bits_last4_average == 0) {
        prc->mad_frame_pred = mad_pred;
    } else {
        prc->mad_frame_pred = (mad_pred * prc->target_frame_bits) / prc->frame_bits_last4_average;
    }
    if (prc->mad_frame_pred < 1) {
        prc->mad_frame_pred = 1;
    }
    prc->qpm = (int8_t)((int)prc->qp_average_frame + (int)prc->eqp);
    *qp = CLIP3(prc->qp_min, prc->qp_max, prc->qpm);
    *rate = 1;
    *pred_mad = (uint32_t)prc->mad_frame_pred;
    if (!is_iframe) {
        target_mb_bits = (int)(prc->target_frame_bits / prc->mb_cnt);
        *rate = (uint32_t)(256.0f * target_mb_bits / prc->mad_frame_pred / 15);
        if (*rate < 1) {
            *rate = 1;
        }
    }
}

void esp_h264_rc_end(esp_h264_rc_hd_t rc_hd, uint32_t total_enc_bits, uint32_t frame_qp_sum, uint32_t frame_mad_sum)
{
    esp_h264_rc_t *prc = (esp_h264_rc_t *)rc_hd;
    float bits_err;
    float mad_cur;
    float err_bit_per_frame;

    if (prc == NULL || prc->mb_cnt == 0 || prc->bits_per_frame == 0) {
        return;
    }

    mad_cur = 1.0f * frame_mad_sum / prc->mb_cnt;
    prc->qp_average_frame = frame_qp_sum / prc->mb_cnt;
    prc->ebits += (int32_t)total_enc_bits - (int32_t)prc->bits_per_frame;

    prc->mad[(prc->frame_num & 0x3)] = mad_cur;
    prc->frame_bits_last[(prc->frame_num & 0x3)] = total_enc_bits;
    prc->frame_bits_last4_average = 0;
    prc->mad_last4_average = 0.0f;
    for (uint8_t i = 0; i < 4; i++) {
        prc->frame_bits_last4_average += prc->frame_bits_last[i];
        prc->mad_last4_average += prc->mad[i];
    }
    prc->frame_bits_last4_average >>= 2;
    prc->mad_last4_average /= 4;

    bits_err = 1.0f * prc->frame_bits_last4_average / prc->bits_per_frame - 1.0f;
    if (prc->err_sum > 10.0f) {
        prc->err_sum = prc->err_sum * 0.85f + bits_err;
    } else {
        prc->err_sum += bits_err;
    }
    prc->eqp = (int8_t)CLIP3(-5, 5, (int)prc->err_sum);
    /* Enforce a minimum step of 1 when cumulative bit error is large, without capping the [-5, 5] range */
    err_bit_per_frame = 1.0f * prc->ebits / prc->bits_per_frame;
    if (err_bit_per_frame > 0.2f && prc->eqp < 1) {
        prc->eqp = 1;
    } else if (err_bit_per_frame < -0.2f && prc->eqp > -1) {
        prc->eqp = -1;
    }
    prc->frame_num++;
}
