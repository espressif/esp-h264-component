/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 *  SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#if defined(__has_include)
#if __has_include("hal/config.h")
#include "hal/config.h"
#endif
#else
#include "hal/config.h"
#endif
#ifndef HAL_CONFIG
#define HAL_CONFIG(x) HAL_CONFIG_##x
#endif
#ifndef HAL_CONFIG_CHIP_SUPPORT_MIN_REV
#define HAL_CONFIG_CHIP_SUPPORT_MIN_REV CONFIG_ESP_REV_MIN_FULL
#endif
