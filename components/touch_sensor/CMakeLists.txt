idf_component_register(SRCS "touch_sensor.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_gpio)


target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)
