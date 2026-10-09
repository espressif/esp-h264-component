# 双码流单帧编码（PPA 独立任务）

- [English](./README.md)
- 中级例程: ⭐⭐

## 例程简介

- 一个软件彩条 generator 只产生一帧 O_UYY_E_VYY。独立的 PPA 任务把这一帧缩小后给码流 0。编码任务再按 0、1 的顺序调用 `esp_h264_enc_dual_process_one()`。
- 使用 ESP32-P4 双路硬件编码器（`esp_h264_enc_dual_hw_new`）和 PPA SRM（`ppa_do_scale_rotate_mirror`）。每路每秒打印一次帧率，结束时打印平均帧率。

### 典型场景

同一路输入做主码流 + 预览/子码流。PPA 缩小和硬件编码并行，每一路单独送一帧。

### 运行流程

```
generator（一路 O_UYY_E_VYY）
        │
        ▼
PPA 任务（SRM 缩小）
        │
        ▼
编码任务
        ├── esp_h264_enc_dual_process_one(0) ──► H.264 码流 0（缩小后）
        └── esp_h264_enc_dual_process_one(1) ──► H.264 码流 1（原始尺寸）
```

同一个句柄上的调用必须保持 0、1、0、1。

### 文件结构

```
hw_dual_enc_ppa_one/
├── main/
│   ├── hw_dual_enc_ppa_one_main.c
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── Kconfig.projbuild
├── CMakeLists.txt
├── pytest_hw_dual_enc_ppa_one.py
├── README.md
└── README_CN.md
```

共用彩条：`esp_h264/examples/common/pattern_input`。

## 环境准备

### 硬件要求

- 带 PSRAM 的 ESP32-P4 开发板（例如 ESP32-P4 Function EV Board）
- USB Serial/JTAG 用于查看日志

### 默认 IDF 分支

本例程需要 IDF release/v5.5.3 及更新版本，以及 ESP32-P4 rev v3.1（ECO5）及以上。

## 编译和烧录

### 编译准备

编译前请先完成 ESP-IDF 环境配置。若已配置，可直接进入工程目录。否则在 ESP-IDF 根目录执行：

```
./install.sh
. ./export.sh
```

完整步骤见 [ESP-IDF 编程指南](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32p4/index.html)。

```
cd esp_h264/examples/hw_dual_enc_ppa_one
idf.py set-target esp32p4
```

### 工程配置

```
idf.py menuconfig
```

在 `Example Configuration` 中配置：

- `Generator width (pixels)`
- `Generator height (pixels)`
- `PPA scale numerator` / `PPA scale denominator`（默认 1/2）
- `Fill generator frames in realtime`（关闭则只填 2 帧后循环）
- `Measurement duration (seconds)`

配置完成后按 `s` 保存，按 `Esc` 退出。

### 编译和烧录命令

- 编译：

```
idf.py build
```

- 烧录并打开串口监视器（将 PORT 换成实际端口）：

```
idf.py -p PORT flash monitor
```

- 退出监视器：`Ctrl-]`

## 如何使用

### 功能说明

Generator 填好 2 帧彩条后循环使用。PPA 任务在 core 1 上缩小。编码任务在 core 0 上先编码缩小后的码流 0，再编码原始尺寸的码流 1。码流数据丢弃，日志打印编码帧率和 PPA 帧率。

### 日志输出

```
I (797) dual_ppa_one: Demo: gen core1, PPA core1, enc core0 realtime=0
I (798) dual_ppa_one: stream0 PPA 960x544, stream1 src 1920x1088
I (885) dual_ppa_one: PPA task on core 1
I (885) dual_ppa_one: encode task on core 0
I (885) dual_ppa_one: generator task on core 1 realtime=0
I (1021) dual_ppa_one: filled 2 frames, recycle without refill
I (1898) dual_ppa_one: stream0 (PPA 960x544) fps=15.80
I (1899) dual_ppa_one: stream1 (src 1920x1088) fps=15.80
I (1935) dual_ppa_one: PPA scale 1920x1088 -> 960x544 fps=17.15
I (2908) dual_ppa_one: stream0 (PPA 960x544) fps=17.83
I (2908) dual_ppa_one: stream1 (src 1920x1088) fps=17.83
I (2944) dual_ppa_one: PPA scale 1920x1088 -> 960x544 fps=17.83
I (8895) dual_ppa_one: average stream0 (PPA 960x544) fps=17.48 frames=140
I (8895) dual_ppa_one: average stream1 (src 1920x1088) fps=17.48 frames=140
I (8896) dual_ppa_one: demo finished
I (8905) dual_ppa_one: generator task exit
I (8918) dual_ppa_one: PPA task exit frames=142
```

第一秒之后编码帧率稳定在 17.57 或 17.83。8 秒平均是 17.48 fps。两路是一对 `esp_h264_enc_dual_process_one()` 调用，因此两路 FPS 相同。

## 故障排除

### Dual encoder create failed

确认芯片是 ESP32-P4，宽高为 16 的倍数，PPA 缩小后不小于 80x80。

### process_one failed

`esp_h264_enc_dual_process_one()` 只接受 0、1、0、1 这个顺序。序号不对会返回参数错误，并且这一帧不会编码。

### PPA SRM failed

确认已打开 PSRAM。PPA 按 1/16 量化后的宽高必须等于 `SRC * NUM / DEN`，是 16 的倍数，并且不小于 80x80。1920x1920 上的 14/15 会被拒绝。

### 没有 USB 日志

在 `Component config` → `ESP System Settings` → `Channel for console output` 中选择 `USB Serial/JTAG`。

## 技术支持

- 技术支持：[esp32.com](https://esp32.com/) 论坛
- 问题反馈：[GitHub issue](https://github.com/espressif/esp-h264-component/issues)
