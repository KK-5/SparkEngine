#include "DepthPreProcessor.h"

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
#include <Request/DrawRequestAssemble.h>
#include <Shader/ShaderBindingsUtils.h>
#include <Resource/Shader/ShaderAsset.h>

#include <View/ViewTags.h>

namespace Spark::Render
{
    void DepthPreProcessor::Shutdown()
    {
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            return;
        }

        // Mark this Pass's DrawRequests for reap. Drawables themselves are
        // reaped by DrawableComposer::Shutdown (called next), and the PassTag
        // marker on Drawable entities goes with them when the entity is
        // destroyed — no need to strip it here.
        rhiCtx->GetView<SPARK_PASS_TAG("DepthPrePass"), DrawRequest>(Exclude<DeadTag>)
            .each([&](RHIHandle request, const DrawRequest&)
        {
            rhiCtx->Add<DeadTag>(request);
        });
    }

    void DepthPreProcessor::Init()
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

        if (rhiCtx->GetView<MainViewTag, RHI::Components::ShaderBindings>().size_hint() == 0)
        {
            return;
        }


        AssembleDrawRequests<SPARK_PASS_TAG("DepthPrePass")>();

        rhiCtx->GetView<SPARK_PASS_TAG("DepthPrePass"), DrawRequest>(Exclude<DeadTag>)
            .each([&](RHIHandle, DrawRequest& req)
        {
            req.m_shaderBindings.clear();
            AddShaderBindings<MainViewTag>(req, *rhiCtx);

            req.m_viewports.resize(1);
            req.m_viewports[0]   = RHI::Viewport{
                0, static_cast<float>(renderSize.x), 0, static_cast<float>(renderSize.y) };
            req.m_viewportsCount = 1;

            req.m_scissors.resize(1);
            req.m_scissors[0]    = RHI::Scissor{ 0, 0, renderSize.x, renderSize.y };
            req.m_scissorsCount  = 1;
        });
    }
}
