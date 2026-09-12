idf_component_register(SRCS "access_controller.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES keypad_driver rfid_driver)


target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)
