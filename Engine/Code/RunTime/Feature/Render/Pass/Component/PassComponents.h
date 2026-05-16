#pragma once

#include <Base.h>
#include <EASTL/fixed_vector.h>
#include <Object/ObjectName.h>

#include <RHI/Resource/ShaderResource/InputStreamLayout.h>
#include <RHI/Attachment/RenderAttachmentLayout.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/Pipeline/RenderTargetLayout.h>
#include <RHI/Pipeline/PipelineState.h>
#include <RHI/Resource/ResourceState.h>
#include <RHI/HardwareQueue.h>
#include <RHI/RHILimits.h>

#include <Resource/Shader/ShaderAsset.h>

#include <Pass/Pass.h>
#include <RHI/Context/RHIContext.h>
#include <Pass/Component/RHIComponents.h>

namespace Spark::RHI
{
    class CommandList;
}

namespace Spark::Render
{
    class RenderGraphBuilder;
    class RenderGraphCompiler;
    class RenderGraphExecuter;
    struct ExecuteWork;


    struct RenderPassTag
    {
    };

    struct ComputePassTag
    {
    };

    struct RayTracingPassTag
    {
    };

    struct ActivePassTag
    {
    };

    //! Marks a pass that manages its own pipeline state (e.g. ImGui), skipping
    //! engine-side PSO compilation. PSO compiler skips passes with this tag.
    struct CustomPipelinePassTag
    {
    };

    //! Tags pass entities that are synthesized by the compiler each frame
    //! (currently: final-transition sink passes from CompileFinalTransitionBarrier).
    //! Executer.End walks this view to destroy these entities so they don't leak.
    struct SinkPassTag
    {
    };

    ////////////////////////////////////////////////////
    struct PassName
    {
        ObjectName m_name {};
    };

    struct PassExecuteQueue
    {
        RHI::HardwareQueueClass m_queue;
    };

    //! Index of the pass in RenderGraphBuilder::TopoSort()'s returned linear order.
    //! Assigned in TopoSort itself, so any code consuming the topo-sorted span can
    //! rely on `m_position == span index`. Used as the opaque ordering key for
    //! cross-queue sync (timeline semaphore values) and for transient resource
    //! lifetime overlap analysis.
    struct PassGlobalTimeline
    {
        uint32_t m_position {0};
    };

    //////////////////////////////
    // PSO components

    //! Fixed-function pipeline state: input layout, render-target formats,
    //! and raster/blend/depth states. Bundled into one component so the
    //! builder and PSO compiler don't need to track three separate pieces.
    struct PassPipelineState
    {
        RHI::InputStreamLayout  m_inputStreamLayout  {};
        RHI::RenderTargetLayout m_renderTargetLayout {};
        RHI::RenderStates       m_renderStates       {};
    };

    struct PassShaders
    {
        Ptr<Resource::ShaderAsset> m_vertexShader   = nullptr;
        Ptr<Resource::ShaderAsset> m_fragmentShader = nullptr;
        Ptr<Resource::ShaderAsset> m_geometryShader = nullptr;
        Ptr<Resource::ShaderAsset> m_computeShader  = nullptr;
    };

    //! Compiled PSO cache. Written by PSO compiler, read by executer.
    struct PassCompiledPSO
    {
        Ptr<RHI::PipelineState> m_pso;
    };

    //! Slot-indexed table of SRG entity references the pass uses. Slot index
    //! corresponds to the shader register space / Vulkan set index — builder
    //! sets the slot explicitly so this aligns with the shader contract.
    //!
    //! NullHandle entries are unused slots. Non-null entries point into the
    //! RHIContext at SRG entities; the consumer dispatches on component
    //! presence:
    //!  - SRG entity has BackingShaderResource → executer binds at pass begin.
    //!  - SRG entity lacks BackingShaderResource (layout-only) → execute
    //!    lambda is responsible for binding per draw.
    //!
    //! In both cases the PSO compiler reads ShaderResourceLayout off the SRG
    //! entity to assemble PipelineLayoutDescriptor.
    struct PassShaderResources
    {
        eastl::fixed_vector<RHIHandle, RHI::Limits::Pipeline::ShaderResourceCountMax> m_slots;
    };

    //! Forces PSO recompilation on next frame (set on shader hot-reload).
    struct PassPSODirtyTag
    {
    };
    //////////////////////////////

    struct PassFunctions
    {
        eastl::function<void(RenderGraphBuilder&)> m_buildFunction;
        eastl::function<void(RenderGraphCompiler&)> m_compileFunction;
        eastl::function<void(ExecuteWork&, RenderGraphExecuter&)> m_executeFunction;
    };

    // External fence waits that must be issued on the GPU queue before this
    // pass's pre-barriers. Populated by barrier compiler on first-touch of a
    // cross-queue resource carrying PendingSync (e.g. upload fence).
    struct PassExternalFenceWaits
    {
        eastl::vector<RHI::PendingSync> m_waits;
    };

    // Compiled barriers for a single pass. Filled by CompileImageBarriers /
    // CompileBufferBarriers, consumed by execute. Per-frame, cleared at frame end.
    struct PassBarriers
    {
        eastl::vector<RHI::ImageBarrier>  m_preImage;
        eastl::vector<RHI::BufferBarrier> m_preBuffer;
        eastl::vector<RHI::ImageBarrier>  m_postImage;
        eastl::vector<RHI::BufferBarrier> m_postBuffer;
        
        //!  Must be issued before the state-transition barriers above so that the heap
        //! range ownership transfer happens before any layout/state work.
        eastl::vector<RHI::AliasingBarrier>  m_preAliasing;
    };

    /////////////////////////////////////////////////////
    // Sync cross queue component. Process per pass, so it has container.
    // Clean every frame
    struct PassPredecessors
    {
        eastl::vector<Pass> m_preds;
    };

    struct PassSuccessors
    {
        eastl::vector<Pass> m_succs;
    };

    struct SyncOperation
    {
        SyncOperation() = default;
        SyncOperation(RHI::HardwareQueueClass queue, uint64_t value)
            : m_queue(queue), m_value(value)
        {
        }

        RHI::HardwareQueueClass m_queue;  // 所属队列,wait 时是源,signal 时是己方
        uint64_t                m_value;
    };

    struct PassSyncWait
    {
        eastl::vector<SyncOperation> m_waits;
    };

    struct PassSyncSignal
    {
        SyncOperation m_signal;
    };
    /////////////////////////////////////////////////////

    // Lives on a Pass entity. Engine invokes m_markFn during the compile phase to
    // tag this pass's PassAttachments with AttachmentCompilingTag, so engine-level
    // barrier compilation can iterate them without scanning all attachments.
    struct PassAttachmentMarker
    {
        void (*m_markFn)(RHIContext&) = nullptr;
    };

    template <typename PassTagT>
    PassAttachmentMarker MarkPassAttachmentCompiling()
    {
        PassAttachmentMarker result;

        result.m_markFn = [](RHIContext& ctx)
        {
            ctx.GetView<ImagePassAttachment, PassTagT>().each(
                [&ctx](auto handle, const ImagePassAttachment&)
                {
                    ctx.Add<AttachmentCompilingTag>(handle);
                });

            ctx.GetView<BufferPassAttachment, PassTagT>().each(
                [&ctx](auto handle, const BufferPassAttachment&)
                {
                    ctx.Add<AttachmentCompilingTag>(handle);
                });
        };

        return result;
    }
}