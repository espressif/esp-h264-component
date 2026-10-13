/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include "esp_h264_check.h"
#include "h264_sleep_retention.h"
#include "h264_ll.h"

#if H264_USE_RETENTION_LINK
#include "h264_periph.h"
#include "esp_private/sleep_retention.h"

static const char *TAG = "h264.sleep_retention";

/** Platform singleton */
typedef struct h264_platform_t {
    bool module_inited;
    bool link_created;
    uint32_t ref_count;
} h264_platform_t;

static h264_platform_t s_h264_platform;

static esp_h264_err_t s_h264_sleep_retention_create_cb(void *arg)
{
    (void)arg;
    esp_h264_err_t ret = sleep_retention_entries_create(h264_regs_retention.link_list,
                         h264_regs_retention.link_num,
                         REGDMA_LINK_PRI_H264,
                         h264_regs_retention.module_id);
    ESP_H264_RETURN_ON_ERROR(ret, TAG, "failed to allocate mem for sleep retention");
    return ret;
}

static esp_h264_err_t h264_sleep_retention_init(void)
{
    esp_h264_err_t err = ESP_H264_ERR_OK;
    if (!s_h264_platform.module_inited) {
        sleep_retention_module_init_param_t init_param = {
            .cbs = {
                .create = {
                    .handle = s_h264_sleep_retention_create_cb,
                    .arg = NULL,
                },
            },
            .depends = RETENTION_MODULE_BITMAP_INIT(CLOCK_SYSTEM)
        };
        err = sleep_retention_module_init(h264_regs_retention.module_id, &init_param);
        if (err != ESP_OK) {
            ESP_H264_LOGW(TAG, "init sleep retention failed on h264, configuration may be lost after light sleep");
        } else {
            s_h264_platform.module_inited = true;
        }
    }
    return err;
}

#endif /* H264_USE_RETENTION_LINK */

esp_h264_err_t h264_sleep_retention_acquire(void)
{
#if H264_USE_RETENTION_LINK
    esp_h264_err_t err = h264_sleep_retention_init();
    if (err != ESP_H264_ERR_OK) {
        ESP_H264_LOGE(TAG, "init sleep retention failed");
        return err;
    }
    if (!s_h264_platform.link_created && s_h264_platform.module_inited) {
        if (sleep_retention_module_allocate(h264_regs_retention.module_id) != ESP_OK) {
            ESP_H264_LOGE(TAG, "create retention module failed, power domain can't turn off");
            return ESP_H264_ERR_FAIL;
        }
        s_h264_platform.link_created = true;
    }
    s_h264_platform.ref_count++;
#endif
    return ESP_H264_ERR_OK;
}

void h264_sleep_retention_free(void)
{
#if H264_USE_RETENTION_LINK
    if (s_h264_platform.ref_count == 0) {
        return;
    }
    s_h264_platform.ref_count--;
    if (s_h264_platform.ref_count == 0) {
        h264_sleep_retention_deinit();
    }
#endif
}

void h264_sleep_retention_deinit(void)
{
#if H264_USE_RETENTION_LINK
    if (s_h264_platform.link_created) {
        sleep_retention_module_free(h264_regs_retention.module_id);
        s_h264_platform.link_created = false;
    }
    if (s_h264_platform.module_inited) {
        sleep_retention_module_deinit(h264_regs_retention.module_id);
        s_h264_platform.module_inited = false;
    }
    s_h264_platform.ref_count = 0;
#endif
}

void h264_sleep_retention_arm_canary(void)
{
    h264_ll_arm_top_pd_canary(H264_LL_GET_HW());
}

bool h264_sleep_retention_hw_lost(void)
{
    return h264_ll_top_pd_lost(H264_LL_GET_HW());
}
