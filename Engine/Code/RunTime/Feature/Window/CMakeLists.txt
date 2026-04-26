set(TARGET_NAME SparkWindow)

set(SOURCES
    GLFW/GlfwWindow.cpp
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        SparkCore
        SparkPlatform
        glfw
)

# 指定当前目录的父目录作为包含目录，其他模块包含时必须使用 #include <Window/...>
target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../>)