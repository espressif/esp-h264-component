# Changelog

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

- Inititial version for esp_h264 component
