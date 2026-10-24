add_library(sensornode_common STATIC
    event_loop.cpp
    log.cpp
    mqtt_client.cpp
    process.cpp
    signal_fd.cpp
    systemd.cpp
    timer.cpp
    unix_server.cpp
)

target_include_directories(sensornode_common PUBLIC ${PROJECT_SOURCE_DIR})

find_package(PkgConfig REQUIRED)
pkg_check_modules(MOSQUITTO REQUIRED IMPORTED_TARGET libmosquitto)

target_link_libraries(sensornode_common
    PUBLIC  sensornode_build_flags
    PRIVATE PkgConfig::MOSQUITTO
)

target_compile_definitions(sensornode_common PUBLIC
    SENSORNODE_VERSION="${PROJECT_VERSION}"
)
