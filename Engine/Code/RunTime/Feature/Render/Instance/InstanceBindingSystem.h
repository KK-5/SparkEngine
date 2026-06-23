#pragma once

#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include "InstanceData.h"

namespace Spark::Render
{
    //! Per-instance frequency system. Owns ONE shared StructuredBuffer (g_Instances,
    //! space1) plus the ShaderBindings entity that exposes its SRV, and a static
    //! per-instance vertex-stream ID buffer ([0, 1, ..., Capacity-1]) that delivers
    //! each draw's InstanceIndex through hardware instancing (PER_INSTANCE_DATA +
    //! StartInstanceLocation; see TODO_InstanceBindingSystemPlan.md §2.5).
    //!
    //! Each frame Update() rebuilds the buffer from scratch: clear last frame's
    //! InstanceSlot components, walk the renderable entities, scatter their transform
    //! into g_Instances, and stamp a per-frame InstanceSlot back on each entity.
    //!
    //! Contract (plan §11): InstanceSlot exists ⟺ entity is renderable THIS frame; the
    //! index value is per-frame and MUST NOT be cached across frames by consumers.
    //!
    //! Not an ISystem: a plain helper owned by RenderSystem and driven from
    //! RenderSystem::OnTick, sequenced after ViewBindingSystem and before the
    //! Processors so g_Instances is ready before any draw references it.
    class InstanceBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! frameIndex is the in-flight slot (swap-chain GetCurrentImageIndex), used to
        //! pick this frame's g_Instances copy. See BindFrameInstances for the frame-index
        //! caveat. RenderSystem::OnTick passes it.
        void Update(uint32_t frameIndex);
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Fixed upper bound. Overflow logs an error and drops the surplus entities
        //! (rendering is not blocked). At Model-only size this is 4 MB (g_Instances) +
        //! 256 KB (ID buffer) — trivially cheap, ~10x headroom over typical scenes.
        //! Hitting it is an optimization signal (plan §10 Y4).
        static constexpr uint32_t Capacity = 65536;

        //! Binds frameIndex's g_Instances copy as the structured SRV, every frame.
        //! Returns false until the ECS has materialized BufferPerFrame (one warmup frame
        //! after Init — non-trivial GPU buffers go through the unified deferred
        //! PendingBufferInit → RHIResourceSystem::OnFrameBegin path on purpose, so the
        //! buffer object does not exist at Init; do NOT "optimize" this into a synchronous
        //! Init create). Data upload is separate (PendingBufferMap / ProcessBufferMaps).
        //!
        //! g_Instances is PER-FRAME (PerFrameTag → N copies) so each frame writes its own
        //! copy without racing the GPU reading last frame's (plan §G2). The SRV therefore
        //! re-binds to frameIndex's copy every frame (one cheap descriptor recompile, like
        //! ViewBindings).
        //!
        //! ⚠ CAVEAT: this frameIndex MUST select the same copy ProcessBufferMaps wrote
        //! (it uses RHIResourceSystem::m_frameIndex). Today that holds only by implicit
        //! lockstep (both mod the device frameCountMax, both advance once per frame). The
        //! robust fix is Device::GetCurrentFrameIndex() — see TODO_AsyncUpload_RemainingIssues.md.
        bool BindFrameInstances(uint32_t frameIndex);

        // Shared resources, owned by their RHIContext entities (this system holds only
        // handles + CPU staging buffers).
        RHI::RHIHandle m_bufferEntity   = RHI::NullHandle;  // Components::BufferPerFrame — host StructuredBuffer<InstanceData>, N copies
        RHI::RHIHandle m_bindingsEntity = RHI::NullHandle;  // Components::ShaderBindings — g_Instances @ space1
        RHI::RHIHandle m_idBufferEntity = RHI::NullHandle;  // Components::Buffer — per-instance vertex stream ([0..Capacity-1])

        // CPU staging for g_Instances. Filled each frame, then handed to the current
        // frame's copy via PendingBufferMap — RHIResourceSystem::ProcessBufferMaps does
        // the Map/memcpy/Unmap (and picks the per-frame slot) for us.
        // Lives for the system's lifetime so the PendingBufferMap source stays valid.
        eastl::vector<InstanceData> m_instanceData;

        // CPU-side source for the one-time ID-buffer upload. Must outlive the async
        // upload (PendingBufferUpload contract), so it lives for the system's lifetime.
        eastl::vector<uint32_t> m_idData;
    };
}
