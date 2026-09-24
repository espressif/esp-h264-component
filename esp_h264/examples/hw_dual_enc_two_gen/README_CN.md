# 双码流编码（两路 generator）

- [English](./README.md)
- 中级例程: ⭐⭐

## 例程简介

- 两个独立的软件彩条 generator 分别作为双路硬件 H.264 编码器的输入。两路分辨率和调色板可以不同。
- 使用 `esp_h264_enc_dual_hw_new` / `esp_h264_enc_dual_process`。每路每秒打印一次帧率，结束时打印平均帧率。

### 典型场景

两路独立输入（或两路虚拟源）同时编码，不经过 PPA 缩小。

### 运行流程

```
generator 0 ──► H.264 码流 0
generator 1 ──► H.264 码流 1
```

### 文件结构

```
hw_dual_enc_two_gen/
├── main/
│   ├── hw_dual_enc_two_gen_main.c
│   ├── CMakeLists.txt
│   ├── idf_component.yml
│   └── Kconfig.projbuild
├── CMakeLists.txt
├── pytest_hw_dual_enc_two_gen.py
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
cd esp_h264/examples/hw_dual_enc_two_gen
idf.py set-target esp32p4
```

### 工程配置

```
idf.py menuconfig
```

在 `Example Configuration` 中配置：

- `Generator 0 width/height`
- `Generator 1 width/height`
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

程序填充两路移动彩条并一起编码。码流数据丢弃，日志只打印编码帧率。

### 日志输出

```
I (341) dual_two_gen: Demo: two generator tasks core1, encode core0 realtime=1
I (840) dual_two_gen: stream0 gen0 1920x1088, stream1 gen1 960x544
I (841) dual_two_gen: gen0 task on core 1 realtime=1
I (842) dual_two_gen: gen1 task on core 1 realtime=1
I (843) dual_two_gen: encode task on core 0
I (2016) dual_two_gen: stream0 (gen0 1920x1088) fps=8.10
I (2017) dual_two_gen: stream1 (gen1 960x544) fps=8.10
I (8945) dual_two_gen: average stream0 (gen0 1920x1088) fps=8.09 frames=65
I (8945) dual_two_gen: average stream1 (gen1 960x544) fps=8.09 frames=65
I (8946) dual_two_gen: demo finished
```

两路共用一次 `esp_h264_enc_dual_process()`，因此两路 FPS 相同。

## 故障排除

### Dual encoder create failed

确认芯片是 ESP32-P4，两路宽高均为 16 的倍数且不小于 80x80。

### 没有 USB 日志

在 `Component config` → `ESP System Settings` → `Channel for console output` 中选择 `USB Serial/JTAG`。

## 技术支持

- 技术支持：[esp32.com](https://esp32.com/) 论坛
- 问题反馈：[GitHub issue](https://github.com/espressif/esp-h264-component/issues)
