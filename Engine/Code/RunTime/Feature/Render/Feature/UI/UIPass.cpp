#include "UIPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>

#include <UI/ImGui/Components.h>

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

                auto s = p.Scope();
                s.RenderTarget(RHI::AttachmentId("SwapChain"), load);
                // The UI draws in .Execute, where nothing names what it samples.
                s.Uses<UI::IconImageTag>();
                s.Close();
            })
            .Execute([&renderUI](ExecuteWork& work, RenderGraphExecuter&)
            {
                renderUI.Render(work.m_commandList);
            })
            .Finalize();
    }
}
