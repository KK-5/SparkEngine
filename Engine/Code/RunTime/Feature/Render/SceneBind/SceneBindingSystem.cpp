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
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Image/ImageAsset.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderBuilder.h>

#include <Shader/ShaderBindingsUtils.h>

#include <RHI/Resource/Image/Image.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>

#include <Light/Components.h>
#include <Skybox/Components.h>

#include <Pass/Component/RHIComponents.h>   // CreateStaticImageAttachment

#include "RenderGraph/RenderGraphUtils.h"
#include "SceneBinding.h"

namespace Spark::Render
{
    namespace
    {
        constexpr const char* LightBufferName      = "g_Lights";
        constexpr const char* LightCountName       = "g_LightCount";
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

        // g_Lights: host-visible StructuredBuffer, PER-FRAME (PerFrameTag -> N copies) so
        // each frame writes its own copy without racing the GPU reading last frame's. Filled
        // each frame from m_lightData via PendingBufferMap. Materialized on the next frame
        // begin (deferred PendingBufferInit path, same as g_Materials).
        m_lightData.resize(Capacity);
        m_buffer = rhiCtx.CreateEntity();
        {
            RHI::BufferDescriptor bufDesc;
            bufDesc.m_byteCount = static_cast<uint64_t>(Capacity) * sizeof(LightData);
            bufDesc.m_bindFlags = RHI::BufferBindFlags::ShaderRead | RHI::BufferBindFlags::CopyRead;

            RHI::PendingBufferInit init;
            init.m_descriptor       = bufDesc;
            init.m_heapMemoryLevel  = RHI::HeapMemoryLevel::Host;
            init.m_hostMemoryAccess = RHI::HostMemoryAccess::Write;
            rhiCtx.Add<RHI::PendingBufferInit>(m_buffer, init);
            rhiCtx.Add<RHI::PerFrameTag>(m_buffer);
            rhiCtx.Add<RHI::ResourceName>(m_buffer, RHI::ResourceName{ ObjectName("g_Lights") });
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

    bool SceneBindingSystem::BindFrameLights(uint32_t frameIndex)
    {
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx || m_buffer == RHI::NullHandle || m_bindings == RHI::NullHandle)
        {
            return false;
        }

        // Wait for RHIResourceSystem to materialize the per-frame buffers (one frame after Init).
        auto* perFrame = rhiCtx->TryGet<RHI::Components::BufferPerFrame>(m_buffer);
        if (!perFrame || !perFrame->m_buffers[frameIndex])
        {
            return false;
        }
        RHI::Buffer* buffer = perFrame->m_buffers[frameIndex].get();

        // Re-bind the SRV to THIS frame's copy (the N copies live at distinct addresses),
        // one cheap descriptor recompile via the dirty mark — like g_Materials.
        const RHI::BufferViewDescriptor viewDesc =
            RHI::BufferViewDescriptor::CreateStructured(0, Capacity, sizeof(LightData));
        RHI::BufferView* view = RHI::GetOrCreateBufferViewPerFrame(
            *rhiCtx, m_buffer, *buffer, viewDesc, frameIndex);
        if (!view)
        {
            LOG_ERROR("[SceneBindingSystem] Failed to create g_Lights structured SRV for frame {}.", frameIndex);
            return false;
        }

        SetShaderBuffer(m_bindings, RHI::InputName(LightBufferName), view);
        return true;
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
        uint32_t slot = 0;
        if (BindFrameLights(frameIndex))
        {
            // Dense marshal: every world LightRenderData -> g_Lights[slot]. Pure field copy
            // — direction/position were already resolved by LightSystem (compute vs write).
            world->GetView<Light::LightRenderData>().each(
                [&](Entity, const Light::LightRenderData& rd)
            {
                if (slot >= Capacity)
                {
                    LOG_ERROR("[SceneBindingSystem] Capacity={} overflow; dropping light.", Capacity);
                    return;
                }

                LightData& d   = m_lightData[slot];
                d.m_direction  = rd.m_worldDirection;
                d.m_intensity  = rd.m_intensity;
                d.m_color      = rd.m_color;
                d.m_type       = static_cast<uint32_t>(rd.m_type);
                d.m_position   = rd.m_worldPosition;
                d.m_invRange   = rd.m_range > 0.0f ? 1.0f / rd.m_range : 0.0f;
                d.m_cosInner   = rd.m_cosInner;
                d.m_cosOuter   = rd.m_cosOuter;
                d.m_pad0       = 0.0f;
                d.m_pad1       = 0.0f;
                ++slot;
            });

            if (slot > 0)
            {
                rhiCtx->AddOrReplace<RHI::PendingBufferMap>(
                    m_buffer,
                    RHI::PendingBufferMap{ m_lightData.data(), 0, static_cast<size_t>(slot) * sizeof(LightData) });
            }
        }

        // g_LightCount rides the SceneConstants cbuffer in the same SRG. Written every
        // frame (0 during warmup) so the CBV is always compiled; SetShaderConstant marks
        // the binding dirty. The shader loop is empty while count is 0.
        SetShaderConstant(m_bindings, RHI::InputName(LightCountName), slot);

        // Same unconditional-write rule as g_LightCount; 0 mips is also the shader's gate,
        // since the cubes share g_Lights' table and "unbound" there means stale, not null.
        const EnvironmentBinding env = BindEnvironmentIBL();
        SetShaderConstant(m_bindings, RHI::InputName(PrefilteredMipsName), env.m_prefilteredMipCount);
        SetShaderConstant(m_bindings, RHI::InputName(EnvIntensityName), env.m_intensity);
    }

    void SceneBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (m_bindings != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindings); }
        if (m_buffer   != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_buffer); }
        if (m_brdfLut  != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_brdfLut); }

        m_bindings = RHI::NullHandle;
        m_buffer   = RHI::NullHandle;
        m_brdfLut  = RHI::NullHandle;
    }
}
