/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Packed YUV420 (O_UYY_E_VYY) size in bytes
 */
static inline uint32_t pattern_ouev_frame_len(uint16_t width, uint16_t height)
{
    return ((uint32_t)width * height * 3U) / 2U;
}

/**
 * @brief  Fill an O_UYY_E_VYY color-bar frame
 *
 * @note  Width must be even. `phase` shifts bars horizontally to create motion.
 *
 * @param[out] buf     Caller-owned frame buffer, at least pattern_ouev_frame_len() bytes
 * @param[in]  width   Frame width in pixels
 * @param[in]  height  Frame height in pixels
 * @param[in]  phase   Horizontal shift in pixels
 * @param[in]  palette Palette index 0 or 1 (two distinct bar palettes)
 */
void pattern_fill_ouev_colorbar(uint8_t *buf, uint16_t width, uint16_t height, uint16_t phase, int palette);

/**
 * @brief  Write-back CPU cache so PPA / H.264 DMA can read `buf`
 */
esp_err_t pattern_cache_writeback(void *buf, uint32_t len);

/**
 * @brief  Invalidate CPU cache after PPA DMA wrote `buf`
 */
esp_err_t pattern_cache_invalidate(void *buf, uint32_t len);

#ifdef __cplusplus
}
#endif
