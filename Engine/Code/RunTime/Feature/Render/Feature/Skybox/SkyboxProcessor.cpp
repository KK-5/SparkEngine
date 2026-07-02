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
        // The DrawItem carrier: a dedicated entity tagged for this pass. The cube
        // ShaderBindings (space1) are created lazily in Process, once the pass has a
        // reflected PassPipelineLayout.
        m_drawEntity = rhiCtx->CreateEntity();
        rhiCtx->Add<SPARK_PASS_TAG("SkyboxPass")>(m_drawEntity);
    }

    void SkyboxProcessor::Shutdown()
    {
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            return;
        }
        if (rhiCtx->Valid(m_drawEntity))
        {
            rhiCtx->Add<DeadTag>(m_drawEntity);
        }
        if (rhiCtx->Valid(m_cubeBindings))
        {
            rhiCtx->Add<DeadTag>(m_cubeBindings);
        }
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

        // Build this frame's full-screen DrawItem, or nothing if any prerequisite (pass
        // layout, environment cube, view bindings) isn't ready yet. Every "not ready" path
        // is a plain `return {}`; the single commit-or-drop decision lives below.
        auto BuildItem = [&]() -> eastl::optional<RHI::DrawItem>
        {
            // Lazily create the space1 cube bindings once the pass layout is reflected.
            if (m_cubeBindings == RHI::NullHandle)
            {
                m_cubeBindings = CreatePassShaderBindings<SPARK_PASS_TAG("SkyboxPass")>(*passCtx, *rhiCtx, 1);
                if (m_cubeBindings == RHI::NullHandle)
                {
                    return {}; // layout not ready yet; retry next frame
                }
            }

            // Find the environment cube published by SkyboxSystem (world component; read
            // directly like InstanceBindingSystem reads Mesh::MeshGPUComponent).
            RHI::RHIHandle cubeHandle = RHI::NullHandle;
            world->GetView<Skybox::SkyboxGPUComponent>().each(
                [&](Entity, const Skybox::SkyboxGPUComponent& gpu)
            {
                if (gpu.m_cubemap != RHI::NullHandle)
                {
                    cubeHandle = gpu.m_cubemap;
                }
            });
            if (cubeHandle == RHI::NullHandle)
            {
                return {}; // no cube yet -> clear color shows
            }

            // Cube SRV + sampler into space1.
            auto* imgComp = rhiCtx->TryGet<RHI::Components::Image>(cubeHandle);
            if (!imgComp || !imgComp->m_image)
            {
                return {};
            }
            RHI::ImageView* cubeView = RHI::GetOrCreateImageView(
                *rhiCtx, cubeHandle, *imgComp->m_image, RHI::ImageViewDescriptor::CreateCubemap());
            if (!cubeView)
            {
                return {};
            }
            SetShaderImage(m_cubeBindings, RHI::InputName("g_SkyCube"), cubeView);
            SetShaderSampler(m_cubeBindings, RHI::InputName("g_SkySampler"),
                RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp));

            // Shared view bindings (space0) — same entity DepthPre uses.
            RHI::RHIHandle viewBindingEntity = RHI::NullHandle;
            rhiCtx->GetView<MainViewTag, RHI::Components::ShaderBindings>().each(
                [&](RHI::RHIHandle e, const RHI::Components::ShaderBindings&) { viewBindingEntity = e; });
            if (viewBindingEntity == RHI::NullHandle)
            {
                return {};
            }

            // Assemble the full-screen DrawItem: DrawLinear(3), view + cube SRGs, PSO left
            // null (auto-bound by the executer). Bindings self-describe their space, so order
            // is irrelevant.
            RHI::DrawItem item;
            item.m_drawArguments = RHI::DrawArguments(RHI::DrawLinear(3, 0));

            for (const RHI::RHIHandle bindingEntity : { viewBindingEntity, m_cubeBindings })
            {
                if (auto* sb = rhiCtx->TryGet<RHI::Components::ShaderBindings>(bindingEntity);
                    sb && sb->m_bindings)
                {
                    item.m_shaderBindings.push_back(sb->m_bindings.get());
                }
            }
            item.m_shaderBindingsCount = static_cast<uint8_t>(item.m_shaderBindings.size());

            item.m_viewports.resize(1);
            item.m_viewports[0]   = RHI::Viewport{ 0, static_cast<float>(renderSize.x), 0, static_cast<float>(renderSize.y) };
            item.m_viewportsCount = 1;
            item.m_scissors.resize(1);
            item.m_scissors[0]    = RHI::Scissor{ 0, 0, renderSize.x, renderSize.y };
            item.m_scissorsCount  = 1;

            return item;
        };

        // Commit or drop, exactly once: emit the fresh DrawItem, otherwise make sure a
        // stale one from an earlier frame doesn't linger (it would sample a cube/bindings
        // that may no longer be valid).
        if (auto item = BuildItem())
        {
            rhiCtx->AddOrReplace<RHI::DrawItem>(m_drawEntity, eastl::move(*item));
        }
        else if (rhiCtx->Has<RHI::DrawItem>(m_drawEntity))
        {
            rhiCtx->Remove<RHI::DrawItem>(m_drawEntity);
        }
    }
}
