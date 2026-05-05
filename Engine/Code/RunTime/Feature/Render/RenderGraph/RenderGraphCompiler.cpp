#include "RenderGraphCompiler.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_map.h>

#include <Log/SpdLogSystem.h>
#include <Service/Service.h>

#include <RHI/Factory.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Transient/TransientResourcePool.h>
#include <RHI/Command/RenderPassBeginInfo.h>

#include <Pass/Component/PassComponents.h>

namespace Spark::Render
{
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
                rhiContext.Add<ImageViewPtr>(viewEntity, ImageViewPtr{ eastl::move(view) });
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
                rhiContext.Add<BufferViewPtr>(viewEntity, BufferViewPtr{ eastl::move(view) });
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

        RHI::ResourceState GetImportedResourceInitialState(RHIHandle resource, const RHIContext& context)
        {
            if (context.Has<ImportedTag>(resource))
            {
                ASSERT(context.Has<ImportedResourceState>(resource), "Imported resource must have ImportedResourceState.");
                return context.Get<ImportedResourceState>(resource).m_initial;
            }
            else if (context.Has<TransientTag>(resource))
            {
                return RHI::ResourceState{RHI::AttachmentUsage::Uninitialized, RHI::AttachmentAccess::Unknown};
            }

            LOG_ERROR("Resource {} has not ImportedTag nor TransientTag", context.Get<ResourceName>(resource).m_name.GetCStr());
            return RHI::ResourceState{RHI::AttachmentUsage::Uninitialized, RHI::AttachmentAccess::Unknown};
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

        void CompileBufferBarriers(
            Pass                        pass, 
            PassContext&                passContext, 
            RHIContext&                 context, 
            RHI::TransientResourcePool& pool, 
            PassBarriers&               out)
        {
            ASSERT(passContext.Has<PassExecuteQueue>(pass), "The pass {} has not PassExecuteQueue", passContext.Get<PassName>(pass).m_name.GetCStr());

            auto view = context.GetView<BufferPassAttachment, AttachmentCompilingTag>();
            view.each([&](auto, const BufferPassAttachment& att)
            {
                RHIHandle resource = ResolveResource(att.m_view, context);
                ASSERT(resource != NullHandle, "Buffer attachment view has no resource.");

                //
                if (!context.Has<ResourceStateTracker>(resource))
                {
                    ResourceStateTracker init;
                    init.m_current = GetImportedResourceInitialState(resource, context);
                    context.Add<ResourceStateTracker>(resource, init);
                }
                auto& tracker = context.Get<ResourceStateTracker>(resource);

                RHI::HardwareQueueClass dstQueue = passContext.Get<PassExecuteQueue>(pass).m_queue;
                RHI::HardwareQueueClass srcQueue;
                if (tracker.m_lastPass != NullPass)
                {
                    ASSERT(passContext.Has<PassExecuteQueue>(tracker.m_lastPass), 
                        "The pass {} has not PassExecuteQueue", 
                        passContext.Get<PassName>(tracker.m_lastPass).m_name.GetCStr());
                    srcQueue = passContext.Get<PassExecuteQueue>(tracker.m_lastPass).m_queue;
                }
                else
                {
                    srcQueue = dstQueue; // 首次接触：同一队列，无需 ownership transfer
                }


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
                    b.m_srcStage  = tracker.m_lastStage;
                    b.m_dstStage  = att.m_stage;
                    b.m_srcQueue  = srcQueue;
                    b.m_dstQueue  = dstQueue;
                    out.m_preBuffer.push_back(b);
                }

                // Sync cross queue, emit Release Barrier on previous pass
                if (srcQueue != dstQueue)
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

                tracker.m_current   = dst;
                tracker.m_lastPass  = pass;
                tracker.m_lastStage = att.m_stage;
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

            auto view = context.GetView<ImagePassAttachment, AttachmentCompilingTag>();
            view.each([&](auto, const ImagePassAttachment& att)
            {
                RHIHandle resource = ResolveResource(att.m_view, context);
                ASSERT(resource != NullHandle, "Image attachment view has no resource.");

                if (!context.Has<ResourceStateTracker>(resource))
                {
                    ResourceStateTracker init;
                    init.m_current = GetImportedResourceInitialState(resource, context);
                    context.Add<ResourceStateTracker>(resource, init);
                }
                auto& tracker = context.Get<ResourceStateTracker>(resource);

                RHI::HardwareQueueClass dstQueue = passContext.Get<PassExecuteQueue>(pass).m_queue;
                RHI::HardwareQueueClass srcQueue;
                if (tracker.m_lastPass != NullPass)
                {
                    ASSERT(passContext.Has<PassExecuteQueue>(tracker.m_lastPass),
                        "The pass {} has not PassExecuteQueue",
                        passContext.Get<PassName>(tracker.m_lastPass).m_name.GetCStr());
                    srcQueue = passContext.Get<PassExecuteQueue>(tracker.m_lastPass).m_queue;
                }
                else
                {
                    srcQueue = dstQueue;
                }

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
                    b.m_srcStage  = tracker.m_lastStage;
                    b.m_dstStage  = att.m_stage;
                    b.m_srcQueue  = srcQueue;
                    b.m_dstQueue  = dstQueue;
                    out.m_preImage.push_back(b);
                }

                if (srcQueue != dstQueue)
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

                tracker.m_current   = dst;
                tracker.m_lastPass  = pass;
                tracker.m_lastStage = att.m_stage;
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

        for(auto pass : passes)
        {
            ASSERT(passContext.Has<PassExecuteQueue>(pass), "The pass {} has not PassExecuteQueue", passContext.Get<PassName>(pass).m_name.GetCStr());
            const auto queue = passContext.Get<PassExecuteQueue>(pass).m_queue;
            const auto queueIndex = static_cast<size_t>(queue);

            result[queueIndex].push_back(pass);

            if (!passContext.Has<PassPredecessors>(pass))
            {
                continue;
            }

            // 跨队列压缩: 每个源队列只留全局 timeline 最大的 pred。
            // 全局 timeline 在每个队列子集上仍然单调递增,所以挑出来的"最晚 pred"和
            // 用队列局部位置比较的结果一致;同时 latestPos 直接当 timeline semaphore 的 value 用。
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

                const auto queueSrc = static_cast<RHI::HardwareQueueClass>(queue);
                const uint64_t value = latestPos[queue];

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

                // pred 自己队列上只 signal 一次
                if (!passContext.Has<PassSyncSignal>(latestPred[queue]))
                {
                    passContext.Add<PassSyncSignal>(latestPred[queue], SyncOperation{ queueSrc, value });
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

            ASSERT(context.Has<ImageViewPtr>(att.m_view),
                "[RenderGraphCompiler] Attachment {}'s view entity has no ImageViewPtr component.",
                att.m_attachmentId.m_id.GetCStr());

            RHI::ImageView* imageView = context.Get<ImageViewPtr>(att.m_view).m_view.get();
            ASSERT(imageView != nullptr,
                "[RenderGraphCompiler] Attachment {}'s ImageViewPtr holds a null RHI::ImageView pointer.",
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
}
