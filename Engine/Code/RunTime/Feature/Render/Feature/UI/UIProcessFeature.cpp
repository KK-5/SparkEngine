#include "UIProcessFeature.h"

#include <EASTL/vector.h>

#include <ECS/Common.h>
#include <Service/Service.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Fence/Fence.h>

#include <UI/ImGui/Components.h>

#include "RenderUI.h"
#include "RenderUIInterface.h"

namespace Spark::Render
{
    void UIProcessFeature::Process()
    {
        auto* world = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx)
        {
            return;
        }

        auto* renderUI = static_cast<RenderUI*>(Service<RenderUIInterface>::Get());
        if (!renderUI || !renderUI->m_rhiImGUi)
        {
            return;
        }

        // Collect icons whose RHI resources are ready but not yet registered with ImGui.
        eastl::vector<const RHI::ImageView*> pendingViews;
        eastl::vector<Entity>                pendingEntities;

        auto view = world->GetView<UI::IconGPUComponent, UI::IconComponent>();
        view.each([&](Entity entity, UI::IconGPUComponent& gpu, const UI::IconComponent& /*icon*/)
        {
            if (gpu.m_iconId != ImTextureID_Invalid)
            {
                return;
            }

            auto* viewComp = rhiCtx->TryGet<RHI::Components::ImageView>(gpu.m_imageView);
            if (!viewComp || !viewComp->m_view)
            {
                return;
            }

            if (auto* sync = rhiCtx->TryGet<RHI::PendingSync>(gpu.m_image))
            {
                if (!(sync->m_fence
                    && sync->m_fence->GetCompletedValue() >= sync->m_fenceValue))
                {
                    return;
                }
            }

            pendingViews.push_back(viewComp->m_view.get());
            pendingEntities.push_back(entity);
        });

        if (pendingViews.empty())
        {
            return;
        }

        eastl::vector<ImTextureID> texIds;
        renderUI->m_rhiImGUi->RegisterTextures(pendingViews, texIds);

        for (size_t i = 0; i < pendingEntities.size(); ++i)
        {
            auto* gpu = world->TryGet<UI::IconGPUComponent>(pendingEntities[i]);
            if (gpu)
            {
                gpu->m_iconId = texIds[i];
            }
        }
    }
}
