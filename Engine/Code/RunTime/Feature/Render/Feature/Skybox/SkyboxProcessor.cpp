#include "SkyboxProcessor.h"

#include <ECS/WorldContext.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Command/DrawArguments.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>

#include <Pass/PassContext.h>
#include <Pass/PassTag.h>
#include <Pass/PassAccess.h>
#include <Pass/Component/RHIComponents.h>   // CreateStaticImageAttachment

#include <Drawable/Drawable.h>

#include <Skybox/Components.h>

namespace Spark::Render
{
    void SkyboxProcessor::Init()
    {
        auto* rhiCtx  = RHI::RHIExecuteContext::Current();
        auto* passCtx = PassExecuteContext::Current();
        if (!rhiCtx || !passCtx)
        {
            return;
        }

        // Procedural full-screen triangle (DrawLinear(3)), no per-object data. Classified
        // with this pass's own PassTag: DeriveDrawItems routes it to exactly the SkyboxPass
        // route (.Accepts<SkyboxPassTag>()) and no other pass. DerivedDrawItems is the
        // reverse ref DrawableComposer teardown reaps through.
        m_drawable = rhiCtx->CreateEntity();
        Drawable drawable;
        drawable.m_drawArgs      = RHI::DrawArguments(RHI::DrawLinear(3, 0));
        drawable.m_instanceCount = 1;
        drawable.m_instanceData  = NoInstanceBinding{};
        rhiCtx->Add<Drawable>(m_drawable, eastl::move(drawable));
        rhiCtx->Add<DrawableTag>(m_drawable);
        rhiCtx->Add<SPARK_PASS_TAG("SkyboxPass")>(m_drawable);
        rhiCtx->Add<DerivedDrawItems>(m_drawable, DerivedDrawItems{});

        // Allocate the cube SRG (space2) now so it exists before the OnTick bind loop
        // (BindPassDrawItems resolves it by PassTag). Process fills its sampler/cube slots.
        GetOrCreatePassShaderBindings<SPARK_PASS_TAG("SkyboxPass")>(*passCtx, *rhiCtx, 2);
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

    void SkyboxProcessor::Process()
    {
        auto* rhiCtx  = RHI::RHIExecuteContext::Current();
        auto* passCtx = PassExecuteContext::Current();
        auto* world   = WorldExecuteContext::Current();
        if (!rhiCtx || !passCtx || !world)
        {
            return;
        }

        // Refresh this pass's space2 cube SRG in place — BindPassDrawItems already baked its
        // pointer onto the DrawItem. The cube is a static import, not a graph transient, so
        // it is resolved here in OnTick rather than a Compile hook. Sampler applied once; the
        // cube view's redundant re-binds are dropped by SetShaderImage's change-detection. No
        // cube yet → g_SkyCube stays unbound and the sky renders black until it materializes.
        RHI::ImageView* cubeView = GetCubeImageView();
        if (!m_samplerApplied)
        {
            m_samplerApplied = SetPassShaderSampler<SPARK_PASS_TAG("SkyboxPass")>(
                2, RHI::InputName("g_SkySampler"),
                RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Clamp));
        }
        SetPassShaderImage<SPARK_PASS_TAG("SkyboxPass")>(2, RHI::InputName("g_SkyCube"), cubeView);
    }
}
