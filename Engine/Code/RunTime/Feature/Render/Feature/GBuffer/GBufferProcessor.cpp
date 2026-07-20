#include "GBufferProcessor.h"

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>

#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/PassAccess.h>
#include <Pass/Component/RHIComponents.h>

#include <Drawable/Drawable.h>
#include <Request/DrawRequest.h>
#include <Request/DrawRequestAssemble.h>
#include <Resource/Shader/ShaderAsset.h>

#include <Shader/ShaderBindingsUtils.h>
#include <View/ViewTags.h>
#include <MaterialBind/MaterialBinding.h>

namespace Spark::Render
{
    void GBufferProcessor::Shutdown()
    {
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            return;
        }

        // Mark this pass's DrawRequests for reap. Drawables themselves are reaped by
        // DrawableComposer; the PassTag marker goes with the DrawRequest entity.
        rhiCtx->GetView<SPARK_PASS_TAG("GBufferPass"), DrawRequest>(Exclude<DeadTag>)
            .each([&](RHIHandle request, const DrawRequest&)
        {
            rhiCtx->Add<DeadTag>(request);
        });
    }

    void GBufferProcessor::Init()
    {
    }

    void GBufferProcessor::Process(const Math::Vector2Int& renderSize)
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

        AssembleDrawRequests<SPARK_PASS_TAG("GBufferPass")>();

        SetPassShaderSampler<SPARK_PASS_TAG("GBufferPass")>(
            2, RHI::InputName("g_MatSampler"),
            RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Wrap));

        rhiCtx->GetView<SPARK_PASS_TAG("GBufferPass"), DrawRequest>(Exclude<DeadTag>)
            .each([&](RHIHandle, DrawRequest& req)
        {
            req.m_shaderBindings.clear();
            AddShaderBindings<MainViewTag>(req, *rhiCtx);
            AddShaderBindings<MaterialBindingTag>(req, *rhiCtx);   // g_Materials @ space3
            AddShaderBindings<SPARK_PASS_TAG("GBufferPass")>(req, *rhiCtx);   // g_MatSampler @ space2

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
