# Changelog

## 1.2.0~1

### Fixes
- Fixed spelling and grammar errors in documentation and code comments
- Corrected hardware encoder resolution ranges (width: 80-1920, height: 80-2048)
- Fixed PPS row description and P-frame description in feature table

## 1.2.0

### Features
- Added hardware encoder support for multiple pixel formats on ESP32-P4(>=3.0.0):
  - BGR888 (24-bit RGB format)
  - BGR565_BE (16-bit RGB Big-Endian format)
  - VUY (YUV 4:4:4 format)
  - UYVY (YUV 4:2:2 format)
- Added hardware version detection macro `ESP_H264_HW_IS_SUPPORTED_PIC_TYPE`
- Added `ESP_H264_GET_BPP_BY_PIC_TYPE` macro to determine the number of bits per pixel for a given picture type
- Added support for ESP32P4 V3 hardware versions

### Fixes
- Fixed a bug where the hardware encoder failed to set GOP via `esp_h264_enc_set_gop`

## 1.1.4

- Fixed wrong frame type output for the H264 software encoder
- Added test cases (GOP, FPS, picture type) for H264 software encoder

## 1.1.3

- Fixed a bug where CmakeLists.txt excessive dependence on freertos pthread and newlib components

## 1.1.2

- Fixed compatibility issue by adding POSIX compatibility layer and resolving FreeRTOS symbol linking problems in static libraries
- Enhanced CMakeLists.txt with proper dependency management for FreeRTOS pthread and newlib components

## 1.1.1

- Fixed compatibility issue by temporarily disabling HWLP for ESP32-P4 in the decoder

## 1.1.0

- Improved decoder performance
- Added support for dual-task decoding
- Updated decoder profile support from baseline profile to constrained baseline profile

## 1.0.4

- Fixed memory wrapper allocating incorrect memory capabilities in the decoder

## 1.0.3

- Fixed slice header error when cache missing
- Fixed bitrate size wrong for hw encoder
- Test case use SPI-RAM

## 1.0.2

- Fixed the CI build error on ESP32S3
- Fixed the component dependencies error
- Fixed the length of out frame incorrect for software encoder

## 1.0.1

- Changed the IDF dependencies from >= 5.3 to >= 4.4
- Fixed the decoder without updating PTS and DTS

## 1.0.0

- Initial version for esp_h264 component
