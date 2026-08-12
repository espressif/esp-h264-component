# Changelog

## 1.3.8

### Features

- Added `README_CN.md` and component registry tags

### Fixes

- Fixed BS DMA timeout check using bitwise `&` instead of logical `&&` (single and dual HW encoders)
- Fixed `set_fps` to regenerate SPS+PPS and update `nal_bit_len` after VUI SPS size changes; raised `SPS_PPS_BUF_SIZE`
- Enabled `H264_INTR_BS_BIT_OVERFLOW` and extended `H264_INTR_MASK` for P4 rev >= 3.0
- Fixed dual-encode error return (no longer OR negative codes) and `get_param_hd1` NULL check
- Fixed dual encode to skip stream1 only on a fatal stream0 error (not on `ESP_H264_ERR_OVERFLOW`)
- Fixed HW encoders to force the next frame to an IDR and clear the output length after a fatal encode failure
- Fixed SW encoder min resolution to allow 16x16 and check `WelsCreateSVCEncoder` result
- Fixed overlapping `memcpy` in flash-encryption AUD insert (`memmove`)
- Fixed HW rate control first-frame QP, `init_mad` bounds, target-bit clamp, cumulative bit-error saturation, and skip `rc_end` on BS overflow
- Fixed HW SPS `level_idc` selection (MaxMBPS / MaxFS per Annex A)
- Fixed IDR SPS/PPS copy to check output buffer length (`esp_h264_enc_hw_get_nal`)
- Fixed `enc_open` to fail when `frame_done` mutex creation fails
- Fixed HW-generated SPS/PPS/slice headers missing Annex-B `emulation_prevention_three_byte` (0x03) insertion, which produced non-conformant bitstreams that standard decoders rejected
- Fixed HW encoders (single and dual) to also report `ESP_H264_ERR_OVERFLOW` when the coded length exceeds `out_frame.raw_data.len` but the BS DMA overflow interrupt/flag was not raised, so `esp_h264_enc_process` can no longer return `ESP_H264_ERR_OK` with a length larger than the buffer the caller supplied
- Fixed HW-generated PPS `pic_init_qs_minus26` (`qp - 26 - 2`) going out of the spec-required 0-51 range whenever `qp < 2`, producing a non-conformant PPS that standard decoders rejected; it now mirrors `pic_init_qp_minus26` since baseline-profile streams never use SP/SI slices
- Changed HW encoders (single and dual) to also force the next frame back to IDR on `ESP_H264_ERR_OVERFLOW`, not just on fatal errors: even though the HW completes the frame internally on overflow, any external decoder never receives that frame's (truncated) bitstream and would otherwise silently desync starting from the very next P frame
- Fixed dual HW encoder force-IDR request consumption so simultaneous requests on both streams produce one IDR instead of leaving stream1's request pending and producing a second consecutive IDR
- Made HW force-IDR requests non-blocking while preserving thread safety

## 1.3.7

### Features

- Signaled frame rate in HW encoder SPS via VUI `timing_info` (`num_units_in_tick`, `time_scale`, `fixed_frame_rate_flag`), so fps can be recovered as `time_scale / (2 * num_units_in_tick)`
- Added `esp_h264_aligned_malloc` and `esp_h264_malloc_prefer` for non-zeroing allocation
- Used malloc for large HW encoder reference and deblocking buffers to reduce `new` latency
- Added `esp_h264_enc_force_idr()` to force the next encoded frame to be an IDR (HW), without changing the configured GOP

### Fixes

- Fixed `CLIP3` argument order in HW rate control so `eqp` is clamped to [-5, 5] correctly (avoids RC oscillation)
- Fixed spelling and grammar in public headers and README

## 1.3.6

### Features

- Supported IDF6.0
- Supported ESP32-S31

### Fixes

- Fixed excessive internal buffer allocation in the decoder

## 1.3.5~1
- Fixed missing headers inclusion in h264_nal.c

## 1.3.5

### Fixes
- Fixed hardware encoder (P4) encoding failure when flash encryption is enabled and accessing PSRAM

## 1.3.0

### Fixes
- Corrected pixel format name from BGR565_BE to RGB565_LE in documentation comments and in the public enum/type definition (naming aligned with actual byte order)

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
