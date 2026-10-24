find_package(nlohmann_json 3.10 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(UBOOTENV REQUIRED IMPORTED_TARGET libubootenv)

add_executable(sensornode-otad
    main.cpp
    control_server.cpp
    fake_installer.cpp
    manifest.cpp
    ota_manager.cpp
    protocol.cpp
    rebooter.cpp
    swupdate_installer.cpp
    swupdate_progress.cpp
    system_info.cpp
)

find_path(SWUPDATE_INCLUDE_DIR progress_ipc.h)
find_library(SWUPDATE_LIBRARY swupdate)
if(SWUPDATE_INCLUDE_DIR AND SWUPDATE_LIBRARY)
    target_include_directories(sensornode-otad PRIVATE ${SWUPDATE_INCLUDE_DIR})
    target_link_libraries(sensornode-otad PRIVATE ${SWUPDATE_LIBRARY})
else()
    message(STATUS "libswupdate not found, using third_party/swupdate")
    add_library(swupdate_progress_ipc STATIC ${PROJECT_SOURCE_DIR}/third_party/swupdate/progress_ipc.c)
    target_include_directories(swupdate_progress_ipc PUBLIC ${PROJECT_SOURCE_DIR}/third_party/swupdate)
    target_compile_definitions(swupdate_progress_ipc PRIVATE _GNU_SOURCE)
    target_link_libraries(sensornode-otad PRIVATE swupdate_progress_ipc)
endif()

target_link_libraries(sensornode-otad
    PRIVATE
        sensornode_common
        nlohmann_json::nlohmann_json
        PkgConfig::UBOOTENV
)

target_include_directories(sensornode-otad PRIVATE ${SENSORNODE_GENERATED_DIR})
add_dependencies(sensornode-otad sensornode_git_version)

install(TARGETS sensornode-otad RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
