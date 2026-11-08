cmake_minimum_required(VERSION 3.22.1)
project(openflight LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(openflight SHARED
    jni_entry.cpp
    engine.cpp
    vulkan_renderer.cpp
    world.cpp
    aircraft.cpp
    physics.cpp)

target_include_directories(openflight PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(openflight PRIVATE VK_USE_PLATFORM_ANDROID_KHR)

target_compile_options(openflight PRIVATE -Wall -Wno-unused-parameter)

target_link_libraries(openflight vulkan android log)
