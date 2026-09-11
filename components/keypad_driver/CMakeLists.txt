idf_component_register(SRCS "keypad_driver.cpp" "keypad.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_gpio)


target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)
 

