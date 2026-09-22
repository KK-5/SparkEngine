set(TARGET_NAME SparkRender)

set(SOURCES
    Feature/UI/RenderUI.cpp
    Feature/UI/UIProcessFeature.cpp
    Feature/UI/CopyFrameBufferPass.cpp
    Feature/DepthPre/DepthPrePass.cpp
    Feature/GBuffer/GBufferPass.cpp
    Feature/ShadowProjection/ShadowProjectionPass.cpp
    Feature/Lighting/LightingPass.cpp
    Feature/Shadow/ShadowPass.cpp
    Feature/Skybox/SkyboxPass.cpp
    Feature/Tonemap/TonemapPass.cpp
    Feature/Velocity/VelocityResolvePass.cpp
    Feature/TemporalAA/TemporalAAPass.cpp
    Pass/PassAccess.cpp
    Pass/Pipeline.cpp
    RenderGraph/RenderGraphBuilder.cpp
    RenderGraph/RenderGraphCompiler.cpp
    RenderGraph/RenderGraphExecuter.cpp
    RenderGraph/RenderGraph.cpp
    RenderGraph/PooledImage.cpp
    RenderSystem.cpp
    View/CameraViewSystem.cpp
    View/ShadowAtlasAllocator.cpp
    View/ShadowPools.cpp
    View/ShadowViewSystem.cpp
    View/ShadowMaskSystem.cpp
    View/ViewFactory.cpp
    Binding/View/ViewBindingSystem.cpp
    Binding/Scene/SceneBindingSystem.cpp
    Binding/Instance/InstanceBindingSystem.cpp
    Binding/Material/MaterialBindingSystem.cpp
    Drawable/MeshGeometryComposer.cpp
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
        SparkAntiAliasing
        SparkCore
        SparkRHI
        opengl32
        glfw
        ImGui
)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)