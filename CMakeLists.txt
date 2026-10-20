cmake_minimum_required(VERSION 3.6)

project(sensornode-core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)

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
    $<$<CXX_COMPILER_ID:GNU>:-Wno-psabi>
)

# configure_file chỉ chạy lúc cấu hình nên git hash sẽ cũ nếu commit mà không cmake lại.
set(SENSORNODE_GENERATED_DIR ${PROJECT_BINARY_DIR}/generated)
add_custom_target(sensornode_git_version
    COMMAND ${CMAKE_COMMAND}
            -DSRC=${PROJECT_SOURCE_DIR}
            -DOUT=${SENSORNODE_GENERATED_DIR}/git_version.h
            -P ${PROJECT_SOURCE_DIR}/cmake/git_version.cmake
    BYPRODUCTS ${SENSORNODE_GENERATED_DIR}/git_version.h
)

add_subdirectory(common)
add_subdirectory(telemetryd)
add_subdirectory(otad)
add_subdirectory(systemd)
