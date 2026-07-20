#include "MaterialBindingSystem.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <CoreComponents/Tags.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/Buffer/Buffer.h>
#include <RHI/Resource/Buffer/BufferView.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderBuilder.h>

#include <Shader/ShaderBindingsUtils.h>

#include <Material/Components.h>
#include <Material/MaterialContext.h>

#include "MaterialBinding.h"

namespace Spark::Render
{
    namespace
    {
        constexpr const char* MaterialBufferName = "g_Materials";

        uint32_t ResolveBaseColorTexIndex(
            RHI::RHIContext& rhiCtx, Material::MaterialContext& matCtx, Material::MaterialHandle h)
        {
            auto* tex = matCtx.TryGet<MaterialGPUTextures>(h);
            if (!tex || tex->m_baseColor == RHI::NullHandle)
            {
                return InvalidTextureIndex;
            }
            auto* imgComp = rhiCtx.TryGet<RHI::Components::Image>(tex->m_baseColor);
            if (!imgComp || !imgComp->m_image)
            {
                return InvalidTextureIndex;
            }
            RHI::ImageView* view = RHI::GetOrCreateImageView(
                rhiCtx, tex->m_baseColor, *imgComp->m_image, RHI::ImageViewDescriptor{});
            return view ? view->GetBindlessReadIndex() : InvalidTextureIndex;
        }
    }

    void MaterialBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "[MaterialBindingSystem] AssetManager is unregistered.");

        // MaterialBindingsReflect.hlsl is a reflection host (#includes MaterialBindings.hlsl
        // + a dummy vertex entry reading g_Materials) so we can reflect the space3 layout
        // here — mirrors InstanceBindingSystem / InstanceBindingsReflect.hlsl.
        const Resource::AssetId assetId = assetManager->MakeAssetId("Shaders/MaterialBindingsReflect.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[MaterialBindingSystem] Failed to resolve MaterialBindingsReflect.hlsl asset id.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);
        if (!shaderAsset)
        {
            LOG_ERROR("[MaterialBindingSystem] Failed to load MaterialBindingsReflect.hlsl.");
            return;
        }

        Resource::ShaderInputBuildResult built = Resource::BuildShaderInputList(*shaderAsset);
        if (built.stageMask == RHI::ShaderStageMask::None)
        {
            LOG_ERROR("[MaterialBindingSystem] MaterialBindings.hlsl produced no shader inputs.");
            return;
        }

        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[MaterialBindingSystem] RHI::RHIInterface service not registered.");
        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();
        ASSERT(factory && device, "[MaterialBindingSystem] RHI factory or device is null.");

        Ptr<RHI::PipelineLayoutDescriptor> layout = factory->CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(built.list, built.stageMask);
        layout->Finalize();

        Ptr<RHI::ShaderBindings> materialBindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = 3;   // MaterialBindings (per-material) is reserved at space3.
        if (materialBindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[MaterialBindingSystem] ShaderBindings::Init failed.");
            return;
        }

        m_bindings = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            m_bindings, RHI::Components::ShaderBindings{ materialBindings });
        rhiCtx.Add<MaterialBindingTag>(m_bindings);

        // g_Materials: host-visible StructuredBuffer, PER-FRAME (PerFrameTag -> N copies)
        // so each frame writes its own copy without racing the GPU reading last frame's.
        // Filled each frame from m_materialData via PendingBufferMap. Materialized on the
        // next frame begin (deferred PendingBufferInit path, same as g_Instances).
        m_materialData.resize(Capacity);
        m_buffer = rhiCtx.CreateEntity();
        {
            RHI::BufferDescriptor bufDesc;
            bufDesc.m_byteCount = static_cast<uint64_t>(Capacity) * sizeof(MaterialData);
            bufDesc.m_bindFlags = RHI::BufferBindFlags::ShaderRead | RHI::BufferBindFlags::CopyRead;

            RHI::PendingBufferInit init;
            init.m_descriptor       = bufDesc;
            init.m_heapMemoryLevel  = RHI::HeapMemoryLevel::Host;
            init.m_hostMemoryAccess = RHI::HostMemoryAccess::Write;
            rhiCtx.Add<RHI::PendingBufferInit>(m_buffer, init);
            rhiCtx.Add<RHI::PerFrameTag>(m_buffer);
            rhiCtx.Add<RHI::ResourceName>(m_buffer, RHI::ResourceName{ ObjectName("g_Materials") });
        }
    }

    bool MaterialBindingSystem::BindFrameMaterials(uint32_t frameIndex)
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
        // one cheap descriptor recompile via the dirty mark — like g_Instances.
        const RHI::BufferViewDescriptor viewDesc =
            RHI::BufferViewDescriptor::CreateStructured(0, Capacity, sizeof(MaterialData));
        RHI::BufferView* view = RHI::GetOrCreateBufferViewPerFrame(
            *rhiCtx, m_buffer, *buffer, viewDesc, frameIndex);
        if (!view)
        {
            LOG_ERROR("[MaterialBindingSystem] Failed to create g_Materials structured SRV for frame {}.", frameIndex);
            return false;
        }

        SetShaderBuffer(m_bindings, RHI::InputName(MaterialBufferName), view);
        return true;
    }

    void MaterialBindingSystem::Update(uint32_t frameIndex)
    {
        auto* matCtx = Material::MaterialExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!matCtx || !rhiCtx)
        {
            return;
        }

        if (!BindFrameMaterials(frameIndex))
        {
            return;
        }

        // Dense-order scatter: every material -> g_Materials[slot], and stamp the
        // material entity with MaterialGPUSlot. Slot = iteration index, rewritten every
        // frame (host full scatter, no stable slot); InstanceBindingSystem re-resolves
        // materialIndex from MaterialGPUSlot each frame, so the reshuffle is absorbed.
        uint32_t slot = 0;
        matCtx->GetView<Material::MaterialParams>().each(
            [&](Material::MaterialHandle h, const Material::MaterialParams& params)
        {
            if (slot >= Capacity)
            {
                LOG_ERROR("[MaterialBindingSystem] Capacity={} overflow; dropping material.", Capacity);
                return;
            }

            MaterialData& d = m_materialData[slot];
            d.m_baseColor         = params.m_baseColor;
            d.m_metallic          = params.m_metallic;
            d.m_roughness         = params.m_roughness;
            d.m_specular          = params.m_specular;
            d.m_baseColorTexIndex = ResolveBaseColorTexIndex(*rhiCtx, *matCtx, h);

            matCtx->AddOrReplace<MaterialGPUSlot>(h, MaterialGPUSlot{ slot });
            ++slot;
        });

        if (slot > 0)
        {
            rhiCtx->AddOrReplace<RHI::PendingBufferMap>(
                m_buffer,
                RHI::PendingBufferMap{ m_materialData.data(), 0, static_cast<size_t>(slot) * sizeof(MaterialData) });
        }
    }

    void MaterialBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (m_bindings != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindings); }
        if (m_buffer   != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_buffer); }

        m_bindings = RHI::NullHandle;
        m_buffer   = RHI::NullHandle;
    }
}
