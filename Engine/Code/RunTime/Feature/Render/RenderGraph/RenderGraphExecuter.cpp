#include "RenderGraphExecuter.h"

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

        // Destroy transient view entities created this frame by
        // CompileTransientImageViews / CompileTransientBufferViews.
        // Each view allocates descriptor handles; without this cleanup the
        // descriptor pool exhausts after ~3 frames.
        {
            auto& rhiContext = *RHIExecuteContext::Current();

            auto imageViewEntities = rhiContext.GetView<TransientViewTag, ImageView>();
            eastl::vector<RHIHandle> imageViewHandles;
            imageViewEntities.each(
                [&](RHIHandle h, const ImageView&) { imageViewHandles.push_back(h); });
            for (RHIHandle h : imageViewHandles)
            {
                rhiContext.DestoryEntity(h);
            }

            auto bufferViewEntities = rhiContext.GetView<TransientViewTag, BufferView>();
            eastl::vector<RHIHandle> bufferViewHandles;
            bufferViewEntities.each(
                [&](RHIHandle h, const BufferView&) { bufferViewHandles.push_back(h); });
            for (RHIHandle h : bufferViewHandles)
            {
                rhiContext.DestoryEntity(h);
            }

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
        // TODO: 按负载合并/拆分 group 的 passes 到 ExecuteWork
        // 当前默认: 每个 pass → 一个 Work → 一个 Item
        for (Pass pass : group.m_passes)
        {
            ExecuteWork work;
            work.m_items.push_back({ pass, {0, 1}, 0, 1 });
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

    void RenderGraphExecuter::ExecutePassViewportState(RHI::CommandList* commandList, Pass pass, PassContext& passContext)
    {
        if (auto* vp = passContext.TryGet<PassViewportState>(pass))
        {
            commandList->SetViewports(&vp->m_viewport, 1);
            commandList->SetScissors(&vp->m_scissor, 1);
        }
    }

    void RenderGraphExecuter::ExecutePassShaderBindings(RHI::CommandList* commandList, Pass pass, PassContext& passContext)
    {
        auto* attached = passContext.TryGet<PassShaderBindings>(pass);
        if (!attached)
        {
            return;
        }

        const bool isCompute = passContext.Has<ComputePassTag>(pass);

        for (const auto& entry : attached->m_entries)
        {
            if (!entry.m_bindings)
            {
                continue;
            }
            if (isCompute)
            {
                commandList->BindShaderInputsForDispatch(*entry.m_bindings);
            }
            else
            {
                commandList->BindShaderInputsForDraw(*entry.m_bindings);
            }
        }
    }

    void RenderGraphExecuter::Execute(ExecuteWork& work, RHI::Factory& factory, RHI::Device& device, RHI::HardwareQueueClass queueClass, PassContext& passContext)
    {
        RHI::CommandList* cmdList = factory.CreateCommandList(device, queueClass);
        cmdList->Open();
        work.m_commandList = cmdList;

        for (auto& item : work.m_items)
        {
            if (item.m_itemIndex == 0)
            {
                ExecuteBindPSO(cmdList, item.m_pass, passContext);
                ExecutePassShaderBindings(cmdList, item.m_pass, passContext);
                ExecutePassViewportState(cmdList, item.m_pass, passContext);
                ExecutePreBarriers(cmdList, item.m_pass, passContext);
                ExecuteBeginRenderPass(cmdList, item.m_pass, passContext);
            }

            cmdList->SetSubmitRange({ item.m_draws.m_startIndex, item.m_draws.m_endIndex });

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
}
