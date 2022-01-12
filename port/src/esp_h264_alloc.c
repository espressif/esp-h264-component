/*
 * SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_h264_alloc.h"
#include "esp_h264_cache.h"
#include "esp_private/esp_cache_private.h"

void* esp_h264_aligned_calloc(uint32_t alignment, uint32_t n, uint32_t size, uint32_t* actual_size, uint32_t caps)
{
    void* out_ptr = NULL;
    uint32_t out_alignment = 0;
    uint32_t flag = ESP_CACHE_MALLOC_FLAG_PSRAM;
    if (caps == ESP_H264_MEM_INTERNAL) {
        flag = 2;
    }
    esp_cache_get_alignment(flag, (size_t *)&out_alignment);
    if (alignment <= out_alignment) {
        esp_cache_aligned_calloc((size_t)n, (size_t)size, flag, &out_ptr, (size_t*)actual_size);
    } else {
        *actual_size = ALIGN_UP(n * size, out_alignment);
        out_ptr = heap_caps_aligned_calloc((size_t)alignment, 1, (size_t)*actual_size, caps);
    }
    if (out_ptr) {
        esp_h264_cache_check_and_writeback(out_ptr, *actual_size);
        esp_h264_cache_check_and_invalidate(out_ptr, *actual_size);
    }
    return out_ptr;
}

void* esp_h264_calloc_prefer(uint32_t n, uint32_t size, uint32_t* actual_size, uint32_t caps1, uint32_t caps2)
{
    void* out_ptr = NULL;
    uint32_t flag1 = ESP_CACHE_MALLOC_FLAG_PSRAM;
    uint32_t flag2 = ESP_CACHE_MALLOC_FLAG_PSRAM;
    if (caps1 == ESP_H264_MEM_INTERNAL) {
        flag1 = 2;
    }
    if (caps2 == ESP_H264_MEM_INTERNAL) {
        flag2 = 2;
    }
    esp_cache_aligned_calloc_prefer((size_t)n, (size_t)size, &out_ptr, (size_t*)actual_size, 2, flag1, flag2);
    if (out_ptr) {
        esp_h264_cache_check_and_writeback(out_ptr, *actual_size);
        esp_h264_cache_check_and_invalidate(out_ptr, *actual_size);
    }
    return out_ptr;
}