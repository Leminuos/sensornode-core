find_package(nlohmann_json 3.10 REQUIRED)

add_executable(sensornode-displayd
    main.cpp
    backlight.cpp
    protocol.cpp
)

target_link_libraries(sensornode-displayd
    PRIVATE
        sensornode_common
        nlohmann_json::nlohmann_json
)

target_include_directories(sensornode-displayd PRIVATE ${SENSORNODE_GENERATED_DIR})
add_dependencies(sensornode-displayd sensornode_git_version)

install(TARGETS sensornode-displayd RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
