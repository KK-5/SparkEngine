#include "RenderGraphExecuter.h"
#include "RenderGraphUtils.h"

#include <EASTL/algorithm.h>

#include <RHI/Command/CommandList.h>
#include <RHI/Command/CommandQueueContext.h>
#include <RHI/Command/CopyItem.h>
#include <RHI/Command/DispatchItem.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/RenderPassBeginInfo.h>
#include <RHI/Factory.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Pass/Component/PassComponents.h>
#include <Pass/Component/ScopeComponents.h>
#include <View/View.h>
#include <View/ViewComponents.h>

namespace Spark::Render
{
    namespace
    {
        RHI::Scissor ScissorFromViewport(const RHI::Viewport& viewport)
        {
            return RHI::Scissor(
                static_cast<int32_t>(viewport.m_minX), static_cast<int32_t>(viewport.m_minY),
                static_cast<int32_t>(viewport.m_maxX), static_cast<int32_t>(viewport.m_maxY));
        }

        void BindShaderInputs(
            RHI::CommandList* commandList, const RHI::PipelineState& pso, const RHI::ShaderBindings& bindings)
        {
            if (pso.GetType() == RHI::PipelineStateType::Dispatch)
            {
                commandList->BindShaderInputsForDispatch(bindings);
            }
            else
            {
                commandList->BindShaderInputsForDraw(bindings);
            }
        }

        //! What an item is, is decided by its components.
        void SubmitItem(RHI::CommandList* commandList, RHIContext& rhiContext, RHIHandle item, uint32_t submitIndex)
        {
            if (const auto* draw = rhiContext.TryGet<RHI::DrawItem>(item))
            {
                commandList->Submit(*draw, submitIndex);
                return;
            }
            if (const auto* dispatch = rhiContext.TryGet<RHI::DispatchItem>(item))
            {
                commandList->Submit(*dispatch, submitIndex);
                return;
            }
            if (const auto* copy = rhiContext.TryGet<RHI::CopyItem>(item))
            {
                commandList->Submit(*copy, submitIndex);
                return;
            }
            ASSERT(false, "[RenderGraphExecuter] Submit list entry {} is neither a view nor an item.", submitIndex);
        }

        //! Every fence the Scope waits for before it runs: its producers' on other queues, then
        //! the external ones on the attachments that first touch a resource.
        template<typename Visit>
        void ForEachScopeWait(
            RHIContext& rhiContext, RHIHandle scope, eastl::span<const RHIHandle> attachments, Visit&& visit)
        {
            if (const auto* wait = rhiContext.TryGet<ScopeWait>(scope))
            {
                for (const RHI::PendingSync& sync : wait->m_sync)
                {
                    if (sync.m_fence)
                    {
                        visit(sync);
                    }
                }
            }
            for (RHIHandle attachment : attachments)
            {
                if (const auto* external = rhiContext.TryGet<ExternalWait>(attachment))
                {
                    visit(external->m_sync);
                }
            }
        }

        void QueueScopePreBarriers(
            RHI::CommandList* commandList, RHIContext& rhiContext, eastl::span<const RHIHandle> attachments)
        {
            for (RHIHandle attachment : attachments)
            {
                // A resource's memory is handed over before its state changes.
                if (const auto* barrier = rhiContext.TryGet<PreAliasingBarrier>(attachment))
                {
                    commandList->QueueBarrier(barrier->m_barrier);
                }
                if (const auto* barrier = rhiContext.TryGet<PreImageBarrier>(attachment))
                {
                    commandList->QueueBarrier(barrier->m_barrier);
                }
                if (const auto* barrier = rhiContext.TryGet<PreBufferBarrier>(attachment))
                {
                    commandList->QueueBarrier(barrier->m_barrier);
                }
            }
        }

        void QueueScopePostBarriers(
            RHI::CommandList* commandList, RHIContext& rhiContext, eastl::span<const RHIHandle> attachments)
        {
            for (RHIHandle attachment : attachments)
            {
                if (const auto* barrier = rhiContext.TryGet<PostImageBarrier>(attachment))
                {
                    commandList->QueueBarrier(barrier->m_barrier);
                }
                if (const auto* barrier = rhiContext.TryGet<PostBufferBarrier>(attachment))
                {
                    commandList->QueueBarrier(barrier->m_barrier);
                }
            }
        }
    }

    void RenderGraphExecuter::Begin(uint32_t frameIndex)
    {
        m_frameIndex = frameIndex;
    }

    void RenderGraphExecuter::End()
    {
        // The submit list is frame-scoped; clear() keeps the capacity so a steady frame
        // allocates nothing.
        m_submitList.clear();

        // Per-resource compile-time state cursor. First touch in CompileScopeBarriers
        // expects a fresh slate each frame — imported resources start at their backing's
        // state, transients at Uninitialized. Without this clear, frame N+1 inherits frame
        // N's m_current and emits wrong barriers.
        RHIExecuteContext::Current()->Clear<ResourceStateTracker>();

        auto& passContext = *PassExecuteContext::Current();

        // Transient views are no longer separate entities: image views live in the
        // resource's ImageViewCache and buffer attachments hold no view. Both are
        // released when the transient RESOURCE entity is destroyed below (its cache
        // component drops the owning Ptr<ImageView>), so no per-view cleanup is needed.
        {
            auto& rhiContext = *RHIExecuteContext::Current();

            // Attachment entities are the pass→resource edges. They live through
            // Build/Compile/Execute (Execute resolves resources by slot via them)
            // and are destroyed here, after Execute — but still before next frame's
            // Build, so next frame's ValidateUniqueSlot sees no stale slot names.
            // StaticImport attachments are excluded: they persist by design.
            eastl::vector<RHIHandle> attachmentHandles;
            rhiContext.GetView<ImagePassAttachment>(Exclude<StaticImportTag>).each(
                [&](RHIHandle h, const ImagePassAttachment&) { attachmentHandles.push_back(h); });
            rhiContext.GetView<BufferPassAttachment>(Exclude<StaticImportTag>).each(
                [&](RHIHandle h, const BufferPassAttachment&) { attachmentHandles.push_back(h); });
            for (RHIHandle h : attachmentHandles)
            {
                rhiContext.DestoryEntity(h);
            }

            // Rebuilt every frame by the builder, as are the items they declared; their
            // attachments (destroyed above) carried ScopeAttachment and are gone with them.
            eastl::vector<RHIHandle> scopeHandles;
            rhiContext.GetView<Scope>().each(
                [&](RHIHandle h, const Scope&) { scopeHandles.push_back(h); });
            rhiContext.GetView<ScopeItem>().each(
                [&](RHIHandle h, const ScopeItem&) { scopeHandles.push_back(h); });
            for (RHIHandle h : scopeHandles)
            {
                rhiContext.DestoryEntity(h);
            }

            // Transient resource entities are rebuilt every frame by the builder
            // (CreateTransientImageResource / CreateTransientBufferResource), so they
            // must be destroyed here too — otherwise they accumulate and next frame's
            // name→resource lookup in CompileTransientResources can bind to a stale
            // entity. The backing GPU memory is owned/recycled by the TransientResourcePool;
            // destroying the entity only drops the borrowed BackingImage/BackingBuffer
            // pointer. TransientTag is on resource entities only, so this view finds
            // exactly them (their views/attachments were already destroyed above).
            eastl::vector<RHIHandle> transientResourceHandles;
            rhiContext.GetView<TransientTag>().each(
                [&](RHIHandle h) { transientResourceHandles.push_back(h); });
            for (RHIHandle h : transientResourceHandles)
            {
                rhiContext.DestoryEntity(h);
            }
        }

        // Frame-scoped components on regular Pass entities. Pass entities themselves
        // persist across frames (created once in BuildPipeline); these components
        // are populated fresh each compile and must be cleared so next frame's
        // Add doesn't trip the entt 'slot not available' assert.
        passContext.Clear<PassGlobalTimeline>();
    }

    void RenderGraphExecuter::ExecuteStaticPreBarriers(RHI::CommandList* commandList, uint32_t queueIndex)
    {
        const auto& barriers = m_staticPreBarriers[queueIndex];

        for (const auto& b : barriers.m_imageBarriers)
        {
            commandList->QueueBarrier(b);
        }

        for (const auto& b : barriers.m_bufferBarriers)
        {
            commandList->QueueBarrier(b);
        }

        commandList->FlushBarriers();
    }

    void RenderGraphExecuter::ExecuteScopes(
        RHIContext& rhiContext, RHI::HardwareQueueClassMask activeQueues,
        RHI::Factory& factory, RHI::Device& device, RHI::CommandQueueContext& queues)
    {
        eastl::array<RHI::CommandList*, RHI::HardwareQueueClassCount> recording {};

        auto open = [&](RHI::HardwareQueueClass queueClass)
        {
            RHI::CommandList*& commandList = recording[static_cast<uint32_t>(queueClass)];
            if (!commandList)
            {
                commandList = factory.CreateCommandList(device, queueClass);
                commandList->Open();
            }
            return commandList;
        };

        auto submit = [&](RHI::HardwareQueueClass queueClass)
        {
            RHI::CommandList*& commandList = recording[static_cast<uint32_t>(queueClass)];
            if (!commandList)
            {
                return;
            }
            commandList->FlushBarriers();
            commandList->Close();
            queues.GetCommandQueue(queueClass).ExecuteCommands({ &commandList, 1 });
            commandList = nullptr;
        };

        // Static imports settle before anything else on their queue.
        for (uint32_t queueIndex = 0; queueIndex < RHI::HardwareQueueClassCount; ++queueIndex)
        {
            const auto queueClass = static_cast<RHI::HardwareQueueClass>(queueIndex);
            if (!CheckBitsAny(activeQueues, RHI::GetHardwareQueueClassMask(queueClass)))
            {
                continue;
            }

            const StaticPreBarriers& staticPre = m_staticPreBarriers[queueIndex];
            for (const auto& sync : staticPre.m_fenceWaits)
            {
                queues.GetCommandQueue(queueClass).Wait(*sync.m_fence, sync.m_fenceValue);
            }
            if (!staticPre.m_imageBarriers.empty() || !staticPre.m_bufferBarriers.empty())
            {
                ExecuteStaticPreBarriers(open(queueClass), queueIndex);
            }
        }

        for (auto [scope, data] : rhiContext.GetStorage<Scope>().each())
        {
            const eastl::span<const RHIHandle> attachments = GetScopeAttachments(rhiContext, scope);

            const auto queueClass = data.m_queue;
            auto&      queue      = queues.GetCommandQueue(queueClass);

            if (rhiContext.Has<WorkStartTag>(scope))
            {
                submit(queueClass);
            }

            // A wait holds back only what is submitted after it, so what came before goes first.
            ForEachScopeWait(rhiContext, scope, attachments, [&](const RHI::PendingSync& sync)
            {
                submit(queueClass);
                queue.Wait(*sync.m_fence, sync.m_fenceValue);
            });

            RHI::CommandList* commandList = open(queueClass);

            QueueScopePreBarriers(commandList, rhiContext, attachments);
            commandList->FlushBarriers();

            RecordScope(commandList, rhiContext, scope);

            // Flushed together with whatever comes next on this CommandList.
            QueueScopePostBarriers(commandList, rhiContext, attachments);

            if (const auto* signal = rhiContext.TryGet<ScopeSignal>(scope))
            {
                submit(queueClass);
                queue.Signal(*signal->m_fence, signal->m_value);
            }
        }

        for (uint32_t i = 0; i < RHI::HardwareQueueClassCount; ++i)
        {
            submit(static_cast<RHI::HardwareQueueClass>(i));
        }
    }

    void RenderGraphExecuter::RecordScope(RHI::CommandList* commandList, RHIContext& rhiContext, RHIHandle scope)
    {
        const auto* beginInfo = rhiContext.TryGet<RHI::RenderPassBeginInfo>(scope);
        if (beginInfo)
        {
            commandList->BeginRenderPass(*beginInfo);
        }

        const auto& state = rhiContext.Get<ScopeState>(scope);
        if (state.m_pso)
        {
            commandList->SetPipelineState(*state.m_pso);
            for (uint8_t i = 0; i < state.m_bindingCount; ++i)
            {
                BindShaderInputs(commandList, *state.m_pso, *state.m_bindings[i]);
            }
        }

        SubmitScopeRange(commandList, rhiContext, scope, state, beginInfo);

        if (beginInfo)
        {
            commandList->EndRenderPass();
        }
    }

    void RenderGraphExecuter::SubmitScopeRange(
        RHI::CommandList* commandList, RHIContext& rhiContext, RHIHandle scope,
        const ScopeState& state, const RHI::RenderPassBeginInfo* beginInfo)
    {
        RHI::Viewport targetViewport;
        RHI::Scissor  targetScissor;
        const bool    hasTarget = beginInfo && ResolveTargetViewport(*beginInfo, targetViewport, targetScissor);

        const auto& range   = rhiContext.Get<ScopeSubmitRange>(scope);
        const auto* execute = rhiContext.TryGet<ScopeExecute>(scope);
        commandList->SetSubmitRange({ range.m_begin, range.m_end });

        ExecuteWork work;
        work.m_commandList = commandList;
        work.m_scopeIndex  = rhiContext.Get<Scope>(scope).m_index;
        auto submitSegment = [&](uint32_t begin, uint32_t end)
        {
            if (execute)
            {
                work.m_submitBase  = begin;
                work.m_itemHandles = eastl::span<const RHIHandle>(m_submitList.data() + begin, end - begin);
                (*execute->m_execute)(work, *this);
                return;
            }
            for (uint32_t i = begin; i < end; ++i)
            {
                SubmitItem(commandList, rhiContext, m_submitList[i], i);
            }
        };

        // A view handle closes the segment before it and sets up the one after it.
        uint32_t segmentBegin = range.m_begin;
        for (uint32_t i = range.m_begin; i < range.m_end; ++i)
        {
            const RHIHandle handle = m_submitList[i];
            const View*     view   = rhiContext.TryGet<View>(handle);
            if (!view)
            {
                continue;
            }

            if (i != range.m_begin)
            {
                submitSegment(segmentBegin, i);
            }

            if (hasTarget)
            {
                const auto&         rect     = view->m_rect;
                const RHI::Viewport viewport = targetViewport.GetScaled(rect.m_minX, rect.m_maxX, rect.m_minY, rect.m_maxY);
                commandList->SetViewport(viewport);
                commandList->SetScissor(ScissorFromViewport(viewport));
            }

            const RHI::ShaderBindings* viewBindings = nullptr;
            if (state.m_pso && ResolveViewShaderBindings(rhiContext, handle, viewBindings) && viewBindings)
            {
                BindShaderInputs(commandList, *state.m_pso, *viewBindings);
            }

            segmentBegin = i + 1;
        }
        submitSegment(segmentBegin, range.m_end);
    }
}
