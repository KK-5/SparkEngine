#pragma once

#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include "LightData.h"
#include "ShadowViewData.h"

namespace Spark::Render
{
    //! Owns the per-scene ShaderBindings (space0): the g_Lights StructuredBuffer + the
    //! SceneConstants cbuffer (g_LightCount). Each frame it MARSHALS every world
    //! Light::LightRenderData into the current frame's g_Lights copy and writes g_LightCount
    //! — a pure field copy, no computation (direction/position were resolved upstream by
    //! LightSystem). Tags the binding entity MainSceneTag so a pass pulls it via
    //! .Binds<MainSceneTag>(), exactly like the per-view group.
    //!
    //! Host per-frame + full re-scatter every frame, symmetric with MaterialBindingSystem
    //! (lights are few, KB-level). Plain helper, not ISystem — owned by RenderSystem, ticked
    //! among the binding systems (before the pass processors that consume MainSceneTag).
    class SceneBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! frameIndex is the in-flight slot (swap-chain image index), picking this frame's
        //! g_Lights copy — same frame-index contract as MaterialBindingSystem.
        void Update(uint32_t frameIndex);
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Fixed upper bound on live lights. Overflow logs and drops the surplus. 256 * 64B
        //! = 16 KB per frame copy — trivially cheap.
        static constexpr uint32_t Capacity = 256;

        //! Binds frameIndex's g_Lights copy as the structured SRV, every frame. Returns
        //! false until the ECS materializes BufferPerFrame (one warmup frame after Init).
        bool BindFrameLights(uint32_t frameIndex);

        //! Same, for g_ShadowViews.
        bool BindFrameShadowViews(uint32_t frameIndex);

        //! Fills m_shadowViewData from the live shadow view entities, addressed by tile slot.
        //! Unallocated slots stay zeroed. The authored bias is NOT written here — it lives on
        //! the light, so the light loop adds it.
        void PackShadowViews(RHI::RHIContext& rhiCtx);

        struct EnvironmentBinding
        {
            //! 0 == no environment bound: the shader must not sample the cubes.
            uint32_t m_prefilteredMipCount = 0;
            //! Independent of the mip count — a skybox whose IBL is still uploading should
            //! already tint the visible sky.
            float    m_intensity = 1.0f;
        };

        //! Binds the active skybox's IBL cubes + sampler into space0 and reports the
        //! matching constants. Stays at defaults until every image is ready.
        EnvironmentBinding BindEnvironmentIBL();

        //! Loads the checked-in BRDF LUT and hands it to the GPU as a static image. Scene
        //! independent, so it happens once at Init rather than following the skybox.
        void CreateBRDFLut(RHI::RHIContext& rhiCtx);

        // Shared resources, owned by their RHIContext entities (this system holds handles).
        RHI::RHIHandle m_buffer   = RHI::NullHandle;  // Components::BufferPerFrame — host StructuredBuffer<LightData>, N copies
        RHI::RHIHandle m_bindings = RHI::NullHandle;  // Components::ShaderBindings — g_Lights + SceneConstants @ space0
        RHI::RHIHandle m_brdfLut  = RHI::NullHandle;  // static 2D RG16F DFG table, created once at Init
        RHI::RHIHandle m_shadowViewBuffer = RHI::NullHandle;  // host StructuredBuffer<ShadowViewData>, N copies

        // CPU staging for g_Lights. Filled each frame, handed to the current frame's copy
        // via PendingBufferMap. Lives for the system's lifetime so the map source stays
        // valid (PendingBufferMap contract, like MaterialBindingSystem::m_materialData).
        eastl::vector<LightData> m_lightData;

        //! Same contract, sized to the atlas tile count rather than Capacity: the buffer is
        //! addressed by tile slot.
        eastl::vector<ShadowViewData> m_shadowViewData;
    };
}
