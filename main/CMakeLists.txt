idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS "." "include"
                    REQUIRES esp_driver_gpio secrets keypad_driver smart_led rfid_driver access_controller display_driver contact_sensor presence_sensor touch_sensor pressure_sensor servo_driver wifi_manager nvs_flash diagnostic_manager telemetry_service
                    )
