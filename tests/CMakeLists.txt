# Test chạy binary trên host, build bằng SDK/Yocto thì không chạy được.
if(CMAKE_CROSSCOMPILING)
    return()
endif()

find_program(SENSORNODE_PYTHON3 python3)
if(NOT SENSORNODE_PYTHON3)
    message(STATUS "python3 not found, otad smoke test disabled")
    return()
endif()

add_test(NAME otad-smoke
    COMMAND ${SENSORNODE_PYTHON3} ${CMAKE_CURRENT_SOURCE_DIR}/otad/smoke_test.py
            --otad $<TARGET_FILE:sensornode-otad>
            --workdir ${PROJECT_BINARY_DIR}/otad-smoke
)
