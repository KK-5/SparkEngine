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
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderBuilder.h>

#include <Material/MaterialContext.h>

#include <Pass/Component/RHIComponents.h>   // CreateStaticImageAttachment

namespace Spark::Render
{
    namespace
    {
        constexpr const char* MaterialBufferName = "g_Materials";

        uint32_t ResolveTexIndex(
            RHI::RHIContext& rhiCtx, Material::MaterialContext& matCtx,
            Material::MaterialHandle h, size_t slot)
        {
            auto* tex = matCtx.TryGet<Material::MaterialGPUTextures>(h);
            if (!tex)
            {
                return InvalidTextureIndex;
            }
            const RHI::RHIHandle handle = tex->m_handles[slot];
            if (handle == RHI::NullHandle)
            {
                return InvalidTextureIndex;
            }
            auto* imgComp = rhiCtx.TryGet<RHI::Components::Image>(handle);
            if (!imgComp || !imgComp->m_image)
            {
                return InvalidTextureIndex;
            }

            // Static-import barrier registration lives HERE (moved off
            // MaterialTextureSystem so the producer stops deciding usage): render
            // registers the texture's upload→shader-read attachment at its
            // resolve/sample point. One-time via the Has-check; slot name is unused by
            // the static-barrier path, so the resource's own ResourceName stands in.
            if (!rhiCtx.Has<ImagePassAttachment>(handle))
            {
                CreateStaticImageAttachment(rhiCtx, handle,
                    rhiCtx.Get<RHI::ResourceName>(handle).m_name,
                    RHI::AttachmentAccess::Read,
                    RHI::AttachmentUsage::Shader,
                    RHI::AttachmentStage::FragmentShader);
            }

            RHI::ImageView* view = RHI::GetOrCreateImageView(
                rhiCtx, handle, *imgComp->m_image, RHI::ImageViewDescriptor{});
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

        // The entity owns the binding (and transitively its layout); the system keeps
        // only the handle. The g_Materials SRV is bound by GlobalBuffer once the upload
        // buffer materializes, so we DO NOT mark it dirty here.
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

        GlobalBuffer<Materials, MaterialData, Material::MaterialParams>::Descriptor bufferDesc;
        bufferDesc.m_capacity       = Capacity;
        bufferDesc.m_resourceName   = ObjectName(MaterialBufferName);
        bufferDesc.m_inputName      = RHI::InputName(MaterialBufferName);
        bufferDesc.m_bindingsEntity = m_bindings;
        m_materials.Init(rhiCtx, bufferDesc);
    }

    void MaterialBindingSystem::Update(uint32_t frameIndex)
    {
        auto* matCtx = Material::MaterialExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!matCtx || !rhiCtx)
        {
            return;
        }

        m_materials.Update(*matCtx, *rhiCtx, frameIndex,
            [&](Material::MaterialHandle h, MaterialData& d, const Material::MaterialParams& params)
        {
            d.m_baseColor   = params.m_baseColor;
            d.m_metallic    = params.m_metallic;
            d.m_roughness   = params.m_roughness;
            d.m_specular    = params.m_specular;
            d.m_normalScale = params.m_normalScale;
            d.m_emissive    = Math::Vector4(Math::Vector3(params.m_emissive), params.m_emissiveStrength);
            for (size_t texSlot = 0; texSlot < Material::MaterialTexSlotCount; ++texSlot)
            {
                d.m_texIndices[texSlot] = ResolveTexIndex(*rhiCtx, *matCtx, h, texSlot);
            }
        });
    }

    void MaterialBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (auto* matCtx = Material::MaterialExecuteContext::Current())
        {
            m_materials.Shutdown(*matCtx, rhiCtx);
        }

        if (m_bindings != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindings); }

        m_bindings = RHI::NullHandle;
    }
}
