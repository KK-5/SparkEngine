#include "RenderGraph.h"
#include "RenderGraphUtils.h"

#include <Log/ILogSystem.h>

#include <Core/Service/Service.h>

#include <RHI/Factory.h>
#include <RHI/Command/CommandQueue.h>
#include <RHI/Command/CommandList.h>
#include <RHI/SwapChain/SwapChain.h>
#include <RHI/Bus/FrameEventBus.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <RHI/Resource/Transient/TransientResourcePool.h>
#include <RHI/Pipeline/PipelineLibrary.h>
#include <RHI/ResourceBuilder.h>

#include <Pass/Pipeline.h>
#include <Pass/Component/RHIComponents.h>
#include <Pass/Component/ScopeComponents.h>
#include <Pass/Component/PassComponents.h>

namespace Spark::Render
{
    namespace
    {
        bool IsUnmapping(const RHI::BufferTileMapping& mapping)
        {
            return mapping.m_backingBuffer == nullptr;
        }

        bool IsUnmapping(const RHI::ImageTileMapping& mapping)
        {
            return mapping.m_backingImage == nullptr;
        }

        //! Submits the pending requests of one kind on `queue`, then destroys them: the backings
        //! they unmapped go to their pools' release queues. The unmappings go in one call before
        //! the mappings in another, so a page or tile both unmapped and mapped this frame ends up
        //! mapped, whatever order the requests are stored in.
        template<typename Request, typename Events>
        void SubmitTileMappings(Events& events, RHI::CommandQueue& queue, const char* kind)
        {
            auto& requests = events.template GetStorage<Request>();
            if (requests.empty())
            {
                return;
            }

            using Mapping = decltype(Request::m_mapping);
            eastl::vector<Mapping> unmappings;
            eastl::vector<Mapping> mappings;
            for (const Request& request : requests)
            {
                if (IsUnmapping(request.m_mapping))
                {
                    unmappings.push_back(request.m_mapping);
                }
                else
                {
                    mappings.push_back(request.m_mapping);
                }
            }

            if (queue.UpdateTileMappings(eastl::span<const Mapping>(unmappings.data(), unmappings.size()))
                != RHI::ResultCode::Success)
            {
                LOG_ERROR("[RenderGraph] Unmapping {} {} tile ranges failed.", unmappings.size(), kind);
            }
            if (queue.UpdateTileMappings(eastl::span<const Mapping>(mappings.data(), mappings.size()))
                != RHI::ResultCode::Success)
            {
                LOG_ERROR("[RenderGraph] Mapping {} {} tile ranges failed.", mappings.size(), kind);
            }

            const eastl::vector<RHI::RHIEvent> submitted(requests.data(), requests.data() + requests.size());
            events.DestoryEntity(submitted.begin(), submitted.end());
        }
    }

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

        m_imagePool = factory.CreateImagePool();
        ASSERT(m_imagePool != nullptr, "[RenderGraph] Factory::CreateImagePool returned null.");
        {
            // Superset of what a pooled image may be declared with: the pool validates that
            // each image's flags are contained here.
            RHI::ImagePoolDescriptor poolDesc;
            poolDesc.m_bindFlags = RHI::ImageBindFlags::Color | RHI::ImageBindFlags::DepthStencil
                                 | RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::ShaderWrite
                                 | RHI::ImageBindFlags::CopyRead | RHI::ImageBindFlags::CopyWrite;
            if (m_imagePool->Init(device, poolDesc) != RHI::ResultCode::Success)
            {
                LOG_ERROR("[RenderGraph] Image pool initialize failed.");
                return false;
            }
        }
        m_builder.m_imagePool = m_imagePool.get();

        m_pipelineLibrary = factory.CreatePipelineLibrary();
        ASSERT(m_pipelineLibrary != nullptr, "[RenderGraph] Factory::CreatePipelineLibrary returned null.");
        RHI::PipelineLibraryDescriptor pipelineLibraryDesc;
        if (m_pipelineLibrary->Init(device, pipelineLibraryDesc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[RenderGraph] PipelineLibrary init failed.");
            return false;
        }
        m_compiler.m_drawPipelineCache.Init(factory, device, m_pipelineLibrary.get());

        m_device = &device;

        return true;
    }

    bool RenderGraph::ImportSwapChain(RHI::SwapChain& swapChain)
    {
        ASSERT(m_device != nullptr, "[RenderGraph] ImportSwapChain called before Init.");

        m_swapChain = &swapChain;
        CreateSwapChainImages(*RHIExecuteContext::Current());
        return true;
    }

    void RenderGraph::CreateSwapChainImages(RHIContext& context)
    {
        // Views come from each image's own view cache, as for any imported image.
        const uint32_t imageCount = m_swapChain->GetImageCount();
        for (uint32_t i = 0; i < imageCount; ++i)
        {
            const RHIHandle image = context.CreateEntity();
            context.Add<ImportedTag>(image);
            context.Add<SwapChainImageTag>(image);
            const eastl::string name(eastl::string::CtorSprintf(), "SwapChain %u", i);
            context.Add<ResourceName>(image, ObjectName{ name.c_str() });
            context.Add<BackingImage>(image, BackingImage{ m_swapChain->GetImage(i) });
            m_swapChainImages.push_back(image);
        }
    }

    void RenderGraph::DestroySwapChainImages(RHIContext& context)
    {
        for (RHIHandle image : m_swapChainImages)
        {
            context.DestoryEntity(image);
        }
        m_swapChainImages.clear();
    }

    void RenderGraph::WaitForGpuIdle()
    {
        for (uint32_t i = 0; i < RHI::HardwareQueueClassCount; ++i)
        {
            m_commandQueueContext.GetCommandQueue(static_cast<RHI::HardwareQueueClass>(i)).WaitForIdle();
        }
    }

    bool RenderGraph::ResizeSwapChain(const Math::Vector2Int& size)
    {
        ASSERT(m_device != nullptr, "[RenderGraph] ResizeSwapChain called before Init.");

        if (size.x <= 0 || size.y <= 0)
        {
            return false;
        }

        if (!m_swapChain)
        {
            LOG_ERROR("[RenderGraph] ResizeSwapChain called before ImportSwapChain.");
            return false;
        }

        auto& context = *RHIExecuteContext::Current();

        // SwapChain::Resize releases the back buffers outright (no deferred release),
        // and ResizeBuffers refuses to run while any reference is outstanding. All
        // queues, not just Graphics: an image can end the frame on Compute or Copy.
        WaitForGpuIdle();

        // Their views were built over the old images, and go with them.
        DestroySwapChainImages(context);

        RHI::SwapChainDimensions dimensions = m_swapChain->GetDescriptor().m_dimensions;
        dimensions.m_imageWidth  = static_cast<uint32_t>(size.x);
        dimensions.m_imageHeight = static_cast<uint32_t>(size.y);

        const bool resized = m_swapChain->Resize(dimensions) == RHI::ResultCode::Success;
        if (!resized)
        {
            LOG_ERROR("[RenderGraph] Swap chain resize to {}x{} failed.", size.x, size.y);
        }

        CreateSwapChainImages(context);
        return resized;
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
        DestroySwapChainImages(context);
        m_swapChain = nullptr;

        // Pooled images outlive every frame, so nothing else reaps them.
        eastl::vector<RHIHandle> pooledImages;
        context.GetView<PooledImageTag>().each(
            [&](RHIHandle entity) { pooledImages.push_back(entity); });
        for (RHIHandle entity : pooledImages)
        {
            context.DestoryEntity(entity);
        }
        m_builder.m_imagePool = nullptr;

        // The images just released wait in the pool's deferred release queue, which only
        // drains at frame end. No frame follows, and the pool's destructor does not flush
        // it; Shutdown does, before its allocator goes. The GPU is already idle.
        m_imagePool->Shutdown();

        m_compiler.m_drawPipelineCache.Shutdown();
    }

    void RenderGraph::ExecutePipeline(PassContext& passContext,
                                      const Math::Vector2Int& renderSize, const Math::Vector2Int& outputSize)
    {
        const uint32_t frameIndex = m_device->GetFrameIndex();

        RHI::FrameEventBus::Broadcast(&RHI::FrameEventBus::Events::OnFrameBegin);
        m_commandQueueContext.Begin();

        auto& context = *RHIExecuteContext::Current();

        // Refresh borrowed pointers (BackingImage / BackingBuffer / Backing*View)
        // for any per-frame imported resources whose Owning component rotates with
        // frameIndex. Single-frame imports are handled lazily by the builder.
        RefreshPerFrameBackings(context, frameIndex);

        ////////////////////////////////////////////////
        // Build
        // Iterate passes in declaration order so that attachment version
        // tracking converges deterministically
        // regardless of entt's pool order.
        const RHIHandle swapChainImage =
            m_swapChain ? m_swapChainImages[m_swapChain->GetCurrentImageIndex()] : NullHandle;
        m_builder.Begin(frameIndex, swapChainImage, renderSize, outputSize);
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
        m_builder.End();
        ////////////////////////////////////////////////

        ////////////////////////////////////////////////
        // Compile
        m_compiler.Begin(frameIndex);

        // Transient resources must be materialized (backing allocated) before the
        // rest of compile. Views are no longer materialized here — they are resolved
        // on demand from each resource's view cache.
        m_compiler.CompileTransientResources(*m_pool);

        // After the transient stage, which links the extracted resources' attachments and
        // leaves them out of the pool.
        m_compiler.CompileExtractedImages(*m_imagePool);

        // After the two above, which link every attachment to its resource.
        m_compiler.SortScopes(passContext, context);
        m_compiler.CompileActiveQueues(context);

        m_compiler.CompilePipelineStates(passContext, *m_device, m_pipelineLibrary.get());

        m_compiler.CompileScopeUploads(passContext, context);
        m_compiler.CompileScopeBarriers(passContext, context);
        m_compiler.CompileExternalWaits(context);
        m_compiler.CompileScopeSync(passContext, context, m_crossQueueFences);
        m_compiler.CompileScopeBeginInfo(passContext, context);

        m_compiler.CompileScopeBindings(passContext, context);
        m_compiler.CompileScopeState(passContext, context);
        m_compiler.CompileItemIndirectArguments(passContext, context);
        m_compiler.CompileItemCopies(passContext, context);
        m_compiler.CompileItemInputAssembly(passContext, context);
        m_compiler.CompileScopeSubmitRanges(passContext, context, m_executer.GetSubmitList());

        m_compiler.CompileShaderInputs(*m_device, context);

        m_compiler.End();
        ////////////////////////////////////////////////

        ////////////////////////////////////////////////
        // Execute
        m_executer.Begin(frameIndex);

        auto* factory = Service<RHI::Factory>::Get();
        ASSERT(factory, "[RenderGraph] RHI::Factory service not registered.");

        // Ahead of every Scope, which may touch the pages and tiles.
        {
            auto& events = *RHI::RHIEventExecuteContext::Current();
            auto& queue  = m_commandQueueContext.GetCommandQueue(RHI::HardwareQueueClass::Graphics);
            SubmitTileMappings<RHI::BufferTileMappingRequest>(events, queue, "buffer");
            SubmitTileMappings<RHI::ImageTileMappingRequest>(events, queue, "image");
        }

        m_executer.ExecuteScopes(context, *factory, *m_device, m_commandQueueContext);

        // The uploads the Scopes acquired are over, their acquires recorded.
        {
            auto& events = *RHI::RHIEventExecuteContext::Current();
            auto& links  = events.GetStorage<ScopeUpload>();
            const eastl::vector<RHI::RHIEvent> acquired(links.data(), links.data() + links.size());
            for (RHI::RHIEvent request : acquired)
            {
                RHI::FinishUploadAcquire(context, events, request);
            }
        }

        // Frame-end: signal each active queue's cross-queue fence one more time.
        // The PendingSync stamp below writes this value onto every imported
        // resource's PendingSync component.
        //
        // m_crossQueueFenceValues is **shared** between two writers:
        //  - CompileScopeSync (in-frame, allocates values for the ScopeSignals
        //    other queues wait on)
        //  - this frame-end step (one extra ++ per active queue, used as the
        //    "frame-end timestamp")
        // Both writers are monotonic in submission order on the same queue, so
        // sharing one counter is safe.
        for (uint32_t qi = 0; qi < static_cast<uint32_t>(RHI::HardwareQueueClass::Count); ++qi)
        {
            if (!m_compiler.IsQueueActive(static_cast<RHI::HardwareQueueClass>(qi)))
            {
                continue;
            }

            const auto queueClass = static_cast<RHI::HardwareQueueClass>(qi);
            auto& queue = m_commandQueueContext.GetCommandQueue(queueClass);

            const uint64_t value = ++m_compiler.m_crossQueueFenceValues[qi];
            queue.Signal(m_crossQueueFences.GetFence(queueClass), value);
        }

        // Stamp PendingSync on every imported resource the RG touched this frame,
        // using the producer queue's frame-end fence value (signaled above).
        {
            auto& ctx = *RHIExecuteContext::Current();
            auto stamp = [&](RHIHandle resource, RHI::HardwareQueueClass queue)
            {
                const auto qi = static_cast<uint32_t>(queue);
                ASSERT(qi < RHI::HardwareQueueClassCount,
                    "State tracker for {} has invalid m_queue ({}).",
                    ctx.Has<ResourceName>(resource)
                        ? ctx.Get<ResourceName>(resource).m_name.GetCStr()
                        : "[Unnamed]",
                    qi);
                PendingSync sync;
                sync.m_fence = &m_crossQueueFences.GetFence(queue);
                sync.m_fenceValue = m_compiler.m_crossQueueFenceValues[qi];
                ctx.AddOrReplace<PendingSync>(resource, sync);
            };

            ctx.GetView<ImportedTag, BufferStateTracker>().each(
                [&](RHIHandle resource, const BufferStateTracker& tracker)
                {
                    stamp(resource, tracker.m_current.m_queue);
                });

            ctx.GetView<ImportedTag, ImageStateTracker>().each(
                [&](RHIHandle resource, const ImageStateTracker& tracker)
                {
                    stamp(resource, GetImageQueue(tracker.m_states,
                        ctx.Has<ResourceName>(resource)
                            ? ctx.Get<ResourceName>(resource).m_name.GetCStr()
                            : "[Unnamed]"));
                });
        }

        // Before End, which destroys the transient resources carrying ExtractedImage.
        ExtractImages(context);

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
    }

    void RenderGraph::ExtractImages(RHIContext& context)
    {
        // Neither read as a previous frame nor taken this frame. In steady state every
        // idle image is taken the next frame, so one left untouched for a whole frame
        // will not be: its descriptor went stale, or its reader stopped. The pool defers
        // the actual release past in-flight frames.
        eastl::vector<RHIHandle> untouched;
        context.GetView<PooledImageTag>(Exclude<PreviousFrameOf, PooledImageActiveTag>).each(
            [&](RHIHandle pooled) { untouched.push_back(pooled); });
        for (RHIHandle pooled : untouched)
        {
            context.DestoryEntity(pooled);
        }

        // Only what was produced this frame is next frame's previous frame; a name no one
        // read this frame has none.
        context.Clear<PreviousFrameOf>();
        context.GetView<TransientTag, ExtractedImage, ResourceName>().each(
            [&](RHIHandle, const ExtractedImage& extracted, const ResourceName& rn)
            {
                context.Add<PreviousFrameOf>(extracted.m_pooledImage, PreviousFrameOf{ rn.m_name });
            });

        context.Clear<PooledImageActiveTag>();
    }
}