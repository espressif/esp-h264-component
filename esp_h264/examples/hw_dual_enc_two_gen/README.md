# Dual-Stream Encode with Two Generators

- [中文版](./README_CN.md)
- Regular Example: ⭐⭐

## Example Brief

- Two independent software color-bar generators feed the ESP32-P4 dual hardware H.264 encoder. Stream 0 and stream 1 can use different resolutions and palettes.
- Uses `esp_h264_enc_dual_hw_new` / `esp_h264_enc_dual_process`. Each stream prints FPS once per second, then an average FPS.

### Typical Scenarios

Two independent sources (or two virtual cameras) encoded at the same time, without a PPA scale path.

### Run Flow

```
generator 0 ──► H.264 stream0
generator 1 ──► H.264 stream1
```

### File Structure

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

Shared color-bar helper: `esp_h264/examples/common/pattern_input`.

## Environment Setup

### Hardware Required

- ESP32-P4 board with PSRAM (for example ESP32-P4 Function EV Board)
- USB Serial/JTAG for console

### Default IDF Branch

This example requires IDF release/v5.5.3 or later, and an ESP32-P4 revision v3.1 (ECO5) or newer.

## Build and Flash

### Build Preparation

Before building this example, ensure the ESP-IDF environment is set up. If it is already set up, skip this paragraph and go to the project directory. If not, run the following in the ESP-IDF root directory. For full steps, see the [ESP-IDF Programming Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32p4/index.html).

```
./install.sh
. ./export.sh
```

Short steps:

```
cd esp_h264/examples/hw_dual_enc_two_gen
idf.py set-target esp32p4
```

### Project Configuration

```
idf.py menuconfig
```

Configure under `Example Configuration`:

- `Generator 0 width/height`
- `Generator 1 width/height`
- `Fill generator frames in realtime` (N = fill 2 frames then recycle)
- `Measurement duration (seconds)`

Press `s` to save and `Esc` to exit after configuration.

### Build and Flash Commands

- Build the example:

```
idf.py build
```

- Flash the firmware and run the serial monitor (replace PORT with your port name):

```
idf.py -p PORT flash monitor
```

- To exit the monitor, use `Ctrl-]`

## How to Use the Example

### Functionality and Usage

The app fills two moving color-bars and encodes them together. Bitstreams are discarded; the log reports encode FPS.

### Log Output

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

The two streams share one `esp_h264_enc_dual_process()` call, so their FPS values match.

## Troubleshooting

### Dual encoder create failed

Confirm the target is ESP32-P4 and both generator sizes are multiples of 16 and at least 80x80.

### No USB console log

Enable `Component config` → `ESP System Settings` → `Channel for console output` → `USB Serial/JTAG`.

## Technical Support

- Technical support: [esp32.com](https://esp32.com/) forum
- Issue reports: [GitHub issue](https://github.com/espressif/esp-h264-component/issues)
