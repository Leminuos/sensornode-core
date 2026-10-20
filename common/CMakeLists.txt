add_library(sensornode_common STATIC
    event_loop.cpp
    log.cpp
    process.cpp
    signal_fd.cpp
    systemd.cpp
    timer.cpp
    unix_server.cpp
)

target_include_directories(sensornode_common PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(sensornode_common PUBLIC sensornode_build_flags)

target_compile_definitions(sensornode_common PUBLIC
    SENSORNODE_VERSION="${PROJECT_VERSION}"
)
