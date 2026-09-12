idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "." "include"
                    REQUIRES esp_driver_gpio keypad_driver smart_led rfid_driver access_controller
                    )
