idf_component_register(SRCS "rfid_driver.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES rc522 esp_driver_spi)


target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)
