#include "DepthPreProcessor.h"

#include <EASTL/vector.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/Component/RHIComponents.h>

#include <Drawable/Drawable.h>
#include <Request/DrawRequest.h>
#include <Resource/Shader/ShaderAsset.h>

#include <View/ViewTags.h>

namespace Spark::Render
{
    void DepthPreProcessor::Shutdown(PassContext& /*passCtx*/)
    {
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            return;
        }
        eastl::vector<RHIHandle> stale;
        rhiCtx->GetView<DrawRequest, SPARK_PASS_TAG("DepthPrePass")>().each(
            [&](RHIHandle entity, const DrawRequest&) { stale.push_back(entity); });
        for (RHIHandle entity : stale)
        {
            rhiCtx->DestoryEntity(entity);
        }
    }

    void DepthPreProcessor::Init(PassContext& /*passCtx*/, RHI::RHIContext& /*rhiCtx*/)
    {
    }

    void DepthPreProcessor::Process(const Math::Vector2Int& renderSize)
    {
        auto* rhiCtx  = RHI::RHIExecuteContext::Current();
        auto* passCtx = PassExecuteContext::Current();
        if (!rhiCtx || !passCtx)
        {
            return;
        }

        // Reap last frame's DrawRequests (their DrawItems were consumed already).
        {
            eastl::vector<RHIHandle> stale;
            rhiCtx->GetView<DrawRequest, SPARK_PASS_TAG("DepthPrePass")>().each(
                [&](RHIHandle entity, const DrawRequest&) { stale.push_back(entity); });
            for (RHIHandle entity : stale)
            {
                rhiCtx->DestoryEntity(entity);
            }
        }

        // Per-pass view binding (space0). Fresh lookup, never long-term cached.
        RHIHandle viewBindingEntity = RHI::NullHandle;
        rhiCtx->GetView<MainViewTag, RHI::Components::ShaderBindings>().each(
            [&](RHIHandle e, const RHI::Components::ShaderBindings&) { viewBindingEntity = e; });
        if (viewBindingEntity == RHI::NullHandle)
        {
            return;
        }

        // One DrawRequest per Drawable. Each request is just (Drawable handle,
        // Pass injection) — geometry/instance/binding flow through Drawable.
        rhiCtx->GetView<DrawableTag>(Exclude<DeadTag>).each(
            [&](RHIHandle drawable)
        {
            RHIHandle drawRequestEntity = rhiCtx->CreateEntity();

            DrawRequest req;
            req.m_drawable = drawable;
            req.m_shaderBindings.push_back(viewBindingEntity);

            req.m_viewports.resize(1);
            req.m_scissors.resize(1);
            req.m_viewportsCount = 1;
            req.m_viewports[0]   = RHI::Viewport{
                0, static_cast<float>(renderSize.x), 0, static_cast<float>(renderSize.y) };
            req.m_scissorsCount  = 1;
            req.m_scissors[0]    = RHI::Scissor{ 0, 0, renderSize.x, renderSize.y };

            rhiCtx->Add<DrawRequest>(drawRequestEntity, eastl::move(req));
            rhiCtx->Add<SPARK_PASS_TAG("DepthPrePass")>(drawRequestEntity);
        });
    }
}
