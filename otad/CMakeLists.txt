add_executable(sensornode-otad
    main.cpp
)

target_link_libraries(sensornode-otad
    PRIVATE
        sensornode_common
)
