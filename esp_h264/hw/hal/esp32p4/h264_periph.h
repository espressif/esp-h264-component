/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/*
 * ESP-IDF v6.1: H.264 REGDMA / retention symbols are not in soc headers yet.
 * Link type 0x2a follows JPEG (0x28) and LCDCAM (0x29). Retention module 40 is
 * unused in enum (between LCDCAM=39 and CLOCK_ICG=62 for SOC_PM_RETENTION_MODULE_NUM=64).
 */

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_PM_ENABLE && CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP

#include "sdkconfig.h"
#include "soc/regdma.h"
#include "soc/retention_periph_defs.h"

/* IDF 5.5 soc headers do not yet name H.264 REGDMA / retention (see IDF v6.1 note above). */
#ifndef REGDMA_H264_LINK
#define REGDMA_H264_LINK(_pri)              ((0x2a << 8) | (_pri))
#endif
#ifndef REGDMA_LINK_PRI_H264
#define REGDMA_LINK_PRI_H264                REGDMA_LINK_PRI_GENERAL_PERIPH
#endif
#ifndef SLEEP_RETENTION_MODULE_H264
#define SLEEP_RETENTION_MODULE_H264         ((periph_retention_module_t)40)
#endif

/**
 * @brief REGDMA link descriptor for H.264 peripheral context retention across light sleep.
 */
typedef struct {
    const regdma_entries_config_t *link_list; /*!< Pointer to REGDMA entry table for this peripheral */
    uint32_t link_num;                        /*!< Number of entries in @p link_list */
    periph_retention_module_t module_id;      /*!< Sleep retention module ID registered with PM */
} h264_reg_ctx_link_t;

/**
 * @brief Predefined H.264 REGDMA retention link and module binding for light sleep.
 */
extern const h264_reg_ctx_link_t h264_regs_retention;
#endif /* CONFIG_PM_ENABLE && CONFIG_PM_POWER_DOWN_PERIPHERAL_IN_LIGHT_SLEEP */
#ifdef __cplusplus
}
#endif
