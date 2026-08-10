#include "ViewFactory.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <CoreComponents/Tags.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Component/Component.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManager.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Shader/ShaderBuilder.h>

namespace Spark::Render
{
    namespace
    {
        constexpr uint32_t kViewSpaceId = 1;   // ViewBindings (per-view) is reserved at space1.

        //! The ViewBindings layout, reflected once for the process: it is a property of
        //! ViewBindingsReflect.hlsl, not of whoever happens to create views. Null when the
        //! shader could not be loaded — logged once, since the attempt is not repeated.
        const Ptr<RHI::PipelineLayoutDescriptor>& GetViewBindingsLayout()
        {
            static Ptr<RHI::PipelineLayoutDescriptor> s_layout;
            static bool s_attempted = false;
            if (s_attempted)
            {
                return s_layout;
            }
            s_attempted = true;

            auto* assetManager = Service<Resource::AssetManager>::Get();
            ASSERT(assetManager, "[ViewFactory] AssetManager is unregistered.");

            // ViewBindings.hlsl has no entry point and cannot be compiled alone;
            // ViewBindingsReflect.hlsl includes it and adds a dummy vertex entry so the
            // space1 group's layout can be reflected here.
            const Resource::AssetId assetId =
                assetManager->MakeAssetId("Shaders/ViewBindingsReflect.hlsl");
            if (!assetId.IsValid())
            {
                LOG_ERROR("[ViewFactory] Failed to resolve ViewBindingsReflect.hlsl asset id.");
                return s_layout;
            }

            auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);
            if (!shaderAsset)
            {
                LOG_ERROR("[ViewFactory] Failed to load ViewBindingsReflect.hlsl.");
                return s_layout;
            }

            Resource::ShaderInputBuildResult built = Resource::BuildShaderInputList(*shaderAsset);
            if (built.stageMask == RHI::ShaderStageMask::None)
            {
                LOG_ERROR("[ViewFactory] ViewBindings.hlsl produced no shader inputs.");
                return s_layout;
            }

            auto* rhi = Service<RHI::RHIInterface>::Get();
            ASSERT(rhi, "[ViewFactory] RHI::RHIInterface service not registered.");
            auto* factory = rhi->GetRHIFactory();
            ASSERT(factory, "[ViewFactory] RHI factory is null.");

            Ptr<RHI::PipelineLayoutDescriptor> layout = factory->CreatePipelineLayoutDescriptor();
            layout->AddShaderInputDescriptors(built.list, built.stageMask);
            layout->Finalize();
            s_layout = eastl::move(layout);
            return s_layout;
        }
    }

    RHI::RHIHandle Internal::CreateViewShaderBindings(RHI::RHIContext& rhiCtx)
    {
        const Ptr<RHI::PipelineLayoutDescriptor>& layout = GetViewBindingsLayout();
        if (!layout)
        {
            return RHI::NullHandle;
        }

        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[ViewFactory] RHI::RHIInterface service not registered.");
        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();
        ASSERT(factory && device, "[ViewFactory] RHI factory or device is null.");

        Ptr<RHI::ShaderBindings> shaderBindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = kViewSpaceId;
        if (shaderBindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ViewFactory] ShaderBindings::Init failed.");
            return RHI::NullHandle;
        }

        RHI::RHIHandle entity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            entity, RHI::Components::ShaderBindings{ shaderBindings });
        rhiCtx.Add<RHI::ShaderBindingsUpdateTag>(entity);
        return entity;
    }

    void DestroyViewEntity(RHI::RHIContext& rhiCtx, RHI::RHIHandle view)
    {
        auto markDead = [&](RHI::RHIHandle entity)
        {
            if (entity != RHI::NullHandle && rhiCtx.Valid(entity) && !rhiCtx.Has<DeadTag>(entity))
            {
                rhiCtx.Add<DeadTag>(entity);
            }
        };

        if (auto* bindings = rhiCtx.TryGet<ViewShaderBindings>(view))
        {
            markDead(bindings->m_bindings);
        }
        markDead(view);
    }
}
