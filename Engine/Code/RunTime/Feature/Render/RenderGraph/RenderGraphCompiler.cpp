#include "RenderGraphCompiler.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_map.h>

#include <Log/SpdLogSystem.h>
#include <Service/Service.h>

#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Pipeline/PipelineState.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/ShaderResource/ShaderResourceCompiler.h>
#include <RHI/Resource/Transient/TransientResourcePool.h>
#include <RHI/Command/RenderPassBeginInfo.h>

#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Pipeline/ShaderStages.h>

#include <Pass/Component/PassComponents.h>

namespace Spark::Render
{

namespace
{
    //! Derive a conservative ShaderStageMask from the shaders a pass carries.
    RHI::ShaderStageMask DeriveStageMask(const PassShaders& shaders)
    {
        RHI::ShaderStageMask mask = RHI::ShaderStageMask::None;
        if (shaders.m_vertexShader)   mask |= RHI::ShaderStageMask::Vertex;
        if (shaders.m_fragmentShader) mask |= RHI::ShaderStageMask::Fragment;
        if (shaders.m_geometryShader) mask |= RHI::ShaderStageMask::Geometry;
        if (shaders.m_computeShader)  mask |= RHI::ShaderStageMask::Compute;
        return mask;
    }

    //! Build a PipelineLayoutDescriptor from a pass's PassShaderResources slots.
    //! @param stageMask  Conservative stage mask — every binding declares visibility
    //!                    to all active stages. Correct but may inhibit driver
    //!                    dead-code elimination of unused descriptors. Will be
    //!                    replaced by per-resource masks once shader reflection is
    //!                    plumbed through ShaderAsset.
    Ptr<RHI::PipelineLayoutDescriptor> BuildPipelineLayoutDescriptor(
        const PassShaderResources& slots,
        RHI::ShaderStageMask       stageMask)
    {
        auto& rhiCtx = *RHIExecuteContext::Current();
        auto* factory = Service<RHI::Factory>::Get();

        auto desc = factory->CreatePipelineLayoutDescriptor();

        for (uint32_t slot = 0; slot < slots.m_slots.size(); ++slot)
        {
            if (slots.m_slots[slot] == NullHandle)
                continue;

            const auto& srgLayout = rhiCtx.Get<ShaderResourceLayout>(slots.m_slots[slot]);
            const auto& layout = *srgLayout.m_layout;

            RHI::ShaderResourceBindingInfo bindingInfo;

            // --- constant data binding ---
            if (const auto* constLayout = layout.GetConstantsLayout())
            {
                // Pick up register / space from the first constant descriptor.
                auto constants = layout.GetShaderInputListForConstants();
                if (!constants.empty())
                {
                    bindingInfo.m_constantDataBindingInfo = RHI::ResourceBindingInfo(
                        stageMask,
                        constants[0].m_registerId,
                        constants[0].m_spaceId);
                }
            }

            // --- per-resource register map ---
            auto addResources = [&](auto& list)
            {
                for (const auto& input : list)
                {
                    bindingInfo.m_resourcesRegisterMap[RHI::InputName(input.m_name)] =
                        RHI::ResourceBindingInfo(stageMask, input.m_registerId, input.m_spaceId);
                }
            };

            addResources(layout.GetShaderInputListForBuffers());
            addResources(layout.GetShaderInputListForImages());
            addResources(layout.GetShaderInputListForSamplers());
            addResources(layout.GetStaticSamplers());

            desc->AddShaderResourceLayoutInfo(layout, bindingInfo);
        }

        desc->Finalize();
        return desc;
    }
} // namespace

    void RenderGraphCompiler::Begin(uint32_t frameIndex)
    {
        m_frameIndex = frameIndex;
    }

    void RenderGraphCompiler::End()
    {
        auto& context = *RHIExecuteContext::Current();

        // Attachment entities are build/compile inputs only; executer reads
        // PassBarriers / RenderPassBeginInfo on Pass entities. Destroy them so
        // next frame's ValidateUniqueSlot doesn't trip over stale slot names.
        auto imageAttachments = context.GetView<ImagePassAttachment>();
        imageAttachments.each([&](RHIHandle handle, ImagePassAttachment&)
        {
            context.DestoryEntity(handle);
        });

        auto bufferAttachments = context.GetView<BufferPassAttachment>();
        bufferAttachments.each([&](RHIHandle handle, BufferPassAttachment&)
        {
            context.DestoryEntity(handle);
        });

        // Per-resource compile-time state cursor is now cleared in
        // Executer::End(), after frame-end PendingSync update consumes it.

        // m_crossQueueFenceValues is intentionally cross-frame (monotonic).
        // AttachmentCompilingTag is per-pass, cleared inline in the compile loop.
    }

    namespace
    {
        //! Per-resource lifetime aggregated from all attachment uses. Stored as ECS
        //! components on the resource entity during CompileTransientResources and
        //! cleared before the function returns.
        struct ImageLifetime
        {
            uint32_t                    m_firstPos   = RHI::InvalidTimelinePosition;
            uint32_t                    m_lastPos    = 0;
            RHI::HardwareQueueClassMask m_queueMask  = RHI::HardwareQueueClassMask::None;
            RHI::AttachmentStage        m_firstStage = RHI::AttachmentStage::Any;
            RHI::AttachmentStage        m_lastStage  = RHI::AttachmentStage::Any;
            eastl::vector<RHIHandle>    m_attachments;
            const RHI::ClearValue*      m_clearValue = nullptr;
        };

        struct BufferLifetime
        {
            uint32_t                    m_firstPos  = RHI::InvalidTimelinePosition;
            uint32_t                    m_lastPos   = 0;
            RHI::HardwareQueueClassMask m_queueMask = RHI::HardwareQueueClassMask::None;
            RHI::AttachmentStage        m_firstStage = RHI::AttachmentStage::Any;
            RHI::AttachmentStage        m_lastStage  = RHI::AttachmentStage::Any;
            eastl::vector<RHIHandle>    m_attachments;
        };

        enum class SweepAction : uint8_t
        {
            Create,
            Discard,
        };

        enum class SweepResourceType : uint8_t
        {
            Image,
            Buffer,
        };

        //! Sweep events. At each timeline position we emit Discards before Creates so
        //! a freshly released heap range is recyclable by a Create at the same pos.
        struct SweepEvent
        {
            uint32_t          m_pos;
            SweepAction       m_action;
            SweepResourceType m_resourceType;
            RHIHandle         m_resource;
        };

        void CompileTransientImageViews(
            RHIContext&                     rhiContext,
            RHI::Factory&                   factory,
            RHIHandle                       resource,
            RHI::Image&                     image,
            const eastl::vector<RHIHandle>& attachments)
        {
            bool firstView = true;
            for (RHIHandle attachmentHandle : attachments)
            {
                auto& a = rhiContext.Get<ImagePassAttachment>(attachmentHandle);

                Ptr<RHI::ImageView> view = factory.CreateImageView();
                ASSERT(view != nullptr, "Factory::CreateImageView returned null.");
                RHI::ResultCode rc = view->Init(image, a.m_viewDescriptor);
                ASSERT(rc == RHI::ResultCode::Success,
                    "Transient ImageView::Init failed for attachment {}.",
                    a.m_attachmentId.m_id.GetCStr());

                RHIHandle viewEntity = rhiContext.CreateEntity();
                RHI::ImageView* viewRaw = view.get();
                rhiContext.Add<ImageView>(viewEntity, ImageView{ eastl::move(view) });
                rhiContext.Add<BackingImageView>(viewEntity, BackingImageView{ viewRaw });
                rhiContext.Add<TransientTag>(viewEntity);
                rhiContext.Add<ViewHierarchy>(viewEntity,
                    ViewHierarchy{ resource, NullHandle, NullHandle });

                a.m_view = viewEntity;

                if (firstView)
                {
                    if (!rhiContext.Has<ResourceHierarchy>(resource))
                    {
                        rhiContext.Add<ResourceHierarchy>(resource, ResourceHierarchy{ viewEntity });
                    }
                    firstView = false;
                }
            }
        }

        void CompileTransientBufferViews(
            RHIContext&                     rhiContext,
            RHI::Factory&                   factory,
            RHIHandle                       resource,
            RHI::Buffer&                    buffer,
            const eastl::vector<RHIHandle>& attachments)
        {
            bool firstView = true;
            for (RHIHandle attachmentHandle : attachments)
            {
                auto& a = rhiContext.Get<BufferPassAttachment>(attachmentHandle);

                Ptr<RHI::BufferView> view = factory.CreateBufferView();
                ASSERT(view != nullptr, "Factory::CreateBufferView returned null.");
                RHI::ResultCode rc = view->Init(buffer, a.m_viewDescriptor);
                ASSERT(rc == RHI::ResultCode::Success,
                    "Transient BufferView::Init failed for attachment {}.",
                    a.m_attachmentId.m_id.GetCStr());

                RHIHandle viewEntity = rhiContext.CreateEntity();
                RHI::BufferView* viewRaw = view.get();
                rhiContext.Add<BufferView>(viewEntity, BufferView{ eastl::move(view) });
                rhiContext.Add<BackingBufferView>(viewEntity, BackingBufferView{ viewRaw });
                rhiContext.Add<TransientTag>(viewEntity);
                rhiContext.Add<ViewHierarchy>(viewEntity,
                    ViewHierarchy{ resource, NullHandle, NullHandle });

                a.m_view = viewEntity;

                if (firstView)
                {
                    if (!rhiContext.Has<ResourceHierarchy>(resource))
                    {
                        rhiContext.Add<ResourceHierarchy>(resource, ResourceHierarchy{ viewEntity });
                    }
                    firstView = false;
                }
            }
        }

        template <typename AttachmentT>
        RHI::ResourceState CompileResourceState(const AttachmentT& attachment)
        {
            ASSERT(attachment.m_usage != RHI::AttachmentUsage::Uninitialized,
                "[RenderSystem] Attachment has uninitialized usage.");
            ASSERT(attachment.m_access != RHI::AttachmentAccess::Unknown,
                "[RenderSystem] Attachment has unknown access.");

            RHI::ResourceState state;
            state.m_usage  = attachment.m_usage;
            state.m_access = RHI::AdjustAccessBasedOnUsage(attachment.m_access, attachment.m_usage);
            return state;
        }

        // View handle → underlying Resource handle. Falls back to the input if it is already a resource.
        RHIHandle ResolveResource(RHIHandle handle, const RHIContext& context)
        {
            if (context.Has<ViewHierarchy>(handle))
                return context.Get<ViewHierarchy>(handle).m_resource;
            return handle;
        }

        //! Read the resource's current observed state from the BackingImage / BackingBuffer
        //! component (set at runtime by barrier emit paths) for first-touch tracker seeding.
        //! Imported resources end the previous frame in whatever state the last barrier left
        //! them in; transient resources start each frame at the descriptor default
        //! (Uninitialized + Graphics queue). The new state model carries queue + stage
        //! alongside usage/access, so the consuming pass's barrier construction has full
        //! src information without consulting a separate "imported initial state" record.
        RHI::ResourceState GetResourceInitialState(RHIHandle resource, const RHIContext& context)
        {
            if (auto* backing = context.TryGet<BackingImage>(resource))
            {
                ASSERT(backing->m_image != nullptr, "BackingImage::m_image is null.");
                return backing->m_image->GetResourceState();
            }
            if (auto* backing = context.TryGet<BackingBuffer>(resource))
            {
                ASSERT(backing->m_buffer != nullptr, "BackingBuffer::m_buffer is null.");
                return backing->m_buffer->GetResourceState();
            }

            LOG_ERROR("Resource {} has neither BackingImage nor BackingBuffer at first-touch.",
                context.Has<ResourceName>(resource)
                    ? context.Get<ResourceName>(resource).m_name.GetCStr()
                    : "[Unnamed]");
            return RHI::ResourceState{};
        }

        //! Collect a cross-queue PendingSync wait onto the consuming pass's
        //! PassExternalFenceWaits component. Common code for buffer / image
        //! first-touch barrier compile — both call this with the resource +
        //! queue pair after computing the first-touch state but before adding
        //! the ResourceStateTracker. Skips if:
        //!  - srcQueue == dstQueue (same queue, no fence wait needed)
        //!  - resource is not Imported (transient resources don't carry PendingSync)
        //!  - no PendingSync on the resource (fresh / never produced by another system)
        //!  - PendingSync's fence has already reached its value (queue.Wait would
        //!    be a no-op; fence values are monotonic so "stale at compile" is
        //!    permanent through execute).
        void CompilePassExternalFenceWait(
            Pass                    pass,
            RHIHandle               resource,
            RHI::HardwareQueueClass srcQueue,
            RHI::HardwareQueueClass dstQueue,
            PassContext&            passContext,
            RHIContext&             context)
        {
            if (srcQueue == dstQueue)
            {
                return;
            }
            if (!context.Has<ImportedTag>(resource))
            {
                return;
            }
            auto* sync = context.TryGet<RHI::PendingSync>(resource);
            if (!sync)
            {
                return;
            }
            if (sync->m_fence != nullptr
                && sync->m_fence->GetCompletedValue() >= sync->m_fenceValue)
            {
                return;
            }

            if (auto* existing = passContext.TryGet<PassExternalFenceWaits>(pass))
            {
                existing->m_waits.push_back(*sync);
            }
            else
            {
                PassExternalFenceWaits waits;
                waits.m_waits.push_back(*sync);
                passContext.Add<PassExternalFenceWaits>(pass, eastl::move(waits));
            }
        }

        void CompileTransientAliasingBarriers(
            Pass                        pass,
            PassContext&                passContext,
            RHI::TransientResourcePool& pool,
            PassBarriers&               out)
        {
            ASSERT(passContext.Has<PassGlobalTimeline>(pass),
                "Pass {} has no PassGlobalTimeline.",
                passContext.Get<PassName>(pass).m_name.GetCStr());
            const uint32_t pos = passContext.Get<PassGlobalTimeline>(pass).m_position;

            pool.GetAliasingBarriers(pos, out.m_preAliasing);
        }

        // Hard-fail any attempt to use an EXCLUSIVE imported resource on a queue
        // other than its declared home queue. EXCLUSIVE's design promise is
        // "stays on one queue, optimized by driver for that queue"; allowing
        // cross-queue access would silently work on DX12 (COMMON-agnostic) but
        // UB on Vulkan EXCLUSIVE (missing QFOT release pair). The caller's fix
        // is to declare multi-bit m_sharedQueueMask (CONCURRENT).
        auto ValidateExclusiveHomeQueue = [](
            RHI::HardwareQueueClassMask mask,
            RHI::HardwareQueueClass     passQueue,
            const char*                 resourceName)
        {
            const uint32_t m = static_cast<uint32_t>(mask);
            const bool exclusive = (m != 0) && ((m & (m - 1)) == 0);
            if (!exclusive)
            {
                return;
            }
            const RHI::HardwareQueueClass homeQueue =
                  (mask == RHI::HardwareQueueClassMask::Compute) ? RHI::HardwareQueueClass::Compute
                : (mask == RHI::HardwareQueueClassMask::Copy)    ? RHI::HardwareQueueClass::Copy
                                                                 : RHI::HardwareQueueClass::Graphics;
            ASSERT(passQueue == homeQueue,
                "Exclusive imported resource '{}' accessed on queue {} but home queue is {}. "
                "Cross-queue access is not supported for EXCLUSIVE resources — declare CONCURRENT "
                "(multi-bit m_sharedQueueMask) if the resource needs to flow across queues.",
                resourceName,
                static_cast<uint32_t>(passQueue),
                static_cast<uint32_t>(homeQueue));
        };

        void CompileBufferBarriers(
            Pass                        pass,
            PassContext&                passContext,
            RHIContext&                 context,
            RHI::TransientResourcePool& pool,
            PassBarriers&               out)
        {
            ASSERT(passContext.Has<PassExecuteQueue>(pass), "The pass {} has not PassExecuteQueue", passContext.Get<PassName>(pass).m_name.GetCStr());

            const RHI::HardwareQueueClass dstQueue = passContext.Get<PassExecuteQueue>(pass).m_queue;

            auto view = context.GetView<BufferPassAttachment, AttachmentCompilingTag>();
            view.each([&](auto, const BufferPassAttachment& att)
            {
                RHIHandle resource = ResolveResource(att.m_view, context);
                ASSERT(resource != NullHandle, "Buffer attachment view has no resource.");

                if (context.Has<ImportedTag>(resource))
                {
                    if (auto* backing = context.TryGet<BackingBuffer>(resource);
                        backing && backing->m_buffer)
                    {
                        ValidateExclusiveHomeQueue(
                            backing->m_buffer->GetDescriptor().m_sharedQueueMask,
                            dstQueue,
                            context.Has<ResourceName>(resource)
                                ? context.Get<ResourceName>(resource).m_name.GetCStr()
                                : "[Unnamed]");
                    }
                }

                if (!context.Has<ResourceStateTracker>(resource))
                {
                    ResourceStateTracker init;
                    init.m_current = GetResourceInitialState(resource, context);
                    CompilePassExternalFenceWait(pass, resource,
                        init.m_current.m_queue, dstQueue, passContext, context);
                    context.Add<ResourceStateTracker>(resource, init);
                }
                auto& tracker = context.Get<ResourceStateTracker>(resource);

                const RHI::HardwareQueueClass srcQueue = tracker.m_current.m_queue;
                const RHI::AttachmentStage    srcStage = tracker.m_current.m_stage;

                RHI::ResourceState src = tracker.m_current;
                RHI::ResourceState dst = CompileResourceState(att);

                RHI::BufferBarrier b;
                if (src != dst || srcQueue != dstQueue)
                {
                    auto* backingBuffer = context.TryGet<BackingBuffer>(resource);
                    ASSERT(backingBuffer != nullptr, "Resource has no BackingBuffer.");
                    RHI::Buffer* buffer = backingBuffer->m_buffer;

                    b.m_buffer    = buffer;
                    b.m_srcUsage  = src.m_usage;
                    b.m_dstUsage  = dst.m_usage;
                    b.m_srcAccess = src.m_access;
                    b.m_dstAccess = dst.m_access;
                    b.m_srcStage  = srcStage;
                    b.m_dstStage  = att.m_stage;
                    b.m_srcQueue  = srcQueue;
                    b.m_dstQueue  = dstQueue;
                    out.m_preBuffer.push_back(b);
                }

                // Cross-queue: push the release half onto the previous-pass entity.
                if (srcQueue != dstQueue && tracker.m_lastPass != NullPass)
                {
                    if (auto barriers = passContext.TryGet<PassBarriers>(tracker.m_lastPass))
                    {
                        barriers->m_postBuffer.push_back(b);
                    }
                    else
                    {
                        PassBarriers passBarriers;
                        passBarriers.m_postBuffer.push_back(b);
                        passContext.Add<PassBarriers>(tracker.m_lastPass, eastl::move(passBarriers));
                    }
                }

                tracker.m_current = RHI::ResourceState{ dst.m_usage, dst.m_access, dstQueue, att.m_stage };
                tracker.m_lastPass = pass;
            });
        }

        void CompileImageBarriers(
            Pass                        pass,
            PassContext&                passContext,
            RHIContext&                 context,
            RHI::TransientResourcePool& pool,
            PassBarriers&               out)
        {
            ASSERT(passContext.Has<PassExecuteQueue>(pass), "The pass {} has not PassExecuteQueue", passContext.Get<PassName>(pass).m_name.GetCStr());

            const RHI::HardwareQueueClass dstQueue = passContext.Get<PassExecuteQueue>(pass).m_queue;

            auto view = context.GetView<ImagePassAttachment, AttachmentCompilingTag>();
            view.each([&](auto, const ImagePassAttachment& att)
            {
                RHIHandle resource = ResolveResource(att.m_view, context);
                ASSERT(resource != NullHandle, "Image attachment view has no resource.");

                if (context.Has<ImportedTag>(resource))
                {
                    if (auto* backing = context.TryGet<BackingImage>(resource);
                        backing && backing->m_image)
                    {
                        ValidateExclusiveHomeQueue(
                            backing->m_image->GetDescriptor().m_sharedQueueMask,
                            dstQueue,
                            context.Has<ResourceName>(resource)
                                ? context.Get<ResourceName>(resource).m_name.GetCStr()
                                : "[Unnamed]");
                    }
                }

                if (!context.Has<ResourceStateTracker>(resource))
                {
                    ResourceStateTracker init;
                    init.m_current = GetResourceInitialState(resource, context);
                    CompilePassExternalFenceWait(pass, resource,
                        init.m_current.m_queue, dstQueue, passContext, context);
                    context.Add<ResourceStateTracker>(resource, init);
                }
                auto& tracker = context.Get<ResourceStateTracker>(resource);

                const RHI::HardwareQueueClass srcQueue = tracker.m_current.m_queue;
                const RHI::AttachmentStage    srcStage = tracker.m_current.m_stage;

                RHI::ResourceState src = tracker.m_current;
                RHI::ResourceState dst = CompileResourceState(att);

                // loadOp=Clear discards prior contents — let backend pick UNDEFINED for src layout.
                if (att.m_action.m_loadAction == RHI::AttachmentLoadAction::Clear)
                {
                    src.m_usage  = RHI::AttachmentUsage::Uninitialized;
                    src.m_access = RHI::AttachmentAccess::Unknown;
                }

                RHI::ImageBarrier b;
                if (src != dst || srcQueue != dstQueue)
                {
                    auto* backingImage = context.TryGet<BackingImage>(resource);
                    ASSERT(backingImage != nullptr, "Resource has no BackingImage.");
                    RHI::Image* image = backingImage->m_image;

                    b.m_image     = image;
                    b.m_srcUsage  = src.m_usage;
                    b.m_dstUsage  = dst.m_usage;
                    b.m_srcAccess = src.m_access;
                    b.m_dstAccess = dst.m_access;
                    b.m_srcStage  = srcStage;
                    b.m_dstStage  = att.m_stage;
                    b.m_srcQueue  = srcQueue;
                    b.m_dstQueue  = dstQueue;
                    out.m_preImage.push_back(b);
                }

                if (srcQueue != dstQueue && tracker.m_lastPass != NullPass)
                {
                    if (auto barriers = passContext.TryGet<PassBarriers>(tracker.m_lastPass))
                    {
                        barriers->m_postImage.push_back(b);
                    }
                    else
                    {
                        PassBarriers passBarriers;
                        passBarriers.m_postImage.push_back(b);
                        passContext.Add<PassBarriers>(tracker.m_lastPass, eastl::move(passBarriers));
                    }
                }

                tracker.m_current = RHI::ResourceState{ dst.m_usage, dst.m_access, dstQueue, att.m_stage };
                tracker.m_lastPass = pass;
            });
        }

    } // namespace

    void RenderGraphCompiler::CompileResourceBarriers(
        Pass                        pass,
        PassContext&                passContext,
        RHIContext&                 context,
        RHI::TransientResourcePool& pool)
    {
        PassBarriers result;

        // Aliasing barriers must be issued before state-transition barriers.
        CompileTransientAliasingBarriers(pass, passContext, pool, result);
        CompileImageBarriers(pass, passContext, context, pool, result);
        CompileBufferBarriers(pass, passContext, context, pool, result);

        passContext.AddOrReplace<PassBarriers>(pass, eastl::move(result));
    }

    QueueBasedPasses RenderGraphCompiler::CompilePassCrossQueue(eastl::span<Pass> passes)
    {
        auto& passContext = *PassExecuteContext::Current();

        QueueBasedPasses result;

        // (dstQueue, srcQueue) → src 队列已同步到的最晚 timeline position
        eastl::array<eastl::array<uint32_t, RHI::HardwareQueueClassCount>, RHI::HardwareQueueClassCount> syncedPositions;
        for (auto& row : syncedPositions)
        {
            row.fill(RHI::InvalidTimelinePosition);
        }

        for(auto pass : passes)
        {
            ASSERT(passContext.Has<PassExecuteQueue>(pass), "The pass {} has not PassExecuteQueue", passContext.Get<PassName>(pass).m_name.GetCStr());
            const auto curQueue = passContext.Get<PassExecuteQueue>(pass).m_queue;
            const auto queueIndex = static_cast<uint32_t>(curQueue);

            result[queueIndex].push_back(pass);

            if (!passContext.Has<PassPredecessors>(pass))
            {
                continue;
            }

            // 跨队列压缩: 每个源队列只留全局 timeline 最大的 pred。
            // 全局 timeline 在每个队列子集上仍然单调递增,所以挑出来的"最晚 pred"和
            // 用队列局部位置比较的结果一致。
            // Fence value 使用 per-queue 单调递增计数器,不与每帧重置的 timeline 绑定。
            eastl::array<Pass,     RHI::HardwareQueueClassCount> latestPred{ NullPass, NullPass, NullPass };
            eastl::array<uint32_t, RHI::HardwareQueueClassCount> latestPos { 0, 0, 0 };

            for (Pass pred: passContext.Get<PassPredecessors>(pass).m_preds)
            {
                ASSERT(passContext.Has<PassExecuteQueue>(pred), "The pass {} has not PassExecuteQueue", passContext.Get<PassName>(pred).m_name.GetCStr());
                ASSERT(passContext.Has<PassGlobalTimeline>(pred), "The pass {} has not PassGlobalTimeline", passContext.Get<PassName>(pred).m_name.GetCStr());
                const auto predQueue = passContext.Get<PassExecuteQueue>(pred).m_queue;
                const auto predQueueIndex = static_cast<size_t>(predQueue);
                const uint32_t predPos = passContext.Get<PassGlobalTimeline>(pred).m_position;
                // 记录最晚的 pred pass
                if (latestPred[predQueueIndex] == NullPass || predPos > latestPos[predQueueIndex])
                {
                    latestPred[predQueueIndex] = pred;
                    latestPos[predQueueIndex] = predPos;
                }
            }

            for (uint32_t queue = 0; queue < RHI::HardwareQueueClassCount; ++queue)
            {
                // 此队列无前驱依赖
                if (latestPred[queue] == NullPass)
                {
                    continue;
                }

                // 同队列不需要同步 — GPU 按提交顺序串行执行
                if (queue == queueIndex)
                {
                    continue;
                }

                // 此队列对的同步已被当前队列上更早的 pass 覆盖
                {
                    const uint32_t lastSynced = syncedPositions[queueIndex][queue];
                    if (lastSynced != RHI::InvalidTimelinePosition && latestPos[queue] <= lastSynced)
                    {
                        continue;
                    }
                }
                syncedPositions[queueIndex][queue] = latestPos[queue];

                const auto queueSrc = static_cast<RHI::HardwareQueueClass>(queue);

                // 每个 src queue 首次 signal 时递增计数器,后续 wait 复用同一个值
                if (!passContext.Has<PassSyncSignal>(latestPred[queue]))
                {
                    const uint64_t value = ++m_crossQueueFenceValues[queue];
                    passContext.Add<PassSyncSignal>(latestPred[queue], SyncOperation{ queueSrc, value });
                }
                const uint64_t value = passContext.Get<PassSyncSignal>(latestPred[queue]).m_signal.m_value;

                if (passContext.Has<PassSyncWait>(pass))
                {
                    passContext.Get<PassSyncWait>(pass).m_waits.emplace_back(SyncOperation{ queueSrc, value });
                }
                else
                {
                    PassSyncWait wait;
                    wait.m_waits.emplace_back(queueSrc, value);
                    passContext.Add<PassSyncWait>(pass, wait);
                }
            }
        }

        return result;
    }

    QueueBasedPasses RenderGraphCompiler::CompilePassCrossQueue2(eastl::span<Pass> passes)
    {
        auto& passContext = *PassExecuteContext::Current();
        QueueBasedPasses result;

        // (dstQueue, srcQueue) → src 队列已同步到的最晚 timeline position
        eastl::array<eastl::array<uint32_t, RHI::HardwareQueueClassCount>, RHI::HardwareQueueClassCount> syncedPositions;
        for (auto& row : syncedPositions)
        {
            row.fill(RHI::InvalidTimelinePosition);
        }

        for (auto pass : passes)
        {
            ASSERT(passContext.Has<PassExecuteQueue>(pass), "The pass {} has not PassExecuteQueue", passContext.Get<PassName>(pass).m_name.GetCStr());
            const auto curQueue = passContext.Get<PassExecuteQueue>(pass).m_queue;
            const auto queueIndex = static_cast<uint32_t>(curQueue);

            result[queueIndex].push_back(pass);

            // ---- signal: successor-driven ----
            // 处理当前 pass 时检查后继,有跨队列依赖则在自己身上 emit signal。后续
            // 处理到 successor 时 predecessor 的 signal 已经就位,直接读取即可
            if (passContext.Has<PassSuccessors>(pass) && !passContext.Has<PassSyncSignal>(pass))
            {
                bool needSignal = false;
                for (Pass succ : passContext.Get<PassSuccessors>(pass).m_succs)
                {
                    if (passContext.Get<PassExecuteQueue>(succ).m_queue != curQueue)
                    {
                        needSignal = true;
                        break;
                    }
                }
                if (needSignal)
                {
                    const uint64_t value = ++m_crossQueueFenceValues[queueIndex];
                    passContext.Add<PassSyncSignal>(pass, SyncOperation{ curQueue, value });
                }
            }

            // ---- wait: predecessor-driven ----
            if (!passContext.Has<PassPredecessors>(pass))
            {
                continue;
            }

            eastl::array<Pass,     RHI::HardwareQueueClassCount> latestPred{ NullPass, NullPass, NullPass };
            eastl::array<uint32_t, RHI::HardwareQueueClassCount> latestPos { 0, 0, 0 };

            for (Pass pred : passContext.Get<PassPredecessors>(pass).m_preds)
            {
                ASSERT(passContext.Has<PassExecuteQueue>(pred), "The pass {} has not PassExecuteQueue", passContext.Get<PassName>(pred).m_name.GetCStr());
                ASSERT(passContext.Has<PassGlobalTimeline>(pred), "The pass {} has not PassGlobalTimeline", passContext.Get<PassName>(pred).m_name.GetCStr());
                const auto predQueue = passContext.Get<PassExecuteQueue>(pred).m_queue;
                const auto predQueueIndex = static_cast<size_t>(predQueue);
                const uint32_t predPos = passContext.Get<PassGlobalTimeline>(pred).m_position;

                if (latestPred[predQueueIndex] == NullPass || predPos > latestPos[predQueueIndex])
                {
                    latestPred[predQueueIndex] = pred;
                    latestPos[predQueueIndex] = predPos;
                }
            }

            for (uint32_t srcQueueIndex = 0; srcQueueIndex < RHI::HardwareQueueClassCount; ++srcQueueIndex)
            {
                if (latestPred[srcQueueIndex] == NullPass)
                {
                    continue;
                }

                if (srcQueueIndex == queueIndex)
                {
                    continue;
                }

                {
                    const uint32_t lastSynced = syncedPositions[queueIndex][srcQueueIndex];
                    if (lastSynced != RHI::InvalidTimelinePosition && latestPos[srcQueueIndex] <= lastSynced)
                        continue;
                }
                syncedPositions[queueIndex][srcQueueIndex] = latestPos[srcQueueIndex];

                const auto queueSrc = static_cast<RHI::HardwareQueueClass>(srcQueueIndex);

                ASSERT(passContext.Has<PassSyncSignal>(latestPred[srcQueueIndex]),
                    "[CompilePassCrossQueue2] Predecessor pass {} has no PassSyncSignal.",
                    passContext.Get<PassName>(latestPred[srcQueueIndex]).m_name.GetCStr());
                const uint64_t value = passContext.Get<PassSyncSignal>(latestPred[srcQueueIndex]).m_signal.m_value;

                if (passContext.Has<PassSyncWait>(pass))
                {
                    passContext.Get<PassSyncWait>(pass).m_waits.emplace_back(SyncOperation{ queueSrc, value });
                }
                else
                {
                    PassSyncWait wait;
                    wait.m_waits.emplace_back(queueSrc, value);
                    passContext.Add<PassSyncWait>(pass, wait);
                }
            }
        }

        return result;
    }

    void RenderGraphCompiler::CompileTransientResources(
        eastl::span<Pass>           /*passes*/,
        RHI::TransientResourcePool& pool)
    {
        auto& rhiContext  = *RHIExecuteContext::Current();
        auto& passContext = *PassExecuteContext::Current();

        // 1. Build name → resource entity for transient images / buffers.
        eastl::unordered_map<RHI::AttachmentId, RHIHandle> nameToImageResource;
        eastl::unordered_map<RHI::AttachmentId, RHIHandle> nameToBufferResource;

        rhiContext.GetView<TransientTag, ResourceName, RHI::ImageDescriptor>().each(
            [&](RHIHandle resource, const ResourceName& rn, const RHI::ImageDescriptor&)
            {
                nameToImageResource.emplace(rn.m_name, resource);
            });

        rhiContext.GetView<TransientTag, ResourceName, RHI::BufferDescriptor>().each(
            [&](RHIHandle resource, const ResourceName& rn, const RHI::BufferDescriptor&)
            {
                nameToBufferResource.emplace(rn.m_name, resource);
            });

        // 2. Walk attachments, accumulate per-resource lifetime + queue mask + clear value
        //    directly on the resource entity as ECS components.
        rhiContext.GetView<ImagePassAttachment>().each(
            [&](RHIHandle attachmentHandle, ImagePassAttachment& a)
            {
                if (a.m_view != NullHandle)
                {
                    return;
                }

                auto it = nameToImageResource.find(a.m_attachmentId.m_id);
                if (it == nameToImageResource.end())
                {
                    return;
                }

                const RHIHandle resource = it->second;

                ASSERT(passContext.Has<PassGlobalTimeline>(a.m_pass),
                    "Transient image attachment {}'s pass has no PassGlobalTimeline.",
                    a.m_attachmentId.m_id.GetCStr());
                ASSERT(passContext.Has<PassExecuteQueue>(a.m_pass),
                    "Transient image attachment {}'s pass has no PassExecuteQueue.",
                    a.m_attachmentId.m_id.GetCStr());

                const uint32_t pos   = passContext.Get<PassGlobalTimeline>(a.m_pass).m_position;
                const auto     queue = passContext.Get<PassExecuteQueue>(a.m_pass).m_queue;

                auto* life = rhiContext.TryGet<ImageLifetime>(resource);
                const bool isNew = (life == nullptr);
                if (isNew)
                {
                    life = &rhiContext.Add<ImageLifetime>(resource);
                }

                const uint32_t oldFirst = life->m_firstPos;
                const uint32_t oldLast  = life->m_lastPos;

                life->m_firstPos  = eastl::min(oldFirst, pos);
                life->m_lastPos   = eastl::max(oldLast,  pos);
                life->m_queueMask = life->m_queueMask | RHI::GetHardwareQueueClassMask(queue);
                life->m_attachments.push_back(attachmentHandle);

                if (isNew || pos < oldFirst) life->m_firstStage = a.m_stage;
                if (isNew || pos > oldLast)  life->m_lastStage  = a.m_stage;

                if (life->m_clearValue == nullptr &&
                    a.m_action.m_loadAction == RHI::AttachmentLoadAction::Clear)
                {
                    life->m_clearValue = &a.m_action.m_clearValue;
                }
            });

        rhiContext.GetView<BufferPassAttachment>().each(
            [&](RHIHandle attachmentHandle, BufferPassAttachment& a)
            {
                if (a.m_view != NullHandle)
                {
                    return;
                }

                auto it = nameToBufferResource.find(a.m_attachmentId.m_id);
                if (it == nameToBufferResource.end())
                {
                    return;
                }

                const RHIHandle resource = it->second;

                ASSERT(passContext.Has<PassGlobalTimeline>(a.m_pass),
                    "Transient buffer attachment {}'s pass has no PassGlobalTimeline.",
                    a.m_attachmentId.m_id.GetCStr());
                ASSERT(passContext.Has<PassExecuteQueue>(a.m_pass),
                    "Transient buffer attachment {}'s pass has no PassExecuteQueue.",
                    a.m_attachmentId.m_id.GetCStr());

                const uint32_t pos   = passContext.Get<PassGlobalTimeline>(a.m_pass).m_position;
                const auto     queue = passContext.Get<PassExecuteQueue>(a.m_pass).m_queue;

                auto* life = rhiContext.TryGet<BufferLifetime>(resource);
                const bool isNew = (life == nullptr);
                if (isNew)
                {
                    life = &rhiContext.Add<BufferLifetime>(resource);
                }

                const uint32_t oldFirst = life->m_firstPos;
                const uint32_t oldLast  = life->m_lastPos;

                life->m_firstPos  = eastl::min(oldFirst, pos);
                life->m_lastPos   = eastl::max(oldLast,  pos);
                life->m_queueMask = life->m_queueMask | RHI::GetHardwareQueueClassMask(queue);
                life->m_attachments.push_back(attachmentHandle);

                if (isNew || pos < oldFirst) life->m_firstStage = a.m_stage;
                if (isNew || pos > oldLast)  life->m_lastStage  = a.m_stage;
            });

        // 3. Build sweep events: emit (firstPos, Create) and (lastPos+1, Discard) for each lifetime.
        eastl::vector<SweepEvent> events;

        rhiContext.GetView<ImageLifetime>().each(
            [&](RHIHandle resource, const ImageLifetime& life)
            {
                if (life.m_attachments.empty())
                {
                    return;
                }
                events.push_back(SweepEvent{ life.m_firstPos,    SweepAction::Create,  SweepResourceType::Image, resource });
                events.push_back(SweepEvent{ life.m_lastPos + 1, SweepAction::Discard, SweepResourceType::Image, resource });
            });

        rhiContext.GetView<BufferLifetime>().each(
            [&](RHIHandle resource, const BufferLifetime& life)
            {
                if (life.m_attachments.empty())
                {
                    return;
                }
                events.push_back(SweepEvent{ life.m_firstPos,    SweepAction::Create,  SweepResourceType::Buffer, resource });
                events.push_back(SweepEvent{ life.m_lastPos + 1, SweepAction::Discard, SweepResourceType::Buffer, resource });
            });

        // Lifetime intervals are [firstPos, lastPos+1). At the same timeline position,
        // a Discard (interval end) logically precedes a Create (interval start).
        eastl::sort(events.begin(), events.end(), [](const SweepEvent& a, const SweepEvent& b)
        {
            if (a.m_pos != b.m_pos)
            {
                return a.m_pos < b.m_pos;
            }
            if (a.m_action != b.m_action)
            {
                return a.m_action == SweepAction::Discard;
            }
            return false;
        });

        // 4. Sweep: call pool, store backing on resource entity, materialize views.
        auto* factoryPtr = Service<RHI::Factory>::Get();
        ASSERT(factoryPtr != nullptr, "RHI::Factory service is not registered.");
        auto& factory = *factoryPtr;

        for (const auto& ev : events)
        {
            switch (ev.m_action)
            {
            case SweepAction::Create:
                switch (ev.m_resourceType)
                {
                case SweepResourceType::Image:
                    {
                        auto&       life = rhiContext.Get<ImageLifetime>(ev.m_resource);
                        const auto& desc = rhiContext.Get<RHI::ImageDescriptor>(ev.m_resource);
                        const auto& name = rhiContext.Get<ResourceName>(ev.m_resource).m_name;

                        RHI::TransientImageCreateInfo info;
                        info.m_descriptor          = desc;
                        info.m_optimizedClearValue = life.m_clearValue;
                        info.m_debugName           = name;

                        RHI::TransientAllocationFence fence{ life.m_queueMask, life.m_firstPos, life.m_firstStage };
                        RHI::Image* image = pool.CreateImage(info, fence);
                        ASSERT(image != nullptr,
                            "TransientResourcePool::CreateImage returned null for '{}'.",
                            name.GetCStr());

                        rhiContext.Add<BackingImage>(ev.m_resource, BackingImage{ image });
                        CompileTransientImageViews(rhiContext, factory, ev.m_resource, *image, life.m_attachments);
                    }
                    break;
                case SweepResourceType::Buffer:
                    {
                        auto&       life = rhiContext.Get<BufferLifetime>(ev.m_resource);
                        const auto& desc = rhiContext.Get<RHI::BufferDescriptor>(ev.m_resource);
                        const auto& name = rhiContext.Get<ResourceName>(ev.m_resource).m_name;

                        RHI::TransientBufferCreateInfo info;
                        info.m_descriptor = desc;
                        info.m_debugName  = name;

                        RHI::TransientAllocationFence fence{ life.m_queueMask, life.m_firstPos, life.m_firstStage };
                        RHI::Buffer* buffer = pool.CreateBuffer(info, fence);
                        ASSERT(buffer != nullptr,
                            "TransientResourcePool::CreateBuffer returned null for '{}'.",
                            name.GetCStr());

                        rhiContext.Add<BackingBuffer>(ev.m_resource, BackingBuffer{ buffer });
                        CompileTransientBufferViews(rhiContext, factory, ev.m_resource, *buffer, life.m_attachments);
                    }
                    break;
                }
                break;
            case SweepAction::Discard:
                switch (ev.m_resourceType)
                {
                case SweepResourceType::Image:
                    if (auto* ti = rhiContext.TryGet<BackingImage>(ev.m_resource))
                    {
                        auto& life = rhiContext.Get<ImageLifetime>(ev.m_resource);
                        RHI::TransientAllocationFence fence{ life.m_queueMask, ev.m_pos, life.m_lastStage };
                        pool.Discard(ti->m_image, fence);
                    }
                    break;
                case SweepResourceType::Buffer:
                    if (auto* tb = rhiContext.TryGet<BackingBuffer>(ev.m_resource))
                    {
                        auto& life = rhiContext.Get<BufferLifetime>(ev.m_resource);
                        RHI::TransientAllocationFence fence{ life.m_queueMask, ev.m_pos, life.m_lastStage };
                        pool.Discard(tb->m_buffer, fence);
                    }
                    break;
                }
                break;
            }
        }

        // 5. Clean up ephemeral lifetime components from resource entities.
        rhiContext.Clear<ImageLifetime>();
        rhiContext.Clear<BufferLifetime>();

        // 6. Seal the pool: no further Create/Discard for this frame, and aliasing
        //    barriers are now queryable for per-pass barrier compilation.
        pool.Seal();
    }

    void RenderGraphCompiler::CompileRenderPassBeginInfo(Pass pass, PassContext& passContext, RHIContext& context)
    {
        RHI::RenderPassBeginInfo info;
        bool hasAny = false;

        auto view = context.GetView<ImagePassAttachment, AttachmentCompilingTag>();
        view.each([&](auto, const ImagePassAttachment& att)
        {
            ASSERT(att.m_view != NullHandle,
                "[RenderGraphCompiler] Attachment {} has a null view entity; transient view materialization may have been skipped.",
                att.m_attachmentId.m_id.GetCStr());

            ASSERT(context.Has<BackingImageView>(att.m_view),
                "[RenderGraphCompiler] Attachment {}'s view entity has no BackingImageView component.",
                att.m_attachmentId.m_id.GetCStr());

            RHI::ImageView* imageView = context.Get<BackingImageView>(att.m_view).m_view;
            ASSERT(imageView != nullptr,
                "[RenderGraphCompiler] Attachment {}'s BackingImageView holds a null RHI::ImageView pointer.",
                att.m_attachmentId.m_id.GetCStr());

            if (att.m_usage == RHI::AttachmentUsage::RenderTarget)
            {
                ASSERT(info.m_colorAttachmentCount < RHI::Limits::Pipeline::AttachmentColorCountMax,
                       "[RenderGraphCompiler] Too many color attachments on pass.");
                auto& color = info.m_colorAttachments[info.m_colorAttachmentCount++];
                color.m_view = imageView;
                color.m_loadStoreAction = att.m_action;
                hasAny = true;
            }
            else if (att.m_usage == RHI::AttachmentUsage::DepthStencil)
            {
                ASSERT(info.m_depthStencilAttachment.m_view == nullptr,
                    "[RenderGraphCompiler] Pass has more than one depth-stencil attachment.");
                info.m_depthStencilAttachment.m_view = imageView;
                info.m_depthStencilAttachment.m_access = att.m_access;
                info.m_depthStencilAttachment.m_loadStoreAction = att.m_action;
                hasAny = true;
            }
        });

        ASSERT(hasAny,
            "[RenderGraphCompiler] Render pass {} has no color or depth-stencil attachment.",
            passContext.Get<PassName>(pass).m_name.GetCStr());
        passContext.AddOrReplace<RHI::RenderPassBeginInfo>(pass, eastl::move(info));
    }

    void RenderGraphCompiler::CompilePipelineStates(
        eastl::span<Pass>,
        PassContext&          passContext,
        RHI::Device&          device,
        RHI::PipelineLibrary* pipelineLibrary)
    {
        auto* factory = Service<RHI::Factory>::Get();
        ASSERT(factory, "RHI::Factory service not registered.");

        // --- Render passes ---
        {
            auto view = passContext.GetView<RenderPassTag, PassShaders, PassPipelineState, PassShaderResources>(
                Exclude<CustomPipelinePassTag>);

            view.each([&](Pass pass, const PassShaders& shaders, const PassPipelineState& pipelineState, const PassShaderResources& srgs)
            {
                if (passContext.Has<PassCompiledPSO>(pass) && !passContext.Has<PassPSODirtyTag>(pass))
                    return;

                RHI::PipelineStateDescriptorForDraw descriptor;

                if (shaders.m_vertexShader)
                {
                    auto* stage = shaders.m_vertexShader->GetShaderData()->GetStageBytecode(RHI::ShaderStage::Vertex);
                    ASSERT(stage, "Pass '{}': VertexShader has no vertex-stage bytecode.",
                        passContext.Get<PassName>(pass).m_name.GetCStr());
                    auto func = factory->CreateShaderStageFunction(RHI::ShaderStage::Vertex);
                    func->SetByteCode(stage->bytecode);
                    func->Finalize();
                    descriptor.m_vertexFunction = eastl::move(func);
                }

                if (shaders.m_fragmentShader)
                {
                    auto* stage = shaders.m_fragmentShader->GetShaderData()->GetStageBytecode(RHI::ShaderStage::Fragment);
                    ASSERT(stage, "Pass '{}': FragmentShader has no fragment-stage bytecode.",
                        passContext.Get<PassName>(pass).m_name.GetCStr());
                    auto func = factory->CreateShaderStageFunction(RHI::ShaderStage::Fragment);
                    func->SetByteCode(stage->bytecode);
                    func->Finalize();
                    descriptor.m_fragmentFunction = eastl::move(func);
                }

                if (shaders.m_geometryShader)
                {
                    auto* stage = shaders.m_geometryShader->GetShaderData()->GetStageBytecode(RHI::ShaderStage::Geometry);
                    ASSERT(stage, "Pass '{}': GeometryShader has no geometry-stage bytecode.",
                        passContext.Get<PassName>(pass).m_name.GetCStr());
                    auto func = factory->CreateShaderStageFunction(RHI::ShaderStage::Geometry);
                    func->SetByteCode(stage->bytecode);
                    func->Finalize();
                    descriptor.m_geometryFunction = eastl::move(func);
                }

                descriptor.m_inputStreamLayout  = pipelineState.m_inputStreamLayout;
                descriptor.m_renderTargetLayout = pipelineState.m_renderTargetLayout;
                descriptor.m_renderStates       = pipelineState.m_renderStates;

                descriptor.m_pipelineLayoutDescriptor =
                    BuildPipelineLayoutDescriptor(srgs, DeriveStageMask(shaders));

                auto pso = factory->CreatePipelineState();
                RHI::ResultCode rc = pso->Init(device, descriptor, pipelineLibrary);
                ASSERT(rc == RHI::ResultCode::Success,
                    "Failed to create graphics PSO for pass '{}'.",
                    passContext.Get<PassName>(pass).m_name.GetCStr());

                passContext.AddOrReplace<PassCompiledPSO>(pass, PassCompiledPSO{eastl::move(pso)});

                if (passContext.Has<PassPSODirtyTag>(pass))
                {
                    passContext.Remove<PassPSODirtyTag>(pass);
                }
            });
        }

        // --- Compute passes ---
        {
            auto view = passContext.GetView<ComputePassTag, PassShaders, PassShaderResources>(
                Exclude<CustomPipelinePassTag>);

            view.each([&](Pass pass, const PassShaders& shaders, const PassShaderResources& srgs)
            {
                if (passContext.Has<PassCompiledPSO>(pass) && !passContext.Has<PassPSODirtyTag>(pass))
                    return;

                ASSERT(shaders.m_computeShader,
                    "Compute pass '{}' has no ComputeShader.",
                    passContext.Get<PassName>(pass).m_name.GetCStr());

                RHI::PipelineStateDescriptorForDispatch descriptor;

                auto* stage = shaders.m_computeShader->GetShaderData()->GetStageBytecode(RHI::ShaderStage::Compute);
                ASSERT(stage, "Pass '{}': ComputeShader has no compute-stage bytecode.",
                    passContext.Get<PassName>(pass).m_name.GetCStr());
                auto func = factory->CreateShaderStageFunction(RHI::ShaderStage::Compute);
                func->SetByteCode(stage->bytecode);
                func->Finalize();
                descriptor.m_computeFunction = eastl::move(func);

                descriptor.m_pipelineLayoutDescriptor =
                    BuildPipelineLayoutDescriptor(srgs, DeriveStageMask(shaders));

                auto pso = factory->CreatePipelineState();
                RHI::ResultCode rc = pso->Init(device, descriptor, pipelineLibrary);
                ASSERT(rc == RHI::ResultCode::Success,
                    "Failed to create compute PSO for pass '{}'.",
                    passContext.Get<PassName>(pass).m_name.GetCStr());

                passContext.AddOrReplace<PassCompiledPSO>(pass, PassCompiledPSO{eastl::move(pso)});

                if (passContext.Has<PassPSODirtyTag>(pass))
                {
                    passContext.Remove<PassPSODirtyTag>(pass);
                }
            });
        }
    }

    void RenderGraphCompiler::CompileShaderResources(RHI::Device& device, RHIContext& context)
    {
        auto& view = context.GetView<RHIUpdateTag, BackingShaderResource>();
        auto* factory = Service<RHI::Factory>::Get();
        ASSERT(factory, "[RenderGraph] RHI::Factory service not registered.");

        auto& shaderResourceCompiler = factory->AcquireShaderResourceCompiler(device);
        eastl::vector<RHI::ShaderResource*> shaderResources;
        shaderResources.reserve(view.size_hint());
        view.each([&](RHIHandle handle, BackingShaderResource& shaderResource)
        {
            shaderResources.push_back(shaderResource.m_shaderResource);
            context.Remove<RHIUpdateTag>(handle);
        });
        shaderResourceCompiler.Compiler(shaderResources);

    }
}
