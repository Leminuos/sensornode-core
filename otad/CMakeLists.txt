find_package(nlohmann_json 3.10 REQUIRED)

add_executable(sensornode-otad
    main.cpp
    protocol.cpp
)

target_link_libraries(sensornode-otad
    PRIVATE
        sensornode_common
        nlohmann_json::nlohmann_json
)

target_include_directories(sensornode-otad PRIVATE ${SENSORNODE_GENERATED_DIR})
add_dependencies(sensornode-otad sensornode_git_version)

install(TARGETS sensornode-otad RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
