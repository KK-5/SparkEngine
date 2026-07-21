#include "SkyboxProcessor.h"

#include <EASTL/optional.h>

#include <ECS/Common.h>
#include <ECS/WorldContext.h>
#include <CoreComponents/Tags.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/DrawArguments.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/PassAccess.h>
#include <Pass/Component/RHIComponents.h>   // CreateStaticImageAttachment

#include <Drawable/Drawable.h>
#include <Request/DrawRequest.h>

#include <Shader/ShaderBindingsUtils.h>
#include <View/ViewTags.h>

#include <Skybox/Components.h>

namespace Spark::Render
{
    void SkyboxProcessor::Init()
    {
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            return;
        }

        // DrawRequest carrier: a dedicated entity tagged for this pass. Its DrawItem is
        // produced by CompileDrawRequests each frame, exactly like every other pass — the
        // cube ShaderBindings (space2) are created lazily in Process, once the pass has a
        // reflected PassPipelineLayout.
        m_drawRequest = rhiCtx->CreateEntity();
        rhiCtx->Add<SPARK_PASS_TAG("SkyboxPass")>(m_drawRequest);

        // Procedural Drawable: a vertex-less full-screen triangle (DrawLinear(3)) with no
        // per-object data. It holds the Drawable COMPONENT but no DrawableTag, so the
        // generic AssembleDrawRequests never sweeps it into other passes — this pass owns
        // it, and CompileDrawRequests still resolves it via TryGet<Drawable>.
        m_drawable = rhiCtx->CreateEntity();
        Drawable drawable;
        drawable.m_drawArgs      = RHI::DrawArguments(RHI::DrawLinear(3, 0));
        drawable.m_instanceCount = 1;
        drawable.m_instanceData  = NoInstanceBinding{};
        rhiCtx->Add<Drawable>(m_drawable, eastl::move(drawable));
    }

    void SkyboxProcessor::Shutdown()
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

    RHI::ImageView* SkyboxProcessor::GetCubeImageView()
    {
        auto& world = *WorldExecuteContext::Current();
        auto& rhiCtx = *RHI::RHIExecuteContext::Current();

        RHI::RHIHandle cubeHandle = RHI::NullHandle;
        world.GetView<Skybox::SkyboxGPUComponent>().each(
        [&](Entity, const Skybox::SkyboxGPUComponent& gpu)
        {
            if (gpu.m_cubemap != RHI::NullHandle)
            {
                cubeHandle = gpu.m_cubemap;
            }
        });

        if (cubeHandle == RHI::NullHandle)
        {
            return nullptr;
        }

        auto* imgComp = rhiCtx.TryGet<RHI::Components::Image>(cubeHandle);
        if (!imgComp || !imgComp->m_image)
        {
            return nullptr;
        }

        // Static-import barrier registration lives HERE (moved off SkyboxSystem to
        // sever the feature→SparkRender reverse dependency): render registers the
        // cube's upload→shader-read attachment at its sampling point. One-time via the
        // Has-check; slot name is unused by the static-barrier path, so the resource's
        // own ResourceName stands in.
        if (!rhiCtx.Has<ImagePassAttachment>(cubeHandle))
        {
            CreateStaticImageAttachment(rhiCtx, cubeHandle,
                rhiCtx.Get<RHI::ResourceName>(cubeHandle).m_name,
                RHI::AttachmentAccess::Read,
                RHI::AttachmentUsage::Shader,
                RHI::AttachmentStage::FragmentShader);
        }

        RHI::ImageView* cubeView = RHI::GetOrCreateImageView(
            rhiCtx, cubeHandle, *imgComp->m_image, RHI::ImageViewDescriptor::CreateCubemap());
        if (!cubeView)
        {
            return nullptr;
        }

        return cubeView;
    }

    void SkyboxProcessor::Process(const Math::Vector2Int& renderSize)
    {
        auto* rhiCtx  = RHI::RHIExecuteContext::Current();
        auto* passCtx = PassExecuteContext::Current();
        auto* world   = WorldExecuteContext::Current();
        if (!rhiCtx || !passCtx || !world)
        {
            return;
        }

        // Build this frame's DrawRequest, or nothing if any prerequisite (pass layout,
        // environment cube, view bindings) isn't ready. The request only REFERENCES the
        // binding entities; the cube SRV/sampler content is updated here as a resource,
        // change-detected, and the sky geometry/draw args come from the procedural
        // Drawable. Every "not ready" path is a plain `return {}`.
        auto BuildRequest = [&]() -> eastl::optional<DrawRequest>
        {
            // Find the environment cube published by SkyboxSystem (world component).
            RHI::ImageView* cubeView = GetCubeImageView();

            // space2 cube SRG: get-or-created + bound entirely inside SetPassShaderXxx —
            // no handle held here. Sampler applied once; the cube view's redundant
            // re-binds are dropped by SetShaderImage's own change-detection. A false
            // return means the pass layout isn't reflected yet, so retry next frame.
            if (!m_samplerApplied)
            {
                m_samplerApplied = SetPassShaderSampler<SPARK_PASS_TAG("SkyboxPass")>(
                    2, RHI::InputName("g_SkySampler"),
                    RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp));
            }
            if (!SetPassShaderImage<SPARK_PASS_TAG("SkyboxPass")>(2, RHI::InputName("g_SkyCube"), cubeView))
            {
                return {};
            }

            // The request references binding ENTITIES (CompileDrawRequests resolves them
            // to SRG pointers); it never touches their content. Bindings self-describe
            // their space, so order is irrelevant. Draw args come from the Drawable.
            DrawRequest req;
            req.m_drawable = m_drawable;
            // Shared view (space1) + this pass's cube SRG (space2), both injected by
            // tag. A zero view count means the view SRG isn't up yet; drop this frame.
            // The cube SRG was just ensured above, so it appends ≥1.
            if (AddShaderBindings<MainViewTag>(req, *rhiCtx) == 0)
            {
                return {};
            }
            AddShaderBindings<SPARK_PASS_TAG("SkyboxPass")>(req, *rhiCtx);

            req.m_viewports.resize(1);
            req.m_viewports[0]   = RHI::Viewport{ 0, static_cast<float>(renderSize.x), 0, static_cast<float>(renderSize.y) };
            req.m_viewportsCount = 1;
            req.m_scissors.resize(1);
            req.m_scissors[0]    = RHI::Scissor{ 0, 0, renderSize.x, renderSize.y };
            req.m_scissorsCount  = 1;

            return req;
        };

        // Commit or drop, exactly once. On drop, clear both the request and any DrawItem
        // an earlier frame compiled: m_drawEntity is persistent (unlike mesh requests it
        // is not destroyed each frame), so a stale full-screen draw would otherwise linger
        // and sample a cube/bindings that may no longer be valid.
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
