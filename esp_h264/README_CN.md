# ESP_H264

[![Component Registry](https://components.espressif.com/components/espressif/esp_h264/badge.svg)](https://components.espressif.com/components/espressif/esp_h264)

[[English]](./README.md)

ESP_H264 是乐鑫轻量级 H.264 编解码组件，提供硬件与软件两种实现。硬件编码器（HW encoder）面向 ESP32-P4，1080P 可达 30fps 以上。软件编码器（SW encoder）基于 [openh264](https://github.com/cisco/openh264) v2.2.0，解码器基于 [tinyH264](https://github.com/udevbe/tinyh264)。软编/软解均针对乐鑫芯片做了内存与 CPU 优化。

## 术语

| 缩写       | 全称                    | 说明                                                         |
| ---------- | ----------------------- | ------------------------------------------------------------ |
| QP         | Quantization parameter  | QP 越大，压缩率越高、画质越差；范围为 0–51                   |
| FPS        | Frames per second       | 帧率，与流畅度相关；普通视频可设为 24                        |
| I-frame    | Intra frame             | 无需参考其他帧即可编码的帧                                   |
| IDR-frame  | Instantaneous Decoder Refresh frame | 特殊 I 帧；解码器可从此帧开始解码                   |
| P-frame    | Predicted frame         | 需参考其他帧才能解码的帧                                     |
| GOP        | Group of pictures       | 一个 I 帧加上两个 I 帧之间的帧数；通常设为输出 FPS           |
| Resolution | Resolution              | 图像宽高                                                     |
| MB         | Macro block             | 亮度宏块 16×16，色度 8×8；`mb_width = (width + 15) >> 4`     |
| Slice      | Slice                   | 由多个宏块组成                                               |
| MV         | Motion vector           | 当前宏块相对前一帧最佳匹配宏块的位移                         |
| ROI        | Region of interest      | 感兴趣区域，可配置不同 QP；量化单位为亮度宏块大小            |
| SPS        | Sequence parameter set  | 开始解码所需                                                   |
| PPS        | Picture parameter set   | 开始解码所需                                                   |
| PTS        | Presentation time stamp | Baseline 下与 DTS 相同                                       |
| DTS        | Decoding time stamp     | Baseline 下与 PTS 相同                                       |
| RC         | Rate control            | 控制输出码流接近目标码率                                     |

## 支持芯片

| ESP_H264 版本 | ESP32-S3  | ESP32-S31 | ESP32-P4  |
| ------------- | --------- | --------- | --------- |
| v1.3.8        | 支持      | 支持      | 支持      |

## 功能

### 编码器

| 功能                | HW 编码器                                                         | SW 编码器                               |
| ------------------- | ----------------------------------------------------------------- | --------------------------------------- |
| profile             | Baseline                                                          | Baseline                                |
| width               | 80–1920                                                           | ≥ 16                                    |
| height              | 80–2032                                                           | ≥ 16                                    |
| QP                  | 全部支持                                                          | 全部支持                                |
| FPS                 | 1–255                                                             | 1–255                                   |
| GOP                 | 1–255                                                             | 1–255                                   |
| Force IDR           | 支持，`esp_h264_enc_force_idr()`                                  | 不支持                                  |
| SPS                 | 每个 IDR 均带 SPS                                                 | 每个 IDR 均带 SPS                       |
| PPS                 | 每个 IDR 均带 PPS                                                 | 每个 IDR 均带 PPS                       |
| 未编码数据类型      | O_UYY_E_VYY / VUY / UYVY / BGR888 / RGB565_LE                     | YUYV / I420                             |
| RC                  | 支持                                                              | 支持                                    |
| de-blocking filter  | 支持                                                              | 支持                                    |
| 单路流              | 支持                                                              | 支持                                    |
| 双路流              | 除 GOP 外各路可独立配置                                           | 不支持                                  |
| ROI                 | 区域数 ≤ 8；支持固定/增量 QP；非 ROI 支持增量 QP                  | 不支持                                  |
| MV                  | 支持输出 MV                                                       | 不支持                                  |

### 解码器

| 功能                                         | SW 解码器                                      |
| -------------------------------------------- | ---------------------------------------------- |
| profile                                      | Constrained Baseline                           |
| width / height                               | ≥ 16                                           |
| slice group                                  | 1                                              |
| QP / FPS / GOP / SPS / PPS                   | 支持                                           |
| 未编码数据类型                               | I420                                           |
| LTR / MMCO / 参考帧重排                      | 支持                                           |
| 双任务解码                                   | 支持，可通过 menuconfig 配置核与优先级         |

## 性能

### ESP32-S3R8

#### SW 编码

| 分辨率     | 原始格式                  | 内存 (Byte) | fps   |
| ---------- | ------------------------- | ----------- | ----- |
| 320 × 192  | ESP_H264_RAW_FMT_I420     | 1 M         | 17.48 |
| 320 × 240  | ESP_H264_RAW_FMT_YUYV     | 1 M         | 11.23 |

#### 解码

内存与码流分辨率、编码数据强相关。

单任务解码：

| 分辨率     | 原始格式              | 内存 (Byte) | fps |
| ---------- | --------------------- | ----------- | --- |
| 640 × 480  | ESP_H264_RAW_FMT_I420 | 2.5 M       | 9   |
| 320 × 192  | ESP_H264_RAW_FMT_I420 | 1.0 M       | 23  |

双任务解码：

| 分辨率     | 原始格式              | 内存 (Byte) | fps |
| ---------- | --------------------- | ----------- | --- |
| 640 × 480  | ESP_H264_RAW_FMT_I420 | 2.5 M       | 11  |
| 320 × 192  | ESP_H264_RAW_FMT_I420 | 1.0 M       | 27  |

### ESP32-P4

#### HW 编码

| 分辨率      | 原始格式                     | 内存 (Byte) | fps |
| ----------- | ---------------------------- | ----------- | --- |
| 1920 × 1080 | ESP_H264_RAW_FMT_O_UYY_E_VYY | 140k        | 30  |

近似估算：

`fps_cur ≈ fps_1080p × (当前分辨率像素 × 每像素字节) ÷ (1920 × 1080 × 1080p 每像素字节)`

#### 解码

单任务：

| 分辨率      | 原始格式              | 内存 (Byte) | fps |
| ----------- | --------------------- | ----------- | --- |
| 1280 × 720  | ESP_H264_RAW_FMT_I420 | 6.2 M       | 7   |
| 640 × 480   | ESP_H264_RAW_FMT_I420 | 2.5 M       | 25  |

双任务：

| 分辨率      | 原始格式              | 内存 (Byte) | fps |
| ----------- | --------------------- | ----------- | --- |
| 1280 × 720  | ESP_H264_RAW_FMT_I420 | 6.2 M       | 10  |
| 640 × 480   | ESP_H264_RAW_FMT_I420 | 2.5 M       | 31  |

## 示例

API 用法详见 `test_apps/esp_h264_*_test.c` 与 `test_apps/esp_h264_*_test.h`。

# FAQ

## 性能问题

### Q: 为什么 ESP32-P4 上解码偏慢？

**A:** 解码性能受多种因素影响：

- **分辨率**：分辨率越高，算力需求越大
- **任务配置**：双任务解码通常更快
- **内存**：需保证 SPIRAM 充足
- **码流复杂度**：复杂流解码更耗时

优化建议：

- 启用双任务解码（menuconfig 配置核与优先级）
  ```
  ESP_H264_DECODER_IRAM=1
  ESP_H264_DUAL_TASK=1
  ```
- 在可接受范围内降低分辨率（如 1280×720 改为 640×480）
- 保证内存分配充足
- 确认码流为支持的 Constrained Baseline profile
