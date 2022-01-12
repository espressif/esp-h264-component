list(APPEND COMPONENT_ADD_INCLUDEDIRS "./interface/include"
                                      "./sw/include"
                                      "./port/include/")
list(APPEND COMPONENT_PRIV_INCLUDEDIRS  "./port/inc/"
                                        "./sw/libs/tinyh264_inc"
                                        "./sw/libs/openh264_inc"
                                        "./sw/src/")
list(APPEND COMPONENT_SRCDIRS "./port/src/"
                              "./interface/src" 
                              "./sw/src")


IF (${IDF_TARGET} STREQUAL "esp32p4") 

list(APPEND COMPONENT_ADD_INCLUDEDIRS "./hw/include")
list(APPEND COMPONENT_PRIV_INCLUDEDIRS  "./hw/hal/esp32p4"
                                        "./hw/soc/esp32p4") 
list(APPEND COMPONENT_SRCDIRS "./hw/src"
                              "./hw/hal/esp32p4" )

ENDIF ()

IF (${IDF_TARGET} STREQUAL "esp32s3") 

list(APPEND COMPONENT_SRCDIRS "./sw/src/asm/${IDF_TARGET}")

ENDIF ()

set(COMPONENT_REQUIRES esp_mm)

register_component()

add_prebuilt_library(openh264 "${CMAKE_CURRENT_SOURCE_DIR}/sw/libs/${IDF_TARGET}/libopenh264.a" PRIV_REQUIRES esp_h264)
set(TARGET_LIB_NAME openh264 "${CMAKE_CURRENT_SOURCE_DIR}/sw/libs/${IDF_TARGET}/libtinyh264.a")
target_link_libraries(${COMPONENT_TARGET} "-Wl,--start-group" ${TARGET_LIB_NAME} "-Wl,--end-group")

IF (${IDF_TARGET} STREQUAL "esp32s3") 

add_definitions(-DHAVE_ESP32S3 )

ENDIF ()
