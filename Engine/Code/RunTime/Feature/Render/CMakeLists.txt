set(TARGET_NAME SparkRender)

set(SOURCES
    Feature/UI/RenderUI.cpp
    Feature/UI/UIProcessFeature.cpp
    Feature/UI/UIPass.cpp
    Feature/DepthPre/DepthPrePass.cpp
    Feature/GBuffer/GBufferPass.cpp
    Feature/NRD/NRDSystem.cpp
    Feature/ShadowProjection/ShadowProjectionPass.cpp
    Feature/Lighting/LightsPass.cpp
    Feature/Lighting/DirectLightingPass.cpp
    Feature/Lighting/LightWeightTreePass.cpp
    Feature/Lighting/IndirectDiffusePass.cpp
    Feature/Lighting/ReflectionsPass.cpp
    Feature/Shadow/ShadowPass.cpp
    Feature/Skybox/SkyboxPass.cpp
    Feature/Translucency/TranslucencyPass.cpp
    Feature/Tonemap/TonemapPass.cpp
    Feature/Velocity/VelocityResolvePass.cpp
    Feature/TemporalAA/TemporalAAPass.cpp
    Feature/SceneDownsample/SceneDownsamplePass.cpp
    Feature/Bloom/BloomPass.cpp
    Feature/PostProcess/PostProcessResources.cpp
    Feature/SceneTextures/SceneTextures.cpp
    Feature/HZB/HZBPass.cpp
    Feature/AmbientOcclusion/AmbientOcclusionPass.cpp
    Feature/ScreenSpaceReflections/ScreenSpaceReflectionsPass.cpp
    Feature/InstanceCulling/InstanceCullingPass.cpp
    Feature/InstanceBinning/InstanceBinningPass.cpp
    Feature/InstanceLists/InstanceLists.cpp
    Pass/PassBuilder.cpp
    Pass/Pipeline.cpp
    PipelineState/PipelineVariantInterpret.cpp
    PipelineState/PipelineStateBuild.cpp
    PipelineState/DrawPipelineCache.cpp
    DrawList/DrawGroup.cpp
    DrawList/DrawGroupSystem.cpp
    DrawList/DrawList.cpp
    DrawList/DrawListCullingSystem.cpp
    DrawList/DrawListKeyCounter.cpp
    Suballocation/Suballocator.cpp
    Suballocation/ReservedBuffer.cpp
    Staging/StagingBuffer.cpp
    Staging/ElementUpdateBuffer.cpp
    RenderGraph/RenderGraphBuilder.cpp
    RenderGraph/RenderGraphCompiler.cpp
    RenderGraph/ImageBarrierMerge.cpp
    RenderGraph/RenderGraphResolve.cpp
    RenderGraph/RenderGraphExecuter.cpp
    RenderGraph/RenderGraph.cpp
    RenderGraph/PooledImage.cpp
    RenderGraph/PassScopes.cpp
    RenderSystem.cpp
    Scene/SceneTextureSystem.cpp
    Material/MaterialTextureSystem.cpp
    View/CameraViewSystem.cpp
    View/ShadowMapAllocator.cpp
    View/ShadowViewSystem.cpp
    View/ShadowMaskSystem.cpp
    Binding/SharedBindings.cpp
    Binding/SharedGroupLayouts.cpp
    Binding/SharedBindingPool.cpp
    Binding/View/ViewBindingSystem.cpp
    Binding/Scene/SceneBindingSystem.cpp
    Binding/Geometry/GeometryBindingSystem.cpp
    Binding/Instance/InstanceBindingSystem.cpp
    Binding/Instance/ActiveInstanceList.cpp
    Binding/RayTracing/RayTracingBindingSystem.cpp
    Binding/DeviceArray.cpp
    Binding/Material/MaterialBindingSystem.cpp
    Geometry/MeshGeometrySystem.cpp
    RayTracing/RayTracingBottomLevel.cpp
    RayTracing/RayTracingSceneSystem.cpp
    Drawable/DrawMask.cpp
)

add_library(${TARGET_NAME} STATIC
    ${SOURCES}
)

if(MSVC)
    # This translation unit can exceed the COFF section limit with debug information enabled.
    set_property(SOURCE RenderGraph/RenderGraphCompiler.cpp APPEND PROPERTY COMPILE_OPTIONS /bigobj)
endif()

target_link_libraries(${TARGET_NAME}
    PRIVATE
        SparkAssetManager
        SparkUI
    PUBLIC
        SparkMaterial
        SparkCamera
        SparkSkybox
        SparkAntiAliasing
        SparkBloom
        SparkAmbientOcclusion
        SparkScreenSpaceReflection
        SparkPostProcess
        SparkTonemap
        SparkCore
        SparkRHI
        opengl32
        glfw
        ImGui
)

target_include_directories(
  ${TARGET_NAME}
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)

if(TARGET NRD::NRD)
    target_sources(${TARGET_NAME} PRIVATE Feature/NRD/NRDInputPass.cpp)
    target_link_libraries(${TARGET_NAME} PRIVATE NRD::NRD)
    target_compile_definitions(${TARGET_NAME} PRIVATE SPARK_WITH_NRD=1)
else()
    target_compile_definitions(${TARGET_NAME} PRIVATE SPARK_WITH_NRD=0)
endif()
