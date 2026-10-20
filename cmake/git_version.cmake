# Chạy lúc build, sinh header chứa `git describe` của source.
# Input: SRC (thư mục source)
# OUT: file header.

execute_process(
    COMMAND git describe --always --dirty
    WORKING_DIRECTORY ${SRC}
    OUTPUT_VARIABLE GIT_DESC
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_DESC)
    set(GIT_DESC "unknown")
endif()

set(CONTENT "#pragma once\n#define SENSORNODE_GIT \"${GIT_DESC}\"\n")

# Chỉ ghi khi nội dung đổi để không làm main.cpp bị compile lại mỗi lần build.
if(EXISTS ${OUT})
    file(READ ${OUT} OLD)
endif()
if(NOT "${CONTENT}" STREQUAL "${OLD}")
    file(WRITE ${OUT} "${CONTENT}")
endif()
