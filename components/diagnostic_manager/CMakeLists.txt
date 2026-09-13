idf_component_register(
    SRCS  "diagnostic_manager.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_system heap freertos
)