/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_h264_alloc.h"
#include "esp_h264_intr_alloc.h"
#include "esp_h264_enc_dual_hw.h"
#include "esp_h264_enc_hw_param.h"

static const char *TAG = "H264_ENC.HW.DUAL";

typedef struct esp_h264_hw_handle {
    esp_h264_enc_dual_t         base;
    esp_h264_enc_param_hw_t    *param_hd0;
    esp_h264_enc_param_hw_t    *param_hd1;
    uint8_t                    *db_tmp;
    h264_hal_context_t          h264_hal;
    h264_dma_hal_context_t      dma2d_hal;
    h264_dma_desc_t            *dsc_yuv;
    h264_dma_desc_t            *dsc_dbtmp[2];
    h264_dma_desc_t            *dsc_bs;
    uint8_t                     frame_num;
    uint8_t                     gop;
    esp_h264_mutex_t            frame_done;
    esp_h264_intr_hd_t          intr_hd;
} esp_h264_hw_handle_t;

static void h264_frame_isr(void *arg)
{
    esp_h264_hw_handle_t *hw_hd = (esp_h264_hw_handle_t *)arg;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    uint32_t status = 0;
    while (1) {
        status = h264_hal_get_intr_status(&hw_hd->h264_hal);
        if (!status) {
            break;
        } else if ((status & H264_INTR_2MB_LINE_DONE)) {
            h264_hal_clear_intr_status(&hw_hd->h264_hal, H264_INTR_2MB_LINE_DONE);
            h264_dma_hal_start_yuv_dma(&hw_hd->dma2d_hal);
            h264_dma_hal_start_ref_dma(&hw_hd->dma2d_hal);
            h264_dma_hal_start_rx_db12_4_dma(&hw_hd->dma2d_hal);
            h264_dma_hal_start_rx_dbtmp_dma(&hw_hd->dma2d_hal);
            h264_dma_hal_start_rx_bs_dma(&hw_hd->dma2d_hal);
            h264_hal_set_start(&hw_hd->h264_hal);
        } else if (status & H264_INTR_DB_TMP_READY) {
            h264_hal_clear_intr_status(&hw_hd->h264_hal, H264_INTR_DB_TMP_READY);
            h264_dma_hal_start_tx_dbtmp_dma(&hw_hd->dma2d_hal);
        } else if (status & H264_INTR_REC_READY) {
            h264_hal_clear_intr_status(&hw_hd->h264_hal, H264_INTR_REC_READY);
        } else if (status & H264_INTR_FRAME_DONE) {
            h264_hal_clear_intr_status(&hw_hd->h264_hal, H264_INTR_FRAME_DONE);
            esp_h264_mutex_unlock_from_isr(hw_hd->frame_done, &xHigherPriorityTaskWoken);
        }
    }
    if (xHigherPriorityTaskWoken) {
        esp_h264_port_yield_from_isr();
    }
}

static inline void h264_start_frame_mode_enc(bool is_iframe, h264_hal_context_t *h264_hal, h264_dma_hal_context_t *dma2d_hal)
{
    h264_dma_hal_deinit(dma2d_hal);
    h264_dma_hal_clear_intr(dma2d_hal);
    h264_hal_clear_intr_status(h264_hal, ~0);
    h264_dma_hal_reset_counter_dbtmp(dma2d_hal);
    h264_dma_hal_reset_counter_ref(dma2d_hal);
    if (is_iframe) {
        h264_dma_hal_start_yuv_dma(dma2d_hal);
        h264_dma_hal_start_ref_dma(dma2d_hal);
        h264_dma_hal_start_tx_db12_4_dma(dma2d_hal);
        h264_dma_hal_start_rx_db12_4_dma(dma2d_hal);
        h264_dma_hal_start_rx_dbtmp_dma(dma2d_hal);
        h264_dma_hal_start_rx_bs_dma(dma2d_hal);
        h264_hal_set_start(h264_hal);
    } else {
        h264_dma_hal_start_rx_mvm_dma(dma2d_hal);
        h264_dma_hal_start_tx_db12_4_dma(dma2d_hal);
        h264_hal_dma_move_start(h264_hal);
    }
}

static inline esp_h264_err_t h264_hw_enc_frame_mode_process(esp_h264_hw_handle_t *hw_hd, esp_h264_enc_param_hw_t *param_hd, uint8_t *in_frame, uint8_t *out_frame, uint32_t out_frame_size, uint32_t *out_len)
{
    esp_h264_rc_hd_t rc_hd = NULL;
    int8_t qp_delta = 0;
    uint8_t qp = 0;
    /** Get rate control(RC) handle */
    esp_h264_enc_hw_get_rc_hd(param_hd, &rc_hd);
    if (rc_hd) {
        /** RC enable. */
        uint32_t rate = 0;
        uint32_t pred_mad = 0;
        uint8_t qp_init = 0;
        /** RC start. Get the rate and predicted MAD, QP. They are from software calculation.*/
        esp_h264_rc_start(rc_hd, !hw_hd->frame_num, &rate, &pred_mad, &qp);
        esp_h264_enc_hw_get_qp_init(param_hd, &qp_init);
        /** Set the rate and predicted MAD, QP to hardware encoding*/
        esp_h264_enc_hw_set_qp(param_hd, qp);
        esp_h264_enc_hw_set_rc_rate_pred(param_hd, rate, pred_mad);
        /** Slice header will record the delta QP */
        qp_delta = qp - qp_init;
    }
    uint32_t *slice_start_code = (uint32_t *)out_frame;
    uint32_t slice_nal_len = 0;
    if (!hw_hd->frame_num) {
        /** To ensure that each IDR-frame can be decoded, it is added SPS and PPS before each IDR-frame. */
        uint16_t nal_bit_len;
        esp_h264_enc_hw_get_nal(param_hd, (uint8_t *)slice_start_code, &nal_bit_len);
        slice_start_code = (uint32_t *)((uint32_t)out_frame + (nal_bit_len >> 3));
        slice_nal_len += nal_bit_len;
    }
    /** Configure slice header */
    slice_nal_len += esp_h264_enc_hw_set_slice((uint8_t *)slice_start_code, out_frame_size - (slice_nal_len >> 3), !hw_hd->frame_num, hw_hd->frame_num, qp_delta, true);
    uint8_t *bs = esp_h264_enc_hw_slice_header_align8(out_frame, slice_nal_len, &hw_hd->h264_hal);
    int out_frame_len = (bs - out_frame);
    esp_h264_cache_check_and_writeback(out_frame, out_frame_len);
    /** Configure descriptor */
    esp_h264_enc_hw_cfg_dma_yuv_bs(param_hd, &hw_hd->dma2d_hal, hw_hd->dsc_yuv, in_frame, hw_hd->dsc_bs, bs, out_frame_size - out_frame_len);
    esp_h264_err_t ret = esp_h264_enc_hw_cfg_dma_mvm(param_hd, &hw_hd->dma2d_hal);
    if (ret != ESP_H264_ERR_OK) {
        ESP_H264_LOGE(TAG, "Please configure MV packet.");
        return ESP_H264_ERR_FAIL;
    }
    esp_h264_enc_hw_cfg_dma_db_ref(param_hd, &hw_hd->dma2d_hal);
    esp_h264_enc_hw_cfg_dma_dbtmp(param_hd, &hw_hd->dma2d_hal, hw_hd->dsc_dbtmp, (uint8_t *)ALIGN_UP((uint32_t)hw_hd->db_tmp, 8));
    /** Start HW encoding */
    h264_start_frame_mode_enc(!hw_hd->frame_num, &hw_hd->h264_hal, &hw_hd->dma2d_hal);

    if (esp_h264_mutex_lock(hw_hd->frame_done, (TickType_t)1000) != pdTRUE) {
        *out_len = 0;
        uint32_t bs_intraw = h264_dma_hal_get_bs_intr(&hw_hd->dma2d_hal);
        h264_hal_reset(&hw_hd->h264_hal);
        h264_dma_hal_reset_counter_dbtmp(&hw_hd->dma2d_hal);
        h264_dma_hal_reset_counter_ref(&hw_hd->dma2d_hal);
        h264_dma_hal_reset_counter_db(&hw_hd->dma2d_hal);
        if ((bs_intraw && 0x3) == 1) {
            ESP_H264_LOGE(TAG, "The out buffer is too small. \n");
            return ESP_H264_ERR_MEM;
        }
        ESP_H264_LOGE(TAG, "Timeout");
        return ESP_H264_ERR_TIMEOUT;
    }
    *out_len = h264_hal_get_coded_len(&hw_hd->h264_hal);
    esp_h264_cache_check_and_invalidate(out_frame, out_frame_size);
    /** CAVLA mustn't be continue zeros.
     *  And HW encoding will check output buffer.
     *  Maybe start code will be wrote error data after HW encoding.
     *  So re-write the right start code. */
    *slice_start_code = 0x01000000;
    esp_h264_cache_check_and_writeback((uint8_t *)slice_start_code, 4);
    if (rc_hd) {
        /** Get the encoder bits and MAD, the sum of QP from HW. */
        uint32_t enc_bits = 0, mad = 0, qp_sum = 0;
        h264_hal_get_rc_bits_mad_qpsum(&hw_hd->h264_hal, &enc_bits, &mad, &qp_sum);
        if (!hw_hd->frame_num) {
            enc_bits = *out_len << 3;
            uint8_t mb_width = 0;
            uint8_t mb_height = 0;
            esp_h264_enc_hw_get_mbres(param_hd, &mb_width, &mb_height);
            qp_sum = qp * mb_width * mb_height;
        }
        /** Software calculation the RC parameter.*/
        esp_h264_rc_end(rc_hd, enc_bits, qp_sum, mad);
    }
    *out_len += out_frame_len;
    if (h264_hal_get_bs_bit_overflow(&hw_hd->h264_hal)) {
        return ESP_H264_ERR_OVERFLOW;
    }
    return ESP_H264_ERR_OK;
}

static esp_h264_err_t enc_process(esp_h264_enc_dual_handle_t enc, esp_h264_enc_in_frame_t *in_frame[2], esp_h264_enc_out_frame_t *out_frame[2])
{
    esp_h264_hw_handle_t *hw_hd = __containerof(enc, esp_h264_hw_handle_t, base);
    esp_h264_err_t ret = ESP_H264_ERR_OK;
    hw_hd->frame_num = hw_hd->frame_num % hw_hd->gop;
    out_frame[0]->dts = in_frame[0]->pts;
    out_frame[0]->pts = in_frame[0]->pts;
    out_frame[0]->frame_type = ESP_H264_FRAME_TYPE_P;
    out_frame[1]->dts = in_frame[1]->pts;
    out_frame[1]->pts = in_frame[1]->pts;
    out_frame[1]->frame_type = ESP_H264_FRAME_TYPE_P;
    /** Intra (I-frame) check */
    if (hw_hd->frame_num == 0) {
        /** Currently, the I-frame is instantaneous decoding refresh frame(IDR-frame).
         * In IDR-frame, the GOP can be updating.*/
        out_frame[0]->frame_type = ESP_H264_FRAME_TYPE_IDR;
        out_frame[1]->frame_type = ESP_H264_FRAME_TYPE_IDR;
        uint16_t gop0 = 0;
        uint16_t gop1 = 0;
        esp_h264_enc_get_gop(&hw_hd->param_hd0->base, (uint8_t *)&gop0);
        esp_h264_enc_get_gop(&hw_hd->param_hd1->base, (uint8_t *)&gop1);
        hw_hd->gop = (gop0 + gop1) >> 1;
        esp_h264_enc_set_gop(&hw_hd->param_hd0->base, hw_hd->gop);
        esp_h264_enc_set_gop(&hw_hd->param_hd1->base, hw_hd->gop);
        h264_hal_reset(&hw_hd->h264_hal);
        h264_dma_hal_reset_counter_db(&hw_hd->dma2d_hal);
    }
    esp_h264_cache_check_and_writeback(in_frame[0]->raw_data.buffer, in_frame[0]->raw_data.len);
    esp_h264_cache_check_and_writeback(in_frame[1]->raw_data.buffer, in_frame[1]->raw_data.len);
    /** In multi-thread, the parameter cann't be set in encoding.
     *  `mutex` is for thread safety.
    */
    esp_h264_mutex_t mutex;
    esp_h264_enc_hw_get_mutex(hw_hd->param_hd0, &mutex);
    esp_h264_mutex_lock(mutex, ESP_H264_MAX_DELAY);
    ret |= h264_hw_enc_frame_mode_process(hw_hd, hw_hd->param_hd0, in_frame[0]->raw_data.buffer, out_frame[0]->raw_data.buffer, out_frame[0]->raw_data.len, &out_frame[0]->length);
    esp_h264_mutex_unlock(mutex);
    esp_h264_enc_hw_get_mutex(hw_hd->param_hd1, &mutex);
    esp_h264_mutex_lock(mutex, ESP_H264_MAX_DELAY);
    ret |= h264_hw_enc_frame_mode_process(hw_hd, hw_hd->param_hd1, in_frame[1]->raw_data.buffer, out_frame[1]->raw_data.buffer, out_frame[1]->raw_data.len, &out_frame[1]->length);
    esp_h264_mutex_unlock(mutex);
    hw_hd->frame_num++;
    return ret;
}

static esp_h264_err_t enc_close(esp_h264_enc_dual_handle_t enc)
{
    esp_h264_hw_handle_t *hw_hd = __containerof(enc, esp_h264_hw_handle_t, base);
    /** Free the interrupt */
    if (hw_hd->intr_hd) {
        esp_h264_intr_free(hw_hd->intr_hd);
        hw_hd->intr_hd = NULL;
        if (hw_hd->frame_done) {
            esp_h264_mutex_delete(hw_hd->frame_done);
        }
    }
    /** Clear all interrupts */
    h264_hal_ena_intr(&hw_hd->h264_hal, 0);
    /** Reset H.264 */
    h264_hal_reset(&hw_hd->h264_hal);
    /** Close DMA */
    h264_dma_hal_deinit(&hw_hd->dma2d_hal);
    return ESP_H264_ERR_OK;
}

static esp_h264_err_t enc_open(esp_h264_enc_dual_handle_t enc)
{
    esp_h264_hw_handle_t *hw_hd = __containerof(enc, esp_h264_hw_handle_t, base);
    hw_hd->frame_num = 0;
    /** Enable H.264 interrupt */
    if (esp_h264_intr_alloc(0, h264_frame_isr, (void *)hw_hd, &hw_hd->intr_hd) == ESP_OK) {
        hw_hd->frame_done = esp_h264_mutex_create();
        h264_hal_ena_intr(&hw_hd->h264_hal, H264_INTR_DB_TMP_READY | H264_INTR_REC_READY | H264_INTR_2MB_LINE_DONE | H264_INTR_FRAME_DONE);
        return ESP_H264_ERR_OK;
    }
    /** Close the encoder */
    enc_close(enc);
    return ESP_H264_ERR_FAIL;
}

static esp_h264_err_t enc_del(esp_h264_enc_dual_handle_t enc)
{
    if (enc) {
        esp_h264_hw_handle_t *hw_hd = __containerof(enc, esp_h264_hw_handle_t, base);
        if (hw_hd->db_tmp) {
            esp_h264_free(hw_hd->db_tmp);
        }

        /** Close the encoder */
        enc_close(enc);

        /** Delete the parameter handle */
        esp_h264_enc_hw_del_param(hw_hd->param_hd0);
        esp_h264_enc_hw_del_param(hw_hd->param_hd1);

        if (hw_hd->dsc_yuv) {
            esp_h264_free(hw_hd->dsc_yuv);
        }
        if (hw_hd->dsc_dbtmp[0]) {
            esp_h264_free(hw_hd->dsc_dbtmp[0]);
        }
        if (hw_hd->dsc_dbtmp[1]) {
            esp_h264_free(hw_hd->dsc_dbtmp[1]);
        }
        if (hw_hd->dsc_bs) {
            esp_h264_free(hw_hd->dsc_bs);
        }
        /** Free the encoder */
        esp_h264_free(hw_hd);
    }
    return ESP_H264_ERR_OK;
}

esp_h264_err_t esp_h264_enc_dual_hw_new(const esp_h264_enc_cfg_dual_hw_t *cfg, esp_h264_enc_dual_handle_t *out_enc)
{
    /* Parameter check */
    ESP_H264_RET_ON_FALSE(cfg && out_enc, ESP_H264_ERR_ARG, TAG, "Invalid h264 configure and handle parameter");
    esp_h264_enc_cfg_t enc_cfg[H264_SUP_MAX_CHANNEL] = { cfg->cfg0, cfg->cfg1 };
    for (uint8_t i = 0; i < H264_SUP_MAX_CHANNEL; i++) {
        ESP_H264_RET_ON_FALSE((enc_cfg[i].pic_type == ESP_H264_RAW_FMT_O_UYY_E_VYY), ESP_H264_ERR_ARG, TAG, "Un-supported h264 picture type parameter");
        ESP_H264_RET_ON_FALSE((enc_cfg[i].rc.qp_max >= enc_cfg[i].rc.qp_min) && (enc_cfg[i].rc.qp_max <= ESP_H264_QP_MAX), ESP_H264_ERR_ARG, TAG, "Invalid h264 QP parameter");
        ESP_H264_RET_ON_FALSE((esp_h264_enc_hw_res_check(enc_cfg[i].res.width, enc_cfg[i].res.height) == ESP_H264_ERR_OK), ESP_H264_ERR_ARG, TAG, "Invalid h264 resolution parameter");
        ESP_H264_RET_ON_FALSE((enc_cfg[i].fps > 0) && (enc_cfg[i].gop > 0), ESP_H264_ERR_ARG, TAG, "Invalid h264 FPS and GOP parameter");
    }

    /* Parameter initalization */
    *out_enc = NULL;
    uint8_t mb_width[H264_SUP_MAX_CHANNEL];
    uint8_t mb_height[H264_SUP_MAX_CHANNEL];
    h264_hal_context_cfg_t cfg_h264_hal = { 0 };
    esp_h264_enc_hw_param_cfg_t param_cfg[H264_SUP_MAX_CHANNEL] = { 0 };
    esp_h264_enc_param_hw_t *param_hd[H264_SUP_MAX_CHANNEL] = { NULL };
    esp_h264_err_t ret = ESP_H264_ERR_OK;
    int width = cfg->cfg0.res.width > cfg->cfg1.res.width ? cfg->cfg0.res.width : cfg->cfg1.res.width;
    for (uint8_t i = 0; i < H264_SUP_MAX_CHANNEL; i++) {
        mb_width[i] = (enc_cfg[i].res.width + 15) >> 4;
        mb_height[i] = (enc_cfg[i].res.height + 15) >> 4;
        cfg_h264_hal.cfg_ch[i].score_luma = H264_SCORE_LUMA;
        cfg_h264_hal.cfg_ch[i].score_chroma = H264_SCORE_CHROMA;
        param_cfg[i].width = enc_cfg[i].res.width;
        param_cfg[i].height = enc_cfg[i].res.height;
        param_cfg[i].qp_min = enc_cfg[i].rc.qp_min;
        param_cfg[i].qp_max = enc_cfg[i].rc.qp_max;
        param_cfg[i].bitrate = enc_cfg[i].rc.bitrate;
        param_cfg[i].fps = enc_cfg[i].fps;
    }
    h264_hal_dma_context_cfg_t cfg_h264_dma_hal = { 0 };
    cfg_h264_dma_hal.burst_size = H264_DMA_BURST_SIZE;
    cfg_h264_dma_hal.exter_addr_end[0] = H264_DMA_END_ADDR;
    cfg_h264_dma_hal.exter_addr_end[1] = H264_DMA_END_ADDR;
    cfg_h264_dma_hal.inter_addr_end[0] = H264_DMA_END_ADDR;
    cfg_h264_dma_hal.inter_addr_end[1] = H264_DMA_END_ADDR;
    cfg_h264_dma_hal.out_ch_conf0[0] = H264_DMA_OUT_CONF0_REORDER_EN | H264_DMA_OUT_CONF0_EOF_EN;
    for (uint8_t i = 1; i < H264_DMA_OUT_MAX_CH_NUM; i++) {
        cfg_h264_dma_hal.out_ch_conf0[i] = H264_DMA_OUT_CONF0_EOF_EN;
    }

    /** Create encoder handle */
    uint32_t actual_size;
    esp_h264_hw_handle_t *hw_hd = (esp_h264_hw_handle_t *)esp_h264_calloc_prefer(1, sizeof(esp_h264_hw_handle_t), &actual_size, ESP_H264_MEM_SPIRAM, ESP_H264_MEM_INTERNAL);
    ESP_H264_RET_ON_FALSE(hw_hd != NULL, ESP_H264_ERR_MEM, TAG, "No memory for encoder handle");

    /** H.264 HAL initalization*/
    cfg_h264_hal.dual_stream_en = true;
    cfg_h264_hal.gop = (cfg->cfg0.gop + cfg->cfg1.gop) >> 1;
    cfg_h264_hal.gop_mode_en = false;
    h264_hal_init(&hw_hd->h264_hal, &cfg_h264_hal);
    h264_hal_reset(&hw_hd->h264_hal);
    /** H.264 DMA HAL initalization*/
    h264_dma_hal_init(&hw_hd->dma2d_hal, &cfg_h264_dma_hal);

    /** Get device */
    param_cfg[0].device = h264_hal_get_param_dev0(&hw_hd->h264_hal);
    param_cfg[1].device = h264_hal_get_param_dev1(&hw_hd->h264_hal);

    for (uint8_t i = 0; i < H264_SUP_MAX_CHANNEL; i++) {
        h264_hal_set_mbres(param_cfg[i].device, mb_width[i], mb_height[i]);
        /** Create a new parameter handle */
        ret |= esp_h264_enc_hw_new_param(&param_cfg[i], &param_hd[i]);
        ESP_H264_GOTO_ON_FALSE(ret == ESP_H264_ERR_OK, ret, __exit__, TAG, "No memory for param handle");
        /** Configure parameter*/
        esp_h264_enc_set_gop(&param_hd[i]->base, enc_cfg[i].gop);
    }
    /** Allocated de-blocking filter temporary parameter memory*/
    hw_hd->db_tmp = (uint8_t *)esp_h264_aligned_calloc(16, 1, esp_h264_enc_hw_max_db_tmp_buffer_size(width), &actual_size, ESP_H264_MEM_INTERNAL);
    ESP_H264_GOTO_ON_FALSE(hw_hd->db_tmp != NULL, ESP_H264_ERR_MEM, __exit__, TAG, "No memory for db_tmp");
    /** Allocated descriptor memory*/
    for (size_t i = 0; i < 2; i++) {
        hw_hd->dsc_dbtmp[i] = esp_h264_aligned_calloc(16, 1, sizeof(h264_dma_desc_t), &actual_size, ESP_H264_MEM_INTERNAL);
        ESP_H264_GOTO_ON_FALSE(hw_hd->dsc_dbtmp[i] != NULL, ESP_H264_ERR_MEM, __exit__, TAG, "No memory for DB descriptor");
    }
    hw_hd->dsc_yuv = esp_h264_aligned_calloc(16, 1, sizeof(h264_dma_desc_t), &actual_size, ESP_H264_MEM_INTERNAL);
    ESP_H264_GOTO_ON_FALSE(hw_hd->dsc_yuv != NULL, ESP_H264_ERR_MEM, __exit__, TAG, "No memory for YUV descriptor");
    hw_hd->dsc_bs = esp_h264_aligned_calloc(16, 1, sizeof(h264_dma_desc_t), &actual_size, ESP_H264_MEM_INTERNAL);
    ESP_H264_GOTO_ON_FALSE(hw_hd->dsc_bs != NULL, ESP_H264_ERR_MEM, __exit__, TAG, "No memory for BS descriptor");

    /** Encoder handle configure */
    hw_hd->gop = cfg_h264_hal.gop;
    hw_hd->param_hd0 = param_hd[0];
    hw_hd->param_hd1 = param_hd[1];
    hw_hd->base.open = enc_open;
    hw_hd->base.process = enc_process;
    hw_hd->base.close = enc_close;
    hw_hd->base.del = enc_del;
    *out_enc = &hw_hd->base;
    return ret;
__exit__:
    /** Delete the encoder handle */
    enc_del(&hw_hd->base);
    return ret;
}

esp_h264_err_t esp_h264_enc_dual_hw_get_param_hd0(esp_h264_enc_dual_handle_t enc, esp_h264_enc_param_hw_handle_t *out_param)
{
    if (enc && out_param) {
        esp_h264_hw_handle_t *hw_hd = __containerof(enc, esp_h264_hw_handle_t, base);
        *out_param = hw_hd->param_hd0;
        return ESP_H264_ERR_OK;
    }
    return ESP_H264_ERR_ARG;
}

esp_h264_err_t esp_h264_enc_dual_hw_get_param_hd1(esp_h264_enc_dual_handle_t enc, esp_h264_enc_param_hw_handle_t *out_param)
{
    if (enc) {
        esp_h264_hw_handle_t *hw_hd = __containerof(enc, esp_h264_hw_handle_t, base);
        *out_param = hw_hd->param_hd1;
        return ESP_H264_ERR_OK;
    }
    return ESP_H264_ERR_ARG;
}