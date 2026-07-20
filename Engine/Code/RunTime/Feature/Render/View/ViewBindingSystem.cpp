#include "ViewBindingSystem.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <ECS/Common.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Component/Component.h>
#include <CoreComponents/Tags.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>

#include <Resource/Shader/ShaderBuilder.h>
#include <Feature/Camera/Components.h>

#include "View.h"
#include "ViewTags.h"

namespace Spark::Render
{
    void ViewBindingSystem::Init(RHI::RHIContext& rhiCtx)
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "[ViewBindingSystem] AssetManager is unregistered.");

        // ViewBindings.hlsl is a pure cbuffer header with no entry point, so the
        // asset builder can't compile/reflect it alone. ViewBindingsReflect.hlsl is
        // a reflection host: it #includes ViewBindings and adds a dummy vertex entry
        // using g_ViewProjection, purely so we can reflect the group's layout here.
        const Resource::AssetId assetId = assetManager->MakeAssetId("Shaders/ViewBindingsReflect.hlsl");
        if (!assetId.IsValid())
        {
            LOG_ERROR("[ViewBindingSystem] Failed to resolve ViewBindingsReflect.hlsl asset id.");
            return;
        }
        auto shaderAsset = assetManager->LoadAsset<Resource::ShaderAsset>(assetId);
        if (!shaderAsset)
        {
            LOG_ERROR("[ViewBindingSystem] Failed to load ViewBindingsReflect.hlsl.");
            return;
        }

        // space1 / ViewBindings group, reflected from the host shader above.
        Resource::ShaderInputBuildResult built = Resource::BuildShaderInputList(*shaderAsset);
        if (built.stageMask == RHI::ShaderStageMask::None)
        {
            LOG_ERROR("[ViewBindingSystem] ViewBindings.hlsl produced no shader inputs.");
            return;
        }

        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[ViewBindingSystem] RHI::RHIInterface service not registered.");
        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();
        ASSERT(factory && device, "[ViewBindingSystem] RHI factory or device is null.");

        // Built locally and handed to the entity below: the ShaderBindings owns its
        // layout, and the entity's Components::ShaderBindings owns the ShaderBindings,
        // so the system itself keeps no Ptr — the entity is the lifetime owner.
        Ptr<RHI::PipelineLayoutDescriptor> layout = factory->CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(built.list, built.stageMask);
        layout->Finalize();

        Ptr<RHI::ShaderBindings> viewBindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = layout;
        desc.m_spaceId = 1;   // ViewBindings (per-view) is reserved at space1.
        if (viewBindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ViewBindingSystem] ShaderBindings::Init failed.");
            return;
        }

        // One shared binding entity, tagged so consumers can find it by type. The
        // entity now owns the binding (and transitively its layout).
        m_viewEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(m_viewEntity, RHI::Components::ShaderBindings{ viewBindings });
        rhiCtx.Add<MainViewTag>(m_viewEntity);
        rhiCtx.Add<RHI::ShaderBindingsUpdateTag>(m_viewEntity);   // compile on the first frame
    }

    void ViewBindingSystem::Update()
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx || m_viewEntity == RHI::NullHandle)
        {
            return;
        }

        // Single main camera for now; multi-view will tag a binding per view type.
        world->GetView<Camera::CameraViewMatrix>().each([&](Entity, const Camera::CameraViewMatrix& mats)
        {
            View view;
            view.m_worldToView = mats.m_viewMatrix;
            view.m_viewToClip  = mats.m_projectionMatrix;
            WriteViewConstants(view, m_viewEntity);   // stages data + marks dirty
        });
    }

    void ViewBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (m_viewEntity != RHI::NullHandle)
        {
            rhiCtx.Add<DeadTag>(m_viewEntity);
            m_viewEntity = RHI::NullHandle;
        }
    }
}
