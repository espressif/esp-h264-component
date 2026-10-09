/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "esp_private/esp_cache_private.h"
#include "esp_h264_alloc.h"
#include "esp_h264_cache.h"

void esp_h264_cache_check_and_writeback(uint8_t *addr, uint32_t length)
{
    if ((addr == NULL) || (length == 0)) {
        return;
    }
    (void)esp_cache_msync(addr, length, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

void esp_h264_cache_check_and_writeback_invalidate(uint8_t *addr, uint32_t length)
{
    if ((addr == NULL) || (length == 0)) {
        return;
    }
    /* C2M allows UNALIGNED. INVALIDATE drops the lines after writeback so a later
     * DMA write into the same line cannot be hidden (or later stomped) by L1. */
    (void)esp_cache_msync(addr, length,
                          ESP_CACHE_MSYNC_FLAG_DIR_C2M |
                          ESP_CACHE_MSYNC_FLAG_INVALIDATE |
                          ESP_CACHE_MSYNC_FLAG_UNALIGNED);
}

void esp_h264_cache_check_and_invalidate(uint8_t *addr, uint32_t length)
{
    if ((addr == NULL) || (length == 0)) {
        return;
    }
    /* M2C forbids UNALIGNED. Expand to the line of *this* address: L1 (64 B)
     * for INTERNAL, L2 (128 B) for PSRAM. Do not use the PSRAM line on an
     * INTERNAL pointer — that over-expand discards neighbor heap metadata. */
    uint32_t caps = esp_ptr_external_ram(addr) ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
    size_t line = 0;
    (void)esp_cache_get_alignment(caps, &line);
    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + (uintptr_t)length;
    uintptr_t aligned_start = ALIGN_DOWN(start, line);
    uintptr_t aligned_end = ALIGN_UP(end, line);
    (void)esp_cache_msync((void *)aligned_start, (size_t)(aligned_end - aligned_start),
                          ESP_CACHE_MSYNC_FLAG_DIR_M2C);
}
