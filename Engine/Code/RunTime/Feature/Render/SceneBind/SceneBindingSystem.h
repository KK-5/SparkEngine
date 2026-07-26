#pragma once

#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include "LightData.h"

namespace Spark::Render
{
    //! Owns the per-scene ShaderBindings (space0): the g_Lights StructuredBuffer + the
    //! SceneConstants cbuffer (g_LightCount). Each frame it MARSHALS every world
    //! Light::LightRenderData into the current frame's g_Lights copy and writes g_LightCount
    //! — a pure field copy, no computation (direction/position were resolved upstream by
    //! LightSystem). Tags the binding entity MainSceneTag so a pass pulls it via
    //! .Binds<MainSceneTag>() (BindPassDrawItems), exactly like the per-view group.
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

        // Shared resources, owned by their RHIContext entities (this system holds handles).
        RHI::RHIHandle m_buffer   = RHI::NullHandle;  // Components::BufferPerFrame — host StructuredBuffer<LightData>, N copies
        RHI::RHIHandle m_bindings = RHI::NullHandle;  // Components::ShaderBindings — g_Lights + SceneConstants @ space0

        // CPU staging for g_Lights. Filled each frame, handed to the current frame's copy
        // via PendingBufferMap. Lives for the system's lifetime so the map source stays
        // valid (PendingBufferMap contract, like MaterialBindingSystem::m_materialData).
        eastl::vector<LightData> m_lightData;
    };
}
