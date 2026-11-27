#pragma once

#include <ECS/Bus/EntityEventBus.h>
#include <ECS/WorldContext.h>
#include <Tick/FrameTime.h>

#include <RHI/Context/RHIContext.h>

#include <EASTL/vector.h>

#include <Binding/DeviceArray.h>
#include <Pass/PassContext.h>
#include <Staging/StagingBuffer.h>

#include "SceneBinding.h"
#include "ShadowViewData.h"

namespace Spark::Render
{
    //! Publishes the per-scene inputs in space0. Lights keep their slots across frames.
    //! Shadow views and their map indices are rebuilt after the shadow and view systems run.
    class SceneBindingSystem : private EntityEventBus::Handler
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void SetUp(PassContext& passContext);
        void Update(const FrameTime& time);
        //! After the GPU is idle.
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Fills m_shadowViews from the lights holding shadow maps, a row a committed shadow
        //! map, and m_viewShadowMaps from their views' rows of g_Views. After ViewBindingSystem
        //! has given the views their rows.
        void PackShadowViews(WorldContext& world, RHI::RHIContext& rhiCtx);

        void AllocateLightSlots(WorldContext& world);
        void PrepareEmissiveLights(WorldContext& world);
        uint32_t UpdateLights(WorldContext& world);

        void OnEntityCreate(Entity) override {}
        void OnEntityDestory(Entity entity) override;

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

        // Shared resources, owned by their RHIContext entities (this system holds handles).
        //! This frame's sampler and constants for space0, which no resource carries. The group
        //! itself belongs to the render graph.
        RHI::RHIHandle m_values = RHI::NullHandle;

        DeviceArray m_lightBuffer;

        //! A row a shadow map, all of them written.
        StagingBuffer                 m_shadowViewBuffer;
        eastl::vector<ShadowViewData> m_shadowViews;

        //! A shadow view's g_Views row -> the shadow map it renders into.
        StagingBuffer           m_viewShadowMapBuffer;
        eastl::vector<uint32_t> m_viewShadowMaps;

        //! The tagged staging slot for each shadow buffer follows the current frame.
        RHI::RHIHandle m_shadowViewSlot = RHI::NullHandle;
        RHI::RHIHandle m_viewShadowMapSlot = RHI::NullHandle;
    };
}
