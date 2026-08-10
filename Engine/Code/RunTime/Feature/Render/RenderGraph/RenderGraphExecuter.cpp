#include "RenderGraphExecuter.h"

#include <EASTL/algorithm.h>

#include <RHI/Command/CommandList.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/RenderPassBeginInfo.h>
#include <RHI/Factory.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Pass/PassCapabilities.h>
#include <Pass/Component/PassComponents.h>
#include <View/View.h>
#include <View/ViewComponents.h>

namespace Spark::Render
{
    namespace
    {
        //! The full-target viewport / scissor, which every view's rect is scaled against.
        //! RenderPassBeginInfo carries no render area and a depth-only pass has no color
        //! attachment, so the extent comes from the first attachment that exists.
        //! False means this is not a render pass — nothing should set a viewport on it.
        bool ResolveTargetViewport(
            Pass pass, const PassContext& passContext, RHI::Viewport& viewport, RHI::Scissor& scissor)
        {
            const auto* beginInfo = passContext.TryGet<RHI::RenderPassBeginInfo>(pass);
            if (!beginInfo)
            {
                return false;
            }

            const RHI::ImageView* target = beginInfo->m_colorAttachmentCount > 0
                ? beginInfo->m_colorAttachments[0].m_view
                : beginInfo->m_depthStencilAttachment.m_view;
            if (!target)
            {
                return false;
            }

            const RHI::Size extent = target->GetImage().GetDescriptor().m_size.GetReducedMip(
                target->GetDescriptor().m_mipSliceMin);

            viewport = RHI::Viewport(
                0.f, static_cast<float>(extent.m_width), 0.f, static_cast<float>(extent.m_height));
            scissor = RHI::Scissor(
                0, 0, static_cast<int32_t>(extent.m_width), static_cast<int32_t>(extent.m_height));
            return true;
        }

        RHI::Scissor ScissorFromViewport(const RHI::Viewport& viewport)
        {
            return RHI::Scissor(
                static_cast<int32_t>(viewport.m_minX), static_cast<int32_t>(viewport.m_minY),
                static_cast<int32_t>(viewport.m_maxX), static_cast<int32_t>(viewport.m_maxY));
        }

        //! A view's space1 SRG, if it declares one. The ViewShaderBindings component IS the
        //! declaration, which is what separates the two nulls:
        //!  - no component      -> the view binds no space1 at all (a pass whose shader has
        //!                         none still wants that view's viewport). Usable, out stays null.
        //!  - component, no SRG -> declared but not compiled yet, e.g. a view created this
        //!                         frame. NOT usable — drawing would leave space1 holding the
        //!                         previous pass's descriptors.
        bool ResolveViewShaderBindings(
            RHIContext& rhiContext, RHI::RHIHandle view, const RHI::ShaderBindings*& out)
        {
            out = nullptr;

            const auto* viewBindings = rhiContext.TryGet<ViewShaderBindings>(view);
            if (!viewBindings)
            {
                return true;
            }

            const auto* component =
                rhiContext.TryGet<RHI::Components::ShaderBindings>(viewBindings->m_bindings);
            if (!component || !component->m_bindings)
            {
                return false;
            }

            out = component->m_bindings.get();
            return true;
        }
    }

    void RenderGraphExecuter::Begin(uint32_t frameIndex)
    {
        m_frameIndex = frameIndex;
    }

    void RenderGraphExecuter::End()
    {
        for (auto& queue : m_queueSegments)
        {
            queue.clear();
        }

        // The draw table is frame-scoped; clear() keeps the capacity so a steady frame
        // allocates nothing.
        m_draws.clear();
        m_drawLists.clear();
        m_drawBatches.clear();

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
        // Mandatory, not just hygiene: the arenas a PassDrawTable indexes are rebuilt every
        // frame, so a stale one would send a pass into another pass's batches.
        passContext.Clear<PassDrawTable>();
    }

    void RenderGraphExecuter::BuildExecuteTable(const QueueBasedPasses& queueBasedPasses, PassContext& passContext)
    {
        // Content first, slicing after: both splitters below key on how much each pass has
        // to draw, which is what BuildDrawTables produces.
        BuildDrawTables(queueBasedPasses, passContext);

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

    void RenderGraphExecuter::BuildDrawTables(const QueueBasedPasses& queueBasedPasses, PassContext& passContext)
    {
        auto* rhiContext = RHIExecuteContext::Current();
        if (!rhiContext)
        {
            return;
        }

        // Compute / Copy passes need only the one empty item BuildExecuteWorks gives them,
        // to drive their barriers and their own hook. Nothing is built for them here.
        const auto graphics = static_cast<uint32_t>(RHI::HardwareQueueClass::Graphics);
        for (Pass pass : queueBasedPasses[graphics])
        {
            BuildPassDrawTable(pass, passContext, *rhiContext);
        }
    }

    void RenderGraphExecuter::BuildPassDrawTable(Pass pass, PassContext& passContext, RHIContext& rhiContext)
    {
        const auto* capabilities = passContext.TryGet<PassCapabilities>(pass);
        if (!capabilities || !capabilities->m_collectDrawItems)
        {
            return;
        }

        const uint32_t drawBegin  = static_cast<uint32_t>(m_draws.size());
        const uint32_t batchBegin = static_cast<uint32_t>(m_drawBatches.size());

        capabilities->m_collectDrawItems(rhiContext, m_draws);
        const uint32_t drawCount = static_cast<uint32_t>(m_draws.size()) - drawBegin;

        // Viewport and space1 both come from the view, so a pass with draws needs one.
        ASSERT(drawCount == 0 || capabilities->m_collectViews,
            "[RenderGraphExecuter] Pass {} has draws but declares no .RendersView<>().",
            static_cast<uint32_t>(pass));

        RHI::Viewport targetViewport;
        RHI::Scissor  targetScissor;
        uint32_t      submitCount = 0;

        if (drawCount != 0 && capabilities->m_collectViews &&
            ResolveTargetViewport(pass, passContext, targetViewport, targetScissor))
        {
            ViewHandleList views;
            capabilities->m_collectViews(rhiContext, views);

            for (RHI::RHIHandle view : views)
            {
                const RHI::ShaderBindings* viewShaderBindings = nullptr;
                if (!ResolveViewShaderBindings(rhiContext, view, viewShaderBindings))
                {
                    // Skipping costs this view one frame; drawing it would be silently wrong.
                    continue;
                }

                const auto& rect = rhiContext.Get<View>(view).m_rect;

                DrawList list;
                list.m_viewport = targetViewport.GetScaled(rect.m_minX, rect.m_maxX, rect.m_minY, rect.m_maxY);
                list.m_scissor  = ScissorFromViewport(list.m_viewport);
                list.m_viewShaderBindings = viewShaderBindings;

                const uint32_t listIndex = static_cast<uint32_t>(m_drawLists.size());
                m_drawLists.push_back(list);

                submitCount = BuildDrawBatches(pass, passContext, listIndex, drawBegin, drawCount, submitCount);
            }
        }

        if (m_drawBatches.size() == batchBegin)
        {
            // Nothing was produced: no draws, not a render pass, or no view was ready.
            // Leave no orphan entries in the arena and register no table.
            m_draws.resize(drawBegin);
            return;
        }

        PassDrawTable table;
        table.m_batchBegin  = batchBegin;
        table.m_batchEnd    = static_cast<uint32_t>(m_drawBatches.size());
        table.m_submitCount = submitCount;
        passContext.AddOrReplace<PassDrawTable>(pass, table);
    }

    uint32_t RenderGraphExecuter::BuildDrawBatches(
        Pass pass, const PassContext& passContext,
        uint32_t listIndex, uint32_t drawBegin, uint32_t drawCount, uint32_t submitBegin)
    {
        // DrawItem carries no variant id yet, so the whole run is one batch. Once variants
        // land this becomes a counting sort over the draws — histogram, prefix sum, scatter
        // — and the prefix sum is exactly the batch boundaries. Nothing outside changes.
        const auto* compiled = passContext.TryGet<PassCompiledPSO>(pass);

        DrawBatch batch;
        batch.m_submitBegin = submitBegin;
        batch.m_submitEnd   = submitBegin + drawCount;
        batch.m_drawBegin   = drawBegin;
        batch.m_listIndex   = listIndex;
        batch.m_variantId   = kSingleVariantId;
        batch.m_pso         = compiled ? compiled->m_pso.get() : nullptr;
        m_drawBatches.push_back(batch);

        return batch.m_submitEnd;
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
        // A group is one ExecuteCommandLists call, and RenderGraph records a group then
        // submits it before recording the next — so the group is what lets the GPU start on
        // the front of a segment while the CPU is still recording the back. Merging a whole
        // segment into one group gives that up: nothing reaches the GPU until everything is
        // recorded. Hence the draw budget belongs here.
        //
        // Consecutive passes only, never reordered: the GPU runs a group's CommandLists in
        // array order and the groups in submission order, which is what preserves pass order.
        ExecuteGroup group;
        uint32_t groupSubmitCount = 0;

        for (Pass pass : segment.m_passes)
        {
            const PassDrawTable* table = passContext.TryGet<PassDrawTable>(pass);
            const uint32_t passSubmitCount = table ? table->m_submitCount : 0;

            // Never break on an empty group: a single pass over the whole budget still has to
            // go somewhere, and it cannot be split until RenderPassBeginInfo carries
            // suspend / resume. The budget is a target, not a cap.
            if (!group.m_passes.empty() &&
                groupSubmitCount + passSubmitCount > kMaxSubmitsPerCommandList)
            {
                segment.m_groups.push_back(eastl::move(group));
                group            = {};
                groupSubmitCount = 0;
            }

            group.m_passes.push_back(pass);
            groupSubmitCount += passSubmitCount;
        }

        if (!group.m_passes.empty())
        {
            segment.m_groups.push_back(eastl::move(group));
        }
    }

    void RenderGraphExecuter::BuildExecuteWorks(ExecuteGroup& group, const PassContext& passContext) const
    {
        // One Work per group: BuildExecuteGroups already bounded the group by the draw budget,
        // so its passes are exactly one CommandList's worth. Handing them out to several Works
        // is what buys parallel recording — the works of a group are the parallel unit — but
        // RenderGraph records them serially today, so more Works would only mean more
        // CommandLists to create and submit.
        //
        // Slicing ONE pass across several Works is what the item's submit range exists for,
        // and that stays blocked until RenderPassBeginInfo carries suspend / resume: a render
        // pass's draws cannot cross CommandLists. m_itemIndex / m_itemCount would then have
        // to be numbered per pass across works rather than fixed at 0 / 1 here.
        ExecuteWork work;
        work.m_items.reserve(group.m_passes.size());

        for (Pass pass : group.m_passes)
        {
            const PassDrawTable* table = passContext.TryGet<PassDrawTable>(pass);

            // A pass with nothing to draw still gets its item: BeginRenderPass carries its
            // clears, and compute / copy / custom-pipeline passes submit through their own
            // hook. An empty submit range simply visits no batch and sets no state.
            ExecuteWork::Item item;
            item.m_pass        = pass;
            item.m_submitBegin = 0;
            item.m_submitEnd   = table ? table->m_submitCount : 0;
            item.m_firstBatch  = table ? table->m_batchBegin : 0;
            item.m_batchEnd    = table ? table->m_batchEnd : 0;
            item.m_itemIndex   = 0;
            item.m_itemCount   = 1;
            work.m_items.push_back(item);
        }

        group.m_works.push_back(eastl::move(work));
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

    //! Returns what it bound so the caller can seed its PSO cursor: the pass's batches carry
    //! this same PipelineState, so without it the pass's first batch would re-bind it.
    const RHI::PipelineState* RenderGraphExecuter::ExecuteBindPSO(
        RHI::CommandList* commandList, Pass pass, PassContext& passContext)
    {
        if (auto* pso = passContext.TryGet<PassCompiledPSO>(pass))
        {
            commandList->SetPipelineState(*pso->m_pso);
            return pso->m_pso.get();
        }
        return nullptr;
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

    void RenderGraphExecuter::ExecuteDrawListState(RHI::CommandList* commandList, const DrawList& list)
    {
        commandList->SetViewport(list.m_viewport);
        commandList->SetScissor(list.m_scissor);
        if (list.m_viewShaderBindings)
        {
            commandList->BindShaderInputsForDraw(*list.m_viewShaderBindings);
        }
    }

    void RenderGraphExecuter::Execute(ExecuteWork& work, RHI::Factory& factory, RHI::Device& device, RHI::HardwareQueueClass queueClass, PassContext& passContext)
    {
        RHI::CommandList* cmdList = factory.CreateCommandList(device, queueClass);
        cmdList->Open();
        work.m_commandList = cmdList;

        // A Work is a fresh CommandList, so no state carries in: both cursors start unset and
        // the first batch re-establishes everything. They then survive across items, which a
        // Work holding several passes relies on — and safely so, because list indices are
        // absolute into m_drawLists and so never repeat between passes.
        constexpr uint32_t kNoList = 0xFFFFFFFF;
        uint32_t boundList = kNoList;
        const RHI::PipelineState* boundPSO = nullptr;

        for (auto& item : work.m_items)
        {
            if (item.m_itemIndex == 0)
            {
                boundPSO = ExecuteBindPSO(cmdList, item.m_pass, passContext);
                ExecuteBindShared(cmdList, item.m_pass, passContext);
                ExecutePreBarriers(cmdList, item.m_pass, passContext);
                ExecuteBeginRenderPass(cmdList, item.m_pass, passContext);
            }

            work.m_pass = item.m_pass;
            const auto& funcs = passContext.Get<PassFunctions>(item.m_pass);

            // Walk the batches this slice covers. The slice is sized by load, so it may
            // start and end mid-batch and may span several — each boundary crossed is a
            // state change, and the hook is handed one state-homogeneous run at a time.
            for (uint32_t batchIndex = item.m_firstBatch; batchIndex < item.m_batchEnd; ++batchIndex)
            {
                const DrawBatch& batch = m_drawBatches[batchIndex];
                if (batch.m_submitBegin >= item.m_submitEnd)
                {
                    break;
                }

                if (batch.m_listIndex != boundList)
                {
                    ExecuteDrawListState(cmdList, m_drawLists[batch.m_listIndex]);
                    boundList = batch.m_listIndex;
                }
                if (batch.m_pso && batch.m_pso != boundPSO)
                {
                    cmdList->SetPipelineState(*batch.m_pso);
                    boundPSO = batch.m_pso;
                }

                const uint32_t submitBegin = eastl::max(batch.m_submitBegin, item.m_submitBegin);
                const uint32_t submitEnd   = eastl::min(batch.m_submitEnd, item.m_submitEnd);
                const uint32_t drawBegin   = batch.m_drawBegin + (submitBegin - batch.m_submitBegin);

                work.m_submitBase  = submitBegin;
                work.m_drawHandles = eastl::span<const RHI::RHIHandle>(m_draws.data() + drawBegin, submitEnd - submitBegin);
                cmdList->SetSubmitRange({ submitBegin, submitEnd });

                if (funcs.m_executeFunction)
                {
                    funcs.m_executeFunction(work, *this);
                }
            }

            // A pass that produced no batch at all — compute, copy, custom-pipeline — never
            // enters the loop above, yet its hook is where all of its work lives. Keyed on
            // the PASS having no batches, not on this slice covering none: an empty slice of
            // a pass that does draw must NOT get an extra call, or a hook that does more than
            // submit (CopyFrameBufferPass copies) would do its work twice.
            if (item.m_firstBatch == item.m_batchEnd)
            {
                work.m_submitBase  = 0;
                work.m_drawHandles = {};
                cmdList->SetSubmitRange({ 0, 0 });

                if (funcs.m_executeFunction)
                {
                    funcs.m_executeFunction(work, *this);
                }
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
        auto& rhiContext = *RHI::RHIExecuteContext::Current();
        for (size_t i = 0; i < work.m_drawHandles.size(); ++i)
        {
            work.m_commandList->Submit(
                rhiContext.Get<RHI::DrawItem>(work.m_drawHandles[i]),
                work.m_submitBase + static_cast<uint32_t>(i));
        }
    }
}
