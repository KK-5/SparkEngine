#pragma once

#include <ECS/WorldContext.h>
#include <Tick/FrameTime.h>

#include <RHI/Context/RHIContext.h>

#include <EASTL/vector.h>

#include <Staging/StagingBuffer.h>

#include "LightData.h"
#include "ShadowViewData.h"

namespace Spark::Render
{
    //! Owns the per-scene ShaderBindings (space0): the g_Lights StructuredBuffer + the
    //! SceneConstants cbuffer (g_LightCount). Each frame it MARSHALS every world
    //! Light::LightRenderData into the current frame's g_Lights copy and writes g_LightCount
    //! — a pure field copy, no computation (direction/position were resolved upstream by
    //! LightSystem). Tags the binding entity MainSceneTag so a pass pulls it via
    //! .Binds<MainSceneTag>(), exactly like the view group.
    //!
    //! Both arrays are small and change every frame, so each is a StagingBuffer rewritten
    //! whole: put together in CPU memory first, then copied in once, as the mapped memory is
    //! write-combined and slow to read back. Nothing stores a light index across frames (the
    //! shader iterates g_Lights), and g_ShadowViews is addressed by a row ShadowAtlasAllocator
    //! hands out.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked among the binding
    //! systems (before the pass processors that consume MainSceneTag).
    class SceneBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void Update(const FrameTime& time);
        //! After the GPU is idle.
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Fills m_shadowViews from the live shadow view entities, addressed by tile
        //! slot. Unallocated slots stay zeroed. The authored bias is NOT written here — it
        //! lives on the light, so PackLightData adds it.
        void PackShadowViews(RHI::RHIContext& rhiCtx);

        //! Fills m_lights from the live lights, packed densely in iteration order; its
        //! size is g_LightCount, the length the shader iterates.
        //!
        //! Also completes the g_ShadowViews rows each light owns with that light's authored
        //! bias, so it must run AFTER PackShadowViews, which zeroes those rows first.
        void PackLightData(WorldContext& world);

        struct EnvironmentBinding
        {
            //! 0 == no environment bound: the shader must not sample the cubes.
            uint32_t m_prefilteredMipCount = 0;
            //! Independent of the mip count — a skybox whose IBL is still uploading should
            //! already tint the visible sky.
            float    m_intensity = 1.0f;
        };

        //! Binds the active skybox's IBL cubes and the BRDF LUT into space0 and reports the
        //! matching constants. Stays at defaults until every image is ready.
        EnvironmentBinding BindEnvironmentIBL();

        //! Binds the two LTC tables into space0. False until both are ready, and then the
        //! shader must not read either — which is what g_AreaLightLutReady tells it.
        bool BindAreaLightLuts(RHI::RHIContext& rhiCtx);

        //! Loads a checked-in table and hands it to the GPU as a static image. Scene
        //! independent, so it happens once at Init rather than following the skybox.
        //! NullHandle when the asset is missing, which every reader treats as never ready.
        RHI::RHIHandle CreateLut(RHI::RHIContext& rhiCtx, const char* assetPath, const char* name);

        // Shared resources, owned by their RHIContext entities (this system holds handles).
        RHI::RHIHandle m_bindings = RHI::NullHandle;  // Components::ShaderBindings — g_Lights + SceneConstants @ space0
        RHI::RHIHandle m_brdfLut  = RHI::NullHandle;  // static 2D RG16F DFG table, created once at Init
        RHI::RHIHandle m_ltc1     = RHI::NullHandle;  // static 2D RGBA16F, LTC inverse matrix
        RHI::RHIHandle m_ltc2     = RHI::NullHandle;  // static 2D RGBA16F, LTC amplitude / Fresnel

        //! Packed densely in iteration order, [0, g_LightCount).
        StagingBuffer            m_lightBuffer;
        eastl::vector<LightData> m_lights;

        //! Every atlas row, addressed by row, so the whole array is written.
        StagingBuffer                 m_shadowViewBuffer;
        eastl::vector<ShadowViewData> m_shadowViews;
    };
}
