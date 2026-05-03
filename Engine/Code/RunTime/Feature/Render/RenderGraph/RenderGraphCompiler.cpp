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
            eastl::vector<RHIHandle>    m_attachments;
            const RHI::ClearValue*      m_clearValue = nullptr;
        };

        struct BufferLifetime
        {
            uint32_t                    m_firstPos  = RHI::InvalidTimelinePosition;
            uint32_t                    m_lastPos   = 0;
            RHI::HardwareQueueClassMask m_queueMask = RHI::HardwareQueueClassMask::None;
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
                rhiContext.Add<TransientImageView>(viewEntity, TransientImageView{ eastl::move(view) });
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
                rhiContext.Add<TransientBufferView>(viewEntity, TransientBufferView{ eastl::move(view) });
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
    } // namespace

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
                if (!life)
                {
                    life = &rhiContext.Add<ImageLifetime>(resource);
                }

                life->m_firstPos  = eastl::min(life->m_firstPos, pos);
                life->m_lastPos   = eastl::max(life->m_lastPos,  pos);
                life->m_queueMask = life->m_queueMask | RHI::GetHardwareQueueClassMask(queue);
                life->m_attachments.push_back(attachmentHandle);

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
                if (!life)
                {
                    life = &rhiContext.Add<BufferLifetime>(resource);
                }

                life->m_firstPos  = eastl::min(life->m_firstPos, pos);
                life->m_lastPos   = eastl::max(life->m_lastPos,  pos);
                life->m_queueMask = life->m_queueMask | RHI::GetHardwareQueueClassMask(queue);
                life->m_attachments.push_back(attachmentHandle);
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

                        RHI::TransientAllocationFence fence{ life.m_queueMask, life.m_firstPos };
                        RHI::Image* image = pool.CreateImage(info, fence);
                        ASSERT(image != nullptr,
                            "TransientResourcePool::CreateImage returned null for '{}'.",
                            name.GetCStr());

                        rhiContext.Add<TransientImage>(ev.m_resource, TransientImage{ image });
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

                        RHI::TransientAllocationFence fence{ life.m_queueMask, life.m_firstPos };
                        RHI::Buffer* buffer = pool.CreateBuffer(info, fence);
                        ASSERT(buffer != nullptr,
                            "TransientResourcePool::CreateBuffer returned null for '{}'.",
                            name.GetCStr());

                        rhiContext.Add<TransientBuffer>(ev.m_resource, TransientBuffer{ buffer });
                        CompileTransientBufferViews(rhiContext, factory, ev.m_resource, *buffer, life.m_attachments);
                    }
                    break;
                }
                break;
            case SweepAction::Discard:
                switch (ev.m_resourceType)
                {
                case SweepResourceType::Image:
                    if (auto* ti = rhiContext.TryGet<TransientImage>(ev.m_resource))
                    {
                        auto& life = rhiContext.Get<ImageLifetime>(ev.m_resource);
                        RHI::TransientAllocationFence fence{ life.m_queueMask, ev.m_pos };
                        pool.Discard(ti->m_image, fence);
                    }
                    break;
                case SweepResourceType::Buffer:
                    if (auto* tb = rhiContext.TryGet<TransientBuffer>(ev.m_resource))
                    {
                        auto& life = rhiContext.Get<BufferLifetime>(ev.m_resource);
                        RHI::TransientAllocationFence fence{ life.m_queueMask, ev.m_pos };
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
    }

    void RenderGraphCompiler::CompileTransientAliasingBarriers(
        eastl::span<Pass>           passes,
        RHI::TransientResourcePool& pool)
    {
        auto& passContext = *PassExecuteContext::Current();

        for (auto pass : passes)
        {
            ASSERT(passContext.Has<PassGlobalTimeline>(pass),
                "Pass {} has no PassGlobalTimeline.",
                passContext.Get<PassName>(pass).m_name.GetCStr());
            const uint32_t pos = passContext.Get<PassGlobalTimeline>(pass).m_position;

            RHI::AliasingBarrierList barriers;
            pool.GetAliasingBarriers(pos, barriers);

            if (barriers.empty())
            {
                continue;
            }

            if (auto* pb = passContext.TryGet<PassBarriers>(pass))
            {
                pb->m_preAliasing = eastl::move(barriers);
            }
            else
            {
                PassBarriers passBarriers;
                passBarriers.m_preAliasing = eastl::move(barriers);
                passContext.Add<PassBarriers>(pass, eastl::move(passBarriers));
            }
        }
    }

}
