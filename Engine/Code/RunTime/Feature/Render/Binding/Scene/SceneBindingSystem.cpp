#include "SceneBindingSystem.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <CoreComponents/Tags.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderBuilder.h>

#include <Shader/ShaderBindingsUtils.h>

#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>

#include <ECS/Common.h>

#include <Light/Components.h>
#include <Skybox/Components.h>

#include <View/View.h>
#include <View/ViewTags.h>
#include <View/ViewComponents.h>
#include <View/ShadowAtlasLayout.h>

#include <Pass/Component/RHIComponents.h>   // CreateStaticImageAttachment

#include "RenderGraph/RenderGraphUtils.h"
#include "SceneBinding.h"

namespace Spark::Render
{
    namespace
    {
        constexpr const char* LightBufferName      = "g_Lights";
        constexpr const char* LightCountName       = "g_LightCount";
        constexpr const char* ShadowViewBufferName = "g_ShadowViews";
        constexpr const char* ShadowTexelSizeName  = "g_ShadowAtlasTexelSize";
        constexpr const char* IrradianceCubeName   = "g_IrradianceCube";
        constexpr const char* PrefilteredCubeName  = "g_PrefilteredCube";
        constexpr const char* BRDFLutName          = "g_BRDFLut";
        constexpr const char* IBLSamplerName       = "g_IBLSampler";
        constexpr const char* PrefilteredMipsName  = "g_IBLPrefilteredMipCount";
        constexpr const char* EnvIntensityName     = "g_EnvIntensity";

        //! Baked offline by SandBox BRDFLutGen and checked in; see BRDFLutBake.hlsl.
        constexpr const char* BRDFLutAssetPath     = "Image/BRDFLut.ktx2";
    }

    void SceneBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "[SceneBindingSystem] AssetManager is unregistered.");

        // SceneBindingsReflect.hlsl is a reflection host (#includes SceneBindings.hlsl + a
        // dummy vertex entry reading g_Lights / g_LightCount) so we can reflect the space0
        // layout here — mirrors ViewBindingSystem / ViewBindingsReflect.hlsl.
        const Resource::AssetId assetId = assetManager->MakeAssetId("Shaders/SceneBindingsReflect.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[SceneBindingSystem] Failed to resolve SceneBindingsReflect.hlsl asset id.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);
        if (!shaderAsset)
        {
            LOG_ERROR("[SceneBindingSystem] Failed to load SceneBindingsReflect.hlsl.");
            return;
        }

        Resource::ShaderInputBuildResult built = Resource::BuildShaderInputList(*shaderAsset);
        if (built.stageMask == RHI::ShaderStageMask::None)
        {
            LOG_ERROR("[SceneBindingSystem] SceneBindings.hlsl produced no shader inputs.");
            return;
        }

        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[SceneBindingSystem] RHI::RHIInterface service not registered.");
        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();
        ASSERT(factory && device, "[SceneBindingSystem] RHI factory or device is null.");

        Ptr<RHI::PipelineLayoutDescriptor> layout = factory->CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(built.list, built.stageMask);
        layout->Finalize();

        Ptr<RHI::ShaderBindings> sceneBindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = 0;   // SceneBindings (per-scene) is reserved at space0.
        if (sceneBindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[SceneBindingSystem] ShaderBindings::Init failed.");
            return;
        }

        m_bindings = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            m_bindings, RHI::Components::ShaderBindings{ sceneBindings });
        rhiCtx.Add<MainSceneTag>(m_bindings);

        // A constant dropped by DXC logs every frame, but a dropped image stays silent —
        // images are only bound when something needs them. Catch it once, here.
        for (const char* name : {IrradianceCubeName, PrefilteredCubeName, BRDFLutName})
        {
            if (!sceneBindings->FindImageInput(RHI::InputName(name)))
            {
                LOG_ERROR("[SceneBindingSystem] '{}' is missing from the reflected space0 "
                          "layout — add a use of it to SceneBindingsReflect.hlsl.", name);
            }
        }
        if (!sceneBindings->FindSamplerInput(RHI::InputName(IBLSamplerName)))
        {
            LOG_ERROR("[SceneBindingSystem] '{}' is missing from the reflected space0 "
                      "layout — add a use of it to SceneBindingsReflect.hlsl.", IBLSamplerName);
        }
        // Marks the group live: every early return above is silent, and downstream just
        // no-ops on a null binding handle.
        LOG_INFO("[SceneBindingSystem] space0 layout reflected (lights + environment IBL).");

        // Both arrays land in the space0 group created above, so they share m_bindings and
        // differ only by input name and capacity. Materialized on the next frame begin
        // (deferred PendingBufferInit path, inside StagedArrayBuffer::Init).
        {
            StagedArrayBuffer<LightData>::Descriptor lightDesc;
            lightDesc.m_capacity       = Capacity;
            lightDesc.m_resourceName   = ObjectName(LightBufferName);
            lightDesc.m_inputName      = RHI::InputName(LightBufferName);
            lightDesc.m_bindingsEntity = m_bindings;
            m_lights.Init(rhiCtx, lightDesc);
        }
        {
            StagedArrayBuffer<ShadowViewData>::Descriptor shadowDesc;
            shadowDesc.m_capacity       = kShadowViewCapacity;
            shadowDesc.m_resourceName   = ObjectName(ShadowViewBufferName);
            shadowDesc.m_inputName      = RHI::InputName(ShadowViewBufferName);
            shadowDesc.m_bindingsEntity = m_bindings;
            m_shadowViews.Init(rhiCtx, shadowDesc);
        }

        CreateBRDFLut(rhiCtx);
    }

    void SceneBindingSystem::CreateBRDFLut(RHI::RHIContext& rhiCtx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        const Resource::AssetId id = assetManager->MakeAssetId(BRDFLutAssetPath);
        if (!id.IsValid())
        {
            LOG_ERROR("[SceneBindingSystem] '{}' not found in any search path; IBL will fall "
                      "back to constant ambient. Regenerate it with the BRDFLutGen tool.",
                      BRDFLutAssetPath);
            return;
        }

        // A .ktx2 is loaded already compiled, so this is a plain deserialize -- no mip
        // generation, no BCn, and the RG16F format comes from the file rather than the
        // descriptor.
        Ptr<Resource::ImageAsset> lut = assetManager->LoadAsset<Resource::ImageAsset>(id);
        const Resource::ImageAssetData* data = lut ? lut->GetImageData() : nullptr;
        if (!lut || lut->GetStatus() != Resource::AssetStatus::Ready || !data
            || data->GetTextureBytes().empty())
        {
            LOG_ERROR("[SceneBindingSystem] Failed to load the BRDF LUT '{}'.", BRDFLutAssetPath);
            return;
        }

        RHI::ImageDescriptor desc = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite,
            lut->GetWidth(), lut->GetHeight(), lut->GetFormat());
        desc.m_mipLevels       = static_cast<uint16_t>(lut->GetMipLevels());
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        // StaticImportTag: sampled every frame, never an attachment, so the compiler emits
        // its one-time CopyDst->ShaderRead barrier -- same treatment as the IBL cubes.
        m_brdfLut = RHI::CreateStaticImage(
            rhiCtx, ObjectName("BRDFLut"), desc,
            RHI::HeapMemoryLevel::Device, RHI::HostMemoryAccess::Write);

        RHI::RequestImageUpload(
            rhiCtx, m_brdfLut, data->GetTextureBytes().data(), data->GetTextureBytes().size(),
            RHI::ImageSubresourceRange(desc), RHI::Origin(), lut->GetFormat());
    }

    void SceneBindingSystem::PackShadowViews(RHI::RHIContext& rhiCtx)
    {
        for (uint32_t row = 0; row < m_shadowViews.Capacity(); ++row)
        {
            m_shadowViews[row] = ShadowViewData{};
        }

        rhiCtx.GetView<ShadowViewTag, View, ShadowViewIndex>(Exclude<DeadTag>).each(
            [&](RHI::RHIHandle, const View& view, const ShadowViewIndex& row)
        {
            if (row.m_index >= kShadowViewCapacity)
            {
                return;
            }

            ShadowViewData& d = m_shadowViews[row.m_index];
            d.m_worldToShadowUV = MakeShadowUVRemap(view.m_rect) * view.GetWorldToClip();
            d.m_uvMinMax = Math::Vector4(
                view.m_rect.m_minX, view.m_rect.m_minY, view.m_rect.m_maxX, view.m_rect.m_maxY);

            // [0][0] is 1/tan(halfFov) under a perspective projection and 1/halfExtent under
            // an orthographic one, so this one expression is the world width NDC [-1,1] spans
            // in either — divided by the texels it lands on, which the rect gives directly.
            const float texels = (view.m_rect.m_maxX - view.m_rect.m_minX)
                               * static_cast<float>(kShadowAtlasResolution);
            const float scaleX = view.m_viewToClip[0][0];
            d.m_texelWorldSizePerW =
                (scaleX != 0.0f && texels > 0.0f) ? 2.0f / (scaleX * texels) : 0.0f;
        });
    }

    uint32_t SceneBindingSystem::PackLightData(WorldContext& world, bool shadowViewsBound)
    {
        // Dense marshal: every live world LightRenderData -> m_lights[slot]. Pure field
        // copy — direction/position were already resolved by LightSystem (compute vs write).
        //
        // DeadTag is excluded because EntityReaper destroys on its own tick, not at the
        // kill site: between the two, the entity still carries LightRenderData. Without
        // this the light keeps lighting the scene after deletion, and this loop disagrees
        // with ShadowViewSystem — which does exclude it — about whether the light exists.
        uint32_t slot = 0;
        world.GetView<Light::LightRenderData>(Exclude<DeadTag>).each(
            [&](Entity entity, const Light::LightRenderData& rd)
        {
            if (slot >= m_lights.Capacity())
            {
                LOG_ERROR("[SceneBindingSystem] Capacity={} overflow; dropping light.",
                          m_lights.Capacity());
                return;
            }

            LightData& d   = m_lights[slot];
            d.m_direction  = rd.m_worldDirection;
            d.m_intensity  = rd.m_intensity;
            d.m_color      = rd.m_color;
            d.m_type       = static_cast<uint32_t>(rd.m_type);
            d.m_position   = rd.m_worldPosition;
            d.m_invRange   = rd.m_range > 0.0f ? 1.0f / rd.m_range : 0.0f;
            d.m_cosInner   = rd.m_cosInner;
            d.m_cosOuter   = rd.m_cosOuter;

            // Read from the light, not from the view entity: g_Lights is packed by
            // iteration order while the row is not, so this is the one place the two
            // index spaces meet.
            const auto* refs      = world.TryGet<ShadowViewRefs>(entity);
            d.m_shadowIndex       = refs ? refs->m_baseIndex : -1;
            d.m_shadowFaceCount   = refs ? static_cast<uint32_t>(refs->m_views.size()) : 1;

            // Every row the light owns, since the authored bias applies to all its faces
            // and a face without a tile keeps its row inverted by PackShadowViews.
            for (uint32_t face = 0; shadowViewsBound && d.m_shadowIndex >= 0
                    && face < d.m_shadowFaceCount; ++face)
            {
                const uint32_t row = static_cast<uint32_t>(d.m_shadowIndex) + face;
                if (row >= m_shadowViews.Capacity())
                {
                    break;
                }
                ShadowViewData& sv       = m_shadowViews[row];
                sv.m_depthBias           = rd.m_shadowBias;
                sv.m_normalOffsetTexels  = rd.m_shadowNormalOffsetTexels;
                sv.m_pcfRadiusTexels     =
                    0.5f * static_cast<float>(Light::ShadowFilterFootprint(rd.m_shadowFilterWidth));
            }
            ++slot;
        });

        return slot;
    }

    SceneBindingSystem::EnvironmentBinding SceneBindingSystem::BindEnvironmentIBL()
    {
        EnvironmentBinding result;

        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx || m_bindings == RHI::NullHandle)
        {
            return result;
        }

        // Keyed off the same tag SkyboxPass imports by, so the visible sky and the
        // environment lighting can never come from two different HDRIs.
        Entity active = NullEntity;
        world->GetView<Skybox::SkyboxGPUComponent>().each(
            [&](Entity entity, const Skybox::SkyboxGPUComponent& gpu)
        {
            if (gpu.m_cubemap != RHI::NullHandle
                && rhiCtx->Has<Skybox::ActiveSkyCubeTag>(gpu.m_cubemap))
            {
                active = entity;
            }
        });
        if (active == NullEntity)
        {
            return result;
        }

        // Straight off the user-facing component: no GPU resource, so it applies from the
        // first frame and costs no rebuild when the slider moves.
        if (const auto* comp = world->TryGet<Skybox::SkyboxComponent>(active))
        {
            result.m_intensity = comp->m_intensity;
        }

        const auto* gpu = world->TryGet<Skybox::SkyboxGPUComponent>(active);
        if (!gpu)
        {
            return result;
        }

        // All or nothing: a partially bound set would leave some slot stale while the mip
        // count says "go ahead and sample". The LUT joins the same gate -- without it the
        // specular term has no F0 scale/bias to apply.
        if (!IsResourceReady(*rhiCtx, gpu->m_irradiance)
            || !IsResourceReady(*rhiCtx, gpu->m_prefiltered)
            || !IsResourceReady(*rhiCtx, m_brdfLut))
        {
            return result;
        }

        auto* irradianceImage  = rhiCtx->TryGet<RHI::Components::Image>(gpu->m_irradiance);
        auto* prefilteredImage = rhiCtx->TryGet<RHI::Components::Image>(gpu->m_prefiltered);
        auto* brdfLutImage     = rhiCtx->TryGet<RHI::Components::Image>(m_brdfLut);
        if (!irradianceImage || !prefilteredImage || !brdfLutImage)
        {
            return result;
        }

        // Register the upload->shader-read attachment at the SAMPLE POINT, like material
        // textures do. The static-barrier compiler keys on ImagePassAttachment, not on
        // StaticImportTag, so without this these images get neither the state transition
        // nor the wait on the upload fence: they stay in CopyWrite on the copy queue and
        // only read correctly because DX12 decays copy-queue resources to COMMON and
        // re-promotes them on first shader read. Vulkan has neither behaviour.
        auto RegisterStaticShaderRead = [&](RHI::RHIHandle handle)
        {
            if (!rhiCtx->Has<ImagePassAttachment>(handle))
            {
                CreateStaticImageAttachment(*rhiCtx, handle,
                    rhiCtx->Get<RHI::ResourceName>(handle).m_name,
                    RHI::AttachmentAccess::Read,
                    RHI::AttachmentUsage::Shader,
                    RHI::AttachmentStage::FragmentShader);
            }
        };
        RegisterStaticShaderRead(gpu->m_irradiance);
        RegisterStaticShaderRead(gpu->m_prefiltered);
        RegisterStaticShaderRead(m_brdfLut);

        const RHI::ImageViewDescriptor cubeView = RHI::ImageViewDescriptor::CreateCubemap();
        RHI::ImageView* irradianceView = RHI::GetOrCreateImageView(
            *rhiCtx, gpu->m_irradiance, *irradianceImage->m_image, cubeView);
        RHI::ImageView* prefilteredView = RHI::GetOrCreateImageView(
            *rhiCtx, gpu->m_prefiltered, *prefilteredImage->m_image, cubeView);
        RHI::ImageView* brdfLutView = RHI::GetOrCreateImageView(
            *rhiCtx, m_brdfLut, *brdfLutImage->m_image, RHI::ImageViewDescriptor{});
        if (!irradianceView || !prefilteredView || !brdfLutView)
        {
            return result;
        }

        SetShaderSampler(m_bindings, RHI::InputName(IBLSamplerName),
            RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear,
                                      RHI::AddressMode::Clamp));
        SetShaderImage(m_bindings, RHI::InputName(IrradianceCubeName), irradianceView);
        SetShaderImage(m_bindings, RHI::InputName(PrefilteredCubeName), prefilteredView);
        SetShaderImage(m_bindings, RHI::InputName(BRDFLutName), brdfLutView);

        // From the live image, not a bake-time constant, so a re-bake at a different mip
        // count needs no change here.
        result.m_prefilteredMipCount =
            prefilteredImage->m_image->GetDescriptor().m_mipLevels;
        return result;
    }

    void SceneBindingSystem::Update(uint32_t frameIndex)
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx || m_bindings == RHI::NullHandle)
        {
            return;
        }

        // Only scatter into g_Lights once this frame's copy is materialized (one warmup
        // frame after Init). The count constant is written UNCONDITIONALLY below — even on
        // the warmup frame — so the SceneConstants CBV always has a compiled GPU address
        // when the SRG binds. The CBV bind is unguarded in CommandList (unlike the SRV
        // table, which is skipped when unbound), so a missing address trips a hard assert.
        // Before the light loop: that loop adds each light's authored bias to the entry its
        // m_shadowIndex points at, and PackShadowViews zeroes the array first.
        const bool shadowViewsBound = m_shadowViews.BindFrame(*rhiCtx, frameIndex);
        if (shadowViewsBound)
        {
            PackShadowViews(*rhiCtx);
        }

        uint32_t lightCount = 0;
        if (m_lights.BindFrame(*rhiCtx, frameIndex))
        {
            lightCount = PackLightData(*world, shadowViewsBound);
            m_lights.Upload(*rhiCtx, lightCount);
        }

        // After the light loop, which is what completes the entries.
        if (shadowViewsBound)
        {
            m_shadowViews.Upload(*rhiCtx, m_shadowViews.Capacity());
        }

        // A per-scene constant, not a per-view one: an atlas texel is 1/resolution in UV
        // whatever tile it falls in, and stays so when tiles stop being uniform.
        SetShaderConstant(m_bindings, RHI::InputName(ShadowTexelSizeName),
            1.0f / static_cast<float>(kShadowAtlasResolution));

        // g_LightCount rides the SceneConstants cbuffer in the same SRG. Written every
        // frame (0 during warmup) so the CBV is always compiled; SetShaderConstant marks
        // the binding dirty. The shader loop is empty while count is 0.
        SetShaderConstant(m_bindings, RHI::InputName(LightCountName), lightCount);

        // Same unconditional-write rule as g_LightCount; 0 mips is also the shader's gate,
        // since the cubes share g_Lights' table and "unbound" there means stale, not null.
        const EnvironmentBinding env = BindEnvironmentIBL();
        SetShaderConstant(m_bindings, RHI::InputName(PrefilteredMipsName), env.m_prefilteredMipCount);
        SetShaderConstant(m_bindings, RHI::InputName(EnvIntensityName), env.m_intensity);
    }

    void SceneBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        m_lights.Shutdown(rhiCtx);
        m_shadowViews.Shutdown(rhiCtx);

        if (m_bindings != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindings); }
        if (m_brdfLut  != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_brdfLut); }

        m_bindings = RHI::NullHandle;
        m_brdfLut  = RHI::NullHandle;
    }
}
