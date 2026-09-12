idf_component_register(SRCS "display.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_lcd esp_driver_spi esp_driver_gpio lvgl esp_lvgl_port)


target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)
