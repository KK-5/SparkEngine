#include "RenderGraphExecuter.h"

#include <RHI/Command/CommandList.h>
#include <RHI/Command/RenderPassBeginInfo.h>
#include <RHI/Factory.h>

#include <Pass/Component/PassComponents.h>

namespace Spark::Render
{

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


    void RenderGraphExecuter::BuildFinalTransitionSegments(RHIContext& context, PassContext& passContext)
    {
        for (uint32_t index = 0; index < static_cast<uint32_t>(RHI::HardwareQueueClass::Count); ++index)
        {
            auto barrierPass = passContext.CreateEntity();
            passContext.Add<PassName>(barrierPass, PassName{ ObjectName{"FinalTransition"} });
            passContext.Add<PassExecuteQueue>(barrierPass, PassExecuteQueue{ static_cast<RHI::HardwareQueueClass>(index) });
            passContext.Add<PassFunctions>(barrierPass, PassFunctions{});   // execute可空，但组件要有
            passContext.AddOrReplace<PassBarriers>(barrierPass, PassBarriers{});

            auto importedView = context.GetView<ImportedTag, ImportedResourceState>();
            importedView.each([&](auto resource, const ImportedResourceState& imp)
            {
                if (context.Has<BackingImage>(resource))
                {
                    RHI::Image* image = context.Get<BackingImage>(resource).m_image;
                    ASSERT(image != nullptr, "BackingImage image is null.");
                    ASSERT(context.Has<ResourceStateTracker>(resource), "Image {} has BackingImage but no ResourceStateTracker.",
                        context.Get<ResourceName>(resource).m_name.GetCStr());

                    ResourceStateTracker current = context.Get<ResourceStateTracker>(resource);
                    const RHI::ResourceState currentState = current.m_current;
                    if (currentState == imp.m_final) 
                    { 
                        return;
                    }

                    RHI::HardwareQueueClass curQueue = passContext.Get<PassExecuteQueue>(current.m_lastPass).m_queue;

                    // 只处理本队列的资源
                    if (index != static_cast<uint32_t>(curQueue))
                    {
                        return;
                    }

                    // 跨队列barrier先不处理
                    if (curQueue != imp.m_finalQueue)
                    {
                        return;
                    }

                    RHI::ImageBarrier b;
                    b.m_image     = image;
                    b.m_srcUsage  = currentState.m_usage;
                    b.m_dstUsage  = imp.m_final.m_usage;
                    b.m_srcAccess = currentState.m_access;
                    b.m_dstAccess = imp.m_final.m_access;
                    b.m_srcStage  = current.m_lastStage;
                    b.m_dstStage  = imp.m_finalStage;
                    b.m_srcQueue  = curQueue;
                    b.m_dstQueue  = imp.m_finalQueue;
                    
                    passContext.Get<PassBarriers>(barrierPass).m_preImage.push_back(b);
                    return;
                }

                if (context.Has<BackingBuffer>(resource))
                {
                    RHI::Buffer* buffer = context.Get<BackingBuffer>(resource).m_buffer;
                    ASSERT(buffer != nullptr, "BackingBuffer buffer is null.");
                    ASSERT(context.Has<ResourceStateTracker>(resource), "Buffer {} has BackingBuffer but no ResourceStateTracker.",
                        context.Get<ResourceName>(resource).m_name.GetCStr());

                    ResourceStateTracker current = context.Get<ResourceStateTracker>(resource);
                    const RHI::ResourceState currentState = current.m_current;
                    if (currentState == imp.m_final) 
                    { 
                        return;
                    }

                    RHI::HardwareQueueClass curQueue = passContext.Get<PassExecuteQueue>(current.m_lastPass).m_queue;

                    // 只处理本队列的资源
                    if (index != static_cast<uint32_t>(curQueue))
                    {
                        return;
                    }

                    // 跨队列barrier先不处理
                    if (curQueue != imp.m_finalQueue)
                    {
                        return;
                    }

                    RHI::BufferBarrier b;
                    b.m_buffer    = buffer;
                    b.m_srcUsage  = currentState.m_usage;
                    b.m_dstUsage  = imp.m_final.m_usage;
                    b.m_srcAccess = currentState.m_access;
                    b.m_dstAccess = imp.m_final.m_access;
                    b.m_srcStage  = current.m_lastStage;
                    b.m_dstStage  = RHI::AttachmentStage::Any;
                    b.m_srcQueue  = curQueue;
                    b.m_dstQueue  = imp.m_finalQueue;

                    passContext.Get<PassBarriers>(barrierPass).m_preBuffer.push_back(b);
                    return;
                }
            });

            if (passContext.Get<PassBarriers>(barrierPass).m_preBuffer.empty() && passContext.Get<PassBarriers>(barrierPass).m_preImage.empty())
            {
                continue;
            }


            QueueSegment segment;
            segment.m_passes.push_back(barrierPass);
            BuildExecuteGroups(segment, passContext);
            BuildExecuteWorks(segment.m_groups[0], passContext);

            m_queueSegments[index].push_back(segment);
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

        for (const auto& item : work.m_items)
        {
            if (item.m_itemIndex == 0)
            {
                ExecutePreBarriers(cmdList, item.m_pass, passContext);
                ExecuteBeginRenderPass(cmdList, item.m_pass, passContext);
            }

            cmdList->SetSubmitRange({ item.m_draws.m_startIndex, item.m_draws.m_endIndex });

            const auto& funcs = passContext.Get<PassFunctions>(item.m_pass);
            if (funcs.m_executeFunction)
            {
                funcs.m_executeFunction(cmdList);
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
