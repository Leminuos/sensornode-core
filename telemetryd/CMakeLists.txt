add_executable(sensornode-telemetryd
    main.cpp
)

target_link_libraries(sensornode-telemetryd
    PRIVATE
        sensornode_common
)
