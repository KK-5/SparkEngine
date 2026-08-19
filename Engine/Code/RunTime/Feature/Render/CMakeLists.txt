set(TARGET_NAME SparkRender)

set(SOURCES
    Feature/UI/RenderUI.cpp
    Feature/UI/UIProcessFeature.cpp
    Feature/UI/CopyFrameBufferPass.cpp
    Feature/DepthPre/DepthPrePass.cpp
    Feature/GBuffer/GBufferPass.cpp
    Feature/Lighting/LightingPass.cpp
    Feature/Shadow/ShadowPass.cpp
    Feature/Skybox/SkyboxPass.cpp
    Feature/Tonemap/TonemapPass.cpp
    Pass/PassAccess.cpp
    Pass/Pipeline.cpp
    RenderGraph/RenderGraphBuilder.cpp
    RenderGraph/RenderGraphCompiler.cpp
    RenderGraph/RenderGraphExecuter.cpp
    RenderGraph/RenderGraph.cpp
    RenderSystem.cpp
    View/ViewBindingSystem.cpp
    View/CameraViewSystem.cpp
    View/ShadowAtlasAllocator.cpp
    View/ShadowViewSystem.cpp
    View/ViewFactory.cpp
    SceneBind/SceneBindingSystem.cpp
    Binding/Instance/InstanceBindingSystem.cpp
    MaterialBind/MaterialBindingSystem.cpp
    Drawable/MeshDrawableComposer.cpp
    Drawable/DrawItemRouter.cpp
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