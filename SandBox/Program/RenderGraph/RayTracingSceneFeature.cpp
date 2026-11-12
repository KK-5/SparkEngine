#include "RayTracingSceneFeature.h"

#include <ECS/WorldContext.h>
#include <Log/ILogSystem.h>
#include <Math/MathUtils.h>
#include <Service/Service.h>
#include <Window/IWindowSystem.h>

#include <Mesh/Components.h>
#include <Transform/Components.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Model/ModelAsset.h>
#include <Resource/Shader/ShaderAsset.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Pipeline/RenderStates.h>

#include <Binding/RayTracing/RayTracingBinding.h>
#include <Binding/View/ViewBinding.h>
#include <Pass/RenderPass.h>
#include <RayTracing/RayTracingScene.h>
#include <RenderGraph/PassScopes.h>
#include <View/ViewFactory.h>
#include <View/ViewTags.h>

#include "../Common/RenderGraphUtil.h"

namespace Spark::SandBox
{
    RayTracingSceneFeature::RayTracingSceneFeature() = default;
    RayTracingSceneFeature::~RayTracingSceneFeature() = default;

    bool RayTracingSceneFeature::Init()
    {
        auto* device = Service<RHI::RHIInterface>::Get()->GetDevice();
        if (!device->GetFeatures().m_rayTracing)
        {
            LOG_ERROR("[RayTracingScene] The device does not support ray tracing.");
            return false;
        }

        auto* assets = Service<Resource::AssetManager>::Get();
        m_shader = assets->LoadAsset<Resource::ShaderAsset>(
            Resource::AssetId::Of<Resource::ShaderAsset>("sandbox://Shader/RayTracingScene.hlsl"));
        m_model = assets->LoadAsset<Resource::ModelAsset>(
            Resource::AssetId::Of<Resource::ModelAsset>("sandbox://Model/CubeTextured.glb"));
        const auto* data = m_model ? m_model->GetModelData() : nullptr;
        const auto* mesh = data ? data->GetMesh(0) : nullptr;
        if (!m_shader || !m_shader->IsReady() || !m_model || !m_model->IsReady()
            || !mesh || mesh->primitives.empty())
        {
            LOG_ERROR("[RayTracingScene] Failed to load the shader or cube geometry.");
            return false;
        }

        auto& world = *WorldExecuteContext::Current();
        for (Entity& cube : m_cubes)
        {
            cube = world.CreateEntity();
            Mesh::MeshComponent component;
            component.m_modelAssetId = m_model->GetAssetId();
            world.Add<Mesh::MeshComponent>(cube, component);
            world.Add<Transform::WorldTransformMatrix>(cube);
        }

        auto& rhiContext = *RHI::RHIExecuteContext::Current();
        m_view = Render::CreateViewEntity<Render::MainViewTag>(rhiContext);
        Update();
        CreatePass();
        TickBus::Handler::BusConnect();
        return true;
    }

    void RayTracingSceneFeature::CreatePass()
    {
        RHI::RenderTargetLayout targets;
        targets.m_colorAttachmentCount = 1;
        targets.m_colorFormats[0] = RHI::Format::R8G8B8A8_UNORM;

        RHI::InputStreamLayoutBuilder input;
        input.SetTopology(RHI::PrimitiveTopology::TriangleList);

        RHI::RenderStates states;
        states.m_depthStencilState.m_depth.m_enable = 0;
        states.m_depthStencilState.m_stencil.m_enable = 0;
        states.m_rasterState.m_cullMode = RHI::CullMode::None;

        auto& passContext = *Render::PassExecuteContext::Current();
        SPARK_RENDER_PASS(passContext, "RayTracingSceneDebug")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(m_shader)
            .FragmentShader(m_shader)
            .InputLayout(input.End())
            .RenderTargetLayout(targets)
            .RenderStates(states)
            .Binds<Render::ViewBindingTag, Render::RayTracingSceneBindingTag>()
            .RendersView<Render::MainViewTag>()
            .Build([](Render::RenderPassScopes& p)
            {
                p.ImportShared<Render::ViewBufferTag>(RHI::AttachmentId("Views"));
                const RHI::AttachmentId swapChain("SwapChain");
                const RHI::AttachmentId storage("RayTracingSceneStorage");
                p.Import(swapChain, p.GetCurrentSwapChainResource());

                RHI::AttachmentLoadStoreAction clear;
                clear.m_loadAction = RHI::AttachmentLoadAction::Clear;
                clear.m_storeAction = RHI::AttachmentStoreAction::Store;
                clear.m_clearValue = RHI::ClearValue::CreateVector4Float(0.025f, 0.035f, 0.055f, 1.0f);

                const bool hasScene = p.Import<Render::RayTracingSceneTag>(storage);

                auto scope = p.Scope();
                scope.ReadBuffer(RHI::AttachmentId("Views"));
                scope.RenderTarget(swapChain, clear);
                if (hasScene)
                {
                    scope.ReadAccelerationStructure(storage).Stage(RHI::AttachmentStage::FragmentShader);
                    scope.Draw(RHI::DrawLinear(3, 0));
                }
                scope.Close();
            })
            .Finalize();
    }

    void RayTracingSceneFeature::OnTick(const FrameTime&)
    {
        Update();
    }

    void RayTracingSceneFeature::Update()
    {
        const auto size = Service<Window::IWindowSystem>::Get()->GetWindowSize();
        auto* world = WorldExecuteContext::Current();
        auto* rhiContext = RHI::RHIExecuteContext::Current();
        if (!world || !rhiContext || size.x <= 0 || size.y <= 0)
        {
            return;
        }

        m_rotationAngle += 0.01f;
        if (world->Valid(m_cubes[0]))
        {
            world->Get<Transform::WorldTransformMatrix>(m_cubes[0]).m_worldMatrix =
                Math::Translate(Math::Matrix4X4Const::IDENTITY, Math::Vector3(-0.55f, 0.0f, 0.25f));
        }
        if (world->Valid(m_cubes[1]))
        {
            auto transform = Math::Translate(Math::Matrix4X4Const::IDENTITY,
                Math::Vector3(0.45f + 0.4f * Math::Sin(m_rotationAngle * 0.5f), 0.0f, -0.45f));
            transform = Math::Rotate(transform, m_rotationAngle, Math::Vector3(0.0f, 1.0f, 0.0f));
            world->Get<Transform::WorldTransformMatrix>(m_cubes[1]).m_worldMatrix =
                Math::Scale(transform, Math::Vector3(0.85f, 1.15f, 0.7f));
        }

        auto& view = rhiContext->Get<Render::View>(m_view.Get());
        view = Render::MakePerspectiveView(Math::Vector3(0.0f, 1.4f, -6.0f), Math::Vector3(0.0f, 0.0f, 0.0f),
            Math::Vector3(0.0f, 1.0f, 0.0f), Math::Radians(45.0f), float(size.x) / float(size.y), 0.1f, 100.0f);
        view.m_bufferSize = size;
    }

    void RayTracingSceneFeature::Shutdown()
    {
        TickBus::Handler::BusDisconnect();
        if (auto* world = WorldExecuteContext::Current())
        {
            for (Entity& cube : m_cubes)
            {
                if (world->Valid(cube))
                {
                    world->DestoryEntity(cube);
                }
                cube = NullEntity;
            }
        }
        m_view.Reset();
    }
}

int main(int, char**)
{
    using namespace Spark;

    auto sys = SandBox::InitRenderGraphApp(1024, 576, "RayTracingScene");
    Render::Pipeline pipeline("RayTracingScene");
    Render::PassExecuteContext::Push(pipeline.GetPassContext());
    sys.m_render->SetUpSystemPasses(pipeline.GetPassContext());

    SandBox::RayTracingSceneFeature feature;
    if (!feature.Init())
    {
        Render::PassExecuteContext::Pop();
        return 1;
    }
    while (!sys.m_window->ShouldClose())
    {
        TickBus::Broadcast(&TickBus::Events::OnTick, FrameTime{});
    }

    feature.Shutdown();
    Render::PassExecuteContext::Pop();
    return 0;
}
