# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-enum-enum-conversion>)

project(SmartLockRTOS_ESP_IDF)
