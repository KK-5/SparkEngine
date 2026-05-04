#include "RenderGraphExecuter.h"

#include <RHI/Command/CommandList.h>

#include <Pass/Component/PassComponents.h>

namespace Spark::Render
{
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
}
