/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_h264_enc_param.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  H.264 region of interest (ROI) mode
 */
typedef enum {
    ESP_H264_ROI_MODE_DISABLE  = -1,  /*<! The ROI mode is disabled */
    ESP_H264_ROI_MODE_FIX_QP   =  0,  /*<! The QP of the ROI is fixed */
    ESP_H264_ROI_MODE_DELTA_QP =  1,  /*<! Delta quantization parameter (QP). The ROI region QP is the sum of delta QP and slice QP */
    ESP_H264_ROI_MODE_INVALID  =  2,  /*<! Invalid value */
} esp_h264_roi_mode_t;

/**
 * @brief  Region of interest (ROI) configuration
 */
typedef struct {
    esp_h264_roi_mode_t roi_mode;          /*<! Region of interest mode */
    int8_t              none_roi_delta_qp; /*<! Delta quantization parameter (QP) of the non-ROI region; range is [-51, 51] */
} esp_h264_enc_roi_cfg_t;

/**
 * @brief  Region of interest (ROI) region.
 *         ROI coding lowers QP in the region of interest to improve local quality, and raises QP
 *         outside that region to save bits, while keeping overall quality acceptable.
 *         This can reduce bandwidth and storage, or improve quality for the same budget.
 *
 * @note  `mb_width`   Width of the picture in macroblocks
 *        `mb_height`  Height of the picture in macroblocks
 *
 *         |<---            mb_width                 ---->|    _
 *         |----------------------------------------------|    ^
 *         |            |<- len_x ->   |                  |
 *         |     (x, y) |______________|_                 |    |
 *         |            |ROI region    |                  |    mb_height
 *         |            |qp/qp+slice qp|len_y             |
 *         |            |______________|_                 |    |
 *         |                                              |
 *         |                                              |
 *         |       Non-ROI region                         |
 *         |       none_roi_delta_qp + slice QP           |    v
 *         |----------------------------------------------|    _
 *
 */
typedef struct {
    uint8_t x;        /*<! Start position in the horizontal direction, in units of macroblock size. Maximum value is `mb_width` */
    uint8_t y;        /*<! Start position in the vertical direction, in units of macroblock size. Maximum value is `mb_height` */
    uint8_t len_x;    /*<! ROI length in the horizontal direction, in units of macroblock size. Maximum value is `mb_width`.
                           The sum of `x` and `len_x` cannot be greater than `mb_width`. */
    uint8_t len_y;    /*<! ROI length in the vertical direction, in units of macroblock size. Maximum value is `mb_height`.
                           The sum of `y` and `len_y` cannot be greater than `mb_height`. */
    int8_t  qp;       /*<! Fixed quantization parameter (QP) or delta QP
                           |-----------------------------|-------------------|----------------------------------|------------|
                           |  roi_mode                   |  ROI QP           |  NONE_ROI_QP                     |  `qp` range
                           |-----------------------------|-------------------|----------------------------------|------------|
                           |  ESP_H264_ROI_MODE_DISABLE  |  disabled         |  disabled                        |  disabled  |
                           |-----------------------------|-------------------|----------------------------------|------------|
                           |  ESP_H264_ROI_MODE_DELTA_QP |  `qp` + slice QP  |  none_roi_delta_qp + slice QP    |  [-51, 51] |
                           |-----------------------------|-------------------|----------------------------------|------------|
                           |  ESP_H264_ROI_MODE_FIX_QP   |  `qp`             |  none_roi_delta_qp + slice QP    |  [0, 51]   |
                           |-----------------------------|-------------------|----------------------------------|------------|
                           NOTE: slice QP is the QP of the current slice. */
    uint8_t reg_idx;  /*<! Index of the ROI region. It must be less than 8 */
} esp_h264_enc_roi_reg_t;

/**
 * @brief  H.264 motion vector (MV) result of a predicted frame (P-frame), for 16x16 macroblocks
 */
typedef enum {
    ESP_H264_MVM_MODE_DISABLE = -1,  /*<! The MV mode is disabled */
    ESP_H264_MVM_MODE_P16X16  =  0,  /*<! Collect 16x16 macroblock MV data */
    ESP_H264_MVM_MODE_MINV,          /*<! If a sub-macroblock exists, use the minimum of the sub-macroblock MV data.
                                          Otherwise use the 16x16 macroblock MV data. */
    ESP_H264_MVM_MODE_MAXV,          /*<! If a sub-macroblock exists, use the maximum of the sub-macroblock MV data.
                                          Otherwise use the 16x16 macroblock MV data. */
    ESP_H264_MVM_MODE_INVALID,       /*<! Invalid value */
} esp_h264_enc_mvm_mode_t;

/**
 * @brief  H.264 motion vector (MV) result format
 */
typedef enum {
    ESP_H264_MVM_FMT_ALL = 0,  /*<! Output all MV data except zero */
    ESP_H264_MVM_FMT_PART,     /*<! Output horizontal or vertical MV data with absolute value greater than or equal to 4 */
    ESP_H264_MVM_FMT_INVALID,  /*<! Invalid value */
} esp_h264_enc_mvm_fmt_t;

/**
 * @brief  Motion vector (MV) configuration
 */
typedef struct {
    esp_h264_enc_mvm_mode_t mv_mode;  /*<! Motion vector(MV) mode */
    esp_h264_enc_mvm_fmt_t  mv_fmt;   /*<! Motion vector(MV) format */
} esp_h264_enc_mv_cfg_t;

/**
 * @brief  Motion vector (MV) data for one macroblock
 *         The position coordinates of the reference macroblock with respect to the current macroblock
 */
typedef union {
    struct {
        int32_t  mv_y: 7;  /*<! Macroblock MV in vertical direction  */
        int32_t  mv_x: 8;  /*<! Macroblock MV in horizontal direction */
        uint32_t mb_y: 7;  /*<! Macroblock position in vertical direction  */
        uint32_t mb_x: 7;  /*<! Macroblock position in horizontal direction  */
        uint32_t rsv : 3;  /*<! Reserved */
    };
    uint32_t data;  /*<! MV data */
} esp_h264_enc_mv_data_t;

/**
 * @brief  Motion vector (MV) packet
 */
typedef struct {
    esp_h264_enc_mv_data_t *data;  /*<! Buffer address used to store MV data */
    uint32_t                len;   /*<! Length of the MV data buffer in bytes.
                                        The maximum is `mb_width * mb_height * sizeof(esp_h264_enc_mv_data_t)` */
} esp_h264_enc_mvm_pkt_t;

/**
 * @brief Handle for accessing hardware-specific H.264 encoder parameters
 */
typedef struct esp_h264_enc_hw_param_if *esp_h264_enc_param_hw_handle_t;

/**
 * @brief  It is a pointer to the hardware H.264 encoding parameters structure
 */
typedef struct esp_h264_enc_hw_param_if {
    esp_h264_enc_param_t base;                                                                               /*<! Base parameter set handle */
    esp_h264_err_t (*cfg_roi)(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_roi_cfg_t cfg);            /*<! Configure the region of interest (ROI) */
    esp_h264_err_t (*get_roi_cfg_info)(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_roi_cfg_t *cfg);  /*<! Get the ROI configuration parameter */
    esp_h264_err_t (*set_roi_reg)(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_roi_reg_t roi_reg);    /*<! Set region of interest (ROI) region */
    esp_h264_err_t (*get_roi_reg)(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_roi_reg_t *roi_reg);   /*<! Get region of interest (ROI) region */
    esp_h264_err_t (*cfg_mv)(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_mv_cfg_t cfg);              /*<! Configure motion vector(MV) */
    esp_h264_err_t (*get_mv_cfg_info)(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_mv_cfg_t *cfg);    /*<! Get the MV configuration parameter */
    esp_h264_err_t (*set_mv_pkt)(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_mvm_pkt_t mv_pkt);      /*<! Set motion vector(MV) packet */
    esp_h264_err_t (*get_mv_data_len)(esp_h264_enc_param_hw_handle_t handle, uint32_t *length);              /*<! Get motion vector(MV) buffer actual length */
} esp_h264_enc_param_hw_t;

/**
 * @brief  This function allows you to specify the region of interest (ROI) for hardware-based H.264 encoding
 *         The ROI refers to a certain area within the frame that is given priority during encoding, ensuring higher/lower quality for that specific region.
 *
 * @param[in]  handle  It is a pointer to the hardware H.264 encoding parameters structure
 * @param[in]  cfg     An `esp_h264_enc_roi_cfg_t` structure that specifies the ROI configuration
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_ARG          Invalid arguments passed
 *       - ESP_H264_ERR_UNSUPPORTED  ROI feature is not supported by the hardware encoder
 */
esp_h264_err_t esp_h264_enc_hw_cfg_roi(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_roi_cfg_t cfg);

/**
 * @brief  This function is used to retrieve information about ROI configuration for H.264 hardware encoder
 *
 * @param[in]   handle   It is a pointer to the hardware H.264 encoding parameters structure
 * @param[out]  out_cfg  A pointer to an `esp_h264_enc_roi_cfg_t` structure where the ROI configuration information will be stored
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_ARG          Invalid arguments passed
 *       - ESP_H264_ERR_UNSUPPORTED  ROI feature is not supported by the hardware encoder
 */
esp_h264_err_t esp_h264_enc_hw_get_roi_cfg_info(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_roi_cfg_t *out_cfg);

/**
 * @brief  The function sets the ROI region based on the provided parameters, allowing the encoder to prioritize encoding quality on specific regions of the input video frame
 *         Please configure ROI first using `esp_h264_enc_hw_cfg_roi`
 *
 * @param[in]  handle   It is a pointer to the hardware H.264 encoding parameters structure
 * @param[in]  roi_reg  It is the region of interest configuration structure
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_ARG          Invalid arguments passed
 *       - ESP_H264_ERR_UNSUPPORTED  ROI feature is not supported by the hardware encoder.
 */
esp_h264_err_t esp_h264_enc_hw_set_roi_region(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_roi_reg_t roi_reg);

/**
 * @brief  Get region of interest (ROI) region
 *
 * @param[in]   handle       It is a pointer to the hardware H.264 encoding parameters structure
 * @param[out]  out_roi_reg  Pointer to the ROI region structure that will store the retrieved ROI
 *                           If `roi_mode` is `ESP_H264_ROI_MODE_DISABLE`, `out_roi_reg` will be memset to zero
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_ARG          Invalid arguments passed
 *       - ESP_H264_ERR_UNSUPPORTED  ROI feature is not supported by the hardware encoder
 */
esp_h264_err_t esp_h264_enc_hw_get_roi_region(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_roi_reg_t *out_roi_reg);

/**
 * @brief  Configure motion vector(MV)
 *
 * @param[in]  handle  It is a pointer to the hardware H.264 encoding parameters structure
 * @param[in]  cfg     An `esp_h264_enc_mv_cfg_t` structure that specifies the MV configuration
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_ARG          Invalid arguments passed
 *       - ESP_H264_ERR_UNSUPPORTED  MV feature is not supported by the hardware encoder
 */
esp_h264_err_t esp_h264_enc_hw_cfg_mv(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_mv_cfg_t cfg);

/**
 * @brief  Get motion vector(MV) mode
 *
 * @param[in]   handle   It is a pointer to the hardware H.264 encoding parameters structure
 * @param[out]  out_cfg  A pointer to an `esp_h264_enc_mv_cfg_t` structure where the MV configuration information will be stored
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_ARG          Invalid arguments passed
 *       - ESP_H264_ERR_UNSUPPORTED  MV feature is not supported by the hardware encoder
 */
esp_h264_err_t esp_h264_enc_hw_get_mv_cfg_info(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_mv_cfg_t *out_cfg);

/**
 * @brief  Set the MV packet for the H.264 hardware encoder
 *
 * @note  Configure MV first with `esp_h264_enc_hw_cfg_mv`; otherwise this function returns `ESP_H264_ERR_ARG`.
 *        After encoding, get the actual MV data length with `esp_h264_enc_hw_get_mv_data_len`.
 *
 * @param[in]  handle  Pointer to the hardware H.264 encoding parameters structure
 * @param[in]  mv_pkt  The MV packet
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_ARG          Invalid arguments passed
 *       - ESP_H264_ERR_UNSUPPORTED  MV feature is not supported by the hardware encoder
 */
esp_h264_err_t esp_h264_enc_hw_set_mv_pkt(esp_h264_enc_param_hw_handle_t handle, esp_h264_enc_mvm_pkt_t mv_pkt);

/**
 * @brief  Get the motion vector (MV) data length for the hardware H.264 encoder
 *
 * @param[in]   handle      Pointer to the hardware H.264 encoding parameters structure
 * @param[out]  out_length  Pointer to a `uint32_t` that receives the number of MV entries.
 *                          MV buffer size in bytes = (*out_length) * sizeof(esp_h264_enc_mv_data_t)
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_ARG          Invalid arguments passed
 *       - ESP_H264_ERR_UNSUPPORTED  MV feature is not supported by the hardware encoder
 */
esp_h264_err_t esp_h264_enc_hw_get_mv_data_len(esp_h264_enc_param_hw_handle_t handle, uint32_t *out_length);

#ifdef __cplusplus
}
#endif
