/*
 * SPDX-FileCopyrightText: 2024-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

/**
 * @brief  Write CPU-dirty cache lines covering [addr, addr+length) back to memory
 *
 * @note  Uses C2M + UNALIGNED. The range itself may be any size; hardware writeback
 *        is still whole cache lines. Does not drop the lines from cache.
 *
 * @param[in]  addr    Start address
 * @param[in]  length  Length in bytes
 */
void esp_h264_cache_check_and_writeback(uint8_t *addr, uint32_t length);

/**
 * @brief  Write back [addr, addr+length) and then drop those cache lines
 *
 * @note  Use before a DMA engine writes into a region that may share a cache line
 *        with CPU-written prefix bytes (slice header vs. bitstream body).
 *        C2M + INVALIDATE + UNALIGNED is legal; M2C + UNALIGNED is not.
 *
 * @param[in]  addr    Start address
 * @param[in]  length  Length in bytes
 */
void esp_h264_cache_check_and_writeback_invalidate(uint8_t *addr, uint32_t length);

/**
 * @brief  Invalidate cache lines covering [addr, addr+length) so CPU reads see memory
 *
 * @note  M2C does not allow UNALIGNED. The helper expands to the line of this
 *        address (INTERNAL L1 64 B, PSRAM L2 128 B on ESP32-P4).
 *
 * @param[in]  addr    Start address
 * @param[in]  length  Length in bytes
 */
void esp_h264_cache_check_and_invalidate(uint8_t *addr, uint32_t length);
