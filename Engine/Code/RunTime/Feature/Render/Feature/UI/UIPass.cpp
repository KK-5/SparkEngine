#include "UIPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>

#include <Pass/PassContext.h>
#include <Pass/RenderPass.h>

#include "RenderUI.h"

namespace Spark::Render
{
    void UIPass::SetUp(PassContext& ctx, RenderUI& renderUI)
    {
        SPARK_RENDER_PASS(ctx, "UIPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .Build([](RenderPassScopes& p)
            {
                RHI::AttachmentLoadStoreAction load;
                load.m_loadAction  = RHI::AttachmentLoadAction::Load;
                load.m_storeAction = RHI::AttachmentStoreAction::Store;
                p.Scope().RenderTarget(RHI::AttachmentId("SwapChain"), load);
            })
            .Execute([&renderUI](ExecuteWork& work, RenderGraphExecuter&)
            {
                renderUI.Render(work.m_commandList);
            })
            .Finalize();
    }
}
