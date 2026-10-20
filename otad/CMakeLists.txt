add_executable(sensornode-otad
    main.cpp
)

target_link_libraries(sensornode-otad
    PRIVATE
        sensornode_common
)

install(TARGETS sensornode-otad RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
