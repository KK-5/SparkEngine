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
        context.Add<ImportedResourceState>(
            m_swapchainResource,
            ImportedResourceState{
                /* m_initial      */ RHI::ResourceState{RHI::AttachmentUsage::Uninitialized, RHI::AttachmentAccess::Unknown},
                /* m_initialStage */ RHI::AttachmentStage::Any,
                /* m_initialQueue */ RHI::HardwareQueueClass::Graphics,
                /* m_final        */ RHI::ResourceState{RHI::AttachmentUsage::Present, RHI::AttachmentAccess::Read},
                /* m_finalStage   */ RHI::AttachmentStage::Any,
                /* m_finalQueue   */ RHI::HardwareQueueClass::Graphics,
            }
        );

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

        m_compiler.CompileFinalTransitionBarrier(passContext, context, passes);

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
}