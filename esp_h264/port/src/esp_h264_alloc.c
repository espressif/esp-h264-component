/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_h264_alloc.h"
#include "esp_h264_cache.h"
#include "esp_private/esp_cache_private.h"

void *esp_h264_aligned_malloc(uint32_t alignment, uint32_t n, uint32_t size, uint32_t *actual_size, uint32_t caps)
{
    void *out_ptr = NULL;
    size_t line = 0;
    (void)esp_cache_get_alignment(caps, &line);
    if (line > alignment) {
        alignment = (uint32_t)line;
    }
    *actual_size = ALIGN_UP(n * size, alignment);
    caps |= MALLOC_CAP_CACHE_ALIGNED;
    out_ptr = heap_caps_aligned_alloc((size_t)alignment, (size_t) * actual_size, caps);
    if (out_ptr) {
        esp_h264_cache_check_and_writeback(out_ptr, *actual_size);
    }
    return out_ptr;
}

void *esp_h264_aligned_calloc(uint32_t alignment, uint32_t n, uint32_t size, uint32_t *actual_size, uint32_t caps)
{
    void *out_ptr = NULL;
    size_t line = 0;
    (void)esp_cache_get_alignment(caps, &line);
    if (line > alignment) {
        alignment = (uint32_t)line;
    }
    *actual_size = ALIGN_UP(n * size, alignment);
    caps |= MALLOC_CAP_CACHE_ALIGNED;
    out_ptr = heap_caps_aligned_calloc((size_t)alignment, 1, (size_t) * actual_size, caps);
    if (out_ptr) {
        esp_h264_cache_check_and_writeback(out_ptr, *actual_size);
    }
    return out_ptr;
}

void *esp_h264_malloc_prefer(uint32_t n, uint32_t size, uint32_t *actual_size, uint32_t caps1, uint32_t caps2)
{
    void *out_ptr = esp_h264_aligned_malloc(4, n, size, actual_size, caps1);
    if (out_ptr) {
        return out_ptr;
    }
    return esp_h264_aligned_malloc(4, n, size, actual_size, caps2);
}

void *esp_h264_calloc_prefer(uint32_t n, uint32_t size, uint32_t *actual_size, uint32_t caps1, uint32_t caps2)
{
    void *out_ptr = esp_h264_aligned_calloc(4, n, size, actual_size, caps1);
    if (out_ptr) {
        return out_ptr;
    }
    return esp_h264_aligned_calloc(4, n, size, actual_size, caps2);
}
