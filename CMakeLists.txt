cmake_minimum_required(VERSION 3.5)

project(sensornode-core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Một INTERFACE library chỉ mang theo cờ compile/link, không có source.
# Target nào link vào nó sẽ nhận các cờ này.
add_library(sensornode_build_flags INTERFACE)

target_compile_options(sensornode_build_flags INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wnon-virtual-dtor
    -Wcast-align
    -Wformat=2
)

add_subdirectory(common)
add_subdirectory(telemetryd)
add_subdirectory(otad)
