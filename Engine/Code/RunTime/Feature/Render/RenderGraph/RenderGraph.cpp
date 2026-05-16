#include "RenderGraph.h"

#include <Log/SpdLogSystem.h>

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

        const uint32_t imageCount = swapChain.GetImageCount();
        auto& context = *RHIExecuteContext::Current();
        auto* factoryPtr = Service<RHI::Factory>::Get();
        ASSERT(factoryPtr != nullptr, "RHI::Factory service is not registered.");
        auto& factory = *factoryPtr;

        m_swapchainResource = context.CreateEntity();
        SwapChainImages swapChainImages;
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            swapChainImages.images[i] = swapChain.GetImage(i);
        }
        context.Add<SwapChainImages>(m_swapchainResource, eastl::move(swapChainImages));
        context.Add<ImportedTag>(m_swapchainResource);
        context.Add<ResourceName>(m_swapchainResource, ObjectName{"SwapChain"});
        // NOTE: swap chain Present-state transition at frame end is TBD —
        // CompileFinalTransitionBarrier was removed alongside ImportedResourceState.

        m_swapchainView = context.CreateEntity();
        SwapChainViews swapChainView;
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            auto image = swapChain.GetImage(i);
            Ptr<RHI::ImageView> imageview = factory.CreateImageView();
            RHI::ImageViewDescriptor viewDesc;
            viewDesc.m_mipSliceMin = 0;
            viewDesc.m_mipSliceMax = 0;
            viewDesc.m_arraySliceMin = 0;
            viewDesc.m_arraySliceMax = 0;
            RHI::ResultCode viewResult = imageview->Init(*image, viewDesc);
            if (viewResult != RHI::ResultCode::Success)
            {
                LOG_ERROR("Create swap chain view failed.");
                continue;
            }
            swapChainView.imageViews[i] = eastl::move(imageview);
        }
        context.Add<SwapChainViews>(m_swapchainView, eastl::move(swapChainView));
        context.Add<ImportedTag>(m_swapchainView);
        context.Add<ResourceName>(m_swapchainView, ObjectName{"SwapChainView"});
        context.Add<ViewHierarchy>(
            m_swapchainView,
            m_swapchainResource,
            NullHandle,
            NullHandle
        );

        context.Add<ResourceHierarchy>(
            m_swapchainResource,
            m_swapchainView
        );

        return true;
    }

    void RenderGraph::ExecutePipeline(PassContext& passContext, uint32_t frameIndex)
    {
        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameBegin);
        m_commandQueueContext.Begin();

        auto& context = *RHIExecuteContext::Current();

        // Refresh borrowed pointers (BackingImage / BackingBuffer / Backing*View)
        // for any per-frame imported resources whose Owning component rotates with
        // frameIndex. Single-frame imports are handled lazily by the builder.
        // Swap chain (SwapChainImages / SwapChainViews) is also refreshed here.
        RefreshPerFrameBackings(context, frameIndex);

        auto passFuncs = passContext.GetView<PassFunctions, ActivePassTag>();

        ////////////////////////////////////////////////
        // Build
        m_builder.Begin(frameIndex);

        passFuncs.each([&](Pass pass, PassFunctions& funcs)
        {
            m_builder.BeginPass(pass);
            if (funcs.m_buildFunction)
            {
                funcs.m_buildFunction(m_builder);
            }
            m_builder.EndPass();
        });

        eastl::vector<Pass> passes = m_builder.End();
        ////////////////////////////////////////////////

        ////////////////////////////////////////////////
        // Compile
        m_compiler.Begin(frameIndex);

        m_compiler.CompileShaderResources(*m_device, context);
        m_compiler.CompilePipelineStates(passes, passContext, *m_device, m_pipelineLibrary.get());
        m_compiler.CompileTransientResources(passes, *m_pool);

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

        QueueBasedPasses queueBasedPasses = m_compiler.CompilePassCrossQueue2(passes);

        m_compiler.End();
        ////////////////////////////////////////////////

        ////////////////////////////////////////////////
        // Execute
        m_executer.Begin(frameIndex);

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

        // Stamp PendingSync on every imported resource the RG touched this
        // frame, using the producer queue's frame-end fence value (signaled
        // above). Note: Graphics queue's stamp value reflects "before the swap
        // chain Present transition cmd list", but no one consumes PendingSync
        // on a swap chain (they're never re-used by other systems), so the
        // small inconsistency is harmless.
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

        m_executer.End();
        ////////////////////////////////////////////////

        m_commandQueueContext.End();
        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameEnd);
    }

    void RenderGraph::RefreshPerFrameBackings(RHIContext& context, uint32_t frameIndex)
    {
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

        context.GetView<ImportedTag, ImageViewPerFrame>().each(
            [&](RHIHandle entity, const ImageViewPerFrame& owning)
            {
                context.AddOrReplace<BackingImageView>(entity,
                    BackingImageView{ owning.m_views[frameIndex].get() });
            });

        context.GetView<ImportedTag, BufferViewPerFrame>().each(
            [&](RHIHandle entity, const BufferViewPerFrame& owning)
            {
                context.AddOrReplace<BackingBufferView>(entity,
                    BackingBufferView{ owning.m_views[frameIndex].get() });
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

        context.GetView<ImportedTag, SwapChainViews>().each(
            [&](RHIHandle entity, const SwapChainViews& owning)
            {
                context.AddOrReplace<BackingImageView>(entity,
                    BackingImageView{ owning.imageViews[frameIndex].get() });
            });
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
                if (cur.m_usage == RHI::AttachmentUsage::Present
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
                b.m_srcUsage  = cur.m_usage;
                b.m_dstUsage  = RHI::AttachmentUsage::Present;
                b.m_srcAccess = cur.m_access;
                b.m_dstAccess = RHI::AttachmentAccess::Read;
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