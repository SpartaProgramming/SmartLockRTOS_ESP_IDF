idf_component_register(SRCS "pressure_sensor.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_adc esp_driver_gpio)


target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)
