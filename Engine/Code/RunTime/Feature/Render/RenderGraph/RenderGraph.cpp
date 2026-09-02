#include "RenderGraph.h"

#include <Log/ILogSystem.h>

#include <Core/Service/Service.h>

#include <RHI/Factory.h>
#include <RHI/Command/CommandQueue.h>
#include <RHI/Command/CommandList.h>
#include <RHI/SwapChain/SwapChain.h>
#include <RHI/Bus/FrameEventBus.h>
#include <RHI/Resource/Transient/TransientResourcePool.h>
#include <RHI/Pipeline/PipelineLibrary.h>

#include <Pass/Pipeline.h>
#include <Pass/Component/RHIComponents.h>
#include <Pass/Component/PassComponents.h>

namespace Spark::Render
{
    bool RenderGraph::Init(RHI::Device& device)
    {
        if (m_commandQueueContext.Init(device) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderGraph] CommandQueueContext Init failed.");
            return false;
        }

        m_crossQueueFences.Init(device, RHI::FenceState::Reset);

        auto* factoryPtr = Service<RHI::Factory>::Get();
        ASSERT(factoryPtr != nullptr, "RHI::Factory service is not registered.");
        auto& factory = *factoryPtr;

        m_pool = factory.CreateTransientResourcePool();
        ASSERT(m_pool != nullptr, "[RenderGraph] Factory::CreateTransientResourcePool returned null.");
        RHI::TransientResourcePoolDescriptor desc;
        desc.m_allowCrossBatchReuse = false;  // There is a bug in cross batch reuse
        if (m_pool->Init(device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderGraph] TransientResourcePool initialize failed.");
            return false;
        }

        m_pipelineLibrary = factory.CreatePipelineLibrary();
        ASSERT(m_pipelineLibrary != nullptr, "[RenderGraph] Factory::CreatePipelineLibrary returned null.");
        RHI::PipelineLibraryDescriptor pipelineLibraryDesc;
        if (m_pipelineLibrary->Init(device, pipelineLibraryDesc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderGraph] PipelineLibrary init failed.");
            return false;
        }

        m_device = &device;

        return true;
    }

    bool RenderGraph::ImportSwapChain(RHI::SwapChain& swapChain)
    {
        ASSERT(m_device != nullptr, "[RenderGraph] ImportSwapChain called before Init.");

        auto& context = *RHIExecuteContext::Current();

        m_swapchainResource = context.CreateEntity();
        RefreshSwapChainImages(swapChain, context);
        context.Add<ImportedTag>(m_swapchainResource);
        context.Add<RHI::PerFrameTag>(m_swapchainResource);
        context.Add<ResourceName>(m_swapchainResource, ObjectName{"SwapChain"});
        // NOTE: swap chain Present-state transition at frame end is TBD —
        // CompileFinalTransitionBarrier was removed alongside ImportedResourceState.
        //
        // Swap chain views are no longer pre-built into a SwapChainViews component.
        // They are materialized lazily, per frame, via the resource's
        // ImageViewCachePerFrame (GetOrCreateImageViewPerFrame), keyed by the
        // attachment's view descriptor and indexed by frameIndex.
        return true;
    }

    void RenderGraph::RefreshSwapChainImages(RHI::SwapChain& swapChain, RHIContext& context)
    {
        ASSERT(m_swapchainResource != NullHandle,
            "[RenderGraph] RefreshSwapChainImages called without a swap chain resource entity.");

        const uint32_t imageCount = swapChain.GetImageCount();
        ASSERT(imageCount <= RHI::Limits::Device::FrameCountMax,
            "[RenderGraph] Swap chain has {} images, more than FrameCountMax.", imageCount);

        // Value-initialized so slots past imageCount stay null instead of holding
        // back buffers from before a resize.
        SwapChainImages swapChainImages {};
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            swapChainImages.images[i] = swapChain.GetImage(i);
        }
        context.AddOrReplace<SwapChainImages>(m_swapchainResource, eastl::move(swapChainImages));
    }

    void RenderGraph::WaitForGpuIdle()
    {
        for (uint32_t i = 0; i < RHI::HardwareQueueClassCount; ++i)
        {
            m_commandQueueContext.GetCommandQueue(static_cast<RHI::HardwareQueueClass>(i)).WaitForIdle();
        }
    }

    bool RenderGraph::ResizeSwapChain(RHI::SwapChain& swapChain, const Math::Vector2Int& size)
    {
        ASSERT(m_device != nullptr, "[RenderGraph] ResizeSwapChain called before Init.");

        if (size.x <= 0 || size.y <= 0)
        {
            return false;
        }

        if (m_swapchainResource == NullHandle)
        {
            LOG_ERROR("[RenderGraph] ResizeSwapChain called before ImportSwapChain.");
            return false;
        }

        auto& context = *RHIExecuteContext::Current();

        // SwapChain::Resize releases the back buffers outright (no deferred release),
        // and ResizeBuffers refuses to run while any reference is outstanding. All
        // queues, not just Graphics: an image can end the frame on Compute or Copy.
        WaitForGpuIdle();

        // The cached per-frame views were built over the old images; drop them so the
        // next frame rebuilds them lazily.
        context.Remove<RHI::Components::ImageViewCachePerFrame>(m_swapchainResource);
        context.Remove<BackingImage>(m_swapchainResource);

        RHI::SwapChainDimensions dimensions = swapChain.GetDescriptor().m_dimensions;
        dimensions.m_imageWidth  = static_cast<uint32_t>(size.x);
        dimensions.m_imageHeight = static_cast<uint32_t>(size.y);

        if (swapChain.Resize(dimensions) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderGraph] Swap chain resize to {}x{} failed.", size.x, size.y);
            return false;
        }

        RefreshSwapChainImages(swapChain, context);
        return true;
    }

    void RenderGraph::Shutdown()
    {
        if (m_device == nullptr)
        {
            return;
        }

        // Drain the GPU before releasing anything the in-flight command lists
        // could still reference (swap chain views, transient pool allocations,
        // PSOs in the pipeline library, etc.).
        WaitForGpuIdle();

        auto& context = *RHIExecuteContext::Current();
        if (m_swapchainResource != NullHandle && context.Valid(m_swapchainResource))
        {
            context.DestoryEntity(m_swapchainResource);
            m_swapchainResource = NullHandle;
        }

    }

    void RenderGraph::ExecutePipeline(PassContext& passContext, uint32_t frameIndex, const Math::Vector2Int& renderSize)
    {
        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameBegin);
        m_commandQueueContext.Begin();

        auto& context = *RHIExecuteContext::Current();

        // Refresh borrowed pointers (BackingImage / BackingBuffer / Backing*View)
        // for any per-frame imported resources whose Owning component rotates with
        // frameIndex. Single-frame imports are handled lazily by the builder.
        // Swap chain BackingImage (from SwapChainImages) is also refreshed here.
        RefreshPerFrameBackings(context, frameIndex);

        ////////////////////////////////////////////////
        // Build
        // Iterate passes in declaration order so that attachment version
        // tracking (LookupLatestVersion / BumpVersion) converges deterministically
        // regardless of entt's pool order.
        m_builder.Begin(frameIndex, m_swapchainResource, renderSize);
        for (Pass pass : passContext.GetPassesInDeclOrder())
        {
            if (!passContext.Has<ActivePassTag>(pass))
            {
                continue;
            }
            auto& funcs = passContext.Get<PassFunctions>(pass);
            m_builder.BeginPass(pass);
            if (funcs.m_buildFunction)
            {
                funcs.m_buildFunction(m_builder);
            }
            m_builder.EndPass();
        }
        eastl::vector<Pass> passes = m_builder.End();
        ////////////////////////////////////////////////

        ////////////////////////////////////////////////
        // Compile
        m_compiler.Begin(frameIndex);

        // Transient resources must be materialized (backing allocated) before the
        // rest of compile. Views are no longer materialized here — they are resolved
        // on demand from each resource's view cache. A ShaderBindings that samples a
        // transient image must obtain its view (FindPassAttachmentImageView) before
        // CompileShaderInputs so the descriptor is compiled with it.
        m_compiler.CompileTransientResources(*m_pool);

        m_compiler.CompilePipelineStates(passContext, *m_device, m_pipelineLibrary.get());

        StaticPreBarrierTable staticPreBarriers = m_compiler.CompileStaticResourceBarriers(context);

        for (auto pass : passes)
        {
            ASSERT(passContext.Has<PassAttachmentMarker>(pass),
                "The Pass {} has not PassAttachmentMarker", passContext.Get<PassName>(pass).m_name.GetCStr());
            passContext.Get<PassAttachmentMarker>(pass).m_markFn(context);

            auto& func = passContext.Get<PassFunctions>(pass);
            if (func.m_compileFunction)
            {
                func.m_compileFunction(m_compiler);
            }

            m_compiler.CompileResourceBarriers(pass, passContext, context, *m_pool);

            if (passContext.Has<RenderPassTag>(pass))
            {
                m_compiler.CompileRenderPassBeginInfo(pass, passContext, context);
            }

            context.Clear<AttachmentCompilingTag>();
        }

        m_compiler.CompilePassSharedBindings(passContext, context);

        m_compiler.CompileShaderInputs(*m_device, context);

        QueueBasedPasses queueBasedPasses = m_compiler.CompilePassCrossQueue2(passes);

        m_compiler.End();
        ////////////////////////////////////////////////

        ////////////////////////////////////////////////
        // Execute
        m_executer.Begin(frameIndex);

        m_executer.SetStaticPreBarriers(eastl::move(staticPreBarriers));
        m_executer.BuildExecuteTable(queueBasedPasses, passContext);

        auto* factory = Service<RHI::Factory>::Get();
        ASSERT(factory, "[RenderGraph] RHI::Factory service not registered.");

        auto& queueSegments = m_executer.GetQueueSegments();

        for (uint32_t qi = 0; qi < static_cast<uint32_t>(RHI::HardwareQueueClass::Count); ++qi)
        {
            auto& segments = queueSegments[qi];
            if (segments.empty())
            {
                continue;
            }

            const auto queueClass = static_cast<RHI::HardwareQueueClass>(qi);
            auto& queue = m_commandQueueContext.GetCommandQueue(queueClass);

            // Static-resource pre-frame barriers — only first frame when
            // resources transition from upload state to steady state.
            if (!m_executer.m_staticPreBarriers[qi].IsEmpty())
            {
                for (auto& sync : m_executer.m_staticPreBarriers[qi].m_fenceWaits)
                {
                    queue.Wait(*sync.m_fence, sync.m_fenceValue);
                }

                auto* preCmdList = factory->CreateCommandList(*m_device, queueClass);
                preCmdList->Open();
                m_executer.ExecuteStaticPreBarriers(preCmdList, qi);
                preCmdList->Close();

                queue.ExecuteCommands({ &preCmdList, 1 });
            }

            for (auto& segment : segments)
            {
                for (const auto& wait : segment.m_waits)
                {
                    queue.Wait(m_crossQueueFences.GetFence(wait.m_queue), wait.m_value);
                }

                for (const auto& wait : segment.m_externalWaits)
                {
                    queue.Wait(*wait.m_fence, wait.m_fenceValue);
                }

                for (auto& group : segment.m_groups)
                {
                    eastl::vector<RHI::CommandList*> cmdLists;

                    for (auto& work : group.m_works)
                    {
                        m_executer.Execute(work, *factory, *m_device, queueClass, passContext);
                        cmdLists.push_back(work.m_commandList);
                    }

                    queue.ExecuteCommands(cmdLists);
                }

                if (segment.m_signal)
                {
                    queue.Signal(m_crossQueueFences.GetFence(segment.m_signal->m_queue), segment.m_signal->m_value);
                }
            }
        }

        // Frame-end: signal each active queue's cross-queue fence one more time.
        // Two consumers depend on this value being live before they run:
        //  - Swap chain Present transition (below): may queue.Wait on a
        //    non-Graphics producer fence if the swap chain image ends on
        //    Compute/Copy (e.g. compute final composite).
        //  - PendingSync stamp (further below): writes this value onto every
        //    imported resource's PendingSync component.
        //
        // m_crossQueueFenceValues is **shared** between two writers:
        //  - CompilePassCrossQueue2 (in-frame, allocates values for cross-queue
        //    pair signals on segment.m_signal)
        //  - this frame-end step (one extra ++ per active queue, used as the
        //    "frame-end timestamp")
        // Both writers are monotonic in submission order on the same queue, so
        // sharing one counter is safe.
        for (uint32_t qi = 0; qi < static_cast<uint32_t>(RHI::HardwareQueueClass::Count); ++qi)
        {
            if (queueSegments[qi].empty())
            {
                continue;
            }

            const auto queueClass = static_cast<RHI::HardwareQueueClass>(qi);
            auto& queue = m_commandQueueContext.GetCommandQueue(queueClass);

            const uint64_t value = ++m_compiler.m_crossQueueFenceValues[qi];
            queue.Signal(m_crossQueueFences.GetFence(queueClass), value);
        }

        SubmitSwapChainPresentTransition(*RHIExecuteContext::Current(), *factory);

        // Stamp PendingSync on every dynamic imported resource the RG touched
        // this frame, using the producer queue's frame-end fence value (signaled
        // above). Note: Graphics queue's stamp value reflects "before the swap
        // chain Present transition cmd list", but no one consumes PendingSync
        // on a swap chain (they're never re-used by other systems), so the
        // small inconsistency is harmless.
        // Static imported resources are handled by a separate block below.
        {
            auto& ctx = *RHIExecuteContext::Current();
            ctx.GetView<ImportedTag, ResourceStateTracker>().each(
                [&](RHIHandle resource, const ResourceStateTracker& tracker)
                {
                    const auto qi = static_cast<uint32_t>(tracker.m_current.m_queue);
                    ASSERT(qi < RHI::HardwareQueueClassCount,
                        "ResourceStateTracker for {} has invalid m_queue ({}).",
                        ctx.Has<ResourceName>(resource)
                            ? ctx.Get<ResourceName>(resource).m_name.GetCStr()
                            : "[Unnamed]",
                        qi);
                    RHI::PendingSync sync;
                    sync.m_fence = &m_crossQueueFences.GetFence(tracker.m_current.m_queue);
                    sync.m_fenceValue = m_compiler.m_crossQueueFenceValues[qi];
                    ctx.AddOrReplace<RHI::PendingSync>(resource, sync);
                });
        }

        // Stamp PendingSync on every static imported resource with the home
        // queue's frame-end fence value.
        //
        // Static resources are excluded from per-pass barrier tracking — no
        // ResourceStateTracker is created for them, so we cannot tell which
        // ones were actually sampled this frame. The conservative choice is to
        // stamp all of them: from the GPU's perspective any texture resident on
        // the home queue may have been read by a shader, and the upload system
        // (streaming textures) must wait for this fence before re-uploading.
        //
        // Guard: skip if the home queue had no work this frame. Overwriting a
        // live upload-system PendingSync{copyFence} with a stale completed
        // Graphics fence would incorrectly signal "safe to re-upload" while
        // the copy transition is still pending.
        {
            auto& ctx = *RHIExecuteContext::Current();

            auto ResolveHomeQueue = [](RHI::HardwareQueueClassMask mask) -> RHI::HardwareQueueClass
            {
                if (mask == RHI::HardwareQueueClassMask::Compute) { return RHI::HardwareQueueClass::Compute; }
                if (mask == RHI::HardwareQueueClassMask::Copy)    { return RHI::HardwareQueueClass::Copy; }
                return RHI::HardwareQueueClass::Graphics;
            };

            // StaticImportTag and BackingImage/Buffer all live on resource entities.
            ctx.GetView<StaticImportTag, BackingImage>().each(
                [&](RHIHandle resource, const BackingImage& backing)
                {
                    if (!backing.m_image) { return; }

                    const auto homeQueue = ResolveHomeQueue(
                        backing.m_image->GetDescriptor().m_sharedQueueMask);
                    const auto qi = static_cast<uint32_t>(homeQueue);
                    if (queueSegments[qi].empty()) { return; }

                    RHI::PendingSync sync;
                    sync.m_fence      = &m_crossQueueFences.GetFence(homeQueue);
                    sync.m_fenceValue = m_compiler.m_crossQueueFenceValues[qi];
                    ctx.AddOrReplace<RHI::PendingSync>(resource, sync);
                });

            ctx.GetView<StaticImportTag, BackingBuffer>().each(
                [&](RHIHandle resource, const BackingBuffer& backing)
                {
                    if (!backing.m_buffer) { return; }

                    const auto homeQueue = ResolveHomeQueue(
                        backing.m_buffer->GetDescriptor().m_sharedQueueMask);
                    const auto qi = static_cast<uint32_t>(homeQueue);
                    if (queueSegments[qi].empty()) { return; }

                    RHI::PendingSync sync;
                    sync.m_fence      = &m_crossQueueFences.GetFence(homeQueue);
                    sync.m_fenceValue = m_compiler.m_crossQueueFenceValues[qi];
                    ctx.AddOrReplace<RHI::PendingSync>(resource, sync);
                });
        }

        m_executer.End();
        ////////////////////////////////////////////////

        m_commandQueueContext.End();
        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameEnd);
    }

    void RenderGraph::RefreshPerFrameBackings(RHIContext& context, uint32_t frameIndex)
    {
        // ImportedTag lives on resource entities only. Per-frame resource
        // backing is refreshed here; per-frame VIEW backing is refreshed below.
        // The ImportedTag filter on per-frame views was superfluous — the
        // PerFrame components are only ever on imported entities by construction.
        context.GetView<ImportedTag, ImagePerFrame>().each(
            [&](RHIHandle entity, const ImagePerFrame& owning)
            {
                context.AddOrReplace<BackingImage>(entity,
                    BackingImage{ owning.m_images[frameIndex].get() });
            });

        context.GetView<ImportedTag, BufferPerFrame>().each(
            [&](RHIHandle entity, const BufferPerFrame& owning)
            {
                context.AddOrReplace<BackingBuffer>(entity,
                    BackingBuffer{ owning.m_buffers[frameIndex].get() });
            });

        // Swap chain is special: the underlying RHI image is owned by SwapChain
        // (Vulkan/DX12 don't allow independent ref-counting of swap chain images),
        // so the resource side stores raw pointers. Treat BackingImage as the
        // borrowed pointer the rest of the graph reads — same contract as for
        // owning ImagePerFrame, just sourced from a raw array.
        context.GetView<ImportedTag, SwapChainImages>().each(
            [&](RHIHandle entity, const SwapChainImages& owning)
            {
                context.AddOrReplace<BackingImage>(entity,
                    BackingImage{ owning.images[frameIndex] });
            });
        // Swap chain views are no longer refreshed here — they live in the resource's
        // ImageViewCachePerFrame and are resolved per frame via GetOrCreateImageViewPerFrame.
    }

    void RenderGraph::SubmitSwapChainPresentTransition(RHIContext& ctx, RHI::Factory& factory)
    {
        struct PresentEntry
        {
            RHI::ImageBarrier       m_barrier;
            RHI::HardwareQueueClass m_producer;
        };
        eastl::vector<PresentEntry> presents;

        ctx.GetView<ImportedTag, SwapChainImages>().each(
            [&](RHIHandle resource, const SwapChainImages&)
            {
                auto* backing = ctx.TryGet<BackingImage>(resource);
                if (!backing || backing->m_image == nullptr)
                {
                    return;
                }

                // Untouched this frame: no transition needed.
                auto* tracker = ctx.TryGet<ResourceStateTracker>(resource);
                if (!tracker)
                {
                    return;
                }

                const RHI::ResourceState cur = tracker->m_current;
                if (cur.m_access == RHI::AccessFlags::Present
                    && cur.m_queue == RHI::HardwareQueueClass::Graphics)
                {
                    return;
                }

                ASSERT(static_cast<uint32_t>(cur.m_queue) < RHI::HardwareQueueClassCount,
                    "Swap chain image '{}' has invalid producer queue ({}).",
                    ctx.Has<ResourceName>(resource)
                        ? ctx.Get<ResourceName>(resource).m_name.GetCStr()
                        : "[Unnamed]",
                    static_cast<uint32_t>(cur.m_queue));

                RHI::ImageBarrier b;
                b.m_image     = backing->m_image;
                b.m_srcAccess = cur.m_access;
                b.m_dstAccess = RHI::AccessFlags::Present;
                b.m_srcStage  = cur.m_stage;
                b.m_dstStage  = RHI::AttachmentStage::Any;
                b.m_srcQueue  = cur.m_queue;
                b.m_dstQueue  = RHI::HardwareQueueClass::Graphics;
                presents.push_back({ b, cur.m_queue });
            });

        if (presents.empty())
        {
            return;
        }

        auto& gfxQueue = m_commandQueueContext.GetCommandQueue(RHI::HardwareQueueClass::Graphics);

        // Wait on each non-Graphics producer queue's frame-end fence before
        // issuing the transition. Dedup by producer queue — multiple swap chain
        // images sharing the same producer only need one wait.
        eastl::array<bool, RHI::HardwareQueueClassCount> waited{};
        for (const auto& p : presents)
        {
            if (p.m_producer == RHI::HardwareQueueClass::Graphics)
            {
                continue;
            }
            const auto qi = static_cast<uint32_t>(p.m_producer);
            if (waited[qi])
            {
                continue;
            }
            waited[qi] = true;
            gfxQueue.Wait(m_crossQueueFences.GetFence(p.m_producer),
                          m_compiler.m_crossQueueFenceValues[qi]);
        }

        RHI::CommandList* cmd = factory.CreateCommandList(*m_device, RHI::HardwareQueueClass::Graphics);
        cmd->Open();
        for (const auto& p : presents)
        {
            cmd->QueueBarrier(p.m_barrier);
        }
        cmd->FlushBarriers();
        cmd->Close();
        gfxQueue.ExecuteCommands({ &cmd, 1 });
    }
}