set(TARGET_NAME SparkInput)

set(SOURCES
    InputCaptureSystem.cpp
    GLFWCaptureSystem.cpp
    InputSystem.cpp
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        SparkCore
        glfw
)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)