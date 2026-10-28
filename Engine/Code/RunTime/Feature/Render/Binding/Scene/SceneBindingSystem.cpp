#include "SceneBindingSystem.h"

#include <string.h>

#include <EASTL/algorithm.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <CoreComponents/Tags.h>

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>

#include <RHI/RHIInterface.h>
#include <RHI/Component/Component.h>
#include <RHI/Device/Device.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAsset.h>

#include <Shader/ShaderBindingsUtils.h>
#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>
#include <Binding/View/ViewBinding.h>

#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>

#include <ECS/Common.h>

#include <Light/Components.h>
#include <Skybox/Components.h>

#include <View/View.h>
#include <View/ViewTags.h>
#include <View/ViewComponents.h>
#include <View/ShadowMapLayout.h>
#include <View/ShadowMaskLayout.h>

#include <Pass/Component/RHIComponents.h>

#include "RenderGraph/RenderGraphUtils.h"
#include "SceneBinding.h"

namespace Spark::Render
{
    namespace
    {
        constexpr const char* LightBufferName      = "g_Lights";
        constexpr const char* LightCountName       = "g_LightCount";
        constexpr const char* ShadowViewBufferName = "g_ShadowViews";
        constexpr const char* ViewShadowMapBufferName = "g_ViewShadowMaps";
        constexpr const char* ShadowTexelSizeName  = "g_ShadowMapTexelSize";
        constexpr const char* IrradianceCubeName   = "g_IrradianceCube";
        constexpr const char* PrefilteredCubeName  = "g_PrefilteredCube";
        constexpr const char* BRDFLutName          = "g_BRDFLut";
        constexpr const char* IBLSamplerName       = "g_IBLSampler";
        constexpr const char* PrefilteredMipsName  = "g_IBLPrefilteredMipCount";
        constexpr const char* EnvIntensityName     = "g_EnvIntensity";
        constexpr const char* FrameNumberName      = "g_SceneFrameNumber";
        constexpr const char* GameTimeName         = "g_SceneGameTime";
        constexpr const char* PrevGameTimeName     = "g_ScenePrevGameTime";
        constexpr const char* DeltaTimeName        = "g_SceneDeltaTime";
        constexpr const char* LTC1Name             = "g_LTC1";
        constexpr const char* LTC2Name             = "g_LTC2";
        constexpr const char* AreaLightLutReadyName = "g_AreaLightLutReady";

        //! Baked offline by SandBox BRDFLutGen and checked in; see BRDFLutBake.hlsl.
        constexpr const char* BRDFLutAssetPath     = "engine://Image/BRDFLut.ktx2";

        //! Imported by SandBox LTCLutGen and checked in.
        constexpr const char* LTC1AssetPath        = "engine://Image/LTC1.ktx2";
        constexpr const char* LTC2AssetPath        = "engine://Image/LTC2.ktx2";

        //! Copies `records` into this frame's buffer of `staging` in one go and binds `input` to
        //! a view of the whole buffer, so each buffer gets one view whatever the count. Room for
        //! one record at least, so the view is never empty. False when the buffer could not
        //! grow: then `input` is bound to nothing.
        template<typename Record>
        bool WriteAndBind(RHI::RHIContext& rhiCtx, StagingBuffer& staging, const eastl::vector<Record>& records,
            RHI::RHIHandle bindings, const char* input)
        {
            const uint64_t byteCount = eastl::max<uint64_t>(records.size(), 1) * sizeof(Record);
            staging.Begin(byteCount);
            const StagingBuffer::Allocation allocation = staging.Allocate(byteCount);
            RHI::Buffer* buffer = staging.GetBuffer();
            if (!allocation.m_data || !buffer)
            {
                SetShaderBuffer(bindings, RHI::InputName(input), nullptr);
                return false;
            }
            if (!records.empty())
            {
                memcpy(allocation.m_data, records.data(), records.size() * sizeof(Record));
            }

            const RHI::BufferViewDescriptor viewDesc = RHI::BufferViewDescriptor::CreateStructured(
                0, static_cast<uint32_t>(buffer->GetDescriptor().m_byteCount / sizeof(Record)), sizeof(Record));
            RHI::BufferView* view = RHI::GetOrCreateBufferView(rhiCtx, staging.GetResource(), *buffer, viewDesc);
            SetShaderBuffer(bindings, RHI::InputName(input), view);
            return view != nullptr;
        }
    }

    void SceneBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        // The layout of SceneBindings.hlsli, declared rather than reflected (see SharedBindings.h).
        struct BufferInput
        {
            const char* m_name;
            uint32_t    m_register;
        };
        struct ImageInput
        {
            const char*               m_name;
            RHI::ShaderInputImageType m_type;
            uint32_t                  m_register;
        };
        constexpr BufferInput Buffers[] = {
            { LightBufferName,         0 },
            { ShadowViewBufferName,    4 },
            { ViewShadowMapBufferName, 7 },
        };
        constexpr ImageInput Images[] = {
            { IrradianceCubeName,  RHI::ShaderInputImageType::ImageCube, 1 },
            { PrefilteredCubeName, RHI::ShaderInputImageType::ImageCube, 2 },
            { BRDFLutName,         RHI::ShaderInputImageType::Image2D,   3 },
            { LTC1Name,            RHI::ShaderInputImageType::Image2D,   5 },
            { LTC2Name,            RHI::ShaderInputImageType::Image2D,   6 },
        };

        RHI::ShaderInputList inputs;
        for (const BufferInput& buffer : Buffers)
        {
            inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
                RHI::InputName(buffer.m_name), RHI::ShaderInputBufferAccess::Read,
                RHI::ShaderInputBufferType::Structured, 1, 0, buffer.m_register, kSceneSpaceId));
        }
        for (const ImageInput& image : Images)
        {
            inputs.m_images.push_back(RHI::ShaderInputImageDescriptor(
                RHI::InputName(image.m_name), RHI::ShaderInputImageAccess::Read,
                image.m_type, 1, image.m_register, kSceneSpaceId));
        }
        inputs.m_samplers.push_back(RHI::ShaderInputSamplerDescriptor(RHI::InputName(IBLSamplerName), 1, 0, kSceneSpaceId));

        // cbuffer SceneConstants : register(b0), every member a 4-byte scalar.
        uint32_t constantOffset = 0;
        for (const char* name : { LightCountName, PrefilteredMipsName, EnvIntensityName, ShadowTexelSizeName,
                                  FrameNumberName, GameTimeName, PrevGameTimeName, DeltaTimeName,
                                  AreaLightLutReadyName })
        {
            inputs.m_constants.push_back(RHI::ShaderInputConstantDescriptor(
                RHI::InputName(name), constantOffset, 4, 0, kSceneSpaceId));
            constantOffset += 4;
        }

        Ptr<RHI::ShaderBindings> sceneBindings = CreateSharedBindings(inputs, kSceneSpaceId, "SceneBindingSystem");
        if (!sceneBindings)
        {
            return;
        }

        m_bindings = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            m_bindings, RHI::Components::ShaderBindings{ sceneBindings });
        rhiCtx.Add<MainSceneTag>(m_bindings);

        // The arrays land in the space0 group created above, read by shaders straight from
        // host memory.
        RHI::Device& device = *Service<RHI::RHIInterface>::Get()->GetDevice();
        {
            StagingBuffer::Descriptor lightDesc;
            lightDesc.m_name      = ObjectName(LightBufferName);
            lightDesc.m_bindFlags = RHI::BufferBindFlags::ShaderRead;
            m_lightBuffer.Init(device, lightDesc);
        }
        {
            StagingBuffer::Descriptor shadowDesc;
            shadowDesc.m_name            = ObjectName(ShadowViewBufferName);
            shadowDesc.m_bindFlags       = RHI::BufferBindFlags::ShaderRead;
            shadowDesc.m_initialCapacity = uint64_t(kShadowMapCount) * sizeof(ShadowViewData);
            m_shadowViewBuffer.Init(device, shadowDesc);
        }
        {
            StagingBuffer::Descriptor viewDesc;
            viewDesc.m_name      = ObjectName(ViewShadowMapBufferName);
            viewDesc.m_bindFlags = RHI::BufferBindFlags::ShaderRead;
            m_viewShadowMapBuffer.Init(device, viewDesc);
        }

        // Without the first, IBL falls back to constant ambient; without the other two, rect
        // lights contribute nothing.
        m_brdfLut = CreateLut(rhiCtx, BRDFLutAssetPath, "BRDFLut");
        m_ltc1    = CreateLut(rhiCtx, LTC1AssetPath, "LTC1");
        m_ltc2    = CreateLut(rhiCtx, LTC2AssetPath, "LTC2");
    }

    RHI::RHIHandle SceneBindingSystem::CreateLut(
        RHI::RHIContext& rhiCtx, const char* assetPath, const char* name)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        const Resource::AssetId id = assetManager->MakeAssetId(assetPath);
        if (!id.IsValid())
        {
            LOG_ERROR("[SceneBindingSystem] '{}' could not be resolved. Regenerate it with "
                      "the tool that writes it (SandBox BRDFLutGen / LTCLutGen).", assetPath);
            return RHI::NullHandle;
        }

        // A .ktx2 is loaded already compiled, so this is a plain deserialize -- no mip
        // generation, no BCn, and the format comes from the file rather than the descriptor.
        Ptr<Resource::ImageAsset> lut = assetManager->LoadAsset<Resource::ImageAsset>(id);
        const Resource::ImageAssetData* data = lut ? lut->GetImageData() : nullptr;
        if (!lut || lut->GetStatus() != Resource::AssetStatus::Ready || !data
            || data->GetTextureBytes().empty())
        {
            LOG_ERROR("[SceneBindingSystem] Failed to load the table '{}'.", assetPath);
            return RHI::NullHandle;
        }

        RHI::ImageDescriptor desc = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite,
            lut->GetWidth(), lut->GetHeight(), lut->GetFormat());
        desc.m_mipLevels       = static_cast<uint16_t>(lut->GetMipLevels());
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        // Sampled every frame through space0, like the IBL cubes: the passes that bind it
        // declare it by its tag.
        const RHI::RHIHandle handle = RHI::CreateImage(
            rhiCtx, ObjectName(name), desc,
            RHI::HeapMemoryLevel::Device, RHI::HostMemoryAccess::Write);
        rhiCtx.Add<SceneLookupTableTag>(handle);

        RHI::RequestImageUpload(
            rhiCtx, handle, data->GetTextureBytes().data(), data->GetTextureBytes().size(),
            RHI::ImageSubresourceRange(desc), RHI::Origin(), lut->GetFormat());
        return handle;
    }

    void SceneBindingSystem::PackShadowViews(WorldContext& world, RHI::RHIContext& rhiCtx)
    {
        m_shadowViews.assign(kShadowMapCount, ShadowViewData{});
        m_viewShadowMaps.clear();

        world.GetView<ShadowMaps, ShadowViewRefs, Light::LightRenderData>(Exclude<DeadTag>).each(
            [&](const ShadowMaps& maps, const ShadowViewRefs& refs, const Light::LightRenderData& rd)
        {
            const float extent = ShadowMapExtent(maps.m_level);
            const float texels = static_cast<float>(ShadowResolution(maps.m_level));
            for (uint32_t i = 0; i < refs.m_views.size(); ++i)
            {
                const RHI::RHIHandle viewHandle = refs.m_views[i].Get();
                const uint32_t       index      = maps.First() + i;

                if (const auto* row = rhiCtx.TryGet<ViewIndex>(viewHandle))
                {
                    if (row->m_index >= m_viewShadowMaps.size())
                    {
                        m_viewShadowMaps.resize(row->m_index + 1, 0);
                    }
                    m_viewShadowMaps[row->m_index] = index;
                }

                // Not committed this frame: its row stays a hole, read as unshadowed.
                if (rhiCtx.Has<ViewInactiveTag>(viewHandle))
                {
                    continue;
                }

                const View&     view = rhiCtx.Get<View>(viewHandle);
                ShadowViewData& d    = m_shadowViews[index];
                d.m_worldToShadowUV  = ShadowClipToUV(extent) * view.GetWorldToClip();
                d.m_uvMax            = extent;

                // [0][0] is 1/tan(halfFov) under a perspective projection and 1/halfExtent under
                // an orthographic one, so this one expression is the world width NDC [-1,1]
                // spans in either — divided by the texels it lands on.
                const float scaleX     = view.m_viewToClip[0][0];
                d.m_texelWorldSizePerW = scaleX != 0.0f ? 2.0f / (scaleX * texels) : 0.0f;

                d.m_depthBias          = rd.m_shadowBias;
                d.m_normalOffsetTexels = rd.m_shadowNormalOffsetTexels;
                d.m_pcfRadiusTexels    =
                    0.5f * static_cast<float>(Light::ShadowFilterFootprint(rd.m_shadowFilterWidth));
            }
        });
    }

    void SceneBindingSystem::PackLightData(WorldContext& world)
    {
        // Dense marshal: every live world LightRenderData -> a record. Pure field copy —
        // direction/position were already resolved by LightSystem (compute vs write).
        //
        // DeadTag is excluded because EntityReaper destroys on its own tick, not at the
        // kill site: between the two, the entity still carries LightRenderData. Without
        // this the light keeps lighting the scene after deletion, and this loop disagrees
        // with ShadowViewSystem — which does exclude it — about whether the light exists.
        m_lights.clear();
        world.GetView<Light::LightRenderData>(Exclude<DeadTag>).each(
            [&](Entity entity, const Light::LightRenderData& rd)
        {
            LightData& d   = m_lights.push_back();
            d.m_direction  = rd.m_worldDirection;
            d.m_intensity  = rd.m_intensity;
            d.m_color      = rd.m_color;
            d.m_type       = static_cast<uint32_t>(rd.m_type);
            d.m_position   = rd.m_worldPosition;
            d.m_invRange   = rd.m_range > 0.0f ? 1.0f / rd.m_range : 0.0f;
            d.m_cosInner   = rd.m_cosInner;
            d.m_cosOuter   = rd.m_cosOuter;
            d.m_right      = rd.m_worldRight;
            d.m_halfWidth  = rd.m_halfWidth;
            d.m_halfHeight = rd.m_halfHeight;

            // Read from the light: g_Lights is packed by iteration order while the shadow maps
            // are not, so this is the one place the two index spaces meet.
            const auto* maps      = world.TryGet<ShadowMaps>(entity);
            const auto* refs      = world.TryGet<ShadowViewRefs>(entity);
            d.m_shadowIndex       = maps ? static_cast<int32_t>(maps->First()) : -1;
            d.m_shadowMapCount    = refs ? static_cast<uint32_t>(refs->m_views.size()) : 1;

            const auto* maskSlot  = world.TryGet<ShadowMaskSlot>(entity);
            d.m_shadowMaskIndex   = maskSlot ? static_cast<int32_t>(maskSlot->m_index) : -1;
        });
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
            if (gpu.m_cubemap.IsValid()
                && rhiCtx->Has<Skybox::ActiveSkyCubeTag>(gpu.m_cubemap.Get()))
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
        const RHI::RHIHandle irradiance  = gpu->m_irradiance.Get();
        const RHI::RHIHandle prefiltered = gpu->m_prefiltered.Get();

        // All or nothing: a partially bound set would leave some slot stale while the mip
        // count says "go ahead and sample". The LUT joins the same gate -- without it the
        // specular term has no F0 scale/bias to apply.
        if (!RHI::IsResourceReady(*rhiCtx, irradiance)
            || !RHI::IsResourceReady(*rhiCtx, prefiltered)
            || !RHI::IsResourceReady(*rhiCtx, m_brdfLut))
        {
            return result;
        }

        auto* irradianceImage  = rhiCtx->TryGet<RHI::Components::Image>(irradiance);
        auto* prefilteredImage = rhiCtx->TryGet<RHI::Components::Image>(prefiltered);
        auto* brdfLutImage     = rhiCtx->TryGet<RHI::Components::Image>(m_brdfLut);
        if (!irradianceImage || !prefilteredImage || !brdfLutImage)
        {
            return result;
        }

        const RHI::ImageViewDescriptor cubeView = RHI::ImageViewDescriptor::CreateCubemap();
        RHI::ImageView* irradianceView = RHI::GetOrCreateImageView(
            *rhiCtx, irradiance, *irradianceImage->m_image, cubeView);
        RHI::ImageView* prefilteredView = RHI::GetOrCreateImageView(
            *rhiCtx, prefiltered, *prefilteredImage->m_image, cubeView);
        RHI::ImageView* brdfLutView = RHI::GetOrCreateImageView(
            *rhiCtx, m_brdfLut, *brdfLutImage->m_image, RHI::ImageViewDescriptor{});
        if (!irradianceView || !prefilteredView || !brdfLutView)
        {
            return result;
        }

        SetShaderImage(m_bindings, RHI::InputName(IrradianceCubeName), irradianceView);
        SetShaderImage(m_bindings, RHI::InputName(PrefilteredCubeName), prefilteredView);
        SetShaderImage(m_bindings, RHI::InputName(BRDFLutName), brdfLutView);

        // From the live image, not a bake-time constant, so a re-bake at a different mip
        // count needs no change here.
        result.m_prefilteredMipCount =
            prefilteredImage->m_image->GetDescriptor().m_mipLevels;
        return result;
    }

    bool SceneBindingSystem::BindAreaLightLuts(RHI::RHIContext& rhiCtx)
    {
        // Both or neither, for the reason BindEnvironmentIBL gives.
        if (!RHI::IsResourceReady(rhiCtx, m_ltc1) || !RHI::IsResourceReady(rhiCtx, m_ltc2))
        {
            return false;
        }

        auto* ltc1Image = rhiCtx.TryGet<RHI::Components::Image>(m_ltc1);
        auto* ltc2Image = rhiCtx.TryGet<RHI::Components::Image>(m_ltc2);
        if (!ltc1Image || !ltc2Image)
        {
            return false;
        }

        RHI::ImageView* ltc1View = RHI::GetOrCreateImageView(
            rhiCtx, m_ltc1, *ltc1Image->m_image, RHI::ImageViewDescriptor{});
        RHI::ImageView* ltc2View = RHI::GetOrCreateImageView(
            rhiCtx, m_ltc2, *ltc2Image->m_image, RHI::ImageViewDescriptor{});
        if (!ltc1View || !ltc2View)
        {
            return false;
        }

        SetShaderImage(m_bindings, RHI::InputName(LTC1Name), ltc1View);
        SetShaderImage(m_bindings, RHI::InputName(LTC2Name), ltc2View);
        return true;
    }

    void SceneBindingSystem::Update(const FrameTime& time)
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx || m_bindings == RHI::NullHandle)
        {
            return;
        }

        PackShadowViews(*world, *rhiCtx);
        PackLightData(*world);
        WriteAndBind(*rhiCtx, m_shadowViewBuffer, m_shadowViews, m_bindings, ShadowViewBufferName);
        WriteAndBind(*rhiCtx, m_viewShadowMapBuffer, m_viewShadowMaps, m_bindings, ViewShadowMapBufferName);
        const uint32_t lightCount =
            WriteAndBind(*rhiCtx, m_lightBuffer, m_lights, m_bindings, LightBufferName)
                ? static_cast<uint32_t>(m_lights.size()) : 0;

        // A per-scene constant, not a per-view one: every layer is as wide, whatever the
        // resolution of the shadow map in it.
        SetShaderConstant(m_bindings, RHI::InputName(ShadowTexelSizeName),
            1.0f / static_cast<float>(kShadowMapResolution));

        // g_LightCount rides the SceneConstants cbuffer in the same SRG. Written every frame
        // so the CBV is always compiled: its bind is unguarded in CommandList, unlike the SRV
        // table's, and a missing address trips a hard assert. The shader loop is empty while
        // count is 0.
        SetShaderConstant(m_bindings, RHI::InputName(LightCountName), lightCount);

        // Same unconditional-write rule as g_LightCount; 0 mips is also the shader's gate,
        // since the cubes share g_Lights' table and "unbound" there means stale, not null.
        const EnvironmentBinding env = BindEnvironmentIBL();
        SetShaderConstant(m_bindings, RHI::InputName(PrefilteredMipsName), env.m_prefilteredMipCount);
        SetShaderConstant(m_bindings, RHI::InputName(EnvIntensityName), env.m_intensity);

        // Not inside either gate: the cubes and the LTC tables are read through it, and
        // each can be bound without the other.
        SetShaderSampler(m_bindings, RHI::InputName(IBLSamplerName),
            RHI::SamplerState::Create(RHI::FilterMode::Linear, RHI::FilterMode::Linear,
                                      RHI::AddressMode::Clamp));

        const uint32_t areaLightLutReady = BindAreaLightLuts(*rhiCtx) ? 1u : 0u;
        SetShaderConstant(m_bindings, RHI::InputName(AreaLightLutReadyName), areaLightLutReady);

        // Doubles on the CPU so they stay exact across a long session; the shader only ever
        // needs the low bits, so the narrowing is deliberate.
        SetShaderConstant(m_bindings, RHI::InputName(FrameNumberName),
            static_cast<uint32_t>(time.m_frameNumber));
        SetShaderConstant(m_bindings, RHI::InputName(GameTimeName),
            static_cast<float>(time.m_gameTime));
        SetShaderConstant(m_bindings, RHI::InputName(PrevGameTimeName),
            static_cast<float>(time.m_prevGameTime));
        SetShaderConstant(m_bindings, RHI::InputName(DeltaTimeName), time.m_deltaTime);
    }

    void SceneBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        // The views hold the buffers, which go before their pools.
        if (m_bindings != RHI::NullHandle)
        {
            SetShaderBuffer(m_bindings, RHI::InputName(LightBufferName), nullptr);
            SetShaderBuffer(m_bindings, RHI::InputName(ShadowViewBufferName), nullptr);
            SetShaderBuffer(m_bindings, RHI::InputName(ViewShadowMapBufferName), nullptr);
        }
        m_lightBuffer.Shutdown();
        m_shadowViewBuffer.Shutdown();
        m_viewShadowMapBuffer.Shutdown();

        if (m_bindings != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindings); }
        if (m_brdfLut  != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_brdfLut); }
        if (m_ltc1     != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_ltc1); }
        if (m_ltc2     != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_ltc2); }

        m_bindings = RHI::NullHandle;
        m_brdfLut  = RHI::NullHandle;
        m_ltc1     = RHI::NullHandle;
        m_ltc2     = RHI::NullHandle;
    }
}
