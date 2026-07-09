#include "LightingProcessor.h"

#include <EASTL/optional.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Command/DrawArguments.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/PassAccess.h>

#include <Drawable/Drawable.h>
#include <Request/DrawRequest.h>

#include <Shader/ShaderBindingsUtils.h>
#include <View/ViewTags.h>

namespace Spark::Render
{
    void LightingProcessor::Init()
    {
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            return;
        }

        // Persistent draw-request carrier, tagged for this pass. Its DrawItem is
        // produced by CompileDrawRequests each frame like every other pass.
        m_drawRequest = rhiCtx->CreateEntity();
        rhiCtx->Add<SPARK_PASS_TAG("LightingPass")>(m_drawRequest);

        // Procedural full-screen triangle (DrawLinear(3)), no per-object data. Holds the
        // Drawable component but no DrawableTag, so the generic AssembleDrawRequests never
        // sweeps it into other passes — this pass owns it.
        m_drawable = rhiCtx->CreateEntity();
        Drawable drawable;
        drawable.m_drawArgs      = RHI::DrawArguments(RHI::DrawLinear(3, 0));
        drawable.m_instanceCount = 1;
        drawable.m_instanceData  = NoInstanceBinding{};
        rhiCtx->Add<Drawable>(m_drawable, eastl::move(drawable));
    }

    void LightingProcessor::Shutdown()
    {
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            return;
        }
        if (rhiCtx->Valid(m_drawRequest))
        {
            rhiCtx->Add<DeadTag>(m_drawRequest);
        }
        if (rhiCtx->Valid(m_drawable))
        {
            rhiCtx->Add<DeadTag>(m_drawable);
        }
    }

    void LightingProcessor::Process(const Math::Vector2Int& renderSize)
    {
        auto* rhiCtx  = RHI::RHIExecuteContext::Current();
        auto* passCtx = PassExecuteContext::Current();
        if (!rhiCtx || !passCtx)
        {
            return;
        }

        auto BuildRequest = [&]() -> eastl::optional<DrawRequest>
        {
            DrawRequest req;
            req.m_drawable = m_drawable;
            // Shared view (space0): the shader reads g_InvView for the camera position.
            // A zero view count means the view SRG isn't up yet; drop this frame.
            if (AddShaderBindings<MainViewTag>(req, *rhiCtx) == 0)
            {
                return {};
            }
            // This pass's GBuffer SRG (space1), just ensured above — appends 1.
            AddShaderBindings<SPARK_PASS_TAG("LightingPass")>(req, *rhiCtx);

            req.m_viewports.resize(1);
            req.m_viewports[0]   = RHI::Viewport{ 0, static_cast<float>(renderSize.x), 0, static_cast<float>(renderSize.y) };
            req.m_viewportsCount = 1;
            req.m_scissors.resize(1);
            req.m_scissors[0]    = RHI::Scissor{ 0, 0, renderSize.x, renderSize.y };
            req.m_scissorsCount  = 1;

            return req;
        };

        // Commit or drop, exactly once. On drop, clear any DrawItem an earlier frame
        // compiled: m_drawRequest is persistent, so a stale full-screen draw would
        // otherwise linger and sample bindings that may no longer be valid.
        if (auto req = BuildRequest())
        {
            rhiCtx->AddOrReplace<DrawRequest>(m_drawRequest, eastl::move(*req));
        }
        else
        {
            if (rhiCtx->Has<DrawRequest>(m_drawRequest))
            {
                rhiCtx->Remove<DrawRequest>(m_drawRequest);
            }
            if (rhiCtx->Has<RHI::DrawItem>(m_drawRequest))
            {
                rhiCtx->Remove<RHI::DrawItem>(m_drawRequest);
            }
        }
    }
}
