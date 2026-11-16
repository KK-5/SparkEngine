#pragma once

#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderInput.h>

namespace Spark::RHI
{
    class Factory;
}

namespace Spark::Render
{
    //! The names shared groups' inputs go by, in their .hlsli and in the code that fills them.
    //! One place: the system that publishes what fills an input and the graph that declares the
    //! input both read the name here, so neither can drift from the other by a spelling.
    namespace SharedInput
    {
        // space0, the scene
        inline constexpr const char* Lights            = "g_Lights";
        inline constexpr const char* LightCount        = "g_LightCount";
        inline constexpr const char* ShadowViews       = "g_ShadowViews";
        inline constexpr const char* ViewShadowMaps    = "g_ViewShadowMaps";
        inline constexpr const char* ShadowTexelSize   = "g_ShadowMapTexelSize";
        inline constexpr const char* IrradianceCube    = "g_IrradianceCube";
        inline constexpr const char* PrefilteredCube   = "g_PrefilteredCube";
        inline constexpr const char* BRDFLut           = "g_BRDFLut";
        inline constexpr const char* IBLSampler        = "g_IBLSampler";
        inline constexpr const char* PrefilteredMips   = "g_IBLPrefilteredMipCount";
        inline constexpr const char* EnvIntensity      = "g_EnvIntensity";
        inline constexpr const char* FrameNumber       = "g_SceneFrameNumber";
        inline constexpr const char* GameTime          = "g_SceneGameTime";
        inline constexpr const char* PrevGameTime      = "g_ScenePrevGameTime";
        inline constexpr const char* DeltaTime         = "g_SceneDeltaTime";
        inline constexpr const char* LTC1              = "g_LTC1";
        inline constexpr const char* LTC2              = "g_LTC2";
        inline constexpr const char* AreaLightLutReady = "g_AreaLightLutReady";

        // space1, the views
        inline constexpr const char* Views = "g_Views";

        // space3, the material table
        inline constexpr const char* Materials = "g_Materials";

        // space4, the instance tables
        inline constexpr const char* Instances           = "g_Instances";
        inline constexpr const char* EmissiveInstanceIDs = "g_EmissiveInstanceIDs";

        // space6, the geometry tables
        inline constexpr const char* Geometries   = "g_Geometries";
        inline constexpr const char* MeshGeometry = "g_MeshGeometry";

        // space7, the ray tracing scene
        inline constexpr const char* RayTracingScene = "g_RayTracingScene";
    }

    //! What a shared group (one HLSL space) declares: every input its space holds, in register
    //! order. This is the render graph's own table, not a system's: a pass builds its PSO layout
    //! from it, and a group is created from it when a frame first needs one. It must match the
    //! group's .hlsli; a pass's shaders are checked against it (BuildPassPipelineLayout).
    //!
    //! Null when no group declares `spaceId`.
    const RHI::ShaderInputList* FindSharedGroupInputs(uint32_t spaceId);

    //! `spaceId`'s layout, built once from its inputs: the whole space, with stage masks left
    //! None. A pass appends it with its own stages, so every pass lays the group out the same
    //! way while binding it only in the stages it uses.
    //!
    //! Null when no group declares `spaceId`.
    const RHI::PipelineLayoutDescriptor* FindSharedGroupLayout(RHI::Factory& factory, uint32_t spaceId);
}
