set(TARGET_NAME SparkRender)

set(SOURCES
    Feature/UI/RenderUI.cpp
    Feature/UI/UIProcessFeature.cpp
    Feature/UI/CopyFrameBufferPass.cpp
    Feature/DepthPre/DepthPrePass.cpp
    Feature/DepthPre/DepthPreProcessor.cpp
    Feature/GBuffer/GBufferPass.cpp
    Feature/GBuffer/GBufferProcessor.cpp
    Feature/Skybox/SkyboxPass.cpp
    Feature/Skybox/SkyboxProcessor.cpp
    Pass/PassAccess.cpp
    Pass/Pipeline.cpp
    RenderGraph/RenderGraphBuilder.cpp
    RenderGraph/RenderGraphCompiler.cpp
    RenderGraph/RenderGraphExecuter.cpp
    RenderGraph/RenderGraph.cpp
    RenderSystem.cpp
    View/ViewBindingSystem.cpp
    Instance/InstanceBindingSystem.cpp
    Drawable/DrawableComposer.cpp
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

target_link_libraries(${TARGET_NAME}
    PRIVATE
        SparkAssetManager
        SparkUI
    PUBLIC
        SparkCamera
        SparkCore
        SparkRHI
        opengl32
        glfw
        ImGui
)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)