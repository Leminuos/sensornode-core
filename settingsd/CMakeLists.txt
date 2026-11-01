find_package(nlohmann_json 3.10 REQUIRED)

add_executable(sensornode-settingsd
    main.cpp
    protocol.cpp
    store.cpp
)

target_link_libraries(sensornode-settingsd
    PRIVATE
        sensornode_common
        nlohmann_json::nlohmann_json
)

target_include_directories(sensornode-settingsd PRIVATE ${SENSORNODE_GENERATED_DIR})
add_dependencies(sensornode-settingsd sensornode_git_version)

install(TARGETS sensornode-settingsd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
