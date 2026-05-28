set(TARGET_NAME SparkRender)

# add_subdirectory(RHI)

set(SOURCES
    Feature/UI/RenderUI.cpp
    Pass/Pipeline.cpp
    RenderGraph/RenderGraphBuilder.cpp
    RenderGraph/RenderGraphCompiler.cpp
    RenderGraph/RenderGraphExecuter.cpp
    RenderGraph/RenderGraph.cpp
    RenderSystem.cpp
    Shader/ShaderBuilder.cpp
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        SparkAssetManager
        SparkUI
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