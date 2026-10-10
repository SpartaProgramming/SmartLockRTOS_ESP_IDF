idf_component_register(SRCS "telemetry_service.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES mqtt cjson freertos heap esp_event esp_timer
                    EMBED_TXTFILES "certs/mqtt_ca.pem"
                    )


target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)
