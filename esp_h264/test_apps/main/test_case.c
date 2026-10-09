/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unity.h>
#include "esp_h264_hw_enc_test.h"
#include "esp_h264_sw_enc_test.h"
#include "esp_h264_sw_dec_test.h"
#include "esp_h264_alloc.h"
#include "h264_io.h"

static int16_t res_width = 128;
static int16_t res_height = 128;

#if CONFIG_IDF_TARGET_ESP32P4

#include "../../hw/src/h264_rc.h"

static int16_t res_width1 = 128;
static int16_t res_height1 = 128;

TEST_CASE("hw_rc_cumulative_error_saturation_test", "[esp_h264]")
{
    esp_h264_rc_hd_t rc_hd = esp_h264_enc_hw_rc_new(51, 0, 1000, 1, 1, 1);
    TEST_ASSERT_NOT_NULL(rc_hd);

    esp_h264_rc_end(rc_hd, UINT32_MAX, 25, 1);

    uint32_t rate;
    uint32_t pred_mad;
    uint8_t qp;
    esp_h264_rc_start(rc_hd, false, &rate, &pred_mad, &qp);
    TEST_ASSERT_EQUAL_UINT8(26, qp);

    esp_h264_enc_hw_rc_del(rc_hd);
}

#if CONFIG_SECURE_FLASH_ENC_ENABLED
TEST_CASE("hw_enc_internal_buffer_no_enc_test", "[esp_h264][flash_encryption]")
{
    esp_h264_enc_cfg_hw_t cfg = {
        .pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY,
        .gop = 5,
        .fps = 30,
        .res = {
            .width = 1920,
            .height = 1080,
        },
        .rc = {
            .bitrate = 1920 * 1080 * 30 / 20,
            .qp_min = 26,
            .qp_max = 26,
        },
    };
    esp_h264_enc_handle_t enc = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_hw_new(&cfg, &enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_open(enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_close(enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_del(enc));
}
#endif

TEST_CASE("hw_enc_single_hw_enc_gop_test", "[esp_h264]")
{
    for (int16_t gop = 1; gop < 256; gop++) {
        esp_h264_enc_cfg_hw_t cfg = { 0 };
        cfg.gop = gop;
        cfg.fps = 30;
        cfg.res.width = res_width;
        cfg.res.height = res_height;
        cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
        cfg.rc.qp_min = 26;
        cfg.rc.qp_max = 26;
        cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_hw_enc_process(cfg));
    }
}

TEST_CASE("hw_enc_single_hw_enc_fps_test", "[esp_h264]")
{
    for (int16_t fps = 1; fps < 256; fps++) {
        esp_h264_enc_cfg_hw_t cfg = { 0 };
        cfg.gop = 5;
        cfg.fps = fps;
        cfg.res.width = res_width;
        cfg.res.height = res_height;
        cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
        cfg.rc.qp_min = 26;
        cfg.rc.qp_max = 26;
        cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_hw_enc_process(cfg));
    }
}

TEST_CASE("hw_enc_single_hw_enc_qp_test", "[esp_h264]")
{
    for (int16_t qp = 0; qp <= 51; qp++) {
        esp_h264_enc_cfg_hw_t cfg = { 0 };
        cfg.gop = 5;
        cfg.fps = 30;
        cfg.res.width = res_width;
        cfg.res.height = res_height;
        cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
        cfg.rc.qp_min = qp < 0 ? 0 : qp;
        cfg.rc.qp_max = (cfg.rc.qp_min + 1) > 51 ? 51 : (cfg.rc.qp_min + 1);
        cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_hw_enc_process(cfg));
    }
}

#if CONFIG_ESP_REV_MIN_FULL >= 300
TEST_CASE("hw_enc_single_hw_enc_pic_type_test", "[esp_h264]")
{
    esp_h264_raw_format_t pic_type_list[] = {
        ESP_H264_RAW_FMT_BGR888,
        ESP_H264_RAW_FMT_RGB565_LE,
        ESP_H264_RAW_FMT_VUY,
        ESP_H264_RAW_FMT_UYVY,
        ESP_H264_RAW_FMT_O_UYY_E_VYY,
    };

    for (int16_t pic_type = 0; pic_type < sizeof(pic_type_list) / sizeof(pic_type_list[0]); pic_type++) {
        esp_h264_enc_cfg_hw_t cfg = { 0 };
        cfg.gop = 5;
        cfg.fps = 30;
        cfg.res.width = res_width;
        cfg.res.height = res_height;
        cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
        cfg.rc.qp_min = 26;
        cfg.rc.qp_max = 26;
        cfg.pic_type = pic_type_list[pic_type];
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_hw_enc_process(cfg));
    }
}
#endif

TEST_CASE("hw_enc_dual_hw_enc_gop_test", "[esp_h264]")
{
    for (int16_t gop = 1; gop < 256; gop++) {
        esp_h264_enc_cfg_dual_hw_t cfg;
        cfg.cfg0.gop = gop;
        cfg.cfg0.fps = 30;
        cfg.cfg0.res.width = res_width;
        cfg.cfg0.res.height = res_height;
        cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
        cfg.cfg0.rc.qp_min = 26;
        cfg.cfg0.rc.qp_max = 26;
        cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

        cfg.cfg1.gop = gop;
        cfg.cfg1.fps = 30;
        cfg.cfg1.res.width = res_width1;
        cfg.cfg1.res.height = res_height1;
        cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;
        cfg.cfg1.rc.qp_min = 26;
        cfg.cfg1.rc.qp_max = 26;
        cfg.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, dual_hw_enc_process(cfg));
    }
}

TEST_CASE("hw_enc_dual_hw_enc_fps_test", "[esp_h264]")
{
    for (int16_t fps = 1; fps < 256; fps++) {
        esp_h264_enc_cfg_dual_hw_t cfg;
        cfg.cfg0.gop = 5;
        cfg.cfg0.fps = fps;
        cfg.cfg0.res.width = res_width;
        cfg.cfg0.res.height = res_height;
        cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
        cfg.cfg0.rc.qp_min = 26;
        cfg.cfg0.rc.qp_max = 26;
        cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

        cfg.cfg1.gop = 5;
        cfg.cfg1.fps = ((fps + 5) % 256) == 0 ? 1 : ((fps + 5) % 256);
        cfg.cfg1.res.width = res_width1;
        cfg.cfg1.res.height = res_height1;
        cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;
        cfg.cfg1.rc.qp_min = 26;
        cfg.cfg1.rc.qp_max = 26;
        cfg.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, dual_hw_enc_process(cfg));
    }
}

TEST_CASE("hw_enc_dual_hw_enc_qp_test", "[esp_h264]")
{
    for (int16_t qp = 0; qp <= 51; qp++) {
        esp_h264_enc_cfg_dual_hw_t cfg;
        cfg.cfg0.gop = 5;
        cfg.cfg0.fps = 30;
        cfg.cfg0.res.width = res_width;
        cfg.cfg0.res.height = res_height;
        cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
        cfg.cfg0.rc.qp_min = qp < 0 ? 0 : qp;
        cfg.cfg0.rc.qp_max = (cfg.cfg0.rc.qp_min + 1) > 51 ? 51 : (cfg.cfg0.rc.qp_min + 1);
        cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

        cfg.cfg1.gop = 5;
        cfg.cfg1.fps = 30;
        cfg.cfg1.res.width = res_width1;
        cfg.cfg1.res.height = res_height1;
        cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;
        cfg.cfg1.rc.qp_min = qp < 0 ? 0 : (qp + 1) % 51;
        cfg.cfg1.rc.qp_max = (cfg.cfg1.rc.qp_min + 1) > 51 ? 51 : (cfg.cfg1.rc.qp_min + 1);
        cfg.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, dual_hw_enc_process(cfg));
    }
}

#if CONFIG_ESP_REV_MIN_FULL >= 300
TEST_CASE("hw_enc_dual_hw_enc_pic_type_test", "[esp_h264]")
{
    esp_h264_raw_format_t pic_type_list[] = {
        ESP_H264_RAW_FMT_BGR888,
        ESP_H264_RAW_FMT_RGB565_LE,
        ESP_H264_RAW_FMT_VUY,
        ESP_H264_RAW_FMT_UYVY,
        ESP_H264_RAW_FMT_O_UYY_E_VYY,
    };
    esp_h264_enc_cfg_dual_hw_t cfg;
    for (int16_t pic_type = 0; pic_type < sizeof(pic_type_list) / sizeof(pic_type_list[0]); pic_type++) {
        cfg.cfg0.gop = 5;
        cfg.cfg0.fps = 30;
        cfg.cfg0.res.width = res_width;
        cfg.cfg0.res.height = res_height;
        cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
        cfg.cfg0.rc.qp_min = 26;
        cfg.cfg0.rc.qp_max = (cfg.cfg0.rc.qp_min + 1) > 51 ? 51 : (cfg.cfg0.rc.qp_min + 1);
        cfg.cfg0.pic_type = pic_type_list[pic_type];

        cfg.cfg1.gop = 5;
        cfg.cfg1.fps = 30;
        cfg.cfg1.res.width = res_width1;
        cfg.cfg1.res.height = res_height1;
        cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;
        cfg.cfg1.rc.qp_min = 26;
        cfg.cfg1.rc.qp_max = (cfg.cfg1.rc.qp_min + 1) > 51 ? 51 : (cfg.cfg1.rc.qp_min + 1);
        cfg.cfg1.pic_type = pic_type_list[pic_type];
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, dual_hw_enc_process(cfg));
    }
    cfg.cfg0.pic_type = pic_type_list[0];
    cfg.cfg1.pic_type = pic_type_list[1];
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, dual_hw_enc_process(cfg));
}
#endif

TEST_CASE("hw_enc_single_hw_enc_open_time_test", "[esp_h264]")
{
    /* Cover small / mid / 1080p: buffer alloc cost is dominated by new() */
    const int16_t res_list[][2] = {
        {128, 128},
        {640, 480},
        {1280, 720},
        {1920, 1080},
    };
    for (size_t i = 0; i < sizeof(res_list) / sizeof(res_list[0]); i++) {
        esp_h264_enc_cfg_hw_t cfg = { 0 };
        cfg.gop = 30;
        cfg.fps = 30;
        cfg.res.width = res_list[i][0];
        cfg.res.height = res_list[i][1];
        cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
        cfg.rc.qp_min = 26;
        cfg.rc.qp_max = 26;
        cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_hw_enc_open_time_test(cfg));
    }
}

TEST_CASE("hw_enc_dual_hw_enc_open_time_test", "[esp_h264]")
{
    /* Cap at 720p: dual allocates two ref/db buffers */
    const int16_t res_list[][2] = {
        {128, 128},
        {640, 480},
        {1280, 720},
        {1920, 1080},
    };
    for (size_t i = 0; i < sizeof(res_list) / sizeof(res_list[0]); i++) {
        esp_h264_enc_cfg_dual_hw_t cfg = { 0 };
        cfg.cfg0.gop = 30;
        cfg.cfg0.fps = 30;
        cfg.cfg0.res.width = res_list[i][0];
        cfg.cfg0.res.height = res_list[i][1];
        cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
        cfg.cfg0.rc.qp_min = 26;
        cfg.cfg0.rc.qp_max = 26;
        cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

        cfg.cfg1.gop = 30;
        cfg.cfg1.fps = 15;
        cfg.cfg1.res.width = res_list[i][0];
        cfg.cfg1.res.height = res_list[i][1];
        cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;
        cfg.cfg1.rc.qp_min = 26;
        cfg.cfg1.rc.qp_max = 26;
        cfg.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, dual_hw_enc_open_time_test(cfg));
    }
}

TEST_CASE("hw_enc_single_hw_enc_force_idr_test", "[esp_h264]")
{
    esp_h264_enc_cfg_hw_t cfg = { 0 };
    cfg.gop = 30;
    cfg.fps = 30;
    cfg.res.width = res_width;
    cfg.res.height = res_height;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_hw_enc_force_idr_test(cfg));
}

TEST_CASE("hw_enc_dual_hw_enc_force_idr_test", "[esp_h264]")
{
    esp_h264_enc_cfg_dual_hw_t cfg = { 0 };
    cfg.cfg0.gop = 30;
    cfg.cfg0.fps = 30;
    cfg.cfg0.res.width = res_width;
    cfg.cfg0.res.height = res_height;
    cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
    cfg.cfg0.rc.qp_min = 26;
    cfg.cfg0.rc.qp_max = 26;
    cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    cfg.cfg1.gop = 30;
    cfg.cfg1.fps = 15;
    cfg.cfg1.res.width = res_width1;
    cfg.cfg1.res.height = res_height1;
    cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;
    cfg.cfg1.rc.qp_min = 26;
    cfg.cfg1.rc.qp_max = 26;
    cfg.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, dual_hw_enc_force_idr_test(cfg));
}

TEST_CASE("hw_enc_set_get_param_single_hw_enc_thread_test", "[esp_h264]")
{
    esp_h264_enc_cfg_hw_t cfg = { 0 };
    cfg.gop = 5;
    cfg.fps = 30;
    cfg.res.width = res_width;
    cfg.res.height = res_height;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_hw_enc_thread_test(cfg));
}

TEST_CASE("hw_enc_set_get_param_dual_hw_enc_thread_test", "[esp_h264]")
{
    esp_h264_enc_cfg_dual_hw_t cfg;
    cfg.cfg0.gop = 5;
    cfg.cfg0.fps = 30;
    cfg.cfg0.res.width = res_width;
    cfg.cfg0.res.height = res_height;
    cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
    cfg.cfg0.rc.qp_min = 26;
    cfg.cfg0.rc.qp_max = 26;
    cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    cfg.cfg1.gop = 5;
    cfg.cfg1.fps = 30;
    cfg.cfg1.res.width = res_width1;
    cfg.cfg1.res.height = res_height1;
    cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;
    cfg.cfg1.rc.qp_min = 26;
    cfg.cfg1.rc.qp_max = 26;
    cfg.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, dual_hw_enc_thread_test(cfg));
}

TEST_CASE("hw_enc_set_get_param_single_hw_enc_roi_cfg_test", "[esp_h264]")
{
    esp_h264_enc_cfg_hw_t cfg = { 0 };
    cfg.gop = 5;
    cfg.fps = 30;
    cfg.res.width = res_width;
    cfg.res.height = res_height;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_hw_enc_roi_cfg_test(cfg));
}

TEST_CASE("hw_enc_set_get_param_dual_hw_enc_roi_cfg_test", "[esp_h264]")
{
    esp_h264_enc_cfg_dual_hw_t cfg;
    cfg.cfg0.gop = 5;
    cfg.cfg0.fps = 30;
    cfg.cfg0.res.width = res_width;
    cfg.cfg0.res.height = res_height;
    cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
    cfg.cfg0.rc.qp_min = 26;
    cfg.cfg0.rc.qp_max = 26;
    cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    cfg.cfg1.gop = 5;
    cfg.cfg1.fps = 30;
    cfg.cfg1.res.width = res_width1;
    cfg.cfg1.res.height = res_height1;
    cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;
    cfg.cfg1.rc.qp_min = 26;
    cfg.cfg1.rc.qp_max = 26;
    cfg.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, dual_hw_enc_roi_cfg_test(cfg));
}

TEST_CASE("hw_enc_set_get_param_single_hw_enc_roi_reg_test", "[esp_h264]")
{
    esp_h264_enc_cfg_hw_t cfg = { 0 };
    cfg.gop = 5;
    cfg.fps = 30;
    cfg.res.width = res_width;
    cfg.res.height = res_height;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_hw_enc_roi_reg_test(cfg));
}

TEST_CASE("hw_enc_set_get_param_dual_hw_enc_roi_reg_test", "[esp_h264]")
{
    esp_h264_enc_cfg_dual_hw_t cfg;
    cfg.cfg0.gop = 5;
    cfg.cfg0.fps = 30;
    cfg.cfg0.res.width = res_width;
    cfg.cfg0.res.height = res_height;
    cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
    cfg.cfg0.rc.qp_min = 26;
    cfg.cfg0.rc.qp_max = 26;
    cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    cfg.cfg1.gop = 5;
    cfg.cfg1.fps = 30;
    cfg.cfg1.res.width = res_width1;
    cfg.cfg1.res.height = res_height1;
    cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;
    cfg.cfg1.rc.qp_min = 26;
    cfg.cfg1.rc.qp_max = 26;
    cfg.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, dual_hw_enc_roi_reg_test(cfg));
}

TEST_CASE("hw_enc_set_get_param_single_hw_enc_mv_pkt_test", "[esp_h264]")
{
    esp_h264_enc_cfg_hw_t cfg = { 0 };
    cfg.gop = 5;
    cfg.fps = 30;
    cfg.res.width = res_width;
    cfg.res.height = res_height;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_hw_enc_mv_pkt_test(cfg));
}

TEST_CASE("hw_enc_set_get_param_dual_hw_enc_mv_pkt_test", "[esp_h264]")
{
    esp_h264_enc_cfg_dual_hw_t cfg;
    cfg.cfg0.gop = 5;
    cfg.cfg0.fps = 30;
    cfg.cfg0.res.width = res_width;
    cfg.cfg0.res.height = res_height;
    cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
    cfg.cfg0.rc.qp_min = 26;
    cfg.cfg0.rc.qp_max = 26;
    cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    cfg.cfg1.gop = 5;
    cfg.cfg1.fps = 30;
    cfg.cfg1.res.width = res_width1;
    cfg.cfg1.res.height = res_height1;
    cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;
    cfg.cfg1.rc.qp_min = 26;
    cfg.cfg1.rc.qp_max = 26;
    cfg.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, dual_hw_enc_mv_pkt_test(cfg));
}

/* error test */
TEST_CASE("hw_enc_error_test", "[esp_h264]")
{
    esp_h264_enc_cfg_hw_t cfg = { 0 };
    esp_h264_enc_handle_t enc = NULL;
    cfg.gop = 255;
    cfg.fps = 30;
    cfg.res.width = res_width;
    cfg.res.height = res_height;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    /* cfg is NULL*/
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_new(NULL, &enc));

    /* enc handle is NULL*/
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_new(&cfg, NULL));

    /* pic_type isn't ESP_H264_RAW_FMT_O_UYY_E_VYY */
    cfg.pic_type = ESP_H264_RAW_FMT_I420;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_new(&cfg, &enc));
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    /* width is greater than 1920 */
    cfg.res.width = 2000;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_new(&cfg, &enc));

    /* width is less than 64 */
    cfg.res.width = 48;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_new(&cfg, &enc));
    cfg.res.width = 128;

    /* height is greater than 2048 */
    cfg.res.height = 3092;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_new(&cfg, &enc));

    /* height is less than 80 */
    cfg.res.height = 48;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_new(&cfg, &enc));
    cfg.res.height = res_height;

    /* qp_min is greater than 51 */
    cfg.rc.qp_min = 52;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_new(&cfg, &enc));
    cfg.rc.qp_min = 26;

    /* qp_max is greater than 51 */
    cfg.rc.qp_max = 52;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_new(&cfg, &enc));

    /* qp_min is greater than qp_max */
    cfg.rc.qp_max = cfg.rc.qp_min - 1;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_new(&cfg, &enc));
    cfg.rc.qp_max = 26;

    /* GOP is 0 */
    cfg.gop = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_new(&cfg, &enc));
    cfg.gop = 30;

    /* FPS is 0 */
    cfg.fps = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_new(&cfg, &enc));
    cfg.fps = 5;

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_hw_new(&cfg, &enc));

    esp_h264_enc_param_hw_handle_t param_hd = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_hw_get_param_hd(enc, &param_hd));

    /* get_resolution: param_hd is NULL */
    esp_h264_resolution_t res;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_resolution(NULL, &res));

    /* get_resolution: resolution is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_resolution(&param_hd->base, NULL));

    /* set_fps: param_hd is NULL */
    uint8_t fps;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_set_fps(NULL, fps));

    /* get_fps: param_hd is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_fps(NULL, &fps));

    /* get_fps: fps is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_fps(&param_hd->base, NULL));

    /* set_gop: param_hd is NULL */
    uint8_t gop;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_set_gop(NULL, gop));

    /* get_gop: param_hd is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_gop(NULL, &gop));

    /* get_gop: gop is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_gop(&param_hd->base, NULL));

    /* set_bitrate: param_hd is NULL */
    esp_h264_enc_rc_t rc = {
        .qp_min = 26,
        .qp_max = 27,
    };
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_set_bitrate(NULL, rc.bitrate));

    /* get_bitrate: param_hd is NULL  */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_bitrate(NULL, &rc.bitrate));

    /* get_bitrate: bitrate is NULL  */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_bitrate(&param_hd->base, NULL));

    /* cfg_roi: param_hd is NULL  */
    esp_h264_enc_roi_cfg_t roi_cfg;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_cfg_roi(NULL, roi_cfg));

    /* cfg_roi: mode is invalid */
    roi_cfg.roi_mode = ESP_H264_ROI_MODE_INVALID;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_cfg_roi(param_hd, roi_cfg));
    roi_cfg.roi_mode = ESP_H264_ROI_MODE_DELTA_QP;

    /* cfg_roi: none_roi_delta_qp is greater than 51 */
    roi_cfg.none_roi_delta_qp = 52;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_cfg_roi(param_hd, roi_cfg));

    /* cfg_roi: none_roi_delta_qp is less than -51 */
    roi_cfg.none_roi_delta_qp = -52;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_cfg_roi(param_hd, roi_cfg));

    /* get_roi_cfg_info: param_hd is NULL  */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_get_roi_cfg_info(NULL, &roi_cfg));

    /* get_roi_cfg_info: ROI is invalid */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_get_roi_cfg_info(param_hd, NULL));

    /* set_roi_region: param_hd is NULL  */
    esp_h264_enc_roi_reg_t roi_reg = { 0 };
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_set_roi_region(NULL, roi_reg));

    /* set_roi_region: qp is greater than 51  */
    roi_reg.qp = 52;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_set_roi_region(param_hd, roi_reg));
    roi_reg.qp = 50;

    /* set_roi_region: ESP_H264_ROI_MODE_DELTA_QP, qp is less than -51  */
    roi_reg.qp = -52;
    roi_cfg.roi_mode = ESP_H264_ROI_MODE_DELTA_QP;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_cfg_roi(param_hd, roi_cfg));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_set_roi_region(param_hd, roi_reg));
    /* set_roi_region: ESP_H264_ROI_MODE_FIX_QP, qp is less than -51  */
    roi_reg.qp = -1;
    roi_cfg.roi_mode = ESP_H264_ROI_MODE_FIX_QP;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_cfg_roi(param_hd, roi_cfg));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_set_roi_region(param_hd, roi_reg));
    roi_reg.qp = 50;

    /* set_roi_region: reg_idx is greater than 8  */
    roi_reg.reg_idx = 9;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_set_roi_region(param_hd, roi_reg));

    /* get_roi_region: param_hd is NULL  */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_get_roi_region(NULL, &roi_reg));

    /* get_roi_region: ROI region is NULL  */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_get_roi_region(param_hd, NULL));

    /* get_roi_region: reg_idx is greater than 8  */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_get_roi_region(param_hd, &roi_reg));

    /* cfg_mv: param_hd is NULL  */
    esp_h264_enc_mv_cfg_t mv_cfg;
    mv_cfg.mv_mode = ESP_H264_MVM_MODE_P16X16;
    mv_cfg.mv_fmt = ESP_H264_MVM_FMT_PART;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_cfg_mv(NULL, mv_cfg));

    /* cfg_mv: mode is invalid */
    mv_cfg.mv_mode = ESP_H264_MVM_MODE_INVALID;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_cfg_mv(param_hd, mv_cfg));
    mv_cfg.mv_mode = ESP_H264_MVM_MODE_P16X16;

    /* cfg_mv: mv_fmt is invalid */
    mv_cfg.mv_fmt = ESP_H264_MVM_FMT_INVALID;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_cfg_mv(param_hd, mv_cfg));
    mv_cfg.mv_fmt = ESP_H264_MVM_FMT_PART;

    /* get_mv_cfg_info: param_hd is NULL  */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_get_mv_cfg_info(NULL, &mv_cfg));

    /* get_mv_cfg_info: MV configure is NULL  */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_get_mv_cfg_info(param_hd, NULL));

    /* set_mv_pkt: param_hd is NULL  */
    esp_h264_enc_mvm_pkt_t mv_pkt;
    esp_h264_enc_mv_data_t mv_data;
    mv_pkt.len = 12;
    mv_pkt.data = &mv_data;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_set_mv_pkt(NULL, mv_pkt));
    /* set_mv_pkt: buffer is NULL  */
    mv_pkt.data = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_set_mv_pkt(param_hd, mv_pkt));
    /* set_mv_pkt: length is 0  */
    mv_pkt.len = 0;
    mv_pkt.data = &mv_data;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_set_mv_pkt(param_hd, mv_pkt));

    /* get_mv_data_len: param_hd is NULL  */
    uint32_t length;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_get_mv_data_len(NULL, &length));

    /* get_mv_data_len: length is NULL  */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_hw_get_mv_data_len(param_hd, NULL));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_open(NULL));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_open(enc));

    esp_h264_enc_in_frame_t in_frame = {
        .raw_data.buffer = (uint8_t *)1,
        .raw_data.len = 1,
    };
    esp_h264_enc_out_frame_t out_frame = {
        .raw_data.buffer = (uint8_t *)1,
        .raw_data.len = 1,
    };

    /* enc is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_process(NULL, &in_frame, &out_frame));
    /* in_frame is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_process(enc, NULL, &out_frame));
    /* in_frame.raw_data.buffer is NULL */
    in_frame.raw_data.buffer = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_process(enc, &in_frame, &out_frame));
    in_frame.raw_data.buffer = (uint8_t *)1;

    /* out_frame is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_process(enc, &in_frame, NULL));
    /* out_frame.raw_data.buffer is NULL */
    out_frame.raw_data.buffer = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_process(enc, &in_frame, &out_frame));
    out_frame.raw_data.buffer = (uint8_t *)1;

    /* enc is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_close(NULL));

    /* close enc */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_close(enc));

    /* enc is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_del(NULL));

    /* delete enc */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_del(enc));

    esp_h264_enc_cfg_dual_hw_t cfg_dual;
    esp_h264_enc_dual_handle_t enc_dual = NULL;
    cfg_dual.cfg0.gop = 5;
    cfg_dual.cfg0.fps = 30;
    cfg_dual.cfg0.res.width = 128;
    cfg_dual.cfg0.res.height = 128;
    cfg_dual.cfg0.rc.bitrate = cfg_dual.cfg0.res.width * cfg_dual.cfg0.res.height * cfg_dual.cfg0.fps / 20;
    cfg_dual.cfg0.rc.qp_min = 26;
    cfg_dual.cfg0.rc.qp_max = 26;
    cfg_dual.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    cfg_dual.cfg1.gop = 5;
    cfg_dual.cfg1.fps = 30;
    cfg_dual.cfg1.res.width = 128;
    cfg_dual.cfg1.res.height = 128;
    cfg_dual.cfg1.rc.bitrate = cfg_dual.cfg1.res.width * cfg_dual.cfg1.res.height * cfg_dual.cfg1.fps / 20;
    cfg_dual.cfg1.rc.qp_min = 26;
    cfg_dual.cfg1.rc.qp_max = 26;
    cfg_dual.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    /* cfg is NULL*/
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(NULL, &enc_dual));

    /* enc handle is NULL*/
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, NULL));

    /* pic_type isn't ESP_H264_RAW_FMT_O_UYY_E_VYY */
    cfg_dual.cfg0.pic_type = ESP_H264_RAW_FMT_I420;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    cfg_dual.cfg1.pic_type = ESP_H264_RAW_FMT_I420;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    /* width is greater than 1920 */
    cfg_dual.cfg0.res.width = 2000;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg0.res.width = 128;

    cfg_dual.cfg1.res.width = 2000;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg1.res.width = 128;

    /* width is less than 64 */
    cfg_dual.cfg0.res.width = 48;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg0.res.width = 128;

    cfg_dual.cfg1.res.width = 48;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg1.res.width = 128;

    /* height is greater than 2048 */
    cfg_dual.cfg0.res.height = 3092;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg0.res.height = 128;

    cfg_dual.cfg1.res.height = 3092;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg1.res.height = 128;

    /* height is less than 80 */
    cfg_dual.cfg0.res.height = 48;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg0.res.height = 128;

    /* height is less than 80 */
    cfg_dual.cfg1.res.height = 48;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg1.res.height = 128;

    /* qp_min is greater than 51 */
    cfg_dual.cfg0.rc.qp_min = 52;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg0.rc.qp_min = 26;

    cfg_dual.cfg1.rc.qp_min = 52;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg1.rc.qp_min = 26;

    /* qp_max is greater than 51 */
    cfg_dual.cfg0.rc.qp_max = 52;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg0.rc.qp_max = 26;

    cfg_dual.cfg1.rc.qp_max = 52;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg1.rc.qp_max = 26;

    /* qp_min is greater than qp_max */
    cfg_dual.cfg0.rc.qp_max = cfg_dual.cfg0.rc.qp_min - 1;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg0.rc.qp_max = 26;

    cfg_dual.cfg1.rc.qp_max = cfg_dual.cfg1.rc.qp_min - 1;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg1.rc.qp_max = 26;

    /* GOP is 0 */
    cfg_dual.cfg0.gop = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg0.gop = 30;

    cfg_dual.cfg1.gop = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg1.gop = 30;

    /* FPS is 0 */
    cfg_dual.cfg0.fps = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg0.fps = 30;

    cfg_dual.cfg1.fps = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));
    cfg_dual.cfg1.fps = 30;

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_hw_new(&cfg_dual, &enc_dual));

    /* get_param_hd0: enc is NULL */
    esp_h264_enc_param_hw_handle_t param_hd0 = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_get_param_hd0(NULL, &param_hd0));

    /* get_param_hd0: out_param is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_get_param_hd0(enc_dual, NULL));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_hw_get_param_hd0(enc_dual, &param_hd0));

    /* get_param_hd1: enc is NULL */
    esp_h264_enc_param_hw_handle_t param_hd1 = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_get_param_hd1(NULL, &param_hd1));

    /* get_param_hd1: out_param is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_hw_get_param_hd1(enc_dual, NULL));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_hw_get_param_hd1(enc_dual, &param_hd1));

    /* enc is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_open(NULL));

    /* open enc */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_open(enc_dual));

    esp_h264_enc_in_frame_t in_frame1 = {
        .raw_data.buffer = (uint8_t *)1,
        .raw_data.len = 1,
    };
    esp_h264_enc_out_frame_t out_frame1 = {
        .raw_data.buffer = (uint8_t *)1,
        .raw_data.len = 1,
    };
    esp_h264_enc_in_frame_t *in_frame_dual[2] = { &in_frame, &in_frame1 };
    esp_h264_enc_out_frame_t *out_frame_dual[2] = { &out_frame, &out_frame1 };

    /* enc_dual is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_process(NULL, in_frame_dual, out_frame_dual));

    /* in_frame is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_process(enc_dual, NULL, out_frame_dual));
    in_frame_dual[0] = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_process(enc_dual, in_frame_dual, out_frame_dual));
    in_frame_dual[0] = &in_frame;

    in_frame_dual[1] = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_process(enc_dual, in_frame_dual, out_frame_dual));
    in_frame_dual[1] = &in_frame1;

    /* in_frame.raw_data.buffer is NULL */
    in_frame.raw_data.buffer = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_process(enc_dual, in_frame_dual, out_frame_dual));
    in_frame.raw_data.buffer = (uint8_t *)1;

    in_frame1.raw_data.buffer = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_process(enc_dual, in_frame_dual, out_frame_dual));
    in_frame1.raw_data.buffer = (uint8_t *)1;

    /* out_frame is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_process(enc_dual, in_frame_dual, NULL));

    out_frame_dual[0] = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_process(enc_dual, in_frame_dual, out_frame_dual));
    out_frame_dual[0] = &out_frame;

    out_frame_dual[1] = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_process(enc_dual, in_frame_dual, out_frame_dual));
    out_frame_dual[1] = &out_frame1;

    /* out_frame.raw_data.buffer is NULL */
    out_frame.raw_data.buffer = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_process(enc_dual, in_frame_dual, out_frame_dual));
    out_frame.raw_data.buffer = (uint8_t *)1;

    out_frame1.raw_data.buffer = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_process(enc_dual, in_frame_dual, out_frame_dual));
    out_frame1.raw_data.buffer = (uint8_t *)1;

    /* enc is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_close(NULL));

    /* close enc */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_close(enc_dual));

    /* enc is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_del(NULL));

    /* delete enc */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_del(enc_dual));
}

/** The first frame is always IDR, so the encoder must prepend SPS+PPS. If the caller's
 *  output buffer is too small for SPS+PPS, `esp_h264_enc_process` must fail with
 *  ESP_H264_ERR_MEM instead of overflowing the buffer, and `out_frame.length` must be 0. */
TEST_CASE("hw_enc_single_hw_out_buf_too_small_test", "[esp_h264]")
{
    esp_h264_enc_cfg_hw_t cfg = { 0 };
    cfg.gop = 5;
    cfg.fps = 30;
    cfg.res.width = res_width;
    cfg.res.height = res_height;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    esp_h264_enc_handle_t enc = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_hw_new(&cfg, &enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_open(enc));

    esp_h264_enc_in_frame_t in_frame = { 0 };
    uint32_t in_len = (uint32_t)((float)cfg.res.width * cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len, &in_len, MALLOC_CAP_INTERNAL);
    in_frame.raw_data.len = in_len;
    TEST_ASSERT_NOT_NULL(in_frame.raw_data.buffer);

    /* Real buffer is large enough to avoid any out-of-bounds write; only the declared
     * `raw_data.len` is set too small so the SPS/PPS size check is what triggers the error. */
    esp_h264_enc_out_frame_t out_frame = { 0 };
    uint32_t out_actual_size = 0;
    out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, 128, &out_actual_size, MALLOC_CAP_INTERNAL);
    TEST_ASSERT_NOT_NULL(out_frame.raw_data.buffer);
    out_frame.raw_data.len = 4;

    TEST_ASSERT_EQUAL(ESP_H264_ERR_MEM, esp_h264_enc_process(enc, &in_frame, &out_frame));
    TEST_ASSERT_EQUAL(0, out_frame.length);

    esp_h264_free(in_frame.raw_data.buffer);
    esp_h264_free(out_frame.raw_data.buffer);
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_close(enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_del(enc));
}

/** Same as above but for the dual encoder: when stream0's output buffer is too small,
 *  stream1 must be skipped entirely (its `length` cleared to 0) rather than encoded with
 *  stream0 left in a half-finished state. */
TEST_CASE("hw_enc_dual_hw_out_buf_too_small_test", "[esp_h264]")
{
    esp_h264_enc_cfg_dual_hw_t cfg = { 0 };
    cfg.cfg0.gop = 5;
    cfg.cfg0.fps = 30;
    cfg.cfg0.res.width = res_width;
    cfg.cfg0.res.height = res_height;
    cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
    cfg.cfg0.rc.qp_min = 26;
    cfg.cfg0.rc.qp_max = 26;
    cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    cfg.cfg1.gop = 5;
    cfg.cfg1.fps = 30;
    cfg.cfg1.res.width = res_width1;
    cfg.cfg1.res.height = res_height1;
    cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;
    cfg.cfg1.rc.qp_min = 26;
    cfg.cfg1.rc.qp_max = 26;
    cfg.cfg1.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    esp_h264_enc_dual_handle_t enc = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_hw_new(&cfg, &enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_open(enc));

    esp_h264_enc_in_frame_t in_frame0 = { 0 };
    uint32_t in_len0 = (uint32_t)((float)cfg.cfg0.res.width * cfg.cfg0.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg0.pic_type));
    in_frame0.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len0, &in_len0, MALLOC_CAP_INTERNAL);
    in_frame0.raw_data.len = in_len0;
    TEST_ASSERT_NOT_NULL(in_frame0.raw_data.buffer);

    esp_h264_enc_in_frame_t in_frame1 = { 0 };
    uint32_t in_len1 = (uint32_t)((float)cfg.cfg1.res.width * cfg.cfg1.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg1.pic_type));
    in_frame1.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len1, &in_len1, MALLOC_CAP_INTERNAL);
    in_frame1.raw_data.len = in_len1;
    TEST_ASSERT_NOT_NULL(in_frame1.raw_data.buffer);

    uint32_t out_actual_size = 0;
    esp_h264_enc_out_frame_t out_frame0 = { 0 };
    out_frame0.raw_data.buffer = esp_h264_aligned_calloc(16, 1, 128, &out_actual_size, MALLOC_CAP_INTERNAL);
    TEST_ASSERT_NOT_NULL(out_frame0.raw_data.buffer);

    esp_h264_enc_out_frame_t out_frame1 = { 0 };
    out_frame1.raw_data.buffer = esp_h264_aligned_calloc(16, 1, 128, &out_actual_size, MALLOC_CAP_INTERNAL);
    TEST_ASSERT_NOT_NULL(out_frame1.raw_data.buffer);

    esp_h264_enc_in_frame_t *in_frame_dual[2] = { &in_frame0, &in_frame1 };
    esp_h264_enc_out_frame_t *out_frame_dual[2] = { &out_frame0, &out_frame1 };

    /* stream0's buffer is too small: stream1 must not be touched */
    out_frame0.raw_data.len = 4;
    out_frame1.raw_data.len = out_actual_size;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_MEM, esp_h264_enc_dual_process(enc, in_frame_dual, out_frame_dual));
    TEST_ASSERT_EQUAL(0, out_frame0.length);
    TEST_ASSERT_EQUAL(0, out_frame1.length);

    /* stream0's buffer is fine, stream1's is too small: stream0's result must stand */
    out_frame0.raw_data.len = out_actual_size;
    out_frame1.raw_data.len = 4;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_MEM, esp_h264_enc_dual_process(enc, in_frame_dual, out_frame_dual));
    TEST_ASSERT_GREATER_THAN(0, out_frame0.length);
    TEST_ASSERT_EQUAL(0, out_frame1.length);

    esp_h264_free(in_frame0.raw_data.buffer);
    esp_h264_free(in_frame1.raw_data.buffer);
    esp_h264_free(out_frame0.raw_data.buffer);
    esp_h264_free(out_frame1.raw_data.buffer);
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_close(enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_del(enc));
}

/* process_one is 0 then 1 and blocking. Stop after stream1 of pair N plus stream0 of
 * pair N+1 (stream1 of that pair never encoded), and stop after unpaired stream0.
 * Both must close/del without timeout or leak — the HW is idle once process_one returns. */
TEST_CASE("hw_enc_dual_process_one_stop_unpaired_test", "[esp_h264][process_one]")
{
    esp_h264_enc_cfg_dual_hw_t cfg = { 0 };
    cfg.cfg0.gop = 5;
    cfg.cfg0.fps = 30;
    cfg.cfg0.res.width = res_width;
    cfg.cfg0.res.height = res_height;
    cfg.cfg0.rc.bitrate = cfg.cfg0.res.width * cfg.cfg0.res.height * cfg.cfg0.fps / 20;
    cfg.cfg0.rc.qp_min = 26;
    cfg.cfg0.rc.qp_max = 26;
    cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    cfg.cfg1 = cfg.cfg0;
    cfg.cfg1.res.width = res_width1;
    cfg.cfg1.res.height = res_height1;
    cfg.cfg1.rc.bitrate = cfg.cfg1.res.width * cfg.cfg1.res.height * cfg.cfg1.fps / 20;

    esp_h264_enc_dual_handle_t enc = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_hw_new(&cfg, &enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_open(enc));

    uint32_t in_len0 = (uint32_t)((float)cfg.cfg0.res.width * cfg.cfg0.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg0.pic_type));
    uint32_t in_len1 = (uint32_t)((float)cfg.cfg1.res.width * cfg.cfg1.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg1.pic_type));
    uint32_t out_len0 = in_len0;
    uint32_t out_len1 = in_len1;
    esp_h264_enc_in_frame_t in0 = { 0 };
    esp_h264_enc_in_frame_t in1 = { 0 };
    esp_h264_enc_out_frame_t out0 = { 0 };
    esp_h264_enc_out_frame_t out1 = { 0 };
    in0.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len0, &in_len0, MALLOC_CAP_INTERNAL);
    in1.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len1, &in_len1, MALLOC_CAP_INTERNAL);
    out0.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_len0, &out_len0, MALLOC_CAP_INTERNAL);
    out1.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_len1, &out_len1, MALLOC_CAP_INTERNAL);
    TEST_ASSERT_NOT_NULL(in0.raw_data.buffer);
    TEST_ASSERT_NOT_NULL(in1.raw_data.buffer);
    TEST_ASSERT_NOT_NULL(out0.raw_data.buffer);
    TEST_ASSERT_NOT_NULL(out1.raw_data.buffer);
    in0.raw_data.len = in_len0;
    in1.raw_data.len = in_len1;
    out0.raw_data.len = out_len0;
    out1.raw_data.len = out_len1;
    memset(in0.raw_data.buffer, 0x5A, in_len0);
    memset(in1.raw_data.buffer, 0xA5, in_len1);

    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_dual_process_one(enc, 1, &in1, &out1));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_process_one(enc, 0, &in0, &out0));
    TEST_ASSERT_GREATER_THAN(0, out0.length);
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_process_one(enc, 1, &in1, &out1));
    TEST_ASSERT_GREATER_THAN(0, out1.length);

    /* Stream1 of the last pair is done. Encode stream0 of the next pair, then stop
     * without stream1 — the case "enc1 finished, enc0 of the next pair has run". */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_process_one(enc, 0, &in0, &out0));
    TEST_ASSERT_GREATER_THAN(0, out0.length);
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_close(enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_del(enc));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_hw_new(&cfg, &enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_open(enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_process_one(enc, 0, &in0, &out0));
    TEST_ASSERT_GREATER_THAN(0, out0.length);
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_close(enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_dual_del(enc));

    esp_h264_free(in0.raw_data.buffer);
    esp_h264_free(in1.raw_data.buffer);
    esp_h264_free(out0.raw_data.buffer);
    esp_h264_free(out1.raw_data.buffer);
}

/* One 640x480 stream (to force overflow) plus one 128x128 stream. Two 640x480
 * refs do not fit in internal RAM; ref buffers are INTERNAL-only. */
static const char *process_one_overflow_case(uint8_t overflow_idx)
{
    const uint16_t large_w = 640;
    const uint16_t large_h = 480;
    const uint16_t small_w = 128;
    const uint16_t small_h = 128;
    const uint32_t small_out = 1200;
    static char fail_buf[80];
    const char *fail = NULL;
    esp_h264_enc_dual_handle_t enc = NULL;
    uint8_t *full0 = NULL;
    uint8_t *full1 = NULL;
    uint8_t *small_buf = NULL;
    esp_h264_enc_in_frame_t in0 = { 0 };
    esp_h264_enc_in_frame_t in1 = { 0 };
    esp_h264_enc_out_frame_t out0 = { 0 };
    esp_h264_enc_out_frame_t out1 = { 0 };
    uint32_t in_len0 = 0;
    uint32_t in_len1 = 0;
    uint32_t full0_len = 0;
    uint32_t full1_len = 0;
    uint32_t small_actual = 0;
    uint16_t w0 = (overflow_idx == 0) ? large_w : small_w;
    uint16_t h0 = (overflow_idx == 0) ? large_h : small_h;
    uint16_t w1 = (overflow_idx == 1) ? large_w : small_w;
    uint16_t h1 = (overflow_idx == 1) ? large_h : small_h;

    esp_h264_enc_cfg_dual_hw_t cfg = { 0 };
    cfg.cfg0.gop = 30;
    cfg.cfg0.fps = 30;
    cfg.cfg0.res.width = w0;
    cfg.cfg0.res.height = h0;
    cfg.cfg0.rc.bitrate = (uint32_t)w0 * h0 * cfg.cfg0.fps / 2;
    cfg.cfg0.rc.qp_min = 1;
    cfg.cfg0.rc.qp_max = 1;
    cfg.cfg0.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    cfg.cfg1 = cfg.cfg0;
    cfg.cfg1.res.width = w1;
    cfg.cfg1.res.height = h1;
    cfg.cfg1.rc.bitrate = (uint32_t)w1 * h1 * cfg.cfg1.fps / 2;

    if (esp_h264_enc_dual_hw_new(&cfg, &enc) != ESP_H264_ERR_OK) {
        fail = "dual_hw_new";
        goto cleanup;
    }
    if (esp_h264_enc_dual_open(enc) != ESP_H264_ERR_OK) {
        fail = "dual_open";
        goto cleanup;
    }

    in_len0 = (uint32_t)((float)w0 * h0 * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg0.pic_type));
    in_len1 = (uint32_t)((float)w1 * h1 * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.cfg1.pic_type));
    in0.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len0, &in_len0, ESP_H264_MEM_SPIRAM);
    in1.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len1, &in_len1, ESP_H264_MEM_SPIRAM);
    full0 = esp_h264_aligned_calloc(16, 1, in_len0, &full0_len, ESP_H264_MEM_SPIRAM);
    full1 = esp_h264_aligned_calloc(16, 1, in_len1, &full1_len, ESP_H264_MEM_SPIRAM);
    small_buf = esp_h264_aligned_calloc(16, 1, small_out, &small_actual, ESP_H264_MEM_SPIRAM);
    if (!in0.raw_data.buffer || !in1.raw_data.buffer || !full0 || !full1 || !small_buf) {
        fail = "alloc";
        goto cleanup;
    }
    in0.raw_data.len = in_len0;
    in1.raw_data.len = in_len1;

    out0.raw_data.buffer = full0;
    out0.raw_data.len = full0_len;
    out1.raw_data.buffer = full1;
    out1.raw_data.len = full1_len;
    if (read_enc_cb(&in0, w0, h0, cfg.cfg0.pic_type) <= 0 ||
            esp_h264_enc_dual_process_one(enc, 0, &in0, &out0) != ESP_H264_ERR_OK ||
            out0.frame_type != ESP_H264_FRAME_TYPE_IDR) {
        fail = "pair0 stream0 idr";
        goto cleanup;
    }
    if (read_enc_cb(&in1, w1, h1, cfg.cfg1.pic_type) <= 0 ||
            esp_h264_enc_dual_process_one(enc, 1, &in1, &out1) != ESP_H264_ERR_OK ||
            out1.frame_type != ESP_H264_FRAME_TYPE_IDR) {
        fail = "pair0 stream1 idr";
        goto cleanup;
    }

    if (overflow_idx == 0) {
        esp_h264_err_t ret;
        out0.raw_data.buffer = small_buf;
        out0.raw_data.len = small_actual;
        if (read_enc_cb(&in0, w0, h0, cfg.cfg0.pic_type) <= 0) {
            fail = "fill stream0";
            goto cleanup;
        }
        ret = esp_h264_enc_dual_process_one(enc, 0, &in0, &out0);
        if (ret != ESP_H264_ERR_OVERFLOW) {
            snprintf(fail_buf, sizeof(fail_buf), "stream0 overflow ret=%d len=%u", (int)ret, (unsigned)out0.length);
            fail = fail_buf;
            goto cleanup;
        }
        if (esp_h264_enc_dual_process_one(enc, 0, &in0, &out0) != ESP_H264_ERR_ARG) {
            fail = "stream0 retry";
            goto cleanup;
        }
        out1.raw_data.buffer = full1;
        out1.raw_data.len = full1_len;
        if (read_enc_cb(&in1, w1, h1, cfg.cfg1.pic_type) <= 0 ||
                esp_h264_enc_dual_process_one(enc, 1, &in1, &out1) != ESP_H264_ERR_OK) {
            fail = "stream1 after stream0 overflow";
            goto cleanup;
        }
    } else {
        if (read_enc_cb(&in0, w0, h0, cfg.cfg0.pic_type) <= 0 ||
                esp_h264_enc_dual_process_one(enc, 0, &in0, &out0) != ESP_H264_ERR_OK ||
                out0.frame_type != ESP_H264_FRAME_TYPE_P) {
            fail = "stream0 p before stream1 overflow";
            goto cleanup;
        }
        esp_h264_err_t ret;
        out1.raw_data.buffer = small_buf;
        out1.raw_data.len = small_actual;
        if (read_enc_cb(&in1, w1, h1, cfg.cfg1.pic_type) <= 0) {
            fail = "fill stream1";
            goto cleanup;
        }
        ret = esp_h264_enc_dual_process_one(enc, 1, &in1, &out1);
        if (ret != ESP_H264_ERR_OVERFLOW) {
            snprintf(fail_buf, sizeof(fail_buf), "stream1 overflow ret=%d len=%u", (int)ret, (unsigned)out1.length);
            fail = fail_buf;
            goto cleanup;
        }
    }

    out0.raw_data.buffer = full0;
    out0.raw_data.len = full0_len;
    out1.raw_data.buffer = full1;
    out1.raw_data.len = full1_len;
    if (read_enc_cb(&in0, w0, h0, cfg.cfg0.pic_type) <= 0 ||
            esp_h264_enc_dual_process_one(enc, 0, &in0, &out0) != ESP_H264_ERR_OK ||
            out0.frame_type != ESP_H264_FRAME_TYPE_IDR) {
        fail = "next pair stream0 idr";
        goto cleanup;
    }
    if (read_enc_cb(&in1, w1, h1, cfg.cfg1.pic_type) <= 0 ||
            esp_h264_enc_dual_process_one(enc, 1, &in1, &out1) != ESP_H264_ERR_OK ||
            out1.frame_type != ESP_H264_FRAME_TYPE_IDR) {
        fail = "next pair stream1 idr";
        goto cleanup;
    }

cleanup:
    if (in0.raw_data.buffer) {
        esp_h264_free(in0.raw_data.buffer);
    }
    if (in1.raw_data.buffer) {
        esp_h264_free(in1.raw_data.buffer);
    }
    if (full0) {
        esp_h264_free(full0);
    }
    if (full1) {
        esp_h264_free(full1);
    }
    if (small_buf) {
        esp_h264_free(small_buf);
    }
    if (enc) {
        (void)esp_h264_enc_dual_close(enc);
        (void)esp_h264_enc_dual_del(enc);
    }
    return fail;
}

TEST_CASE("hw_enc_dual_process_one_overflow_forces_idr_test", "[esp_h264][process_one]")
{
    const char *fail0 = process_one_overflow_case(0);
    TEST_ASSERT_NULL_MESSAGE(fail0, fail0);
    const char *fail1 = process_one_overflow_case(1);
    TEST_ASSERT_NULL_MESSAGE(fail1, fail1);
}

/* Round-trip test: feed every HW-encoded frame straight into the independent SW (tinyh264)
 * decoder. This is the strongest available check that the HW encoder produces a
 * spec-compliant bitstream (correct SPS/PPS, level_idc, slice headers, AUD, etc.), since
 * a real 3rd-party decoder implementation must be able to parse it without errors. */
TEST_CASE("hw_enc_sw_dec_roundtrip_test", "[esp_h264]")
{
    esp_h264_enc_cfg_hw_t enc_cfg = { 0 };
    enc_cfg.gop = 4;
    enc_cfg.fps = 30;
    enc_cfg.res.width = res_width;
    enc_cfg.res.height = res_height;
    enc_cfg.rc.bitrate = enc_cfg.res.width * enc_cfg.res.height * enc_cfg.fps / 10;
    enc_cfg.rc.qp_min = 15;
    enc_cfg.rc.qp_max = 30;
    enc_cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    esp_h264_enc_handle_t enc = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_hw_new(&enc_cfg, &enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_open(enc));

    esp_h264_dec_cfg_sw_t dec_cfg = { 0 };
    dec_cfg.pic_type = ESP_H264_RAW_FMT_I420;
    esp_h264_dec_handle_t dec = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_sw_new(&dec_cfg, &dec));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_open(dec));

    esp_h264_enc_in_frame_t in_frame = { 0 };
    uint32_t in_len = (uint32_t)((float)enc_cfg.res.width * enc_cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(enc_cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len, &in_len, MALLOC_CAP_INTERNAL);
    in_frame.raw_data.len = in_len;
    TEST_ASSERT_NOT_NULL(in_frame.raw_data.buffer);

    esp_h264_enc_out_frame_t out_frame = { 0 };
    uint32_t out_buf_len = in_len;
    out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_buf_len, &out_buf_len, MALLOC_CAP_INTERNAL);
    out_frame.raw_data.len = out_buf_len;
    TEST_ASSERT_NOT_NULL(out_frame.raw_data.buffer);

    uint32_t expect_dec_size = (uint32_t)enc_cfg.res.width * enc_cfg.res.height
                               + ((uint32_t)enc_cfg.res.width * enc_cfg.res.height >> 1);
    int frame_count = 0;
    int decoded_pic_count = 0;

    while (1) {
        int ret_w = read_enc_cb(&in_frame, enc_cfg.res.width, enc_cfg.res.height, enc_cfg.pic_type);
        if (ret_w <= 0) {
            break;
        }
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_process(enc, &in_frame, &out_frame));
        TEST_ASSERT_GREATER_THAN(0, out_frame.length);
        if (frame_count == 0) {
            TEST_ASSERT_EQUAL(ESP_H264_FRAME_TYPE_IDR, out_frame.frame_type);
        }

        /* Feed the just-encoded bitstream straight into the SW decoder, one NAL unit at a time */
        esp_h264_dec_in_frame_t dec_in = { 0 };
        dec_in.raw_data.buffer = out_frame.raw_data.buffer;
        dec_in.raw_data.len = out_frame.length;
        while (dec_in.raw_data.len > 0) {
            esp_h264_dec_out_frame_t dec_out = { 0 };
            TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_process(dec, &dec_in, &dec_out));
            TEST_ASSERT_GREATER_THAN(0, dec_in.consume);
            dec_in.raw_data.buffer += dec_in.consume;
            dec_in.raw_data.len -= dec_in.consume;
            if (dec_out.out_size > 0) {
                TEST_ASSERT_EQUAL(expect_dec_size, dec_out.out_size);
                decoded_pic_count++;
            }
        }
        frame_count++;
    }
    /* Sanity: the input pattern must have produced more than one frame, so both an IDR and
     * at least one P frame went through the encode->decode round trip. */
    TEST_ASSERT_GREATER_THAN(1, frame_count);
    /* Every encoded access unit must decode into exactly one displayable picture: nothing
     * dropped, nothing stuck waiting for more data. */
    TEST_ASSERT_EQUAL(frame_count, decoded_pic_count);

    esp_h264_resolution_t dec_res = { 0 };
    esp_h264_dec_param_sw_handle_t dec_param_hd = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_sw_get_param_hd(dec, &dec_param_hd));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_get_resolution(dec_param_hd, &dec_res));
    TEST_ASSERT_EQUAL(enc_cfg.res.width, dec_res.width);
    TEST_ASSERT_EQUAL(enc_cfg.res.height, dec_res.height);

    esp_h264_free(in_frame.raw_data.buffer);
    esp_h264_free(out_frame.raw_data.buffer);
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_close(enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_del(enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_close(dec));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_del(dec));
}

/* Same round-trip idea as `hw_enc_sw_dec_roundtrip_test`, but swept across several
 * resolutions (min/typical/max-ish, all MB-aligned) and GOP sizes: for every combination,
 * each individual encoded frame must be immediately decodable by the SW decoder, and IDR
 * frames must land exactly on GOP boundaries. */
TEST_CASE("hw_enc_sw_dec_roundtrip_multi_res_gop_test", "[esp_h264]")
{
    typedef struct {
        uint16_t width;
        uint16_t height;
    } res_case_t;
    const res_case_t resolutions[] = {
        { 80, 80 },     /* ESP_H264_MIN_WIDTH/HEIGHT */
        { 160, 128 },
        { 320, 240 },
        { 640, 480 },
    };
    const uint8_t gops[] = { 1, 5, 15 };

    for (size_t ri = 0; ri < sizeof(resolutions) / sizeof(resolutions[0]); ri++) {
        for (size_t gi = 0; gi < sizeof(gops) / sizeof(gops[0]); gi++) {
            uint16_t width = resolutions[ri].width;
            uint16_t height = resolutions[ri].height;
            uint8_t gop = gops[gi];

            esp_h264_enc_cfg_hw_t enc_cfg = { 0 };
            enc_cfg.gop = gop;
            enc_cfg.fps = 30;
            enc_cfg.res.width = width;
            enc_cfg.res.height = height;
            enc_cfg.rc.bitrate = (uint32_t)width * height * enc_cfg.fps / 10;
            enc_cfg.rc.qp_min = 15;
            enc_cfg.rc.qp_max = 30;
            enc_cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

            esp_h264_enc_handle_t enc = NULL;
            TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_hw_new(&enc_cfg, &enc));
            TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_open(enc));

            esp_h264_dec_cfg_sw_t dec_cfg = { 0 };
            dec_cfg.pic_type = ESP_H264_RAW_FMT_I420;
            esp_h264_dec_handle_t dec = NULL;
            TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_sw_new(&dec_cfg, &dec));
            TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_open(dec));

            /* PSRAM for all sizes here: keeps this test independent of internal-RAM budget
             * regardless of which resolution is currently under test. */
            esp_h264_enc_in_frame_t in_frame = { 0 };
            uint32_t in_len = (uint32_t)((float)width * height * ESP_H264_GET_BPP_BY_PIC_TYPE(enc_cfg.pic_type));
            in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len, &in_len, ESP_H264_MEM_SPIRAM);
            in_frame.raw_data.len = in_len;
            TEST_ASSERT_NOT_NULL(in_frame.raw_data.buffer);

            esp_h264_enc_out_frame_t out_frame = { 0 };
            uint32_t out_buf_len = in_len;
            out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_buf_len, &out_buf_len, ESP_H264_MEM_SPIRAM);
            out_frame.raw_data.len = out_buf_len;
            TEST_ASSERT_NOT_NULL(out_frame.raw_data.buffer);

            uint32_t expect_dec_size = (uint32_t)width * height + ((uint32_t)width * height >> 1);
            /* Run past at least one full GOP boundary: IDR, gop-1 P frames, next IDR. */
            int frames_to_run = gop + 2;

            for (int f = 0; f < frames_to_run; f++) {
                int ret_w = read_enc_cb(&in_frame, width, height, enc_cfg.pic_type);
                if (ret_w <= 0) {
                    /* Hit the shared color-table wraparound (index_c resets internally); retry once. */
                    ret_w = read_enc_cb(&in_frame, width, height, enc_cfg.pic_type);
                }
                TEST_ASSERT_GREATER_THAN(0, ret_w);

                TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_process(enc, &in_frame, &out_frame));
                TEST_ASSERT_GREATER_THAN(0, out_frame.length);
                esp_h264_frame_type_t expect_type = ((f % gop) == 0) ? ESP_H264_FRAME_TYPE_IDR : ESP_H264_FRAME_TYPE_P;
                TEST_ASSERT_EQUAL(expect_type, out_frame.frame_type);

                /* The point of this test: encode exactly one frame, decode it immediately. */
                esp_h264_dec_in_frame_t dec_in = { 0 };
                dec_in.raw_data.buffer = out_frame.raw_data.buffer;
                dec_in.raw_data.len = out_frame.length;
                int decoded_this_frame = 0;
                while (dec_in.raw_data.len > 0) {
                    esp_h264_dec_out_frame_t dec_out = { 0 };
                    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_process(dec, &dec_in, &dec_out));
                    TEST_ASSERT_GREATER_THAN(0, dec_in.consume);
                    dec_in.raw_data.buffer += dec_in.consume;
                    dec_in.raw_data.len -= dec_in.consume;
                    if (dec_out.out_size > 0) {
                        TEST_ASSERT_EQUAL(expect_dec_size, dec_out.out_size);
                        decoded_this_frame++;
                    }
                }
                TEST_ASSERT_EQUAL(1, decoded_this_frame);
            }

            esp_h264_free(in_frame.raw_data.buffer);
            esp_h264_free(out_frame.raw_data.buffer);
            TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_close(enc));
            TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_del(enc));
            TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_close(dec));
            TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_del(dec));
        }
    }
}

/* GOP=1 I-frames at 800x800 with a 64-byte-aligned (not 128-byte) out slot.
 * On P4 L2 the CPU slice header and DMA2D first MB share a cache line; a silent
 * M2C miss plus the 4-byte start-code writeback produced
 * `mb_type in I slice too large at 0 0`. Each encoded AU must SW-decode. */
TEST_CASE("hw_enc_800x800_gop1_64b_out_cache_test", "[esp_h264][issue22]")
{
    const uint16_t width = 800;
    const uint16_t height = 800;
    const int frames_to_run = 8;
    const uint32_t out_slot = 65536;
    const char *fail = NULL;
    esp_h264_enc_handle_t enc = NULL;
    esp_h264_dec_handle_t dec = NULL;
    uint8_t *pool = NULL;
    esp_h264_enc_in_frame_t in_frame = { 0 };

    esp_h264_enc_cfg_hw_t enc_cfg = { 0 };
    enc_cfg.gop = 1;
    enc_cfg.fps = 25;
    enc_cfg.res.width = width;
    enc_cfg.res.height = height;
    enc_cfg.rc.bitrate = 400000;
    enc_cfg.rc.qp_min = 26;
    enc_cfg.rc.qp_max = 38;
    enc_cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    if (esp_h264_enc_hw_new(&enc_cfg, &enc) != ESP_H264_ERR_OK ||
            esp_h264_enc_open(enc) != ESP_H264_ERR_OK) {
        fail = "enc new/open";
        goto cleanup;
    }

    esp_h264_dec_cfg_sw_t dec_cfg = { 0 };
    dec_cfg.pic_type = ESP_H264_RAW_FMT_I420;
    if (esp_h264_dec_sw_new(&dec_cfg, &dec) != ESP_H264_ERR_OK ||
            esp_h264_dec_open(dec) != ESP_H264_ERR_OK) {
        fail = "dec new/open";
        goto cleanup;
    }

    uint32_t in_len = (uint32_t)((float)width * height * ESP_H264_GET_BPP_BY_PIC_TYPE(enc_cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len, &in_len, ESP_H264_MEM_SPIRAM);
    in_frame.raw_data.len = in_len;
    if (!in_frame.raw_data.buffer) {
        fail = "in alloc";
        goto cleanup;
    }

    /* 128-aligned pool, then +64 so the encoder sees the reporter's 64B / 64 KB slot. */
    uint32_t pool_len = out_slot + 128;
    pool = (uint8_t *)esp_h264_aligned_calloc(128, 1, pool_len, &pool_len, ESP_H264_MEM_SPIRAM);
    if (!pool) {
        fail = "out alloc";
        goto cleanup;
    }
    uint8_t *out_buf = pool + 64;
    if (((uintptr_t)out_buf % 64) != 0 || ((uintptr_t)out_buf % 128) == 0) {
        fail = "out align";
        goto cleanup;
    }

    esp_h264_enc_out_frame_t out_frame = { 0 };
    out_frame.raw_data.buffer = out_buf;
    out_frame.raw_data.len = out_slot;

    uint32_t expect_dec_size = (uint32_t)width * height + ((uint32_t)width * height >> 1);
    for (int f = 0; f < frames_to_run; f++) {
        if (read_enc_cb(&in_frame, width, height, enc_cfg.pic_type) <= 0) {
            if (read_enc_cb(&in_frame, width, height, enc_cfg.pic_type) <= 0) {
                fail = "fill";
                goto cleanup;
            }
        }
        if (esp_h264_enc_process(enc, &in_frame, &out_frame) != ESP_H264_ERR_OK ||
                out_frame.length == 0 ||
                out_frame.frame_type != ESP_H264_FRAME_TYPE_IDR) {
            fail = "encode";
            goto cleanup;
        }

        esp_h264_dec_in_frame_t dec_in = { 0 };
        dec_in.raw_data.buffer = out_frame.raw_data.buffer;
        dec_in.raw_data.len = out_frame.length;
        int decoded_this_frame = 0;
        while (dec_in.raw_data.len > 0) {
            esp_h264_dec_out_frame_t dec_out = { 0 };
            if (esp_h264_dec_process(dec, &dec_in, &dec_out) != ESP_H264_ERR_OK ||
                    dec_in.consume == 0) {
                fail = "decode";
                goto cleanup;
            }
            dec_in.raw_data.buffer += dec_in.consume;
            dec_in.raw_data.len -= dec_in.consume;
            if (dec_out.out_size > 0) {
                if (dec_out.out_size != expect_dec_size) {
                    fail = "dec size";
                    goto cleanup;
                }
                decoded_this_frame++;
            }
        }
        if (decoded_this_frame != 1) {
            fail = "dec count";
            goto cleanup;
        }
    }

cleanup:
    if (in_frame.raw_data.buffer) {
        esp_h264_free(in_frame.raw_data.buffer);
    }
    if (pool) {
        esp_h264_free(pool);
    }
    if (enc) {
        (void)esp_h264_enc_close(enc);
        (void)esp_h264_enc_del(enc);
    }
    if (dec) {
        (void)esp_h264_dec_close(dec);
        (void)esp_h264_dec_del(dec);
    }
    TEST_ASSERT_NULL_MESSAGE(fail, fail);
}

/* Regression test for the Annex-B `emulation_prevention_three_byte` fix: scan every
 * encoded access unit (IDR with SPS/PPS/slice, and plain P slices) for any forbidden,
 * un-escaped byte sequence. This directly validates the bit-writer's escaping logic
 * without depending on a 3rd-party decoder. */
TEST_CASE("hw_enc_annexb_no_forbidden_sequence_test", "[esp_h264]")
{
    esp_h264_enc_cfg_hw_t cfg = { 0 };
    cfg.gop = 4;
    cfg.fps = 30;
    cfg.res.width = res_width;
    cfg.res.height = res_height;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 10;
    cfg.rc.qp_min = 15;
    cfg.rc.qp_max = 30;
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    esp_h264_enc_handle_t enc = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_hw_new(&cfg, &enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_open(enc));

    esp_h264_enc_in_frame_t in_frame = { 0 };
    uint32_t in_len = (uint32_t)((float)cfg.res.width * cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len, &in_len, MALLOC_CAP_INTERNAL);
    in_frame.raw_data.len = in_len;
    TEST_ASSERT_NOT_NULL(in_frame.raw_data.buffer);

    esp_h264_enc_out_frame_t out_frame = { 0 };
    uint32_t out_buf_len = in_len;
    out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_buf_len, &out_buf_len, MALLOC_CAP_INTERNAL);
    out_frame.raw_data.len = out_buf_len;
    TEST_ASSERT_NOT_NULL(out_frame.raw_data.buffer);

    int frame_count = 0;
    while (frame_count < cfg.gop * 3) {
        int ret_w = read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type);
        if (ret_w <= 0) {
            break;
        }
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_process(enc, &in_frame, &out_frame));
        TEST_ASSERT_GREATER_THAN(0, out_frame.length);
        size_t bad_off = 0;
        bool has_forbidden = esp_h264_test_annexb_has_forbidden_sequence(out_frame.raw_data.buffer, out_frame.length, &bad_off);
        if (has_forbidden) {
            printf("forbidden seq at frame %d (type %d), offset %u of %u\n",
                   frame_count, out_frame.frame_type, (unsigned)bad_off, (unsigned)out_frame.length);
        }
        TEST_ASSERT_FALSE(has_forbidden);
        frame_count++;
    }
    /* Sanity: must have covered at least one full GOP (IDR + several P frames). */
    TEST_ASSERT_GREATER_THAN(1, frame_count);

    esp_h264_free(in_frame.raw_data.buffer);
    esp_h264_free(out_frame.raw_data.buffer);
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_close(enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_del(enc));
}

/* Regression test for the `set_fps` fix: it must regenerate the full SPS+PPS blob
 * (not just the fps field) since the SPS length itself can change with VUI timing_info,
 * and the *next* IDR frame must embed the newly configured fps. */
TEST_CASE("hw_enc_set_fps_regenerates_sps_test", "[esp_h264]")
{
    esp_h264_enc_cfg_hw_t cfg = { 0 };
    cfg.gop = 10;
    cfg.fps = 15;
    cfg.res.width = res_width;
    cfg.res.height = res_height;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    esp_h264_enc_handle_t enc = NULL;
    esp_h264_enc_param_hw_handle_t param_hd = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_hw_new(&cfg, &enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_hw_get_param_hd(enc, &param_hd));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_open(enc));

    esp_h264_enc_in_frame_t in_frame = { 0 };
    uint32_t in_len = (uint32_t)((float)cfg.res.width * cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len, &in_len, MALLOC_CAP_INTERNAL);
    in_frame.raw_data.len = in_len;
    TEST_ASSERT_NOT_NULL(in_frame.raw_data.buffer);

    esp_h264_enc_out_frame_t out_frame = { 0 };
    uint32_t out_buf_len = in_len;
    out_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, out_buf_len, &out_buf_len, MALLOC_CAP_INTERNAL);
    out_frame.raw_data.len = out_buf_len;
    TEST_ASSERT_NOT_NULL(out_frame.raw_data.buffer);

    /* Frame 0: natural IDR, SPS must embed the fps configured at open() time. */
    TEST_ASSERT_GREATER_THAN(0, read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_process(enc, &in_frame, &out_frame));
    TEST_ASSERT_EQUAL(ESP_H264_FRAME_TYPE_IDR, out_frame.frame_type);
    TEST_ASSERT_EQUAL(cfg.fps, esp_h264_test_parse_sps_vui_fps(out_frame.raw_data.buffer, out_frame.length));

    /* Change fps, then force an IDR: the *new* SPS must reflect the updated fps. */
    const uint8_t new_fps = 24;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_set_fps(&param_hd->base, new_fps));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_force_idr(&param_hd->base));
    TEST_ASSERT_GREATER_THAN(0, read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_process(enc, &in_frame, &out_frame));
    TEST_ASSERT_EQUAL(ESP_H264_FRAME_TYPE_IDR, out_frame.frame_type);
    TEST_ASSERT_EQUAL(new_fps, esp_h264_test_parse_sps_vui_fps(out_frame.raw_data.buffer, out_frame.length));

    uint8_t readback_fps = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_get_fps(&param_hd->base, &readback_fps));
    TEST_ASSERT_EQUAL(new_fps, readback_fps);

    esp_h264_free(in_frame.raw_data.buffer);
    esp_h264_free(out_frame.raw_data.buffer);
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_close(enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_del(enc));
}

/* Regression test for the overflow-handling design decision: `ESP_H264_ERR_OVERFLOW` now forces
 * the next frame back to IDR (frame_num is reset), exactly like other (fatal) encode errors.
 * Rationale (see the reference-desync investigation this test replaces): even though the HW's
 * internal reconstructed-picture pipeline completes the whole frame on overflow -- only the
 * *compressed output* was truncated because the caller's buffer was too small -- any external
 * decoder never received that frame's bitstream at all and cannot track the HW's internal
 * reference. Continuing to encode ordinary P frames against it would silently desync every
 * downstream decoder. Forcing a fresh IDR after overflow means a caller that simply discards
 * the truncated frame and keeps calling esp_h264_enc_process gets an automatic, correct
 * resync point on the very next call, with no extra logic required on its part.
 *
 * This test proves both the bookkeeping (frame_type == IDR on the next call) and the practical
 * consequence (that IDR decodes correctly with a decoder whose reference is still the stale,
 * pre-overflow picture -- proving the resync actually works end-to-end, not just in theory). */
TEST_CASE("hw_enc_overflow_forces_idr_test", "[esp_h264]")
{
    /* read_enc_cb walks a 38-entry palette whose second half is the same color.
     * After a full CI suite the next two fills can be identical, so a 640x480 QP=1
     * P-frame collapses to a few hundred bytes and fits the old magic 1200 B slot
     * (which aligned_calloc also rounds up to a cache line). Measure a high-residual
     * P-frame and declare a gentle shortfall instead of a fixed size. */
    const uint32_t overflow_shortfall = 64;
    const uint32_t min_p_len = 256;
    const char *fail = NULL;
    char fail_buf[96];
    esp_h264_enc_handle_t enc = NULL;
    esp_h264_dec_handle_t dec = NULL;
    uint8_t *full_buf = NULL;
    uint8_t *small_buf = NULL;
    esp_h264_enc_in_frame_t in_frame = { 0 };
    esp_h264_enc_out_frame_t out_frame = { 0 };
    uint32_t out_full_size = 0;
    uint32_t expect_dec_size = 0;

    esp_h264_enc_cfg_hw_t cfg = { 0 };
    cfg.gop = 30;
    cfg.fps = 30;
    cfg.res.width = 640;
    cfg.res.height = 480;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 2;
    cfg.rc.qp_min = 1;
    cfg.rc.qp_max = 1;
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;

    if (esp_h264_enc_hw_new(&cfg, &enc) != ESP_H264_ERR_OK ||
            esp_h264_enc_open(enc) != ESP_H264_ERR_OK) {
        fail = "enc new/open";
        goto cleanup;
    }

    esp_h264_dec_cfg_sw_t dec_cfg = { 0 };
    dec_cfg.pic_type = ESP_H264_RAW_FMT_I420;
    if (esp_h264_dec_sw_new(&dec_cfg, &dec) != ESP_H264_ERR_OK ||
            esp_h264_dec_open(dec) != ESP_H264_ERR_OK) {
        fail = "dec new/open";
        goto cleanup;
    }

    uint32_t in_len = (uint32_t)((float)cfg.res.width * cfg.res.height * ESP_H264_GET_BPP_BY_PIC_TYPE(cfg.pic_type));
    in_frame.raw_data.buffer = esp_h264_aligned_calloc(16, 1, in_len, &in_len, ESP_H264_MEM_SPIRAM);
    in_frame.raw_data.len = in_len;
    full_buf = esp_h264_aligned_calloc(16, 1, in_len, &out_full_size, ESP_H264_MEM_SPIRAM);
    out_frame.raw_data.buffer = full_buf;
    out_frame.raw_data.len = out_full_size;
    if (!in_frame.raw_data.buffer || !full_buf) {
        fail = "alloc";
        goto cleanup;
    }

    if (read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type) <= 0 &&
            read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type) <= 0) {
        fail = "fill idr";
        goto cleanup;
    }
    if (esp_h264_enc_process(enc, &in_frame, &out_frame) != ESP_H264_ERR_OK ||
            out_frame.frame_type != ESP_H264_FRAME_TYPE_IDR) {
        fail = "idr0";
        goto cleanup;
    }
    expect_dec_size = (uint32_t)cfg.res.width * cfg.res.height + ((uint32_t)cfg.res.width * cfg.res.height >> 1);
    {
        esp_h264_dec_in_frame_t dec_in0 = { 0 };
        dec_in0.raw_data.buffer = out_frame.raw_data.buffer;
        dec_in0.raw_data.len = out_frame.length;
        int decoded0 = 0;
        while (dec_in0.raw_data.len > 0) {
            esp_h264_dec_out_frame_t dec_out = { 0 };
            if (esp_h264_dec_process(dec, &dec_in0, &dec_out) != ESP_H264_ERR_OK ||
                    dec_in0.consume == 0) {
                fail = "dec0";
                goto cleanup;
            }
            dec_in0.raw_data.buffer += dec_in0.consume;
            dec_in0.raw_data.len -= dec_in0.consume;
            if (dec_out.out_size > 0) {
                if (dec_out.out_size != expect_dec_size) {
                    fail = "dec0 size";
                    goto cleanup;
                }
                decoded0++;
            }
        }
        if (decoded0 != 1) {
            fail = "dec0 count";
            goto cleanup;
        }
    }

    /* XOR the current IDR picture so the next P cannot collapse to skip MBs, even
     * when the palette would have produced the same solid color twice. */
    for (uint32_t i = 0; i < in_frame.raw_data.len; i++) {
        in_frame.raw_data.buffer[i] ^= 0x80;
    }
    if (esp_h264_enc_process(enc, &in_frame, &out_frame) != ESP_H264_ERR_OK ||
            out_frame.frame_type != ESP_H264_FRAME_TYPE_P) {
        fail = "measure p";
        goto cleanup;
    }
    uint32_t p_len = out_frame.length;
    if (p_len <= (min_p_len + overflow_shortfall)) {
        snprintf(fail_buf, sizeof(fail_buf), "p_len %u too small", (unsigned)p_len);
        fail = fail_buf;
        goto cleanup;
    }

    /* Gentle shortfall: close to the real coded size so HW finishes and the
     * driver reports OVERFLOW, not a DMA stall (MEM/TIMEOUT). Declare the
     * intended length; do not use the cache-line-rounded actual allocation. */
    uint32_t overflow_len = p_len - overflow_shortfall;
    uint32_t small_actual = 0;
    small_buf = esp_h264_aligned_calloc(16, 1, overflow_len, &small_actual, ESP_H264_MEM_SPIRAM);
    if (!small_buf) {
        fail = "small alloc";
        goto cleanup;
    }
    out_frame.raw_data.buffer = small_buf;
    out_frame.raw_data.len = overflow_len;

    for (uint32_t i = 0; i < in_frame.raw_data.len; i++) {
        in_frame.raw_data.buffer[i] ^= 0x5a;
    }
    {
        esp_h264_err_t overflow_ret = esp_h264_enc_process(enc, &in_frame, &out_frame);
        if (overflow_ret != ESP_H264_ERR_OVERFLOW) {
            snprintf(fail_buf, sizeof(fail_buf), "overflow ret=%d p=%u decl=%u",
                     (int)overflow_ret, (unsigned)p_len, (unsigned)overflow_len);
            fail = fail_buf;
            goto cleanup;
        }
    }

    out_frame.raw_data.buffer = full_buf;
    out_frame.raw_data.len = out_full_size;

    if (read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type) <= 0 &&
            read_enc_cb(&in_frame, cfg.res.width, cfg.res.height, cfg.pic_type) <= 0) {
        fail = "fill idr2";
        goto cleanup;
    }
    uint8_t expect_y2 = in_frame.raw_data.buffer[1];
    if (esp_h264_enc_process(enc, &in_frame, &out_frame) != ESP_H264_ERR_OK ||
            out_frame.frame_type != ESP_H264_FRAME_TYPE_IDR) {
        fail = "idr after overflow";
        goto cleanup;
    }

    {
        esp_h264_dec_in_frame_t dec_in2 = { 0 };
        dec_in2.raw_data.buffer = out_frame.raw_data.buffer;
        dec_in2.raw_data.len = out_frame.length;
        int decoded2 = 0;
        uint8_t decoded_y2 = 0;
        while (dec_in2.raw_data.len > 0) {
            esp_h264_dec_out_frame_t dec_out = { 0 };
            if (esp_h264_dec_process(dec, &dec_in2, &dec_out) != ESP_H264_ERR_OK ||
                    dec_in2.consume == 0) {
                fail = "dec2";
                goto cleanup;
            }
            dec_in2.raw_data.buffer += dec_in2.consume;
            dec_in2.raw_data.len -= dec_in2.consume;
            if (dec_out.out_size > 0 && dec_out.outbuf != NULL) {
                if (dec_out.out_size != expect_dec_size) {
                    fail = "dec2 size";
                    goto cleanup;
                }
                decoded_y2 = dec_out.outbuf[0];
                decoded2++;
            }
        }
        if (decoded2 != 1) {
            fail = "dec2 count";
            goto cleanup;
        }
        printf("post-overflow forced-IDR decode: expect_y=%u decoded_y=%u p_len=%u decl=%u\n",
               expect_y2, decoded_y2, (unsigned)p_len, (unsigned)overflow_len);
        if (abs((int)decoded_y2 - (int)expect_y2) > 8) {
            fail = "idr y";
            goto cleanup;
        }
    }

cleanup:
    if (in_frame.raw_data.buffer) {
        esp_h264_free(in_frame.raw_data.buffer);
    }
    if (full_buf) {
        esp_h264_free(full_buf);
    }
    if (small_buf) {
        esp_h264_free(small_buf);
    }
    if (enc) {
        (void)esp_h264_enc_close(enc);
        (void)esp_h264_enc_del(enc);
    }
    if (dec) {
        (void)esp_h264_dec_close(dec);
        (void)esp_h264_dec_del(dec);
    }
    TEST_ASSERT_NULL_MESSAGE(fail, fail);
}
#endif //CONFIG_IDF_TARGET_ESP32P4

/* error test */
TEST_CASE("sw_dec_error_test", "[esp_h264]")
{
    esp_h264_dec_cfg_sw_t cfg;
    cfg.pic_type = ESP_H264_RAW_FMT_I420;
    esp_h264_dec_handle_t dec = NULL;

    /* cfg is NULL*/
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_dec_sw_new(NULL, &dec));

    /* dec handle is NULL*/
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_dec_sw_new(&cfg, NULL));

    /* pic_type isn't ESP_H264_RAW_FMT_I420*/
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_dec_sw_new(&cfg, NULL));

    cfg.pic_type = ESP_H264_RAW_FMT_I420;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_sw_new(&cfg, &dec));

    esp_h264_dec_param_handle_t param_hd = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_sw_get_param_hd(dec, &param_hd));

    /* get_resolution: param_hd is NULL */
    esp_h264_resolution_t res;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_dec_get_resolution(NULL, &res));

    /* get_resolution: resolution is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_dec_get_resolution(param_hd, NULL));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_dec_open(NULL));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_open(dec));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_dec_close(NULL));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_close(dec));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_dec_del(NULL));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_dec_del(dec));
}

/* configure test */
TEST_CASE("sw_dec_data_error_test", "[esp_h264]")
{
    uint8_t *yuv = NULL;
    esp_h264_dec_cfg_sw_t cfg;
    cfg.pic_type = ESP_H264_RAW_FMT_I420;

    //start code error
    uint32_t inbuf_len = 4;
    uint8_t inbuf[30] = {0x00, 0x00, 0x00, 0x02};
    TEST_ASSERT_EQUAL(ESP_H264_ERR_FAIL, single_sw_dec_process(cfg, inbuf, inbuf_len, yuv));

    //forbidden_zero_bit is not 0
    inbuf_len = 5;
    inbuf[3] = 0x01;
    inbuf[4] = 0xFF;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_FAIL, single_sw_dec_process(cfg, inbuf, inbuf_len, yuv));

    //nal_uint_type is error
    inbuf_len = 5;
    inbuf[3] = 0x01;
    inbuf[4] = 0x63;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_FAIL, single_sw_dec_process(cfg, inbuf, inbuf_len, yuv));

    //profile id is error
    inbuf_len = 6;
    inbuf[3] = 0x01;
    inbuf[4] = 0x67;
    inbuf[5] = 0x43;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_FAIL, single_sw_dec_process(cfg, inbuf, inbuf_len, yuv));

    //level id is error
    inbuf_len = 7;
    inbuf[5] = 0x42;
    inbuf[6] = 0xff;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_FAIL, single_sw_dec_process(cfg, inbuf, inbuf_len, yuv));

    //SPS ID is greater than 31
    inbuf_len = 8;
    inbuf[6] = 0xc0;
    inbuf[7] = 0xff;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_FAIL,  single_sw_dec_process(cfg, inbuf, inbuf_len, yuv));

    //SPS error
    inbuf_len = 10;
    inbuf[7] = 0x0b;
    inbuf[8] = 0x8c;
    inbuf[9] = 0x8d;
    inbuf[10] = 0x4f;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_FAIL, single_sw_dec_process(cfg, inbuf, inbuf_len, yuv));

    //entropy_coding_mode_flag error
    inbuf_len = 30;
    uint8_t sps[50] = {0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0b, 0x8c, 0x8d, 0x41, 0x02, 0x24, 0x03, 0xc2, 0x21, 0x1a, 0x80, 0x00, 0x00, 0x00, 0x01, 0x68};
    sps[23] = 0xff;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_FAIL, single_sw_dec_process(cfg, sps, inbuf_len, yuv));

    //weighted_bipred_idc error
    inbuf_len = 30;
    sps[23] = 0xCE;
    sps[24] = 0xff;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_FAIL, single_sw_dec_process(cfg, sps, inbuf_len, yuv));

    printf("Slice header error testing. \n");
    //ACCESS UNIT BOUNDARY CHECK
    inbuf_len = 50;
    sps[24] = 0x3c;
    sps[25] = 0x80;
    sps[26] = 0x00;
    sps[27] = 0x00;
    sps[28] = 0x00;
    sps[29] = 0x01;
    sps[30] = 0x65;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_FAIL, single_sw_dec_process(cfg, sps, inbuf_len, yuv));

    //Slice data error
    sps[31] = 0xB8;
    sps[32] = 0x00;
    sps[33] = 0x00;
    sps[34] = 0x50;
    sps[35] = 0x8c;
    sps[36] = 0x56;
    sps[37] = 0x38;
    sps[38] = 0x00;
    sps[39] = 0x19;
    sps[40] = 0xff;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_FAIL, single_sw_dec_process(cfg, sps, inbuf_len, yuv));
}

TEST_CASE("sw_enc_set_get_param_single_thread_test", "[esp_h264]")
{
    esp_h264_enc_cfg_sw_t cfg = { 0 };
    cfg.gop = 5;
    cfg.fps = 30;
    cfg.res.width = res_width;
    cfg.res.height = res_height;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = ESP_H264_RAW_FMT_I420;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_sw_enc_thread_test(cfg));
}

TEST_CASE("sw_enc_single_hw_enc_gop_test", "[esp_h264]")
{
    for (int16_t gop = 1; gop < 256; gop++) {
        esp_h264_enc_cfg_sw_t cfg = { 0 };
        cfg.gop = gop;
        cfg.fps = 30;
        cfg.res.width = res_width;
        cfg.res.height = res_height;
        cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
        cfg.rc.qp_min = 26;
        cfg.rc.qp_max = 26;
        cfg.pic_type = ESP_H264_RAW_FMT_I420;
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_sw_enc_thread_test(cfg));
    }
}

TEST_CASE("sw_enc_single_hw_enc_fps_test", "[esp_h264]")
{
    for (int16_t fps = 1; fps < 256; fps++) {
        esp_h264_enc_cfg_sw_t cfg = { 0 };
        cfg.gop = 5;
        cfg.fps = fps;
        cfg.res.width = res_width;
        cfg.res.height = res_height;
        cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
        cfg.rc.qp_min = 26;
        cfg.rc.qp_max = 26;
        cfg.pic_type = ESP_H264_RAW_FMT_I420;
        TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_sw_enc_thread_test(cfg));
    }
}

TEST_CASE("sw_enc_single_hw_enc_pic_type_test", "[esp_h264]")
{
    esp_h264_enc_cfg_sw_t cfg = { 0 };
    cfg.gop = 5;
    cfg.fps = 30;
    cfg.res.width = res_width;
    cfg.res.height = res_height;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = ESP_H264_RAW_FMT_YUYV;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, single_sw_enc_thread_test(cfg));
}

/* error test */
TEST_CASE("sw_enc_error_test", "[esp_h264]")
{
    esp_h264_enc_cfg_sw_t cfg = { 0 };
    esp_h264_enc_handle_t enc = NULL;
    cfg.gop = 255;
    cfg.fps = 30;
    cfg.res.width = res_width;
    cfg.res.height = res_height;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = ESP_H264_RAW_FMT_I420;

    /* cfg is NULL*/
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_sw_new(NULL, &enc));

    /* enc handle is NULL*/
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_sw_new(&cfg, NULL));

    /* pic_type is ESP_H264_RAW_FMT_O_UYY_E_VYY */
    cfg.pic_type = ESP_H264_RAW_FMT_O_UYY_E_VYY;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_sw_new(&cfg, &enc));
    cfg.pic_type = ESP_H264_RAW_FMT_I420;

    /* width is less than 16 */
    cfg.res.width = 15;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_sw_new(&cfg, &enc));
    cfg.res.width = res_width;

    /* height is less than 15 */
    cfg.res.height = 15;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_sw_new(&cfg, &enc));
    cfg.res.height = res_height;

    /* qp_min is greater than 51 */
    cfg.rc.qp_min = 52;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_sw_new(&cfg, &enc));
    cfg.rc.qp_min = 26;

    /* qp_max is greater than 51 */
    cfg.rc.qp_max = 52;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_sw_new(&cfg, &enc));

    /* qp_min is greater than qp_max */
    cfg.rc.qp_max = cfg.rc.qp_min - 1;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_sw_new(&cfg, &enc));
    cfg.rc.qp_max = 26;

    /* GOP is 0 */
    cfg.gop = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_sw_new(&cfg, &enc));
    cfg.gop = 255;

    /* FPS is 0 */
    cfg.fps = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_sw_new(&cfg, &enc));
    cfg.fps = 30;

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_sw_new(&cfg, &enc));
    // printf("ret %d\n", esp_h264_enc_sw_new(&cfg, &enc));

    esp_h264_enc_param_handle_t param_hd = NULL;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_sw_get_param_hd(enc, &param_hd));

    /* get_resolution: param_hd is NULL */
    esp_h264_resolution_t res;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_resolution(NULL, &res));

    /* get_resolution: resolution is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_resolution(param_hd, NULL));

    /* set_fps: param_hd is NULL */
    uint8_t fps;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_set_fps(NULL, fps));

    /* get_fps: param_hd is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_fps(NULL, &fps));

    /* get_fps: fps is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_fps(param_hd, NULL));

    /* set_gop: param_hd is NULL */
    uint8_t gop;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_set_gop(NULL, gop));

    /* get_gop: param_hd is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_gop(NULL, &gop));

    /* get_gop: gop is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_gop(param_hd, NULL));

    /* set_bitrate: param_hd is NULL */
    esp_h264_enc_rc_t rc = {
        .qp_min = 26,
        .qp_max = 27,
    };
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_set_bitrate(NULL, rc.bitrate));

    /* get_bitrate: param_hd is NULL  */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_bitrate(NULL, &rc.bitrate));

    /* get_bitrate: bitrate is NULL  */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_get_bitrate(param_hd, NULL));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_open(NULL));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_open(enc));

    esp_h264_enc_in_frame_t in_frame = {
        .raw_data.buffer = (uint8_t *)1,
        .raw_data.len = 1,
    };
    esp_h264_enc_out_frame_t out_frame = {
        .raw_data.buffer = (uint8_t *)1,
        .raw_data.len = 1,
    };

    /* enc is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_process(NULL, &in_frame, &out_frame));
    /* in_frame is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_process(enc, NULL, &out_frame));
    /* in_frame.raw_data.buffer is NULL */
    in_frame.raw_data.buffer = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_process(enc, &in_frame, &out_frame));
    in_frame.raw_data.buffer = (uint8_t *)1;

    /* out_frame is NULL */
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_process(enc, &in_frame, NULL));
    /* out_frame.raw_data.buffer is NULL */
    out_frame.raw_data.buffer = 0;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_process(enc, &in_frame, &out_frame));
    out_frame.raw_data.buffer = (uint8_t *)1;

    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_close(NULL));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_close(enc));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_del(NULL));

    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_del(enc));
}

/** ESP_H264_SW_MIN_WIDTH/HEIGHT are both 16, and the resolution check must accept the
 *  minimum value itself, not only strictly-greater values. This only exercises
 *  `esp_h264_enc_sw_new`'s resolution validation, not the full encode pipeline, so it
 *  isn't sensitive to output-buffer sizing at such a tiny resolution. */
TEST_CASE("sw_enc_min_resolution_boundary_test", "[esp_h264]")
{
    esp_h264_enc_cfg_sw_t cfg = { 0 };
    cfg.gop = 5;
    cfg.fps = 30;
    cfg.rc.qp_min = 26;
    cfg.rc.qp_max = 26;
    cfg.pic_type = ESP_H264_RAW_FMT_I420;

    /* width == ESP_H264_SW_MIN_WIDTH, height == ESP_H264_SW_MIN_HEIGHT: must succeed */
    esp_h264_enc_handle_t enc = NULL;
    cfg.res.width = 16;
    cfg.res.height = 16;
    cfg.rc.bitrate = cfg.res.width * cfg.res.height * cfg.fps / 20;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_sw_new(&cfg, &enc));
    TEST_ASSERT_EQUAL(ESP_H264_ERR_OK, esp_h264_enc_del(enc));

    /* width == ESP_H264_SW_MIN_WIDTH - 1: must still fail */
    cfg.res.width = 15;
    cfg.res.height = 16;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_sw_new(&cfg, &enc));

    /* height == ESP_H264_SW_MIN_HEIGHT - 1: must still fail */
    cfg.res.width = 16;
    cfg.res.height = 15;
    TEST_ASSERT_EQUAL(ESP_H264_ERR_ARG, esp_h264_enc_sw_new(&cfg, &enc));
}
