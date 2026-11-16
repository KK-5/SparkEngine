#include "UIPass.h"

#include <RHI/HardwareQueue.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>
#include <RHI/Context/RHIContext.h>

#include <CoreComponents/Tags.h>
#include <ECS/WorldContext.h>

#include <UI/ImGui/Components.h>

#include <Binding/ShaderUsage.h>

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
                // The UI owns its icon images; the pass that samples them says how they are
                // read, the same rule the scene's images follow. Every frame, as an icon may
                // have been made since the last one.
                auto* world = WorldExecuteContext::Current();
                if (world != nullptr)
                {
                    auto& rhiContext = *RHI::RHIExecuteContext::Current();
                    world->GetView<UI::IconGPUComponent>().each(
                        [&](Entity, const UI::IconGPUComponent& icon)
                        {
                            const RHI::RHIHandle image = icon.m_image.Get();
                            if (image != RHI::NullHandle && !rhiContext.Has<DeadTag>(image))
                            {
                                rhiContext.AddOrReplace<ShaderImageUsage>(image, ShaderImageUsage{
                                    ShaderInputBinding::Bindless(), icon.m_viewDesc });
                            }
                        });
                }

                RHI::AttachmentLoadStoreAction load;
                load.m_loadAction  = RHI::AttachmentLoadAction::Load;
                load.m_storeAction = RHI::AttachmentStoreAction::Store;

                // The UI draws in .Execute, where nothing names what it samples: the icons are
                // read as a set, so their uploads are acquired here and their accesses carry
                // the views published above.
                p.ImportSharedSet<UI::IconImageTag>(RHI::AttachmentId("Icons"));

                auto s = p.Scope();
                s.RenderTarget(RHI::AttachmentId("SwapChain"), load);
                s.ReadSet(RHI::AttachmentId("Icons")).Stage(RHI::AttachmentStage::FragmentShader);
                s.Close();
            })
            .Execute([&renderUI](ExecuteWork& work, RenderGraphExecuter&)
            {
                renderUI.Render(work.m_commandList);
            })
            .Finalize();
    }
}
