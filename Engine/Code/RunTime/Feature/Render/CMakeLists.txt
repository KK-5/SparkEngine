set(TARGET_NAME SparkRender)

# add_subdirectory(RHI)

set(SOURCES
    Feature/UI/RenderUI.cpp
    RenderSystem.cpp
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

target_link_libraries(${TARGET_NAME}
    PUBLIC
        SparkCore
        SparkRHI
        opengl32
        glfw
        ImGui
)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)