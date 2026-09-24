# 硬件 ROI 编码

- [English](./README.md)
- 中级例程: ⭐⭐

## 例程简介

- 一路软件彩条 generator 送给 ESP32-P4 单路硬件 H.264 编码器。
- `esp_h264_enc_open()` 之后配置硬件 ROI：画面中心区域 QP 更低，非 ROI 区域用更高的 delta QP。
- 编码在 core 0，generator 在 core 1。日志打印 FPS 和码率。

### 典型场景

人脸或 OSD 区域更清晰，背景少花码字。

### 运行流程

```
generator ──► ROI（中心宏块区域） ──► H.264 码流 0
```

### 文件结构

```
hw_roi_encode/
├── main/
│   ├── hw_roi_encode_main.c
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── Kconfig.projbuild
├── CMakeLists.txt
├── pytest_hw_roi_encode.py
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
cd esp_h264/examples/hw_roi_encode
idf.py set-target esp32p4
```

### 工程配置

```
idf.py menuconfig
```

在 `Example Configuration` 中配置：

- `Generator width/height`
- `Fill generator frames in realtime`（关闭则只填 2 帧后循环）
- `ROI mode`（ROI 内固定 QP / ROI 增量 QP / 关闭 ROI）
- `ROI region QP / delta QP`
- `Non-ROI delta QP`
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

程序填充移动彩条，打开中心 ROI，然后尽量快地编码。码流数据丢弃，日志只打印编码帧率和码率。

ROI 坐标单位是 16×16 宏块。默认区域是画面中心一半。

### 日志输出

```
I (212) esp_psram: Speed: 250MHz
I (790) cpu_start: cpu freq: 400000000 Hz
I (797) hw_roi: Demo: generator core1, encode core0 realtime=1
I (798) hw_roi: stream0 src 1920x1088
I (867) hw_roi: ROI mode=FIX_QP none_roi_delta_qp=12
I (868) hw_roi: ROI region mb x=30 y=17 len_x=60 len_y=34 qp=20 (frame 1920x1088, mb 120x68)
I (868) hw_roi: generator task on core 1 realtime=1
I (868) hw_roi: encode task on core 0
I (1933) hw_roi: stream0 (src 1920x1088) fps=13.15 bitrate=279 kbps avg_nal=2651
I (8910) hw_roi: average stream0 (src 1920x1088) fps=13.30 frames=107 bitrate=268 kbps
I (8911) hw_roi: demo finished
```

## 故障排除

### Create encoder failed

确认芯片是 ESP32-P4，宽高均为 16 的倍数且不小于 80x80。

### set_roi_region failed

FIX_QP 下 ROI QP 必须在 [0, 51]；DELTA_QP 允许 [-51, 51]。ROI 矩形必须落在画面宏块范围内。

### 没有 USB 日志

在 `Component config` → `ESP System Settings` → `Channel for console output` 中选择 `USB Serial/JTAG`。

## 技术支持

- 技术支持：[esp32.com](https://esp32.com/) 论坛
- 问题反馈：[GitHub issue](https://github.com/espressif/esp-h264-component/issues)
