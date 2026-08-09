#include "RenderGraphExecuter.h"

#include <EASTL/sort.h>

#include <RHI/Command/CommandList.h>
#include <RHI/Command/RenderPassBeginInfo.h>
#include <RHI/Factory.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Pass/Component/PassComponents.h>

namespace Spark::Render
{
    void RenderGraphExecuter::Begin(uint32_t frameIndex)
    {
        m_frameIndex = frameIndex;
    }

    void RenderGraphExecuter::End()
    {
        // Before the works are dropped: entries found dead while recording. Deferred to
        // here because removing one shifts the batch ranges the items were built from.
        ReapDeadDrawEntries(*PassExecuteContext::Current());

        for (auto& queue : m_queueSegments)
        {
            queue.clear();
        }

        // Per-resource compile-time state cursor. Lazy-init in CompileImage/BufferBarriers
        // expects a fresh slate each frame — imported resources start at m_initial,
        // transients at Uninitialized. Without this clear, frame N+1 inherits frame N's
        // m_current and emits wrong barriers.
        RHIExecuteContext::Current()->Clear<ResourceStateTracker>();

        // Destroy compiler-synthesized sink passes (final transition barriers).
        // They are recreated each frame; persisting them would leak entities
        // and cause CompileFinalTransitionBarrier to clash with stale state.
        auto& passContext = *PassExecuteContext::Current();
        auto sinks = passContext.GetView<SinkPassTag>();
        eastl::vector<Pass> sinkPasses;
        sinks.each([&](Pass pass) { sinkPasses.push_back(pass); });
        for (Pass pass : sinkPasses)
        {
            passContext.DestoryEntity(pass);
        }

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
        passContext.Clear<PassPredecessors>();
        passContext.Clear<PassSuccessors>();
        passContext.Clear<PassSyncWait>();
        passContext.Clear<PassSyncSignal>();
        passContext.Clear<PassBarriers>();
        passContext.Clear<PassExternalFenceWaits>();
        passContext.Clear<RHI::RenderPassBeginInfo>();
    }

	void RenderGraphExecuter::BuildExecuteTable(const QueueBasedPasses& queueBasedPasses, const PassContext& passContext)
	{
	    for (uint32_t i = 0; i < static_cast<uint32_t>(RHI::HardwareQueueClass::Count); ++i)
	    {
	        BuildSegments(i, queueBasedPasses[i], passContext);
	        for (auto& segment : m_queueSegments[i])
	        {
	            BuildExecuteGroups(segment, passContext);
	            for (auto& group : segment.m_groups)
	            {
	                BuildExecuteWorks(group, passContext);
	            }
	        }
	    }
	}

    void RenderGraphExecuter::BuildSegments(
        uint32_t queueIndex,
        eastl::span<const Pass> queuePasses,
        const PassContext&      passContext)
    {
        auto& out = m_queueSegments[queueIndex];
        QueueSegment cur;

        for (Pass pass : queuePasses)
        {
            const bool hasWait = passContext.Has<PassSyncWait>(pass);
            if (hasWait && !cur.m_passes.empty())
            {
                out.push_back(eastl::move(cur));
                cur = {};
            }
            if (hasWait)
            {
                const auto& wait = passContext.Get<PassSyncWait>(pass);
                cur.m_waits.insert(cur.m_waits.end(), wait.m_waits.begin(), wait.m_waits.end());
            }

            cur.m_passes.push_back(pass);

            if (auto* extWaits = passContext.TryGet<PassExternalFenceWaits>(pass))
            {
                cur.m_externalWaits.insert(cur.m_externalWaits.end(),
                                           extWaits->m_waits.begin(),
                                           extWaits->m_waits.end());
            }

            if (passContext.Has<PassSyncSignal>(pass))
            {
                cur.m_signal = passContext.Get<PassSyncSignal>(pass).m_signal;
                out.push_back(eastl::move(cur));
                cur = {};
            }
        }

        if (!cur.m_passes.empty())
        {
            out.push_back(eastl::move(cur));
        }
    }

    void RenderGraphExecuter::BuildExecuteGroups(QueueSegment& segment, const PassContext& passContext) const
    {
        // TODO: 按负载将 segment 的 passes 分组到 ExecuteGroup
        // 当前默认: 每个 pass → 一个 ExecuteGroup
        for (Pass pass : segment.m_passes)
        {
            ExecuteGroup group;
            group.m_passes.push_back(pass);
            segment.m_groups.push_back(eastl::move(group));
        }
    }

    void RenderGraphExecuter::BuildExecuteWorks(ExecuteGroup& group, const PassContext& passContext) const
    {
        // One Work per pass. Splitting a pass across Works is what the item shape exists
        // for, but it stays disabled until RenderPassBeginInfo carries suspend / resume:
        // a render pass's draws cannot cross CommandLists today. The load figure the
        // split would key on is now available — the sum of the lists' entry counts.
        for (Pass pass : group.m_passes)
        {
            ExecuteWork work;

            if (const auto* lists = passContext.TryGet<PassDrawLists>(pass))
            {
                for (uint16_t li = 0; li < static_cast<uint16_t>(lists->m_lists.size()); ++li)
                {
                    const DrawList& list = lists->m_lists[li];
                    for (uint16_t bi = 0; bi < static_cast<uint16_t>(list.m_batches.size()); ++bi)
                    {
                        const DrawBatch& batch = list.m_batches[bi];
                        ExecuteWork::Item item;
                        item.m_pass       = pass;
                        item.m_listIndex  = li;
                        item.m_batchIndex = bi;
                        item.m_draws      = { batch.m_begin, batch.m_end };
                        work.m_items.push_back(item);
                    }
                }
            }

            // A pass with nothing to draw still needs one item: BeginRenderPass carries
            // its clears, and a pass without DrawLists submits through its own hook.
            if (work.m_items.empty())
            {
                ExecuteWork::Item item;
                item.m_pass = pass;
                work.m_items.push_back(item);
            }

            const uint32_t count = static_cast<uint32_t>(work.m_items.size());
            for (uint32_t i = 0; i < count; ++i)
            {
                work.m_items[i].m_itemIndex = i;
                work.m_items[i].m_itemCount = count;
            }

            group.m_works.push_back(eastl::move(work));
        }
    }

    void RenderGraphExecuter::ExecutePreBarriers(RHI::CommandList* commandList, Pass pass, PassContext& passContext)
    {
        const PassBarriers& barriers = passContext.Get<PassBarriers>(pass);

        for (const auto& b : barriers.m_preDeviceMemory)
        {
            commandList->QueueBarrier(b);
        }

        for (const auto& b : barriers.m_preImage)
        {
            commandList->QueueBarrier(b);
        }

        for (const auto& b : barriers.m_preBuffer)
        {
            commandList->QueueBarrier(b);
        }

        // QueueBarrier only batches; the actual ResourceBarrier call lives in
        // FlushBarriers. Must flush before BeginRenderPass / draws so the
        // resource is in the expected state at first use.
        commandList->FlushBarriers();
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

    void RenderGraphExecuter::ExecutePostBarriers(RHI::CommandList* commandList, Pass pass, PassContext& passContext)
    {
        const PassBarriers& barriers = passContext.Get<PassBarriers>(pass);

        for (const auto& b : barriers.m_postImage)
        {
            commandList->QueueBarrier(b);
        }

        for (const auto& b : barriers.m_postBuffer)
        {
            commandList->QueueBarrier(b);
        }

        commandList->FlushBarriers();
    }

    void RenderGraphExecuter::ExecuteBeginRenderPass(RHI::CommandList* commandList, Pass pass, PassContext& passContext)
    {
        if (passContext.Has<RHI::RenderPassBeginInfo>(pass))
        {
            commandList->BeginRenderPass(passContext.Get<RHI::RenderPassBeginInfo>(pass));
        }
    }

    void RenderGraphExecuter::ExecuteEndRenderPass(RHI::CommandList* commandList, Pass pass, PassContext& passContext)
    {
        if (passContext.Has<RHI::RenderPassBeginInfo>(pass))
        {
            commandList->EndRenderPass();
        }
    }

    void RenderGraphExecuter::ExecuteBindPSO(RHI::CommandList* commandList, Pass pass, PassContext& passContext)
    {
        if (auto* pso = passContext.TryGet<PassCompiledPSO>(pass))
        {
            commandList->SetPipelineState(*pso->m_pso);
        }
    }

    void RenderGraphExecuter::ExecuteBindShared(RHI::CommandList* commandList, Pass pass, PassContext& passContext)
    {
        // No compiled PSO means no pipeline layout to resolve spaces against, and the bind
        // would assert. Custom-pipeline passes land here too.
        auto* pso = passContext.TryGet<PassCompiledPSO>(pass);
        if (!pso || !pso->m_pso)
        {
            return;
        }

        auto* shared = passContext.TryGet<PassSharedBindings>(pass);
        if (!shared)
        {
            return;
        }

        // Same discriminator the backend uses to pick the graphics vs compute root
        // signature in SetPipelineState.
        const bool isDispatch = pso->m_pso->GetType() == RHI::PipelineStateType::Dispatch;
        for (const RHI::ShaderBindings* bindings : shared->m_bindings)
        {
            if (isDispatch)
            {
                commandList->BindShaderInputsForDispatch(*bindings);
            }
            else
            {
                commandList->BindShaderInputsForDraw(*bindings);
            }
        }
    }

    void RenderGraphExecuter::ExecutePassViewportState(RHI::CommandList* commandList, Pass pass, PassContext& passContext)
    {
        if (auto* vp = passContext.TryGet<PassViewportState>(pass))
        {
            commandList->SetViewports(&vp->m_viewport, 1);
            commandList->SetScissors(&vp->m_scissor, 1);
        }
    }

    void RenderGraphExecuter::ExecuteDrawListState(RHI::CommandList* commandList, const DrawList& list)
    {
        commandList->SetViewport(list.m_viewport);
        commandList->SetScissor(list.m_scissor);
        if (list.m_viewBindings)
        {
            commandList->BindShaderInputsForDraw(*list.m_viewBindings);
        }
    }

    //! Fetch the slice's DrawItem components once, up front: an entry whose DrawItem is
    //! gone is dropped here and queued for removal, so every hook — the default one and
    //! any pass-authored replacement — only ever sees live draws.
    static void ResolveDrawItems(ExecuteWork& work, const ExecuteWork::Item& item, const DrawList* list)
    {
        if (!list)
        {
            return;
        }
        auto& ctx = *RHI::RHIExecuteContext::Current();

        const eastl::vector<RHI::RHIHandle>& entries = list->m_entries;
        for (uint32_t i = item.m_draws.m_startIndex; i < item.m_draws.m_endIndex; ++i)
        {
            const RHI::RHIHandle entry = entries[i];
            const RHI::DrawItem* drawItem = ctx.Valid(entry) ? ctx.TryGet<RHI::DrawItem>(entry) : nullptr;
            if (!drawItem)
            {
                work.m_deadEntries.push_back(DeadDrawEntry{ item.m_pass, item.m_listIndex, i });
                continue;
            }
            work.m_drawItems.push_back(ResolvedDraw{ drawItem, i });
        }
    }

    void RenderGraphExecuter::Execute(ExecuteWork& work, RHI::Factory& factory, RHI::Device& device, RHI::HardwareQueueClass queueClass, PassContext& passContext)
    {
        RHI::CommandList* cmdList = factory.CreateCommandList(device, queueClass);
        cmdList->Open();
        work.m_commandList = cmdList;

        // A Work is a fresh CommandList, so no state carries in: both cursors start unset
        // and the first item re-establishes everything.
        uint16_t boundList  = kNoDrawList;
        uint16_t boundBatch = 0xFFFF;

        for (auto& item : work.m_items)
        {
            if (item.m_itemIndex == 0)
            {
                ExecuteBindPSO(cmdList, item.m_pass, passContext);
                ExecuteBindShared(cmdList, item.m_pass, passContext);
                ExecutePassViewportState(cmdList, item.m_pass, passContext);
                ExecutePreBarriers(cmdList, item.m_pass, passContext);
                ExecuteBeginRenderPass(cmdList, item.m_pass, passContext);
            }

            const DrawList*  list  = nullptr;
            const DrawBatch* batch = nullptr;
            if (item.m_listIndex != kNoDrawList)
            {
                auto* lists = passContext.TryGet<PassDrawLists>(item.m_pass);
                list  = &lists->m_lists[item.m_listIndex];
                batch = &list->m_batches[item.m_batchIndex];

                if (item.m_listIndex != boundList)
                {
                    ExecuteDrawListState(cmdList, *list);
                    boundList  = item.m_listIndex;
                    boundBatch = 0xFFFF;
                }
                if (item.m_batchIndex != boundBatch)
                {
                    if (batch->m_pso)
                    {
                        cmdList->SetPipelineState(*batch->m_pso);
                    }
                    boundBatch = item.m_batchIndex;
                }
            }

            work.m_pass      = item.m_pass;
            work.m_drawList  = list;
            work.m_drawBatch = batch;
            work.m_draws     = item.m_draws;
            work.m_listIndex = item.m_listIndex;
            work.m_drawItems.clear();
            cmdList->SetSubmitRange({ item.m_draws.m_startIndex, item.m_draws.m_endIndex });

            ResolveDrawItems(work, item, list);

            const auto& funcs = passContext.Get<PassFunctions>(item.m_pass);
            if (funcs.m_executeFunction)
            {
                funcs.m_executeFunction(work, *this);
            }

            if (item.m_itemIndex == item.m_itemCount - 1)
            {
                ExecuteEndRenderPass(cmdList, item.m_pass, passContext);
                ExecutePostBarriers(cmdList, item.m_pass, passContext);
            }
        }

        cmdList->Close();
    }

    void SubmitDrawBatch(ExecuteWork& work, RenderGraphExecuter&)
    {
        for (const ResolvedDraw& draw : work.m_drawItems)
        {
            work.m_commandList->Submit(*draw.m_item, draw.m_submitIndex);
        }
    }

    void RenderGraphExecuter::ReapDeadDrawEntries(PassContext& passContext)
    {
        m_deadDrawEntries.clear();
        ForEachWork([&](ExecuteWork& work)
        {
            m_deadDrawEntries.insert(
                m_deadDrawEntries.end(), work.m_deadEntries.begin(), work.m_deadEntries.end());
        });
        if (m_deadDrawEntries.empty())
        {
            return;
        }

        // Descending within a list: DrawListRemoveAt only disturbs indices ABOVE the one
        // removed, so every index still pending is untouched. One list's entries can come
        // from several works, hence the sort spans all of them.
        eastl::sort(m_deadDrawEntries.begin(), m_deadDrawEntries.end(),
            [](const DeadDrawEntry& a, const DeadDrawEntry& b)
        {
            const uint32_t passA = static_cast<uint32_t>(a.m_pass);
            const uint32_t passB = static_cast<uint32_t>(b.m_pass);
            if (passA != passB)
            {
                return passA < passB;
            }
            if (a.m_listIndex != b.m_listIndex)
            {
                return a.m_listIndex < b.m_listIndex;
            }
            return a.m_entryIndex > b.m_entryIndex;
        });

        for (const DeadDrawEntry& dead : m_deadDrawEntries)
        {
            auto* lists = passContext.TryGet<PassDrawLists>(dead.m_pass);
            if (!lists || dead.m_listIndex >= lists->m_lists.size())
            {
                continue;
            }
            DrawListRemoveAt(lists->m_lists[dead.m_listIndex], dead.m_entryIndex);
        }
    }
}
