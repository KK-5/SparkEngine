#pragma once

#include <ECS/WorldContext.h>

#include <RHI/Context/RHIContext.h>

#include <Binding/StagedArrayBuffer.h>

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
    //! Host per-frame + full re-scatter every frame. Both arrays deliberately stay on the
    //! bare StagedArrayBuffer rather than GlobalBuffer's stable slots: nothing stores a
    //! light index across frames (the shader iterates g_Lights), and g_ShadowViews is
    //! addressed by a row ShadowAtlasAllocator hands out. Stable slots would only buy
    //! holes here — see TODO_GlobalBufferUploadPlan.md §8.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked among the binding
    //! systems (before the pass processors that consume MainSceneTag).
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

        //! Fills m_shadowViews from the live shadow view entities, addressed by tile slot.
        //! Unallocated slots stay zeroed. The authored bias is NOT written here — it lives on
        //! the light, so PackLightData adds it.
        void PackShadowViews(RHI::RHIContext& rhiCtx);

        //! Fills m_lights from the live lights, packed densely in iteration order, and
        //! returns the count — which is g_LightCount, the length the shader iterates.
        //!
        //! Also completes the g_ShadowViews rows each light owns with that light's authored
        //! bias, so it must run AFTER PackShadowViews, which zeroes those rows first.
        //! shadowViewsBound false means that array has no copy this frame, and the bias
        //! write is skipped.
        uint32_t PackLightData(WorldContext& world, bool shadowViewsBound);

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
        RHI::RHIHandle m_bindings = RHI::NullHandle;  // Components::ShaderBindings — g_Lights + SceneConstants @ space0
        RHI::RHIHandle m_brdfLut  = RHI::NullHandle;  // static 2D RG16F DFG table, created once at Init

        //! Packed densely in iteration order, [0, g_LightCount).
        StagedArrayBuffer<LightData> m_lights;

        //! Sized to the atlas row count rather than Capacity, and addressed by row, so its
        //! upload always spans the whole array.
        StagedArrayBuffer<ShadowViewData> m_shadowViews;
    };
}
