# Dual-Stream Single-Frame Encode with a PPA Task

- [中文版](./README_CN.md)
- Regular Example: ⭐⭐

## Example Brief

- One software color-bar generator produces a single O_UYY_E_VYY frame. A separate PPA task scale-down that frame for stream 0. The encode task then calls `esp_h264_enc_dual_process_one()` for stream 0 and stream 1 in that order.
- Uses the ESP32-P4 dual hardware encoder (`esp_h264_enc_dual_hw_new`) and PPA SRM (`ppa_do_scale_rotate_mirror`). Each stream prints FPS once per second, then an average FPS.

### Typical Scenarios

Main-stream plus preview/sub-stream from one source, where PPA scale-down runs in parallel with hardware encode and each stream is submitted as its own frame.

### Run Flow

```
generator (one O_UYY_E_VYY frame)
        │
        ▼
PPA task (SRM scale-down)
        │
        ▼
encode task
        ├── esp_h264_enc_dual_process_one(0) ──► H.264 stream0 (scaled)
        └── esp_h264_enc_dual_process_one(1) ──► H.264 stream1 (original)
```

Calls on one handle must stay in the order 0, 1, 0, 1.

### File Structure

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
cd esp_h264/examples/hw_dual_enc_ppa_one
idf.py set-target esp32p4
```

### Project Configuration

```
idf.py menuconfig
```

Configure under `Example Configuration`:

- `Generator width (pixels)`
- `Generator height (pixels)`
- `PPA scale numerator` / `PPA scale denominator` (default 1/2)
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

The generator fills two color-bar frames and then recycles them. The PPA task scales each frame on core 1. The encode task on core 0 encodes the scaled frame as stream 0, then the original frame as stream 1. Bitstreams are discarded; the log reports encode FPS and PPA FPS.

### Log Output

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

After the first second, encode FPS stays at 17.57 or 17.83. The 8-second average is 17.48 fps. Both streams share one pair of `esp_h264_enc_dual_process_one()` calls, so their FPS values match.

## Troubleshooting

### Dual encoder create failed

Confirm the target is ESP32-P4 and the width/height are multiples of 16, not smaller than 80x80 after PPA scale-down.

### process_one failed

`esp_h264_enc_dual_process_one()` accepts only stream 0, then 1, then 0, then 1. A call out of that order returns an argument error and does not encode.

### PPA SRM failed

Confirm PSRAM is enabled. The PPA 1/16 size must equal `SRC * NUM / DEN`, be a multiple of 16, and be at least 80x80. A ratio such as 14/15 on 1920x1920 is rejected.

### No USB console log

Enable `Component config` → `ESP System Settings` → `Channel for console output` → `USB Serial/JTAG`.

## Technical Support

- Technical support: [esp32.com](https://esp32.com/) forum
- Issue reports: [GitHub issue](https://github.com/espressif/esp-h264-component/issues)
