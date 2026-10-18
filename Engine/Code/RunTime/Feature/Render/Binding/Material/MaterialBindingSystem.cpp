#include "MaterialBindingSystem.h"

#include <Log/ILogSystem.h>
#include <CoreComponents/Tags.h>

#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/Image/ImageView.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <EASTL/fixed_vector.h>

#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>

#include <ECS/ExecuteContext.h>
#include <ECS/WorldContext.h>

#include <Material/MaterialContext.h>
#include <Material/MaterialUtils.h>

#include <Pass/Component/RHIComponents.h>

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
            if (!RHI::IsResourceReady(rhiCtx, handle))
            {
                return InvalidTextureIndex;
            }

            RHI::ImageView* view = RHI::GetOrCreateImageView(
                rhiCtx, handle, *rhiCtx.Get<RHI::Components::Image>(handle).m_image, RHI::ImageViewDescriptor{});
            return view ? view->GetBindlessReadIndex() : InvalidTextureIndex;
        }
    }

    void MaterialBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        // The layout of MaterialBindings.hlsli, declared rather than reflected (see SharedBindings.h).
        RHI::ShaderInputList inputs;
        inputs.m_buffers.push_back(RHI::ShaderInputBufferDescriptor(
            RHI::InputName(MaterialBufferName), RHI::ShaderInputBufferAccess::Read,
            RHI::ShaderInputBufferType::Structured, 1, 0, 0, kMaterialSpaceId));

        // The entity owns the binding (and transitively its layout); the system keeps
        // only the handle. The g_Materials SRV is bound by GlobalBuffer once the upload
        // buffer materializes, so we DO NOT mark it dirty here.
        Ptr<RHI::ShaderBindings> materialBindings =
            CreateSharedBindings(inputs, kMaterialSpaceId, "MaterialBindingSystem");
        if (!materialBindings)
        {
            return;
        }

        m_bindings = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            m_bindings, RHI::Components::ShaderBindings{ materialBindings });
        rhiCtx.Add<MaterialBindingTag>(m_bindings);

        GlobalBuffer<Materials, MaterialData, Resource::StandardPBR>::Descriptor bufferDesc;
        bufferDesc.m_capacity       = Capacity;
        bufferDesc.m_resourceName   = ObjectName(MaterialBufferName);
        bufferDesc.m_inputName      = RHI::InputName(MaterialBufferName);
        bufferDesc.m_bindingsEntity = m_bindings;
        m_materials.Init(rhiCtx, bufferDesc);

        EntityEventBus::Handler::BusConnect();
    }

    void MaterialBindingSystem::OnEntityDestory(Entity entity)
    {
        auto* world  = WorldExecuteContext::Current();
        auto* matCtx = Material::MaterialExecuteContext::Current();
        if (!world || !matCtx)
        {
            return;
        }

        // Read-only on the world: this fires from inside DestoryEntity, which is walking
        // the registry's storage. The mark lands in the material context instead.
        if (const auto* ref = world->TryGet<MaterialOverrideRef>(entity))
        {
            if (matCtx->Valid(ref->m_material))
            {
                matCtx->AddOrReplace<DeadTag>(ref->m_material);
            }
        }
    }

    void SyncOverrideMaterials()
    {
        auto* world  = WorldExecuteContext::Current();
        auto* matCtx = Material::MaterialExecuteContext::Current();
        if (!world || !matCtx)
        {
            return;
        }

        world->GetView<Material::StandardPBROverride>().each(
            [&](Entity entity, const Material::StandardPBROverride& params)
        {
            auto* ref = world->TryGet<MaterialOverrideRef>(entity);
            if (!ref || !matCtx->Valid(ref->m_material))
            {
                const Material::MaterialHandle h = Material::CreateMaterial(*matCtx, params);
                world->AddOrReplace<MaterialOverrideRef>(entity, MaterialOverrideRef{h});
                return;
            }

            // Rewritten every frame rather than tracked: it is one 68-byte copy per
            // override, and it makes an edit -- to the override or to the material it
            // shadows -- need no notification at all.
            matCtx->Get<Resource::StandardPBR>(ref->m_material) = params;
        });

        // Collected first: the link is the pool being iterated, and this repo does not
        // write into one (see GlobalBuffer::Update).
        eastl::fixed_vector<Entity, 8> stale;
        world->GetView<MaterialOverrideRef>(Exclude<Material::StandardPBROverride>).each(
            [&](Entity entity, const MaterialOverrideRef& ref)
        {
            if (matCtx->Valid(ref.m_material))
            {
                matCtx->AddOrReplace<DeadTag>(ref.m_material);
            }
            stale.push_back(entity);
        });
        for (Entity entity : stale)
        {
            world->Remove<MaterialOverrideRef>(entity);
        }
    }

    void ReapDeadMaterials()
    {
        auto* matCtx = Material::MaterialExecuteContext::Current();
        if (!matCtx)
        {
            return;
        }

        eastl::vector<Material::MaterialHandle> dead;
        for (Material::MaterialHandle h : matCtx->GetView<DeadTag>())
        {
            dead.push_back(h);
        }
        if (!dead.empty())
        {
            matCtx->DestoryEntity(dead.begin(), dead.end());
        }
    }

    void MaterialBindingSystem::Update(uint32_t frameIndex)
    {
        auto* matCtx = Material::MaterialExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!matCtx || !rhiCtx)
        {
            return;
        }

        SyncOverrideMaterials();

        m_materials.Update(*matCtx, *rhiCtx, frameIndex,
            [&](Material::MaterialHandle h, MaterialData& d, const Resource::StandardPBR& params)
        {
            d.m_baseColor   = params.m_baseColor;
            d.m_metallic    = params.m_metallic;
            d.m_roughness   = params.m_roughness;
            d.m_specular    = params.m_specular;
            d.m_normalScale = params.m_normalScale;
            d.m_emissive    = Math::Vector4(Math::Vector3(params.m_emissive), params.m_emissiveStrength);
            for (size_t texSlot = 0; texSlot < Resource::MaterialTexSlotCount; ++texSlot)
            {
                d.m_texIndices[texSlot] = ResolveTexIndex(*rhiCtx, *matCtx, h, texSlot);
            }
        });

        ReapDeadMaterials();
    }

    void MaterialBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        EntityEventBus::Handler::BusDisconnect();

        // The links outlive this system otherwise, naming material entities that are
        // about to stop existing.
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<MaterialOverrideRef>();
        }

        if (auto* matCtx = Material::MaterialExecuteContext::Current())
        {
            m_materials.Shutdown(*matCtx, rhiCtx);
        }

        if (m_bindings != RHI::NullHandle) { rhiCtx.Add<DeadTag>(m_bindings); }

        m_bindings = RHI::NullHandle;
    }
}
