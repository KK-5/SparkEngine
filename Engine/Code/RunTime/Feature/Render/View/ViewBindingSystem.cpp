#include "ViewBindingSystem.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>
#include <ECS/Common.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Component/Component.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>

#include <Shader/ShaderBuilder.h>
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

        // space0 / ViewBindings group, reflected from the host shader above.
        ShaderInputBuildResult built = BuildShaderInputList(*shaderAsset);
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

        m_layout = factory->CreatePipelineLayoutDescriptor();
        m_layout->AddShaderInputDescriptors(built.list, built.stageMask);
        m_layout->Finalize();

        m_viewBindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = m_layout;
        desc.m_spaceId = 0;   // ViewBindings is reserved at space0.
        if (m_viewBindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ViewBindingSystem] ShaderBindings::Init failed.");
            m_viewBindings = nullptr;
            m_layout = nullptr;
            return;
        }

        // One shared binding entity, tagged so consumers can find it by type.
        m_viewEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(m_viewEntity, RHI::Components::ShaderBindings{ m_viewBindings });
        rhiCtx.Add<MainViewTag>(m_viewEntity);
        rhiCtx.Add<RHI::ShaderBindingsUpdateTag>(m_viewEntity);   // compile on the first frame
    }

    void ViewBindingSystem::Update()
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx || !m_viewBindings || m_viewEntity == RHI::NullHandle)
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
            rhiCtx.DestoryEntity(m_viewEntity);
            m_viewEntity = RHI::NullHandle;
        }
        m_viewBindings = nullptr;
        m_layout = nullptr;
    }
}
