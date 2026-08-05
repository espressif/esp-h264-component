/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_h264_types.h"
#include "esp_h264_enc_param.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  H.264 single stream encoder handle
 */
typedef struct esp_h264_enc_if *esp_h264_enc_handle_t;

/**
 * @brief  The H.264 single stream encoder interface
 */
typedef struct esp_h264_enc_if {
    esp_h264_err_t (*open)(esp_h264_enc_handle_t enc);                                       /*<! The open function */
    esp_h264_err_t (*process)(esp_h264_enc_handle_t enc, esp_h264_enc_in_frame_t *in_frame,
                              esp_h264_enc_out_frame_t *out_frame);                          /*<! The process function */
    esp_h264_err_t (*close)(esp_h264_enc_handle_t enc);                                      /*<! The close function */
    esp_h264_err_t (*del)(esp_h264_enc_handle_t enc);                                        /*<! The delete function */
} esp_h264_enc_t;

/**
 * @brief  This function opens an H.264 encoder in single stream
 *
 * @param[in]  enc  A pointer to the H.264 encoder instance
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_FAIL         Failed
 *       - ESP_H264_ERR_ARG          Invalid arguments passed
 *       - ESP_H264_ERR_UNSUPPORTED  Open feature is not supported by the encoder
 */
esp_h264_err_t esp_h264_enc_open(esp_h264_enc_handle_t enc);

/**
 * @brief  Encode one H.264 video frame in a single stream.
 *         For an IDR frame, the encoder automatically prepends SPS and PPS NALUs.
 *
 * @note  Returns ESP_H264_ERR_TIMEOUT if `out_frame.raw_data.len` is smaller than the actual
 *        encoded data length when using the hardware encoder.
 *        If the image width or height is not a multiple of 16, align them as follows:
 *        `width = ((width +15) >> 4 << 4);`
 *        `height = ((height+15) >> 4 << 4);`
 *        `in_frame.raw_data.len = ( width * height + (width * height >> 1));`
 *        `in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_frame.raw_data.len, &in_frame.raw_data.len, MALLOC_CAP_DEFAULT);`
 *        Allocate `out_frame.raw_data.buffer` with at least `in_frame.raw_data.len` bytes so the
 *        encoded bitstream does not exceed the output buffer. If the encoded size is larger than
 *        the output buffer, the function returns ESP_H264_ERR_MEM.
 *
 * @param[in]      enc        Pointer to the H.264 encoder instance
 * @param[in]      in_frame   Pointer to the unencoded input frame
 * @param[in/out]  out_frame  Pointer to the encoded output frame
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_ARG          Invalid arguments passed
 *       - ESP_H264_ERR_MEM          Insufficient memory
 *       - ESP_H264_ERR_FAIL         Failed
 *       - ESP_H264_ERR_TIMEOUT      Timeout
 *       - ESP_H264_ERR_OVERFLOW     Encoded image size is greater than `out_frame.raw_data.len`
 *       - ESP_H264_ERR_UNSUPPORTED  Process feature is not supported by the encoder
 */
esp_h264_err_t esp_h264_enc_process(esp_h264_enc_handle_t enc, esp_h264_enc_in_frame_t *in_frame, esp_h264_enc_out_frame_t *out_frame);

/**
 * @brief  This function closes the H.264 encoder instance specified by `enc`
 *
 * @param[in]  enc  A pointer to the H.264 encoder instance
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_ARG          Invalid arguments passed
 *       - ESP_H264_ERR_UNSUPPORTED  Close feature is not supported by the encoder
 */
esp_h264_err_t esp_h264_enc_close(esp_h264_enc_handle_t enc);

/**
 * @brief  This function is used to delete an H.264 encoder
 *
 * @param[in]  enc  A pointer to the H.264 encoder instance
 *
 * @return
 *       - ESP_H264_ERR_OK           Succeeded
 *       - ESP_H264_ERR_ARG          Invalid arguments passed
 *       - ESP_H264_ERR_UNSUPPORTED  Delete feature is not supported by the encoder
 */
esp_h264_err_t esp_h264_enc_del(esp_h264_enc_handle_t enc);

#ifdef __cplusplus
}
#endif
