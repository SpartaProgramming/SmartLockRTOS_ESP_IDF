if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/include/secrets.hpp")
    message(FATAL_ERROR
        "Brak components/secrets/include/secrets.hpp - "
        "skopiuj secrets.example.hpp jako secrets.hpp i uzupelnij")
endif()

idf_component_register(INCLUDE_DIRS "include")

