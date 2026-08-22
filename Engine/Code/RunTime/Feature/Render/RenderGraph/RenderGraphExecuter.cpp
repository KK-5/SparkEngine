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

        //! Overflowing the cap means the pass declared two groups for one register space —
        //! a build-time mistake, not a runtime condition.
        void PushBinding(SubmitState& state, const RHI::ShaderBindings* bindings)
        {
            ASSERT(state.m_bindingCount < RHI::Limits::Pipeline::ShaderInputGroupCountMax,
                "[RenderGraphExecuter] More binding groups than register spaces.");
            if (state.m_bindingCount < RHI::Limits::Pipeline::ShaderInputGroupCountMax)
            {
                state.m_bindings[state.m_bindingCount++] = bindings;
            }
        }

        //! Unconditional by design: the CommandList owns the dedup, because what a change
        //! invalidates is backend knowledge (DX12 drops every root parameter with the root
        //! signature; Vulkan keeps descriptor sets across compatible layouts).
        void ApplySubmitState(RHI::CommandList* commandList, const SubmitState& state)
        {
            // Bindings sit under the PSO, not beside it: they resolve their space against its
            // pipeline layout, and DX12 takes the root signature from it — so the bind must
            // follow, and without a PSO there is nothing to bind against. A copy pass's state
            // is empty on both counts.
            if (state.m_pso)
            {
                commandList->SetPipelineState(*state.m_pso);

                const bool isDispatch = state.m_pso->GetType() == RHI::PipelineStateType::Dispatch;
                for (uint8_t i = 0; i < state.m_bindingCount; ++i)
                {
                    if (isDispatch)
                    {
                        commandList->BindShaderInputsForDispatch(*state.m_bindings[i]);
                    }
                    else
                    {
                        commandList->BindShaderInputsForDraw(*state.m_bindings[i]);
                    }
                }
            }

            if (state.m_hasViewport)
            {
                commandList->SetViewport(state.m_viewport);
                commandList->SetScissor(state.m_scissor);
            }
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
        m_submitItems.clear();
        m_submitBatches.clear();

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
        // Mandatory, not just hygiene: the arenas a PassSubmitTable indexes are rebuilt every
        // frame, so a stale one would send a pass into another pass's batches.
        passContext.Clear<PassSubmitTable>();
    }

    void RenderGraphExecuter::BuildExecuteTable(const QueueBasedPasses& queueBasedPasses, PassContext& passContext)
    {
        // Content first, slicing after: both splitters below key on how much each pass has
        // to submit, which is what BuildSubmitTables produces.
        BuildSubmitTables(queueBasedPasses, passContext);

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

    void RenderGraphExecuter::BuildSubmitTables(const QueueBasedPasses& queueBasedPasses, PassContext& passContext)
    {
        auto* rhiContext = RHIExecuteContext::Current();
        if (!rhiContext)
        {
            return;
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(RHI::HardwareQueueClass::Count); ++i)
        {
            for (Pass pass : queueBasedPasses[i])
            {
                BuildPassSubmitTable(pass, passContext, *rhiContext);
            }
        }
    }

    void RenderGraphExecuter::BuildPassSubmitTable(Pass pass, PassContext& passContext, RHIContext& rhiContext)
    {
        const auto* capabilities = passContext.TryGet<PassCapabilities>(pass);
        if (!capabilities || !capabilities->m_collectSubmitItems)
        {
            return;
        }

        RHI::Viewport targetViewport;
        RHI::Scissor  targetScissor;

        // A render pass needs a view — viewport and space1 both come from it — and needs a
        // render area to scale that view's rect against. Checked before the collect, since
        // items are gathered per view and a missing view would silently drop them.
        // Copy / compute have neither and emit one viewless batch instead.
        const bool isRenderPass = passContext.Has<RenderPassTag>(pass);
        if (isRenderPass)
        {
            ASSERT(capabilities->m_collectViews,
                "[RenderGraphExecuter] Render pass {} declares no .RendersView<>().",
                static_cast<uint32_t>(pass));
            if (!capabilities->m_collectViews ||
                !ResolveTargetViewport(pass, passContext, targetViewport, targetScissor))
            {
                return;
            }
        }

        const uint32_t passBegin  = static_cast<uint32_t>(m_submitItems.size());
        const uint32_t batchBegin = static_cast<uint32_t>(m_submitBatches.size());

        // Resolved once; only space1 and the viewport differ per view.
        const auto* compiled = passContext.TryGet<PassCompiledPSO>(pass);
        const auto* shared   = passContext.TryGet<PassSharedBindings>(pass);

        auto emitBatch = [&](RHI::RHIHandle view)
        {
            const RHI::ShaderBindings* viewShaderBindings = nullptr;
            if (view != RHI::NullHandle &&
                !ResolveViewShaderBindings(rhiContext, view, viewShaderBindings))
            {
                // Skipping costs this view one frame; drawing it would be silently wrong.
                return;
            }

            // Per view, not once for the pass: the replay is expanded into the arena so each
            // view owns its own stretch. That is what lets a later per-view cull hand back a
            // different set here.
            const uint32_t begin = static_cast<uint32_t>(m_submitItems.size());
            capabilities->m_collectSubmitItems(rhiContext, passContext, pass, view, m_submitItems);
            const uint32_t end = static_cast<uint32_t>(m_submitItems.size());
            if (begin == end)
            {
                return;
            }

            SubmitState state;
            state.m_pso = compiled ? compiled->m_pso.get() : nullptr;
            if (state.m_pso)
            {
                if (shared)
                {
                    for (const RHI::ShaderBindings* b : shared->m_bindings)
                    {
                        PushBinding(state, b);
                    }
                }
                if (viewShaderBindings)
                {
                    PushBinding(state, viewShaderBindings);
                }
            }

            if (isRenderPass)
            {
                const auto& rect = rhiContext.Get<View>(view).m_rect;
                state.m_viewport    = targetViewport.GetScaled(rect.m_minX, rect.m_maxX, rect.m_minY, rect.m_maxY);
                state.m_scissor     = ScissorFromViewport(state.m_viewport);
                state.m_hasViewport = true;
            }

            BuildSubmitBatches(state, begin, end);
        };

        if (!capabilities->m_collectViews)
        {
            emitBatch(RHI::NullHandle);
        }
        else
        {
            ViewHandleList views;
            capabilities->m_collectViews(rhiContext, views);
            for (RHI::RHIHandle view : views)
            {
                emitBatch(view);
            }
        }

        if (m_submitBatches.size() == batchBegin)
        {
            // Nothing was produced: no items, not a render pass, or no view was ready.
            // Leave no orphan entries in the arena and register no table.
            m_submitItems.resize(passBegin);
            return;
        }

        PassSubmitTable table;
        table.m_batchBegin  = batchBegin;
        table.m_batchEnd    = static_cast<uint32_t>(m_submitBatches.size());
        table.m_submitBegin = passBegin;
        table.m_submitEnd   = static_cast<uint32_t>(m_submitItems.size());
        passContext.AddOrReplace<PassSubmitTable>(pass, table);
    }

    void RenderGraphExecuter::BuildSubmitBatches(
        const SubmitState& state, uint32_t submitBegin, uint32_t submitEnd)
    {
        // DrawItem carries no variant id yet, so the whole run is one batch. Once variants
        // land this becomes a counting sort over the items — histogram, prefix sum, scatter
        // — and the prefix sum is exactly the batch boundaries; each batch then differs from
        // its neighbours only in SubmitState::m_pso. Nothing outside changes.
        SubmitBatch batch;
        batch.m_submitBegin = submitBegin;
        batch.m_submitEnd   = submitEnd;
        batch.m_state       = state;
        m_submitBatches.push_back(batch);
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
            const PassSubmitTable* table = passContext.TryGet<PassSubmitTable>(pass);
            const uint32_t passSubmitCount = table ? table->m_submitEnd - table->m_submitBegin : 0;

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
        // Splitting ONE pass across several Works is what the item's submit range exists for,
        // and that stays blocked until RenderPassBeginInfo carries suspend / resume: a render
        // pass's draws cannot cross CommandLists. m_itemIndex / m_itemCount would then have
        // to be numbered per pass across works rather than fixed at 0 / 1 here.
        ExecuteWork work;
        work.m_items.reserve(group.m_passes.size());

        for (Pass pass : group.m_passes)
        {
            const PassSubmitTable* table = passContext.TryGet<PassSubmitTable>(pass);

            // A pass with nothing to submit still gets its item: BeginRenderPass carries its
            // clears, and compute / copy / custom-pipeline passes submit through their own
            // hook. An empty submit range simply visits no batch and sets no state.
            ExecuteWorkItem item;
            item.m_pass        = pass;
            item.m_submitBegin = table ? table->m_submitBegin : 0;
            item.m_submitEnd   = table ? table->m_submitEnd : 0;
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

    void RenderGraphExecuter::Execute(ExecuteWork& work, RHI::Factory& factory, RHI::Device& device, RHI::HardwareQueueClass queueClass, PassContext& passContext)
    {
        RHI::CommandList* cmdList = factory.CreateCommandList(device, queueClass);
        cmdList->Open();
        work.m_commandList = cmdList;

        // No state cursor: ApplySubmitState runs for every batch and the CommandList
        // collapses the repeats — which is also why a fresh one needs no special case.
        for (auto& item : work.m_items)
        {
            if (item.m_itemIndex == 0)
            {
                ExecutePreBarriers(cmdList, item.m_pass, passContext);
                ExecuteBeginRenderPass(cmdList, item.m_pass, passContext);
            }

            const auto& funcs = passContext.Get<PassFunctions>(item.m_pass);

            // The item is sized by load, so it may start and end mid-batch and span several;
            // the hook is handed one state-homogeneous run at a time.
            for (uint32_t batchIndex = item.m_firstBatch; batchIndex < item.m_batchEnd; ++batchIndex)
            {
                const SubmitBatch& batch = m_submitBatches[batchIndex];

                ApplySubmitState(cmdList, batch.m_state);

                const uint32_t submitBegin = eastl::max(batch.m_submitBegin, item.m_submitBegin);
                const uint32_t submitEnd   = eastl::min(batch.m_submitEnd, item.m_submitEnd);

                work.m_submitBase  = submitBegin;
                work.m_itemHandles = eastl::span<const RHI::RHIHandle>(m_submitItems.data() + submitBegin, submitEnd - submitBegin);
                cmdList->SetSubmitRange({ submitBegin, submitEnd });

                if (funcs.m_executeFunction)
                {
                    funcs.m_executeFunction(work, *this);
                }
            }

            // A pass that produced no batch — compute, copy, custom-pipeline — never enters
            // the loop above, yet its hook is where all of its work lives. Keyed on the PASS
            // having no batches, not on this item covering none: an extra call would make a
            // hook that does more than submit (CopyFrameBufferPass copies) run twice.
            if (item.m_firstBatch == item.m_batchEnd)
            {
                work.m_submitBase  = 0;
                work.m_itemHandles = {};
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
        for (size_t i = 0; i < work.m_itemHandles.size(); ++i)
        {
            work.m_commandList->Submit(
                rhiContext.Get<RHI::DrawItem>(work.m_itemHandles[i]),
                work.m_submitBase + static_cast<uint32_t>(i));
        }
    }
}
