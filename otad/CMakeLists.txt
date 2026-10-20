find_package(nlohmann_json 3.10 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(UBOOTENV REQUIRED IMPORTED_TARGET libubootenv)

add_executable(sensornode-otad
    main.cpp
    fake_installer.cpp
    ota_manager.cpp
    protocol.cpp
    swupdate_installer.cpp
    system_info.cpp
)

target_link_libraries(sensornode-otad
    PRIVATE
        sensornode_common
        nlohmann_json::nlohmann_json
        PkgConfig::UBOOTENV
)

target_include_directories(sensornode-otad PRIVATE ${SENSORNODE_GENERATED_DIR})
add_dependencies(sensornode-otad sensornode_git_version)

install(TARGETS sensornode-otad RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
