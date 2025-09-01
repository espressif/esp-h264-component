/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "h264_io.h"

#define CLAMP(x) ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))
#define set_bgr565_be(r, g, b, rgb) {                       \
    (rgb)[0] = ((b) & (uint32_t)0xf8) | ((g) >> 5);         \
    (rgb)[1] = (((g) & (uint32_t)0x1c) << 3) | ((r) >> 3);  \
}

static void yuv_to_rgb(uint8_t y, uint8_t u, uint8_t v, uint8_t *r, uint8_t *g, uint8_t *b)
{
    int c = y - 16;
    int d = u - 128;
    int e = v - 128;
    int r_temp = (298 * c + 409 * e + 128) >> 8;            // R = Y + 1.403 * (V-128)
    int g_temp = (298 * c - 100 * d - 208 * e + 128) >> 8;  // G = Y - 0.344 * (U-128) - 0.714 * (V-128)
    int b_temp = (298 * c + 516 * d + 128) >> 8;            // B = Y + 1.770 * (U-128)
    *r = CLAMP(r_temp);
    *g = CLAMP(g_temp);
    *b = CLAMP(b_temp);
}

const uint8_t yuv_table[38][3] = {
    { 0xd6, 0x71, 0xb8 },
    { 0x7e, 0x6a, 0xa0 },
    { 0x41, 0x81, 0x78 },
    { 0xde, 0x39, 0x92 },
    { 0x66, 0xa1, 0x71 },
    { 0x61, 0x71, 0xc5 },
    { 0x99, 0x73, 0x93 },
    { 0x3b, 0x74, 0x98 },
    { 0x57, 0x89, 0x7c },
    { 0x88, 0x57, 0x9b },
    { 0x48, 0x93, 0x7b },
    { 0x51, 0x5e, 0x61 },
    { 0x89, 0x9d, 0x7e },
    { 0x33, 0x70, 0xb4 },
    { 0xde, 0x39, 0x92 },
    { 0x66, 0xa1, 0x71 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x61, 0x71, 0xc5 },
    { 0x99, 0x73, 0x93 },
    { 0x66, 0xa1, 0x71 },
    { 0x61, 0x71, 0xc5 },
    { 0x3b, 0x74, 0x98 },
    { 0x57, 0x89, 0x7c },
};

static int index_c = 0;
#define COLOR_NUM (sizeof(yuv_table) / sizeof(yuv_table[0]))

static int read_enc_cb_ouev(esp_h264_enc_in_frame_t *frame, int16_t width, int16_t height)
{
    index_c++;
    if (index_c > COLOR_NUM) {
        index_c = 0;
        return -1;
    }
    int cor_idx = index_c % COLOR_NUM;
    uint8_t y = yuv_table[cor_idx][0];
    uint8_t u = yuv_table[cor_idx][1];
    uint8_t v = yuv_table[cor_idx][2];
    uint8_t *yuv = frame->raw_data.buffer;
    for (int j = 0; j < height; j += 2) {
        for (int i = 0; i < width; i += 16) {
            *yuv++ = u; // U0
            *yuv++ = y; // Y0
            *yuv++ = y; // Y1
            *yuv++ = u; // U1
            *yuv++ = y; // Y2
            *yuv++ = y; // Y3
            *yuv++ = u; // U2
            *yuv++ = y; // Y4

            *yuv++ = y; // Y5
            *yuv++ = u; // U3
            *yuv++ = y; // Y6
            *yuv++ = y; // Y7
            *yuv++ = u; // U4
            *yuv++ = y; // Y8
            *yuv++ = y; // Y9
            *yuv++ = u; // U5

            *yuv++ = y; // Y10
            *yuv++ = y; // Y11
            *yuv++ = u; // U6
            *yuv++ = y; // Y12
            *yuv++ = y; // Y13
            *yuv++ = u; // U7
            *yuv++ = y; // Y14
            *yuv++ = y; // Y15
        }
        for (int i = 0; i < width; i += 16) {
            *yuv++ = v; // V0
            *yuv++ = y; // Y0
            *yuv++ = y; // Y1
            *yuv++ = v; // V1
            *yuv++ = y; // Y2
            *yuv++ = y; // Y3
            *yuv++ = v; // V2
            *yuv++ = y; // Y4

            *yuv++ = y; // Y5
            *yuv++ = v; // V3
            *yuv++ = y; // Y6
            *yuv++ = y; // Y7
            *yuv++ = v; // V4
            *yuv++ = y; // Y8
            *yuv++ = y; // Y9
            *yuv++ = v; // V5

            *yuv++ = y; // Y10
            *yuv++ = y; // Y11
            *yuv++ = v; // V6
            *yuv++ = y; // Y12
            *yuv++ = y; // Y13
            *yuv++ = v; // V7
            *yuv++ = y; // Y14
            *yuv++ = y; // Y15
        }
    }
    return 1;
}

static int read_enc_cb_vuy(esp_h264_enc_in_frame_t *frame, int16_t width, int16_t height)
{
    index_c++;
    if (index_c > COLOR_NUM) {
        index_c = 0;
        return -1;
    }
    int cor_idx = index_c % COLOR_NUM;
    uint8_t y = yuv_table[cor_idx][0];
    uint8_t u = yuv_table[cor_idx][1];
    uint8_t v = yuv_table[cor_idx][2];
    uint8_t *yuv = frame->raw_data.buffer;
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            *yuv++ = v;  // V0
            *yuv++ = u;  // U0
            *yuv++ = y;  // Y0
        }
    }
    return 1;
}

static int read_enc_cb_bgr888(esp_h264_enc_in_frame_t *frame, int16_t width, int16_t height)
{
    index_c++;
    if (index_c > COLOR_NUM) {
        index_c = 0;
        return -1;
    }
    int cor_idx = index_c % COLOR_NUM;
    uint8_t y = yuv_table[cor_idx][0];
    uint8_t u = yuv_table[cor_idx][1];
    uint8_t v = yuv_table[cor_idx][2];
    uint8_t *rgb = frame->raw_data.buffer;
    for (int j = 0; j < height; j ++) {
        for (int i = 0; i < width; i ++) {
            yuv_to_rgb(y, u, v, rgb + 0, rgb + 1, rgb + 2);
            rgb += 3;
        }
    }
    return 1;
}

static int read_enc_cb_bgr565_be(esp_h264_enc_in_frame_t *frame, int16_t width, int16_t height)
{
    index_c++;
    if (index_c > COLOR_NUM) {
        index_c = 0;
        return -1;
    }
    int cor_idx = index_c % COLOR_NUM;
    uint8_t y = yuv_table[cor_idx][0];
    uint8_t u = yuv_table[cor_idx][1];
    uint8_t v = yuv_table[cor_idx][2];
    uint8_t *rgb = frame->raw_data.buffer;
    for (int j = 0; j < height; j ++) {
        for (int i = 0; i < width; i ++) {
            uint8_t r, g, b;
            yuv_to_rgb(y, u, v, &r, &g, &b);
            set_bgr565_be(r, g, b, rgb);
            rgb += 2;
        }
    }
    return 1;
}

// This is the function for generating grayscale images. It is not used for now and will be added in a future version.
// static int read_enc_cb_grey(esp_h264_enc_in_frame_t *frame, int16_t width, int16_t height)
// {
//     index_c++;
//     if (index_c > COLOR_NUM) {
//         index_c = 0;
//         return -1;
//     }
//     int cor_idx = index_c % COLOR_NUM;
//     uint8_t y = yuv_table[cor_idx][0];
//     uint8_t *grey = frame->raw_data.buffer;
//     for (int j = 0; j < height; j ++) {
//         for (int i = 0; i < width; i ++) {
//             *grey++ = y;
//         }
//     }
//     return 1;
// }

static int read_enc_cb_uyvy(esp_h264_enc_in_frame_t *frame, int16_t width, int16_t height)
{
    index_c++;
    if (index_c > COLOR_NUM) {
        index_c = 0;
        return -1;
    }
    int cor_idx = index_c % COLOR_NUM;
    uint8_t y = yuv_table[cor_idx][0];
    uint8_t u = yuv_table[cor_idx][1];
    uint8_t v = yuv_table[cor_idx][2];
    uint8_t *uyvy = frame->raw_data.buffer;
    for (int j = 0; j < height; j ++) {
        for (int i = 0; i < width; i += 2) {
            *uyvy++ = u;
            *uyvy++ = y;
            *uyvy++ = v;
            *uyvy++ = y;
        }
    }
    return 1;
}

static int read_enc_cb_yuyv(esp_h264_enc_in_frame_t *frame, int16_t width, int16_t height)
{
    if (index_c > COLOR_NUM) {
        index_c = 0;
        return -1;
    }
    int cor_idx = index_c % COLOR_NUM;
    uint8_t y = yuv_table[cor_idx][0];
    uint8_t u = yuv_table[cor_idx][1];
    uint8_t v = yuv_table[cor_idx][2];
    uint8_t *yuyv = frame->raw_data.buffer;
    for (int j = 0; j < height; j ++) {
        for (int i = 0; i < width; i += 2) {
            *yuyv++ = y;
            *yuyv++ = u;
            *yuyv++ = y;
            *yuyv++ = v;
        }
    }
    index_c++;
    return 1;
}

static int read_enc_cb_i420(esp_h264_enc_in_frame_t *frame, int16_t width, int16_t height)
{
    index_c++;
    if (index_c > COLOR_NUM) {
        index_c = 0;
        return -1;
    }
    int cor_idx = index_c % COLOR_NUM;
    uint8_t y = yuv_table[cor_idx][0];
    uint8_t u = yuv_table[cor_idx][1];
    uint8_t v = yuv_table[cor_idx][2];
    uint8_t *yuv = frame->raw_data.buffer;
    for (int j = 0; j < height * width; j++) {
        *yuv++ = y; // Y
    }
    for (int j = 0; j < height *width >> 2; j++) {
        *yuv++ = u; // u
    }
    for (int j = 0; j < height *width >> 2; j++) {
        *yuv++ = v; // v
    }
    return 1;
}

int read_enc_cb(esp_h264_enc_in_frame_t *frame, int16_t width, int16_t height, esp_h264_raw_format_t format)
{
    switch (format) {
    // case ESP_H264_RAW_FMT_GREY:
    //     return read_enc_cb_grey(frame, width, height);
    case ESP_H264_RAW_FMT_BGR888:
        return read_enc_cb_bgr888(frame, width, height);
    case ESP_H264_RAW_FMT_BGR565_BE:
        return read_enc_cb_bgr565_be(frame, width, height);
    case ESP_H264_RAW_FMT_VUY:
        return read_enc_cb_vuy(frame, width, height);
    case ESP_H264_RAW_FMT_UYVY:
        return read_enc_cb_uyvy(frame, width, height);
    case ESP_H264_RAW_FMT_YUYV:
        return read_enc_cb_yuyv(frame, width, height);
    case ESP_H264_RAW_FMT_I420:
        return read_enc_cb_i420(frame, width, height);
    case ESP_H264_RAW_FMT_O_UYY_E_VYY:
        return read_enc_cb_ouev(frame, width, height);
    default:
        return -1;
    }
}

int write_enc_cb(esp_h264_enc_out_frame_t *frame)
{
    return 1;
}

int read_dec_cd(uint8_t *inbuf, uint32_t inbuf_len, esp_h264_dec_in_frame_t *frame)
{
    frame->raw_data.buffer = inbuf;
    frame->raw_data.len = inbuf_len;
    return 1;
}

int write_dec_cd(esp_h264_dec_out_frame_t *frame, uint8_t *yuv)
{
    return frame->out_size;
}
