idf_component_register(SRCS "access_controller.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES keypad_driver rfid_driver contact_sensor presence_sensor touch_sensor pressure_sensor servo_driver display_driver smart_led)


target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)
