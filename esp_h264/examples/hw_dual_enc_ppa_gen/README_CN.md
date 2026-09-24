# 双码流编码（PPA 缩小）

- [English](./README.md)
- 中级例程: ⭐⭐

## 例程简介

- 一个软件彩条 generator 只产生一帧 O_UYY_E_VYY。码流 0：PPA scale down 后再硬件 H.264 编码；码流 1：同一输入直接编码。
- 使用 ESP32-P4 双路硬件编码器（`esp_h264_enc_dual_hw_new`）和 PPA SRM（`ppa_do_scale_rotate_mirror`）。每路每秒打印一次帧率，结束时打印平均帧率。

### 典型场景

同一路输入做主码流 + 预览/子码流，子码流用 PPA 缩小，不需要第二路 Sensor。

### 运行流程

```
generator（一路 O_UYY_E_VYY）
        ├── PPA SRM 缩小 ──► H.264 码流 0
        └── 同一 buffer ──► H.264 码流 1
```

### 文件结构

```
hw_dual_enc_ppa_gen/
├── main/
│   ├── hw_dual_enc_ppa_gen_main.c
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── Kconfig.projbuild
├── CMakeLists.txt
├── pytest_hw_dual_enc_ppa_gen.py
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
cd esp_h264/examples/hw_dual_enc_ppa_gen
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

程序填充移动彩条，码流 0 经 PPA 缩小后编码，码流 1 用同一输入编码。码流数据丢弃，日志只打印编码帧率。

### 日志输出

```
I (342) dual_ppa: Demo: gen core1, PPA core1, enc core0 realtime=1 parallel=1
I (844) dual_ppa: stream0 PPA 960x544, stream1 src 1920x1088
I (845) dual_ppa: generator task on core 1 realtime=1 parallel=1
I (846) dual_ppa: PPA task on core 1
I (847) dual_ppa: encode task on core 0 parallel=1
I (1981) dual_ppa: stream0 (PPA 960x544) fps=7.47
I (1982) dual_ppa: stream1 (src 1920x1088) fps=7.47
I (8950) dual_ppa: average stream0 (PPA 960x544) fps=7.46 frames=60
I (8951) dual_ppa: average stream1 (src 1920x1088) fps=7.46 frames=60
I (8951) dual_ppa: demo finished
```

两路共用一次 `esp_h264_enc_dual_process()`，因此两路 FPS 相同。

## 故障排除

### Dual encoder create failed

确认芯片是 ESP32-P4，宽高为 16 的倍数，PPA 缩小后不小于 80x80。

### PPA SRM failed

确认已打开 PSRAM。PPA 按 1/16 量化后的宽高必须等于 `SRC * NUM / DEN`，是 16 的倍数，并且不小于 80x80。1920x1920 上的 14/15 会被拒绝。

### 没有 USB 日志

在 `Component config` → `ESP System Settings` → `Channel for console output` 中选择 `USB Serial/JTAG`。

## 技术支持

- 技术支持：[esp32.com](https://esp32.com/) 论坛
- 问题反馈：[GitHub issue](https://github.com/espressif/esp-h264-component/issues)
