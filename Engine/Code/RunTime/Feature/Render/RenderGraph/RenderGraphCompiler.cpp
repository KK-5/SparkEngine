#include "RenderGraphCompiler.h"
#include "RenderGraphUtils.h"
#include "PooledImage.h"

#include <EASTL/algorithm.h>
#include <EASTL/bonus/overloaded.h>
#include <EASTL/sort.h>
#include <EASTL/unordered_map.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/Factory.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Command/CommandQueueContext.h>
#include <RHI/Device/Device.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Pipeline/PipelineState.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImagePool.h>
#include <RHI/Resource/ShaderInput/ShaderInputCompiler.h>
#include <RHI/Resource/Transient/TransientResourcePool.h>
#include <RHI/Command/RenderPassBeginInfo.h>

#include <RHI/Command/DrawItem.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Pipeline/ShaderStages.h>

#include <Drawable/GeometrySpec.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/ScopeComponents.h>
#include <Pass/PassCapabilities.h>
#include <Shader/ShaderBindingsUtils.h>
#include <View/View.h>
#include <View/ViewComponents.h>

namespace Spark::Render
{

    void RenderGraphCompiler::Begin(uint32_t frameIndex)
    {
        m_frameIndex = frameIndex;
    }

    StaticPreBarrierTable RenderGraphCompiler::CompileStaticResourceBarriers(RHIContext& context)
    {
        StaticPreBarrierTable table;

        auto ResolveHomeQueue = [](RHI::HardwareQueueClassMask mask) -> RHI::HardwareQueueClass
        {
            if (mask == RHI::HardwareQueueClassMask::Compute) { return RHI::HardwareQueueClass::Compute; }
            if (mask == RHI::HardwareQueueClassMask::Copy)    { return RHI::HardwareQueueClass::Copy; }
            return RHI::HardwareQueueClass::Graphics;
        };

        // Collect the cross-queue upload fence-wait for `resource` onto its home queue's
        // pre-barrier list, de-duplicated by (fence, value): several static resources
        // uploaded in one batch share a single fence value, so the queue only waits once.
        // No-op if the resource carries no PendingSync, or its fence already reached the
        // value (the wait would be redundant — fence values are monotonic, so "done at
        // compile" stays done through execute).
        auto CollectFenceWait = [&context](RHIHandle resource, StaticPreBarriers& out)
        {
            auto* sync = context.TryGet<RHI::PendingSync>(resource);
            if (!sync)
            {
                return;
            }
            if (sync->m_fence && sync->m_fence->GetCompletedValue() >= sync->m_fenceValue)
            {
                return;
            }
            for (const auto& w : out.m_fenceWaits)
            {
                if (w.m_fence == sync->m_fence && w.m_fenceValue == sync->m_fenceValue)
                {
                    return; // already collected for this queue
                }
            }
            out.m_fenceWaits.push_back(*sync);
        };

        // Buffer static imports
        context.GetView<StaticImportTag, BufferPassAttachment>().each(
            [&](RHIHandle resource, const BufferPassAttachment& att) {
                // StaticImportTag lives on the resource entity; the attachment is
                // also on the resource entity (static imports are accessed via SRG
                // bindings, not as pass attachments).
                // Materialized AND its one-time upload submitted — otherwise defer to a
                // later frame. Advancing state before upload-submit would race the pending
                // cross-queue copy. See IsResourceReady (RenderGraphUtils.h) for the full
                // rationale (shared with the imported-resource pass path).
                if (!IsResourceReady(context, resource))
                {
                    return;
                }
                auto* buf = context.TryGet<Buffer>(resource);

                const RHI::ResourceState src = buf->m_buffer->GetResourceState();
                RHI::ResourceState       dst = CompileResourceState(att);

                const auto homeQueue = ResolveHomeQueue(
                    buf->m_buffer->GetDescriptor().m_sharedQueueMask);
                const auto qi = static_cast<uint32_t>(homeQueue);
                // Pin dst to the resource's steady (post-acquire) identity — its home
                // queue and the attachment's shader stage. CompileResourceState only
                // fills usage/access, leaving stage=Any / queue=default; without this
                // the acquired src ({Shader, homeQueue, FragmentShader}) never equals
                // dst and the steady-state guard below re-emits a redundant barrier
                // every frame. Scoped here (not in CompileResourceState) so the
                // transient per-pass paths keep their own comparison semantics.
                dst.m_queue = homeQueue;
                dst.m_stage = att.m_stage;

                if (src == dst && src.m_queue == homeQueue)
                {
                    return; // Steady state
                }

                // Fence wait for cross-queue handoff from upload
                if (src.m_queue != homeQueue)
                {
                    CollectFenceWait(resource, table[qi]);
                }

                RHI::BufferBarrier b;
                b.m_buffer    = buf->m_buffer.get();
                b.m_srcAccess = src.m_access;
                b.m_dstAccess = dst.m_access;
                b.m_srcStage  = src.m_stage;
                b.m_dstStage  = att.m_stage;
                b.m_srcQueue  = src.m_queue;
                b.m_dstQueue  = homeQueue;
                table[qi].m_bufferBarriers.push_back(b);
            });

        // Image static imports
        context.GetView<StaticImportTag, ImagePassAttachment>().each(
            [&](RHIHandle resource, const ImagePassAttachment& att) {
                // StaticImportTag lives on the resource entity; the attachment is
                // also on the resource entity (static imports are accessed via SRG
                // bindings, not as pass attachments).
                // Materialized AND upload submitted — see the buffer path above.
                if (!IsResourceReady(context, resource))
                {
                    return;
                }
                auto* img = context.TryGet<Image>(resource);

                const RHI::ResourceState src = img->m_image->GetResourceState();
                RHI::ResourceState       dst = CompileResourceState(att);

                const auto homeQueue = ResolveHomeQueue(
                    img->m_image->GetDescriptor().m_sharedQueueMask);
                const auto qi = static_cast<uint32_t>(homeQueue);
                // See the buffer path above: pin dst to the acquired identity (home
                // queue + attachment stage) so the steady-state guard recognises the
                // resource and stops re-emitting once the handoff has settled.
                dst.m_queue = homeQueue;
                dst.m_stage = att.m_stage;

                if (src == dst && src.m_queue == homeQueue)
                {
                    return;
                }

                if (src.m_queue != homeQueue)
                {
                    CollectFenceWait(resource, table[qi]);
                }

                // loadOp=Clear discards prior contents — force src to the "no access"
                // state so the barrier transitions from COMMON.
                RHI::ResourceState srcForBarrier = src;
                if (att.m_action.m_loadAction == RHI::AttachmentLoadAction::Clear)
                {
                    srcForBarrier.m_access = RHI::AccessFlags::None;
                }

                if (srcForBarrier != dst || src.m_queue != homeQueue)
                {
                    RHI::ImageBarrier b;
                    b.m_image     = img->m_image.get();
                    b.m_srcAccess = srcForBarrier.m_access;
                    b.m_dstAccess = dst.m_access;
                    b.m_srcStage  = src.m_stage;
                    b.m_dstStage  = att.m_stage;
                    b.m_srcQueue  = src.m_queue;
                    b.m_dstQueue  = homeQueue;
                    table[qi].m_imageBarriers.push_back(b);
                }
            });

        return table;
    }

    void RenderGraphCompiler::End()
    {
        // Attachment entities are the pass→resource edges. They are NOT destroyed
        // here: Execute still records their barriers. They are destroyed in
        // RenderGraphExecuter::End(), after Execute, before next frame's Build.

        // Per-resource compile-time state cursor is now cleared in
        // Executer::End(), after frame-end PendingSync update consumes it.

        // m_crossQueueFenceValues is intentionally cross-frame (monotonic).
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

        // Transient image AND buffer views are no longer materialized as view
        // entities. Image views are built lazily via GetOrCreateImageView (resource's
        // ImageViewCache). Buffer views currently have no consumer, so no view is
        // created at all — only the buffer itself (att.m_buffer → BackingBuffer) is
        // needed for barriers. A buffer-side view cache
        // can be added when a buffer-view consumer appears.

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

    } // namespace

    void RenderGraphCompiler::CompileExtractedImages(RHI::ImagePool& pool)
    {
        auto& ctx = *RHIExecuteContext::Current();

        // Attachments already point at these resources (CompileTransientResources links by
        // name); only the backing differs — a pooled image that outlives the frame.
        for (auto [resource, extracted] : ctx.GetView<TransientTag, ExtractedImage>().each())
        {
            // Copies: acquiring may add to the ImageDescriptor / BackingImage storages.
            const RHI::ImageDescriptor desc = ctx.Get<RHI::ImageDescriptor>(resource);
            const RHI::AttachmentId    name = ctx.Get<ResourceName>(resource).m_name;
            extracted.m_pooledImage = AcquirePooledImage(
                ctx, pool, desc, ctx.TryGet<RHI::ClearValue>(resource), name);

            const BackingImage backing = ctx.Get<BackingImage>(extracted.m_pooledImage);
            ctx.Add<BackingImage>(resource, backing);
        }
    }

    namespace
    {
        struct ScopeOrderKey
        {
            uint32_t m_passPosition = 0;
            uint32_t m_scopeIndex   = 0;

            bool operator<(const ScopeOrderKey& other) const
            {
                if (m_passPosition != other.m_passPosition)
                {
                    return m_passPosition < other.m_passPosition;
                }
                return m_scopeIndex < other.m_scopeIndex;
            }
        };

        bool ScopeAttachmentLess(const ScopeAttachment& lhs, const ScopeAttachment& rhs)
        {
            if (lhs.m_scopeOrder != rhs.m_scopeOrder)
            {
                return lhs.m_scopeOrder < rhs.m_scopeOrder;
            }
            return entt::to_integral(lhs.m_resource) < entt::to_integral(rhs.m_resource);
        }

        ScopeOrderKey MakeScopeOrderKey(const Scope& scope, const PassContext& passContext)
        {
            return ScopeOrderKey{ passContext.Get<PassGlobalTimeline>(scope.m_pass).m_position, scope.m_index };
        }

        //! Put on every Scope the range RangeT of its entries in LinkT's packed array, which is
        //! sorted by Scope: an entry names its Scope in m_scope. A Scope with no entry gets an
        //! empty range.
        template<typename LinkT, typename RangeT>
        void RecordScopeRanges(RHIContext& context)
        {
            for (auto [scope, data] : context.GetStorage<Scope>().each())
            {
                context.Add<RangeT>(scope);
            }

            auto&            links  = context.GetStorage<LinkT>();
            const RHIHandle* packed = links.data();
            const auto       count  = static_cast<uint32_t>(links.size());
            for (uint32_t begin = 0; begin < count;)
            {
                const RHIHandle scope = links.get(packed[begin]).m_scope;
                uint32_t        end   = begin + 1;
                while (end < count && links.get(packed[end]).m_scope == scope)
                {
                    ++end;
                }

                RangeT& range = context.Get<RangeT>(scope);
                ASSERT(range.m_begin == range.m_end,
                    "[RenderGraphCompiler] A Scope's entries are not contiguous after sorting.");
                range = RangeT{ begin, end };
                begin = end;
            }
        }
    }

    void RenderGraphCompiler::SortScopes(PassContext& passContext, RHIContext& context)
    {
        context.Sort<Scope>([&](const Scope& lhs, const Scope& rhs)
        {
            return MakeScopeOrderKey(lhs, passContext) < MakeScopeOrderKey(rhs, passContext);
        });

        // Copied onto the links once, so their sorts below look nothing up per comparison.
        uint32_t order = 0;
        for (auto [scope, data] : context.GetStorage<Scope>().each())
        {
            data.m_order = order++;
        }
        for (auto [attachment, link] : context.GetStorage<ScopeAttachment>().each())
        {
            link.m_scopeOrder = context.Get<Scope>(link.m_scope).m_order;
        }
        for (auto [item, link] : context.GetStorage<ScopeItem>().each())
        {
            link.m_scopeOrder = context.Get<Scope>(link.m_scope).m_order;
        }

        context.Sort<ScopeAttachment>(ScopeAttachmentLess);

        if constexpr (s_scopeOrderValidation)
        {
            // What later stages walk the two storages in lockstep on: attachments ascend, and
            // the Scopes they name appear in Scope storage order.
            auto scopes  = context.GetStorage<Scope>().each();
            auto scopeIt = scopes.begin();

            const ScopeAttachment* previous = nullptr;
            for (auto [attachment, link] : context.GetStorage<ScopeAttachment>().each())
            {
                ASSERT(link.m_resource != NullHandle,
                    "[RenderGraphCompiler] A Scope attachment is not linked to its resource.");
                ASSERT(previous == nullptr || !ScopeAttachmentLess(link, *previous),
                    "[RenderGraphCompiler] ScopeAttachment storage is out of order after sorting.");
                previous = &link;

                while (scopeIt != scopes.end())
                {
                    auto [scope, data] = *scopeIt;
                    if (scope == link.m_scope)
                    {
                        break;
                    }
                    ++scopeIt;
                }
                ASSERT(scopeIt != scopes.end(),
                    "[RenderGraphCompiler] A ScopeAttachment names a Scope that is out of order, or not in the stream.");
            }
        }

        context.Sort<ScopeItem>([](const ScopeItem& lhs, const ScopeItem& rhs)
        {
            return lhs.m_scopeOrder < rhs.m_scopeOrder;
        });

        // Each Scope's attachments and items now sit contiguously in their packed arrays:
        // record where, so they can be reached without walking every Scope before.
        RecordScopeRanges<ScopeAttachment, ScopeAttachmentRange>(context);
        RecordScopeRanges<ScopeItem, ScopeItemRange>(context);
    }

    void RenderGraphCompiler::CompileActiveQueues(RHIContext& context)
    {
        m_activeQueues = RHI::HardwareQueueClassMask::None;
        for (auto [scope, data] : context.GetStorage<Scope>().each())
        {
            m_activeQueues |= RHI::GetHardwareQueueClassMask(data.m_queue);
        }
    }

    namespace
    {
        //! One resource's attachments within one Scope, merged into a single access.
        struct ScopeResourceAccess
        {
            RHIHandle            m_scope      = NullHandle;
            RHIHandle            m_resource   = NullHandle;
            RHIHandle            m_attachment = NullHandle;   //!< the first of them; carries the barrier
            bool                 m_isImage    = false;
            RHI::AccessFlags     m_access     = RHI::AccessFlags::None;
            RHI::AttachmentStage m_stage      = RHI::AttachmentStage::Any;
        };

        //! The fence an imported resource's first access must wait for: another system left it
        //! pending on another queue (e.g. an upload) and the fence has not been reached yet.
        const RHI::PendingSync* FindExternalWait(
            RHIHandle               resource,
            RHI::HardwareQueueClass srcQueue,
            RHI::HardwareQueueClass dstQueue,
            const RHIContext&       context)
        {
            if (srcQueue == dstQueue || !context.Has<ImportedTag>(resource))
            {
                return nullptr;
            }
            const auto* sync = context.TryGet<RHI::PendingSync>(resource);
            if (!sync)
            {
                return nullptr;
            }
            // Fence values are monotonic, so reached at compile stays reached through execute.
            if (sync->m_fence != nullptr && sync->m_fence->GetCompletedValue() >= sync->m_fenceValue)
            {
                return nullptr;
            }
            return sync;
        }

        //! The consumer Scope waits for the producer Scope on srcQueue. Only the latest producer
        //! per source queue is kept: waiting for it covers every earlier signal on that queue.
        //! Values are assigned later, in stream order (CompileScopeSync).
        void RecordCrossQueueWait(
            RHIHandle               consumer,
            RHIHandle               producer,
            RHI::HardwareQueueClass srcQueue,
            const PassContext&      passContext,
            RHIContext&             context)
        {
            if (!context.Has<ScopeSignal>(producer))
            {
                context.Add<ScopeSignal>(producer);
            }

            auto* wait = context.TryGet<ScopeWait>(consumer);
            if (!wait)
            {
                wait = &context.Add<ScopeWait>(consumer);
            }

            RHIHandle& latest = wait->m_producer[static_cast<uint32_t>(srcQueue)];
            if (latest == NullHandle
                || MakeScopeOrderKey(context.Get<Scope>(latest), passContext)
                     < MakeScopeOrderKey(context.Get<Scope>(producer), passContext))
            {
                latest = producer;
            }
        }

        void CompileScopeResourceBarrier(
            const ScopeResourceAccess&        access,
            PassContext&                      passContext,
            RHIContext&                       context,
            const RHI::TransientResourcePool& pool)
        {
            const RHI::HardwareQueueClass dstQueue = context.Get<Scope>(access.m_scope).m_queue;

            const auto* backingImage  = access.m_isImage ? context.TryGet<BackingImage>(access.m_resource) : nullptr;
            const auto* backingBuffer = access.m_isImage ? nullptr : context.TryGet<BackingBuffer>(access.m_resource);
            ASSERT(access.m_isImage ? backingImage != nullptr : backingBuffer != nullptr,
                "Resource {} has no backing.",
                context.Has<ResourceName>(access.m_resource)
                    ? context.Get<ResourceName>(access.m_resource).m_name.GetCStr()
                    : "[Unnamed]");

            if (context.Has<ImportedTag>(access.m_resource))
            {
                const RHI::HardwareQueueClassMask mask = access.m_isImage
                    ? backingImage->m_image->GetDescriptor().m_sharedQueueMask
                    : backingBuffer->m_buffer->GetDescriptor().m_sharedQueueMask;
                ValidateExclusiveHomeQueue(mask, dstQueue,
                    context.Has<ResourceName>(access.m_resource)
                        ? context.Get<ResourceName>(access.m_resource).m_name.GetCStr()
                        : "[Unnamed]");
            }

            auto* tracker = context.TryGet<ResourceStateTracker>(access.m_resource);
            if (!tracker)
            {
                ResourceStateTracker init;
                init.m_current = GetResourceInitialState(access.m_resource, context);
                if (const RHI::PendingSync* sync = FindExternalWait(
                        access.m_resource, init.m_current.m_queue, dstQueue, context))
                {
                    context.Add<ExternalWait>(access.m_attachment, ExternalWait{ *sync });
                }
                // A transient resource's first touch is where the pool placed it.
                RHI::DeviceMemoryBarrier aliasing;
                if (context.Has<TransientTag>(access.m_resource)
                    && pool.GetAliasingBarrier(access.m_isImage
                            ? static_cast<const RHI::Resource&>(*backingImage->m_image)
                            : static_cast<const RHI::Resource&>(*backingBuffer->m_buffer),
                        aliasing))
                {
                    context.Add<PreAliasingBarrier>(access.m_attachment, PreAliasingBarrier{ aliasing });
                }
                tracker = &context.Add<ResourceStateTracker>(access.m_resource, init);
            }

            const RHI::ResourceState src = tracker->m_current;
            const RHI::ResourceState dst { access.m_access, dstQueue, access.m_stage };

            // Same state with a write on either side is still an execution / memory dependency
            // (a UAV barrier on DX12); whether it costs anything is the backend's call.
            if (src != dst || RHI::HasWrite(src.m_access) || RHI::HasWrite(dst.m_access))
            {
                const bool release = src.m_queue != dstQueue && tracker->m_lastAttachment != NullHandle;
                if (access.m_isImage)
                {
                    RHI::ImageBarrier b;
                    b.m_image     = backingImage->m_image;
                    b.m_srcAccess = src.m_access;
                    b.m_dstAccess = dst.m_access;
                    b.m_srcStage  = src.m_stage;
                    b.m_dstStage  = dst.m_stage;
                    b.m_srcQueue  = src.m_queue;
                    b.m_dstQueue  = dstQueue;
                    context.Add<PreImageBarrier>(access.m_attachment, PreImageBarrier{ b });
                    if (release)
                    {
                        context.Add<PostImageBarrier>(tracker->m_lastAttachment, PostImageBarrier{ b });
                    }
                }
                else
                {
                    RHI::BufferBarrier b;
                    b.m_buffer    = backingBuffer->m_buffer;
                    b.m_srcAccess = src.m_access;
                    b.m_dstAccess = dst.m_access;
                    b.m_srcStage  = src.m_stage;
                    b.m_dstStage  = dst.m_stage;
                    b.m_srcQueue  = src.m_queue;
                    b.m_dstQueue  = dstQueue;
                    context.Add<PreBufferBarrier>(access.m_attachment, PreBufferBarrier{ b });
                    if (release)
                    {
                        context.Add<PostBufferBarrier>(tracker->m_lastAttachment, PostBufferBarrier{ b });
                    }
                }

                if (release)
                {
                    RecordCrossQueueWait(access.m_scope,
                        context.Get<ScopeAttachment>(tracker->m_lastAttachment).m_scope,
                        src.m_queue, passContext, context);
                }
            }

            tracker->m_current        = dst;
            tracker->m_lastAttachment = access.m_attachment;
        }
    }

    void RenderGraphCompiler::CompileScopeBarriers(
        PassContext& passContext, RHIContext& context, const RHI::TransientResourcePool& pool)
    {
        // SortScopes made one resource's attachments within a Scope adjacent: merge them into
        // one access, and compile it when the next attachment starts another group.
        ScopeResourceAccess group;
        for (auto [attachment, link] : context.GetStorage<ScopeAttachment>().each())
        {
            ScopeResourceAccess current;
            current.m_scope      = link.m_scope;
            current.m_attachment = attachment;
            if (const auto* image = context.TryGet<ImagePassAttachment>(attachment))
            {
                current.m_resource = image->m_image;
                current.m_isImage  = true;
                current.m_access   = CompileResourceState(*image).m_access;
                current.m_stage    = image->m_stage;
            }
            else
            {
                const auto& buffer = context.Get<BufferPassAttachment>(attachment);
                current.m_resource = buffer.m_buffer;
                current.m_access   = CompileResourceState(buffer).m_access;
                current.m_stage    = buffer.m_stage;
            }

            if (group.m_attachment != NullHandle
                && group.m_scope == current.m_scope
                && group.m_resource == current.m_resource)
            {
                group.m_access |= current.m_access;
                group.m_stage  |= current.m_stage;
                ASSERT(!RHI::HasWrite(group.m_access)
                    || group.m_access == (RHI::AccessFlags::ShaderStorageRead | RHI::AccessFlags::ShaderStorageWrite),
                    "Resource combined with conflicting read+write access in a single Scope.");
                continue;
            }

            if (group.m_attachment != NullHandle)
            {
                CompileScopeResourceBarrier(group, passContext, context, pool);
            }
            group = current;
        }

        if (group.m_attachment != NullHandle)
        {
            CompileScopeResourceBarrier(group, passContext, context, pool);
        }
    }

    namespace
    {
        //! The attachment's view from its resource's view cache: per-frame resources (swap
        //! chain, ImagePerFrame) resolve this frame's slot.
        RHI::ImageView* ResolveAttachmentView(
            RHIContext& context, const ImagePassAttachment& att, uint32_t frameIndex)
        {
            auto* backImage = context.TryGet<BackingImage>(att.m_image);
            if (!backImage || !backImage->m_image)
            {
                return nullptr;
            }
            if (context.Has<RHI::PerFrameTag>(att.m_image))
            {
                return RHI::GetOrCreateImageViewPerFrame(
                    context, att.m_image, *backImage->m_image, att.m_viewDescriptor, frameIndex);
            }
            return RHI::GetOrCreateImageView(context, att.m_image, *backImage->m_image, att.m_viewDescriptor);
        }

        //! The heap index a shader reaches the attachment's view by (.BindIndex): its UAV's if
        //! the access writes, else its SRV's.
        uint32_t ResolveBindlessIndex(RHIContext& context, const ImagePassAttachment& att, uint32_t frameIndex)
        {
            const RHI::ImageView* view = ResolveAttachmentView(context, att, frameIndex);
            ASSERT(view != nullptr, "[RenderGraphCompiler] {} has no view to take an index of.", att.m_attachmentId.m_id.GetCStr());
            const bool     writes = (att.m_access & RHI::AttachmentAccess::Write) != RHI::AttachmentAccess::Unknown;
            const uint32_t index  = writes ? view->GetBindlessReadWriteIndex() : view->GetBindlessReadIndex();
            ASSERT(index != RHI::ImageView::InvalidBindlessIndex,
                "[RenderGraphCompiler] {} has no bindless descriptor: the device lacks bindless, or the view its bind flag.",
                att.m_attachmentId.m_id.GetCStr());
            return index;
        }

        uint32_t AttachmentLayerCount(const RHIContext& context, const ImagePassAttachment& att)
        {
            const auto* backImage = context.TryGet<BackingImage>(att.m_image);
            if (!backImage || !backImage->m_image)
            {
                return 1;
            }
            return RHI::GetArraySliceCount(backImage->m_image->GetDescriptor(), att.m_viewDescriptor);
        }

        RHI::RenderPassBeginInfo BuildScopeBeginInfo(
            eastl::span<const RHIHandle> attachments, Pass pass,
            const PassContext& passContext, RHIContext& context, uint32_t frameIndex)
        {
            const char* passName = passContext.Get<PassName>(pass).m_name.GetCStr();

            RHI::RenderPassBeginInfo info;
            uint32_t colorCount    = 0;
            uint32_t maxLayerCount = 1;
            bool     hasAny        = false;

            for (RHIHandle attachment : attachments)
            {
                const auto* att = context.TryGet<ImagePassAttachment>(attachment);
                if (!att)
                {
                    continue;
                }

                if (att->m_usage == RHI::AttachmentUsage::RenderTarget)
                {
                    const uint32_t index = context.Get<ColorAttachmentIndex>(attachment).m_index;
                    ASSERT(index < RHI::Limits::Pipeline::AttachmentColorCountMax,
                        "[RenderGraphCompiler] Too many color attachments on pass {}.", passName);

                    RHI::ImageView* view = ResolveAttachmentView(context, *att, frameIndex);
                    ASSERT(view != nullptr,
                        "[RenderGraphCompiler] Attachment {}'s view could not be resolved from its resource view cache.",
                        att->m_attachmentId.m_id.GetCStr());

                    auto& color = info.m_colorAttachments[index];
                    color.m_view            = view;
                    color.m_loadStoreAction = att->m_action;
                    colorCount              = eastl::max(colorCount, index + 1);
                    maxLayerCount           = eastl::max(maxLayerCount, AttachmentLayerCount(context, *att));
                    hasAny                  = true;
                }
                else if (att->m_usage == RHI::AttachmentUsage::DepthStencil)
                {
                    ASSERT(info.m_depthStencilAttachment.m_view == nullptr,
                        "[RenderGraphCompiler] Pass {} has more than one depth-stencil attachment.", passName);

                    RHI::ImageView* view = ResolveAttachmentView(context, *att, frameIndex);
                    ASSERT(view != nullptr,
                        "[RenderGraphCompiler] Attachment {}'s view could not be resolved from its resource view cache.",
                        att->m_attachmentId.m_id.GetCStr());

                    info.m_depthStencilAttachment.m_view            = view;
                    info.m_depthStencilAttachment.m_access          = att->m_access;
                    info.m_depthStencilAttachment.m_loadStoreAction = att->m_action;
                    maxLayerCount = eastl::max(maxLayerCount, AttachmentLayerCount(context, *att));
                    hasAny        = true;
                }
            }

            info.m_colorAttachmentCount = colorCount;
            for (uint32_t i = 0; i < colorCount; ++i)
            {
                ASSERT(info.m_colorAttachments[i].m_view != nullptr,
                    "[RenderGraphCompiler] Pass {} has no color attachment at index {}.", passName, i);
            }

            if (const auto* pipelineState = passContext.TryGet<PassPipelineState>(pass))
            {
                ASSERT(colorCount == pipelineState->m_renderTargetLayout.m_colorAttachmentCount,
                    "[RenderGraphCompiler] Pass {} declares {} color attachments, its RenderTargetLayout {}.",
                    passName, colorCount, pipelineState->m_renderTargetLayout.m_colorAttachmentCount);
            }

            // Vulkan renders exactly this many layers and ignores the views' own extents, so
            // leaving it at 1 would silently drop every slice but the first; DX12 infers it from
            // the RTV instead. Taking the max lets a mismatched set hit validation rather than
            // quietly under-render.
            info.m_layerCount = maxLayerCount;

            for (RHIHandle attachment : attachments)
            {
                const auto* att = context.TryGet<ImagePassAttachment>(attachment);
                if (!att || att->m_usage != RHI::AttachmentUsage::Resolve)
                {
                    continue;
                }

                RHI::ImageView* resolved = ResolveAttachmentView(context, *att, frameIndex);
                ASSERT(resolved != nullptr,
                    "[RenderGraphCompiler] Resolve attachment {}'s view could not be resolved from its resource view cache.",
                    att->m_attachmentId.m_id.GetCStr());

                const RHIHandle source = context.Get<ResolveSource>(attachment).m_source;
                info.m_colorAttachments[context.Get<ColorAttachmentIndex>(source).m_index].m_resolveView = resolved;
            }

            ASSERT(hasAny,
                "[RenderGraphCompiler] Render pass {} has no color or depth-stencil attachment.", passName);
            return info;
        }
    }

    void RenderGraphCompiler::CompileScopeBeginInfo(PassContext& passContext, RHIContext& context)
    {
        for (auto [scope, data] : context.GetStorage<Scope>().each())
        {
            const eastl::span<const RHIHandle> attachments = GetScopeAttachments(context, scope);
            if (attachments.empty() || !passContext.Has<RenderPassTag>(data.m_pass))
            {
                continue;
            }
            context.Add<RHI::RenderPassBeginInfo>(scope,
                BuildScopeBeginInfo(attachments, data.m_pass, passContext, context, m_frameIndex));
        }
    }

    void RenderGraphCompiler::CompileScopeSync(
        PassContext& passContext, RHIContext& context, RHI::FenceSet& crossQueueFences)
    {
        // [waiting queue][source queue]: the highest value already waited for. A later wait on
        // a value no higher is redundant — the timeline has passed it.
        eastl::array<eastl::array<uint64_t, RHI::HardwareQueueClassCount>, RHI::HardwareQueueClassCount> waited {};

        // In stream order, so a producer's value is assigned before any consumer reads it and
        // each queue's values rise in the order its Scopes are submitted.
        for (auto [scope, data] : context.GetStorage<Scope>().each())
        {
            const auto queueIndex = static_cast<uint32_t>(data.m_queue);

            if (auto* wait = context.TryGet<ScopeWait>(scope))
            {
                for (uint32_t source = 0; source < RHI::HardwareQueueClassCount; ++source)
                {
                    wait->m_sync[source] = {};
                    if (wait->m_producer[source] == NullHandle)
                    {
                        continue;
                    }

                    const ScopeSignal& signal = context.Get<ScopeSignal>(wait->m_producer[source]);
                    ASSERT(signal.m_value != 0,
                        "[RenderGraphCompiler] A Scope waits for a producer that comes after it in the stream.");
                    if (signal.m_value <= waited[queueIndex][source])
                    {
                        continue;
                    }
                    waited[queueIndex][source] = signal.m_value;
                    wait->m_sync[source]       = RHI::PendingSync{ signal.m_fence, signal.m_value };
                }
            }

            if (auto* signal = context.TryGet<ScopeSignal>(scope))
            {
                signal->m_fence = &crossQueueFences.GetFence(static_cast<RHI::HardwareQueueClass>(queueIndex));
                signal->m_value = ++m_crossQueueFenceValues[queueIndex];
            }
        }
    }

    void RenderGraphCompiler::CompileScopeBindings(PassContext& passContext, RHIContext& context)
    {
        Pass      pass     = NullPass;
        RHIHandle bindings = NullHandle;
        const RHI::ConstantsLayout* rootConstants = nullptr;   // the pass's, if its shaders declare any
        bool      declared = false;                    // the pass set anything this frame
        eastl::fixed_vector<RHI::InputName, 16> bound; // the inputs its attachments were bound to

        // A pass that set anything leaves no view it did not bind this frame in its per-pass
        // space: an image or buffer input nothing bound gets null. Passes that set nothing are
        // left alone — they may still set their inputs themselves.
        auto finishPass = [&]()
        {
            if (!declared)
            {
                return;
            }
            auto isBound = [&bound](const RHI::InputName& input)
            {
                return eastl::find(bound.begin(), bound.end(), input) != bound.end();
            };
            const RHI::ShaderBindings& sb = *context.Get<RHI::Components::ShaderBindings>(bindings).m_bindings;
            for (const RHI::ShaderInputImage& image : sb.GetImageInputs())
            {
                if (!isBound(image.GetDescription().m_name))
                {
                    SetShaderImage(bindings, image.GetDescription().m_name, nullptr);
                }
            }
            for (const RHI::ShaderInputBuffer& buffer : sb.GetBufferInputs())
            {
                if (!isBound(buffer.GetDescription().m_name) && buffer.GetView(0))
                {
                    SetShaderBuffer(bindings, buffer.GetDescription().m_name, nullptr);
                }
            }
        };

        // One pass's Scopes are adjacent in stream order. They share the pass's per-pass space,
        // so an input two of them set holds the later value for both: keeping such inputs
        // identical across Scopes is the pass's job.
        for (auto [scope, data] : context.GetStorage<Scope>().each())
        {
            if (data.m_pass != pass)
            {
                finishPass();
                pass     = data.m_pass;
                declared = false;
                bound.clear();
                const auto* own = passContext.TryGet<PassBindings>(pass);
                bindings = own != nullptr ? own->m_bindings : NullHandle;
                const auto* layout = passContext.TryGet<PassPipelineLayout>(pass);
                rootConstants = (layout != nullptr && layout->m_layout) ? layout->m_layout->GetRootConstantsLayout() : nullptr;
            }

            for (RHIHandle attachment : GetScopeAttachments(context, scope))
            {
                if (const auto* binding = context.TryGet<ShaderInputBinding>(attachment))
                {
                    SetShaderImage(bindings, binding->m_input,
                        ResolveAttachmentView(context, context.Get<ImagePassAttachment>(attachment), m_frameIndex));
                    bound.push_back(binding->m_input);
                    declared = true;
                }
                else if (const auto* indexBinding = context.TryGet<IndexBinding>(attachment))
                {
                    const uint32_t index = ResolveBindlessIndex(context, context.Get<ImagePassAttachment>(attachment), m_frameIndex);
                    const RHI::ShaderInputIndex root = rootConstants != nullptr
                        ? rootConstants->FindShaderInputIndex(indexBinding->m_input) : RHI::InvalidShaderInputIndex;
                    if (root != RHI::InvalidShaderInputIndex)
                    {
                        auto& block = context.Get<ScopeRootConstants>(scope);
                        memcpy(block.m_bytes.data() + rootConstants->GetInterval(root).m_min, &index, sizeof(index));
                    }
                    else
                    {
                        SetShaderConstantData(bindings, indexBinding->m_input, &index, sizeof(index));
                        declared = true;
                    }
                }
            }

            if (const auto* samplers = context.TryGet<ScopeSamplers>(scope))
            {
                for (const ScopeSampler& sampler : samplers->m_samplers)
                {
                    SetShaderSampler(bindings, sampler.m_input, sampler.m_state);
                }
                declared = true;
            }

            if (const auto* constants = context.TryGet<ScopeConstants>(scope))
            {
                for (const ScopeConstant& constant : constants->m_constants)
                {
                    SetShaderConstantData(bindings, constant.m_input, constant.m_bytes.data(), constant.m_byteCount);
                }
                declared = true;
            }
        }
        finishPass();
    }

    void RenderGraphCompiler::CompileScopeState(PassContext& passContext, RHIContext& context)
    {
        for (auto [scope, data] : context.GetStorage<Scope>().each())
        {
            ScopeState state;
            if (const auto* compiled = passContext.TryGet<PassCompiledPSO>(data.m_pass))
            {
                state.m_pso = compiled->m_pso.get();
            }

            // Bindings resolve their space against the PSO's layout: without one there is
            // nothing to bind them against. The pass's own group first, then the shared ones.
            if (state.m_pso)
            {
                ShaderBindingsList bindings;
                if (const auto* own = passContext.TryGet<PassBindings>(data.m_pass))
                {
                    const auto& comp = context.Get<RHI::Components::ShaderBindings>(own->m_bindings);
                    if (comp.m_bindings)
                    {
                        bindings.push_back(comp.m_bindings.get());
                    }
                }
                const auto* caps = passContext.TryGet<PassCapabilities>(data.m_pass);
                if (caps && caps->m_resolveSharedBindings)
                {
                    caps->m_resolveSharedBindings(context, bindings);
                }
                for (const RHI::ShaderBindings* b : bindings)
                {
                    state.m_bindings[state.m_bindingCount++] = b;
                }
            }

            context.Add<ScopeState>(scope, state);

            const auto& execute = passContext.Get<PassFunctions>(data.m_pass).m_executeFunction;
            if (execute)
            {
                context.Add<ScopeExecute>(scope, ScopeExecute{ &execute });
            }
        }
    }

    namespace
    {
        //! Under `view` (NullHandle for a viewless pass): the Scope's own items, then those it
        //! selects.
        void AppendItems(RHIHandle scope, RHIHandle view, RHIContext& context, eastl::vector<RHIHandle>& submitList)
        {
            const eastl::span<const RHIHandle> items = GetScopeItems(context, scope);
            submitList.insert(submitList.end(), items.begin(), items.end());
            if (const auto* selections = context.TryGet<ScopeSelections>(scope))
            {
                for (ScopeSelections::Collect collect : selections->m_collects)
                {
                    collect(context, view, submitList);
                }
            }
        }

        void AppendScopeSubmissions(
            RHIHandle scope, Pass pass, const RHI::RenderPassBeginInfo* beginInfo,
            PassContext& passContext, RHIContext& context, eastl::vector<RHIHandle>& submitList)
        {
            // A compute Scope's items run under no view.
            const bool isRenderPass = passContext.Has<RenderPassTag>(pass);
            if (!isRenderPass)
            {
                AppendItems(scope, NullHandle, context, submitList);
                return;
            }

            const auto* capabilities = passContext.TryGet<PassCapabilities>(pass);
            if (!capabilities)
            {
                return;
            }
            ASSERT(capabilities->m_collectViews,
                "[RenderGraphCompiler] Render pass {} declares no .RendersView<>().",
                passContext.Get<PassName>(pass).m_name.GetCStr());
            if (!capabilities->m_collectViews)
            {
                return;
            }

            // A render pass scales each view's rect against its target, so without a target
            // extent it has nothing to draw into.
            RHI::Viewport targetViewport;
            RHI::Scissor  targetScissor;
            if (!beginInfo || !ResolveTargetViewport(*beginInfo, targetViewport, targetScissor))
            {
                return;
            }

            ViewHandleList views;
            capabilities->m_collectViews(context, views);
            for (RHIHandle view : views)
            {
                // Skipping costs this view one frame; drawing it would be silently wrong.
                const RHI::ShaderBindings* viewBindings = nullptr;
                if (!ResolveViewShaderBindings(context, view, viewBindings))
                {
                    continue;
                }

                const View& viewData = context.Get<View>(view);
                ASSERT(viewData.m_bufferSize == Math::Vector2Int(0, 0)
                    || (viewData.m_bufferSize.x == static_cast<int>(targetViewport.m_maxX)
                        && viewData.m_bufferSize.y == static_cast<int>(targetViewport.m_maxY)),
                    "[RenderGraphCompiler] Pass {} targets {}x{}, but its view's rect is a fraction of {}x{}.",
                    passContext.Get<PassName>(pass).m_name.GetCStr(),
                    static_cast<int>(targetViewport.m_maxX), static_cast<int>(targetViewport.m_maxY),
                    viewData.m_bufferSize.x, viewData.m_bufferSize.y);

                submitList.push_back(view);
                AppendItems(scope, view, context, submitList);
            }
        }
    }

    void RenderGraphCompiler::CompileScopeSubmitRanges(
        PassContext& passContext, RHIContext& context, eastl::vector<RHIHandle>& submitList)
    {
        for (auto [scope, data] : context.GetStorage<Scope>().each())
        {
            ScopeSubmitRange range;
            range.m_begin = static_cast<uint32_t>(submitList.size());
            AppendScopeSubmissions(scope, data.m_pass, context.TryGet<RHI::RenderPassBeginInfo>(scope),
                passContext, context, submitList);
            range.m_end = static_cast<uint32_t>(submitList.size());

            context.Add<ScopeSubmitRange>(scope, range);
        }
    }

    void RenderGraphCompiler::CompileTransientResources(RHI::TransientResourcePool& pool)
    {
        auto& rhiContext  = *RHIExecuteContext::Current();
        auto& passContext = *PassExecuteContext::Current();

        // 1. Build name → resource-entity lookup for transient images / buffers.
        //    These hold ONLY transient resources, so an attachment whose id misses
        //    the lookup targets an imported/static resource (its m_image/m_buffer was
        //    already resolved at Build time) and is skipped here.
        eastl::unordered_map<RHI::AttachmentId, RHIHandle> transientImageByName;
        eastl::unordered_map<RHI::AttachmentId, RHIHandle> transientBufferByName;

        rhiContext.GetView<TransientTag, ResourceName, RHI::ImageDescriptor>().each(
            [&](RHIHandle resource, const ResourceName& rn, const RHI::ImageDescriptor&)
            {
                transientImageByName.emplace(rn.m_name, resource);
            });

        rhiContext.GetView<TransientTag, ResourceName, RHI::BufferDescriptor>().each(
            [&](RHIHandle resource, const ResourceName& rn, const RHI::BufferDescriptor&)
            {
                transientBufferByName.emplace(rn.m_name, resource);
            });

        // 2. Walk attachments, accumulate per-resource lifetime + queue mask + clear value
        //    directly on the resource entity as ECS components.
        rhiContext.GetView<ImagePassAttachment>(Exclude<StaticImportTag, PreviousFrameTag>).each(
            [&](RHIHandle attachmentHandle, ImagePassAttachment& a)
            {
                auto it = transientImageByName.find(a.m_attachmentId.m_id);
                if (it == transientImageByName.end())
                {
                    return;
                }

                const RHIHandle resource = it->second;
                a.m_image = resource;   // primary link for Read/Write/Create attachments

                // Backed by a pooled image (CompileExtractedImages): no transient allocation,
                // no aliasing.
                if (rhiContext.Has<ExtractedImage>(resource))
                {
                    return;
                }

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

        rhiContext.GetView<BufferPassAttachment>(Exclude<StaticImportTag>).each(
            [&](RHIHandle attachmentHandle, BufferPassAttachment& a)
            {
                auto it = transientBufferByName.find(a.m_attachmentId.m_id);
                if (it == transientBufferByName.end())
                {
                    return;
                }

                const RHIHandle resource = it->second;
                a.m_buffer = resource;   // primary link for Read/Write/Create attachments

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

        // 4. Sweep: call pool, store backing on resource entity. Views are no longer
        //    materialized here — they come from the resource's view cache on demand.
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
                        image->SetName(name);
                        ASSERT(image != nullptr,
                            "TransientResourcePool::CreateImage returned null for '{}'.",
                            name.GetCStr());

                        rhiContext.Add<BackingImage>(ev.m_resource, BackingImage{ image });
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
                        buffer->SetName(name);
                        ASSERT(buffer != nullptr,
                            "TransientResourcePool::CreateBuffer returned null for '{}'.",
                            name.GetCStr());

                        rhiContext.Add<BackingBuffer>(ev.m_resource, BackingBuffer{ buffer });
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

    void RenderGraphCompiler::CompilePipelineStates(
        PassContext&          passContext,
        RHI::Device&          device,
        RHI::PipelineLibrary* pipelineLibrary)
    {
        auto* factory = Service<RHI::Factory>::Get();
        ASSERT(factory, "RHI::Factory service not registered.");

        // --- Render passes ---
        {
            // A pass without shaders has no PassPipelineState, so no PSO.
            auto view = passContext.GetView<RenderPassTag, PassShaders, PassPipelineState>();

            view.each([&](Pass pass, const PassShaders& shaders, const PassPipelineState& pipelineState)
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

                ASSERT(passContext.Has<PassPipelineLayout>(pass),
                    "Pass '{}' has no PassPipelineLayout (eager build in PassBuilder::Finalize failed?).",
                    passContext.Get<PassName>(pass).m_name.GetCStr());
                descriptor.m_pipelineLayoutDescriptor =
                    passContext.Get<PassPipelineLayout>(pass).m_layout;

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
            auto view = passContext.GetView<ComputePassTag, PassShaders>();

            view.each([&](Pass pass, const PassShaders& shaders)
            {
                // A pass without shaders has no PSO.
                if (!shaders.m_computeShader)
                {
                    return;
                }
                if (passContext.Has<PassCompiledPSO>(pass) && !passContext.Has<PassPSODirtyTag>(pass))
                    return;

                RHI::PipelineStateDescriptorForDispatch descriptor;

                auto* stage = shaders.m_computeShader->GetShaderData()->GetStageBytecode(RHI::ShaderStage::Compute);
                ASSERT(stage, "Pass '{}': ComputeShader has no compute-stage bytecode.",
                    passContext.Get<PassName>(pass).m_name.GetCStr());
                auto func = factory->CreateShaderStageFunction(RHI::ShaderStage::Compute);
                func->SetByteCode(stage->bytecode);
                func->Finalize();
                descriptor.m_computeFunction = eastl::move(func);

                ASSERT(passContext.Has<PassPipelineLayout>(pass),
                    "Compute pass '{}' has no PassPipelineLayout (eager build in PassBuilder::Finalize failed?).",
                    passContext.Get<PassName>(pass).m_name.GetCStr());
                descriptor.m_pipelineLayoutDescriptor =
                    passContext.Get<PassPipelineLayout>(pass).m_layout;

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

    void RenderGraphCompiler::CompileShaderInputs(RHI::Device& device, RHIContext& context)
    {
        auto& view = context.GetView<ShaderBindingsUpdateTag, RHI::Components::ShaderBindings>();
        auto* factory = Service<RHI::Factory>::Get();
        ASSERT(factory, "[RenderGraph] RHI::Factory service not registered.");

        auto& shaderInputCompiler = factory->AcquireShaderInputCompiler(device);
        view.each([&](RHIHandle handle, RHI::Components::ShaderBindings& comp)
        {
            ASSERT(comp.m_bindings,
                "[CompileShaderInputs] Components::ShaderBindings holds null Ptr.");
            const RHI::ResultCode rc = shaderInputCompiler.Compile(*comp.m_bindings);
            ASSERT(rc == RHI::ResultCode::Success,
                "[CompileShaderInputs] ShaderInputCompiler::Compile failed.");
            context.Remove<ShaderBindingsUpdateTag>(handle);
        });
    }
}
