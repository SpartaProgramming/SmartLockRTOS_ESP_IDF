idf_component_register(SRCS "wifi_manager.cpp"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_wifi esp_netif esp_event )

target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
)
 