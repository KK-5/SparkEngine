#include "UIProcessFeature.h"

#include <EASTL/vector.h>

#include <ECS/Common.h>
#include <Service/Service.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Origin.h>
#include <RHI/Command/CopyItem.h>

#include <Pass/PassTag.h>
#include <Request/CopyRequest.h>

#include <UI/ImGui/Components.h>
#include <UI/UIBaseSystem.h>

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

        BuildCopyFrameBufferPassRequest(*rhiCtx);

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

            // Resolve the icon's view from the image resource's view cache. A missing
            // Image component means the resource is not materialized yet — skip.
            auto* imgComp = rhiCtx->TryGet<RHI::Components::Image>(gpu.m_image);
            if (!imgComp || !imgComp->m_image)
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

            auto* imageView = RHI::GetOrCreateImageView(
                *rhiCtx, gpu.m_image, *imgComp->m_image, gpu.m_viewDesc);
            if (!imageView)
            {
                return;
            }

            pendingViews.push_back(imageView);
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

    void UIProcessFeature::BuildCopyFrameBufferPassRequest(RHI::RHIContext& rhiCtx)
    {
        // Every scalar copy parameter is settled here, on the business side. The
        // pass only resolves the source/dest resources by slot name and never
        // reverse-derives anything from them. This blits the full scene color
        // (size = render/framebuffer size) into the UI framebuffer position.
        Math::Vector2Int fbPos{};
        Math::Vector2Int fbSize{};
        if (auto* ui = Service<UI::UIBaseSystem>::Get())
        {
            fbPos  = ui->GetFrameBufferPos();
            fbSize = ui->GetFrameBufferSize();
        }

        // Spell out every field, even the defaulted ones, so the copy's full
        // intent is on the page and not implied by aggregate zero-init.
        RHI::CopyImageDescriptor imageCopy{};
        // Source/destination images are resolved by the pass from the slot names.
        imageCopy.m_sourceImage            = nullptr;
        imageCopy.m_destinationImage       = nullptr;
        // Full-image blit: the whole mip0/array0 subresource, from the top-left.
        imageCopy.m_sourceSubresource      = RHI::ImageSubresource{};
        imageCopy.m_sourceOrigin           = RHI::Origin{0, 0, 0};
        imageCopy.m_sourceSize             = RHI::Size{static_cast<uint32_t>(fbSize.x), static_cast<uint32_t>(fbSize.y), 1};
        imageCopy.m_destinationSubresource = RHI::ImageSubresource{};
        imageCopy.m_destinationOrigin      = RHI::Origin{static_cast<uint32_t>(fbPos.x), static_cast<uint32_t>(fbPos.y), 0};

        // Create the request entity once and tag it with the owning pass; on every
        // later frame only refresh the template (origin/size can change on resize).
        if (m_copyRequestEntity == RHI::NullHandle || !rhiCtx.Valid(m_copyRequestEntity))
        {
            m_copyRequestEntity = rhiCtx.CreateEntity();

            CopyRequest req;
            req.m_sourceSlot = RHI::InputName("CopySource");
            req.m_destSlot   = RHI::InputName("CopyWrite");
            req.m_template   = RHI::CopyItem(imageCopy);

            rhiCtx.Add<CopyRequest>(m_copyRequestEntity, eastl::move(req));
            rhiCtx.Add<SPARK_PASS_TAG("CopyFrameBufferPass")>(m_copyRequestEntity);
        }
        else
        {
            rhiCtx.Get<CopyRequest>(m_copyRequestEntity).m_template = RHI::CopyItem(imageCopy);
        }
    }
}
