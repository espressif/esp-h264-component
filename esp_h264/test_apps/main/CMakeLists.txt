set(srcs
 "test_app_main.c"
 "esp_h264_hw_enc_test.c"
 "esp_h264_sw_enc_test.c"
 "esp_h264_sw_dec_test.c"
 "h264_io.c"
 "test_case.c")

set(priv_requires
        esp_h264
        # general
        unity
)

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES ${priv_requires}
                       REQUIRES
                       WHOLE_ARCHIVE TRUE)

