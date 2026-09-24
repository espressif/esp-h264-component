/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>

#include "pattern_input.h"
#include "esp_cache.h"

/* SMPTE-style YUV (BT.601 limited) bars: Y, U, V */
static const uint8_t s_palette0[8][3] = {
    {180, 128, 128}, /* white-ish */
    {162,  44, 142}, /* yellow */
    {131, 156,  44}, /* cyan */
    {112,  72,  58}, /* green */
    { 84, 184, 198}, /* magenta */
    { 65, 100, 212}, /* red */
    { 35, 212, 114}, /* blue */
    { 16, 128, 128}, /* black */
};

static const uint8_t s_palette1[8][3] = {
    { 16, 128, 128}, /* black */
    { 35, 212, 114}, /* blue */
    { 65, 100, 212}, /* red */
    { 84, 184, 198}, /* magenta */
    {112,  72,  58}, /* green */
    {131, 156,  44}, /* cyan */
    {162,  44, 142}, /* yellow */
    {180, 128, 128}, /* white-ish */
};

void pattern_fill_ouev_colorbar(uint8_t *buf, uint16_t width, uint16_t height, uint16_t phase, int palette)
{
    const uint8_t (*yuv)[3] = (palette == 0) ? s_palette0 : s_palette1;
    const uint16_t bar_w = (width / 8) ? (width / 8) : 1;
    const uint16_t line_bytes = (uint16_t)(width + (width >> 1));

    for (uint16_t y = 0; y < height; y++) {
        uint8_t *line = buf + (uint32_t)y * line_bytes;
        const bool even_line = ((y & 1U) == 0U);
        for (uint16_t x = 0; x < width; x += 2) {
            const uint16_t bar = (uint16_t)(((x + phase) / bar_w) & 7U);
            const uint8_t luma = yuv[bar][0];
            const uint8_t chroma = even_line ? yuv[bar][1] : yuv[bar][2];
            line[0] = chroma;
            line[1] = luma;
            line[2] = luma;
            line += 3;
        }
    }
}

esp_err_t pattern_cache_writeback(void *buf, uint32_t len)
{
    if (!buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_cache_msync(buf, len, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
}

esp_err_t pattern_cache_invalidate(void *buf, uint32_t len)
{
    if (!buf || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    return esp_cache_msync(buf, len, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}
