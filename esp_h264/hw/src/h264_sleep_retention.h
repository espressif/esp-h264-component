/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include "esp_h264_types.h"
#include "esp_h264_check.h"

#ifdef __cplusplus
extern "C" {
#endif

#define H264_USE_RETENTION_LINK  (CONFIG_PM_ENABLE && CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP)

/**
 * @brief  Ensure H.264 register retention (REGDMA) is initialized and the runtime link is allocated
 *
 * Registers the sleep retention module and allocates the retention link when
 * `CONFIG_PM_ENABLE` and `CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP` are enabled.
 * Otherwise the call is a no-op and returns success.
 *
 * Reference-counted: overlapping single/dual encoder instances share one link.
 * Pair each successful acquire with @ref h264_sleep_retention_free.
 *
 * Typical use: call from @ref esp_h264_enc_hw_new (or dual) before allocating the handle.
 *
 * @return
 *       - ESP_H264_ERR_OK     Succeeded or retention not compiled in (disabled Kconfig)
 *       - ESP_H264_ERR_FAIL   Retention module init or link allocation failed
 */
esp_h264_err_t h264_sleep_retention_acquire(void);

/**
 * @brief  Release one acquire of the H.264 sleep retention link
 *
 * Decrements the acquire refcount. The last free calls @ref h264_sleep_retention_deinit.
 * Safe when retention is disabled or the refcount is already zero (no-op).
 */
void h264_sleep_retention_free(void);

/**
 * @brief  Deinitialize the H.264 sleep retention module
 *
 * Frees any allocated link and calls `sleep_retention_module_deinit` for the H.264 module
 * when retention was active. The last @ref h264_sleep_retention_free invokes this.
 */
void h264_sleep_retention_deinit(void);

/**
 * @brief  Write the TOP-PD canary into a register that REGDMA does not restore
 *
 * Uses `debug_dma_sel` (reset default 0). It is a debug DMA-select: each of the
 * low 8 bits marks one H.264 DMA for observation. The encode path never writes
 * it, so a non-zero magic does not change production DMA. REGDMA Head B2 stops
 * before this register, so after TOP PD it is not restored and reads back 0.
 * Light sleep without TOP PD leaves the magic in place. `sys_rst_pulse` also
 * clears it to 0, so re-arm after every encode.
 *
 * @note  Not ISR-safe. Requires the H.264 clock to be on.
 */
void h264_sleep_retention_arm_canary(void);

/**
 * @brief  Return whether H.264 MMIO looks reset (TOP PD or HW reset)
 *
 * True when `debug_dma_sel` is no longer the canary (after TOP PD or reset it
 * is 0). REGDMA may have restored other config, but reconstructed reference
 * SRAM is gone, so the next encode must be IDR.
 *
 * @note  Not ISR-safe. Requires the H.264 clock to be on.
 *
 * @return
 *       - true   Canary missing; treat the next encode as force-IDR
 *       - false  Canary still present
 */
bool h264_sleep_retention_hw_lost(void);

#ifdef __cplusplus
}
#endif
