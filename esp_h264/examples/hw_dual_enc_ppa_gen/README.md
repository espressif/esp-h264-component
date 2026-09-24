# Dual-Stream Encode with PPA Scale-Down

- [中文版](./README_CN.md)
- Regular Example: ⭐⭐

## Example Brief

- One software color-bar generator produces a single O_UYY_E_VYY frame. Stream 0 is PPA scale-down then hardware H.264 encode. Stream 1 encodes the same generator frame at the original size.
- Uses ESP32-P4 dual hardware encoder (`esp_h264_enc_dual_hw_new`) and PPA SRM (`ppa_do_scale_rotate_mirror`). Each stream prints FPS once per second, then an average FPS.

### Typical Scenarios

Main-stream plus preview/sub-stream from one source, where the sub-stream is scaled by PPA instead of a second sensor.

### Run Flow

```
generator (one O_UYY_E_VYY frame)
        ├── PPA SRM scale-down ──► H.264 stream0
        └── same buffer ──────────► H.264 stream1
```

### File Structure

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
cd esp_h264/examples/hw_dual_enc_ppa_gen
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

The app fills a moving color-bar, scales it with PPA for stream 0, and encodes both streams as fast as the hardware allows. Bitstreams are discarded; the log reports encode FPS.

### Log Output

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

The two streams share one `esp_h264_enc_dual_process()` call, so their FPS values match.

## Troubleshooting

### Dual encoder create failed

Confirm the target is ESP32-P4 and the width/height are multiples of 16, not smaller than 80x80 after PPA scale-down.

### PPA SRM failed

Confirm PSRAM is enabled. The PPA 1/16 size must equal `SRC * NUM / DEN`, be a multiple of 16, and be at least 80x80. A ratio such as 14/15 on 1920x1920 is rejected.

### No USB console log

Enable `Component config` → `ESP System Settings` → `Channel for console output` → `USB Serial/JTAG`.

## Technical Support

- Technical support: [esp32.com](https://esp32.com/) forum
- Issue reports: [GitHub issue](https://github.com/espressif/esp-h264-component/issues)
