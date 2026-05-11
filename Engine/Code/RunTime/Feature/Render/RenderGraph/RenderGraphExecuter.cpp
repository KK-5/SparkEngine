#include "RenderGraphExecuter.h"

#include <RHI/Command/CommandList.h>
#include <RHI/Command/RenderPassBeginInfo.h>
#include <RHI/Factory.h>

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
            work.m_items.push_back({ pass, {}, 0, 1 });
            group.m_works.push_back(eastl::move(work));
        }
    }

    void RenderGraphExecuter::ExecutePreBarriers(RHI::CommandList* commandList, Pass pass, PassContext& passContext)
    {
        const PassBarriers& barriers = passContext.Get<PassBarriers>(pass);

        for (const auto& b : barriers.m_preAliasing)
        {
            commandList->QueueAliasingBarrier(b);
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

        auto& rhiCtx = *RHIExecuteContext::Current();

        for (auto& item : work.m_items)
        {
            // --- resolve per-item PSO / SRG binding ---
            item.m_pipelineState = nullptr;
            item.m_shaderResources.clear();

            if (auto* pso = passContext.TryGet<PassCompiledPSO>(item.m_pass))
            {
                item.m_pipelineState = pso->m_pso.get();
            }

            if (auto* srgs = passContext.TryGet<PassShaderResources>(item.m_pass))
            {
                for (RHIHandle handle : srgs->m_slots)
                {
                    if (handle == NullHandle)
                    {
                        continue;
                    }
                    if (auto* backing = rhiCtx.TryGet<BackingShaderResource>(handle))
                    {
                        item.m_shaderResources.push_back(backing->m_shaderResource);
                    }
                }
            }

            if (item.m_itemIndex == 0)
            {
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
