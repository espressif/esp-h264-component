# Hardware ROI Encode

- [中文版](./README_CN.md)
- Regular Example: ⭐⭐

## Example Brief

- One software color-bar generator feeds the ESP32-P4 single-stream hardware H.264 encoder.
- After `esp_h264_enc_open()`, the demo configures hardware ROI: a center region uses a lower QP, the rest of the frame uses a higher non-ROI delta QP.
- Encode runs on core 0, generator on core 1. The log prints FPS and bitstream bitrate.

### Typical Scenarios

Keep a face or OSD region sharper while spending fewer bits on the background.

### Run Flow

```
generator ──► ROI (center MB region) ──► H.264 stream0
```

### File Structure

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
cd esp_h264/examples/hw_roi_encode
idf.py set-target esp32p4
```

### Project Configuration

```
idf.py menuconfig
```

Configure under `Example Configuration`:

- `Generator width/height`
- `Fill generator frames in realtime` (N = fill 2 frames then recycle)
- `ROI mode` (`Fixed QP in ROI` / `Delta QP in ROI` / `Disable ROI`)
- `ROI region QP / delta QP`
- `Non-ROI delta QP`
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

The app fills a moving color-bar, enables a center ROI, and encodes as fast as the hardware allows. Bitstreams are discarded; the log reports encode FPS and bitrate.

ROI coordinates are in 16x16 macroblocks. The default region is the center half of the frame.

### Log Output

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

## Troubleshooting

### Create encoder failed

Confirm the target is ESP32-P4 and width/height are multiples of 16 and at least 80x80.

### set_roi_region failed

FIX_QP requires ROI QP in [0, 51]. DELTA_QP allows [-51, 51]. The ROI rectangle must stay inside the picture in macroblock units.

### No USB console log

Enable `Component config` → `ESP System Settings` → `Channel for console output` → `USB Serial/JTAG`.

## Technical Support

- Technical support: [esp32.com](https://esp32.com/) forum
- Issue reports: [GitHub issue](https://github.com/espressif/esp-h264-component/issues)
