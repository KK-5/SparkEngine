#include "ViewBindingSystem.h"

#include <EASTL/fixed_vector.h>

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
#include "ViewComponents.h"
#include "ViewTags.h"

namespace Spark::Render
{
    namespace
    {
        constexpr uint32_t kViewSpaceId = 1;   // ViewBindings (per-view) is reserved at space1.

        void MarkDead(RHI::RHIContext& ctx, RHI::RHIHandle entity)
        {
            if (entity != RHI::NullHandle && !ctx.Has<DeadTag>(entity))
            {
                ctx.Add<DeadTag>(entity);
            }
        }
    }

    void ViewBindingSystem::Init()
    {
        auto* assetManager = Service<Resource::AssetManager>::Get();
        ASSERT(assetManager, "[ViewBindingSystem] AssetManager is unregistered.");

        // ViewBindings.hlsl has no entry point and can't be compiled alone;
        // ViewBindingsReflect.hlsl includes it and adds a dummy vertex entry so the
        // space1 group's layout can be reflected here.
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

        Resource::ShaderInputBuildResult built = Resource::BuildShaderInputList(*shaderAsset);
        if (built.stageMask == RHI::ShaderStageMask::None)
        {
            LOG_ERROR("[ViewBindingSystem] ViewBindings.hlsl produced no shader inputs.");
            return;
        }

        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[ViewBindingSystem] RHI::RHIInterface service not registered.");
        auto* factory = rhi->GetRHIFactory();
        ASSERT(factory, "[ViewBindingSystem] RHI factory is null.");

        Ptr<RHI::PipelineLayoutDescriptor> layout = factory->CreatePipelineLayoutDescriptor();
        layout->AddShaderInputDescriptors(built.list, built.stageMask);
        layout->Finalize();
        m_viewLayout = eastl::move(layout);
    }

    RHI::RHIHandle ViewBindingSystem::CreateView(RHI::RHIContext& rhiCtx)
    {
        auto* rhi = Service<RHI::RHIInterface>::Get();
        ASSERT(rhi, "[ViewBindingSystem] RHI::RHIInterface service not registered.");
        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();
        ASSERT(factory && device, "[ViewBindingSystem] RHI factory or device is null.");

        Ptr<RHI::ShaderBindings> shaderBindings = factory->CreateShaderBindings();
        RHI::ShaderBindings::Descriptor desc;
        desc.m_layout  = m_viewLayout;
        desc.m_spaceId = kViewSpaceId;
        if (shaderBindings->Init(*device, desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[ViewBindingSystem] ShaderBindings::Init failed.");
            return RHI::NullHandle;
        }

        RHI::RHIHandle bindingsEntity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::ShaderBindings>(
            bindingsEntity, RHI::Components::ShaderBindings{ shaderBindings });
        rhiCtx.Add<RHI::ShaderBindingsUpdateTag>(bindingsEntity);

        // The bindings entity carries no view tag: that is what keeps a pass's .Binds<>()
        // from ever resolving to it. Per-view bindings are reachable only through the view
        // entity below, and are bound per DrawList.
        RHI::RHIHandle view = rhiCtx.CreateEntity();
        rhiCtx.Add<MainViewTag>(view);
        rhiCtx.Add<View>(view, View{});
        rhiCtx.Add<ViewShaderBindings>(view, ViewShaderBindings{ bindingsEntity });
        return view;
    }

    void ViewBindingSystem::DestroyView(RHI::RHIContext& rhiCtx, RHI::RHIHandle view)
    {
        if (auto* bindings = rhiCtx.TryGet<ViewShaderBindings>(view))
        {
            MarkDead(rhiCtx, bindings->m_bindings);
        }
        MarkDead(rhiCtx, view);
    }

    void ViewBindingSystem::Update(const Math::Vector2Int& renderSize)
    {
        auto* world  = WorldExecuteContext::Current();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!world || !rhiCtx || !m_viewLayout || renderSize.y <= 0)
        {
            return;   // a minimized / zero framebuffer would divide by zero below
        }

        eastl::fixed_vector<Entity, 4> orphans;
        world->GetView<MainViewRef>().each([&](Entity e, const MainViewRef& ref)
        {
            if (world->Has<DeadTag>(e) || !world->Has<Camera::CameraComponent>(e))
            {
                DestroyView(*rhiCtx, ref.m_view);
                orphans.push_back(e);
            }
        });
        for (Entity e : orphans)
        {
            world->Remove<MainViewRef>(e);
        }

        // Projection is built HERE, not in CameraSystem: aspect is a render-target
        // property the world layer doesn't know.
        const float aspect = static_cast<float>(renderSize.x) / static_cast<float>(renderSize.y);

        world->GetView<Camera::CameraComponent, Camera::CameraViewMatrix>(Exclude<DeadTag>).each(
            [&](Entity e, const Camera::CameraComponent& camera, const Camera::CameraViewMatrix& mats)
        {
            auto* ref = world->TryGet<MainViewRef>(e);
            if (!ref)
            {
                const RHI::RHIHandle created = CreateView(*rhiCtx);
                if (created == RHI::NullHandle)
                {
                    return;
                }
                ref = &world->Add<MainViewRef>(e, MainViewRef{ created });
            }

            View& view = rhiCtx->Get<View>(ref->m_view);
            view.m_worldToView = mats.m_viewMatrix;
            view.m_viewToClip  = Math::PerspectiveFov(
                Math::Radians(camera.m_fov), aspect, camera.m_clipStart, camera.m_clipEnd);
            WriteViewConstants(view, rhiCtx->Get<ViewShaderBindings>(ref->m_view).m_bindings);
        });
    }

    void ViewBindingSystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        rhiCtx.GetView<MainViewTag, ViewShaderBindings>().each(
            [&](RHI::RHIHandle view, const ViewShaderBindings&)
        {
            DestroyView(rhiCtx, view);
        });

        // Strip the world-side refs so a re-init starts clean.
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<MainViewRef>();
        }
    }
}
