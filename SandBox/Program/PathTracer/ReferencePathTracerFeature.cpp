#include "ReferencePathTracerFeature.h"

#include <GLFW/glfw3.h>
#include <Log/ILogSystem.h>
#include <Math/MathUtils.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Shader/ShaderAsset.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/RHIInterface.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <Window/IWindowSystem.h>

#include <Binding/Geometry/GeometryBinding.h>
#include <Binding/Instance/InstanceBinding.h>
#include <Binding/Material/MaterialBinding.h>
#include <Binding/RayTracing/RayTracingBinding.h>
#include <Binding/Scene/SceneBinding.h>
#include <Binding/View/ViewBinding.h>
#include <Geometry/MeshGeometry.h>
#include <Pass/RenderPass.h>
#include <RayTracing/RayTracingScene.h>
#include <RenderGraph/PassScopes.h>
#include <View/ViewFactory.h>
#include <View/ViewTags.h>

namespace Spark::SandBox
{
    namespace
    {
        constexpr const char* DebugModes[] = {
            "Direct lighting", "Base color", "Shading normal",
            "Geometric normal", "UV / checker", "Roughness / metallic / specular",
            "Path tracing (8 bounces)"
        };
        constexpr uint32_t DebugModeCount = sizeof(DebugModes) / sizeof(DebugModes[0]);
    }

    ReferencePathTracerFeature::ReferencePathTracerFeature() = default;
    ReferencePathTracerFeature::~ReferencePathTracerFeature() = default;

    bool ReferencePathTracerFeature::Init(uint32_t debugMode)
    {
        auto* device = Service<RHI::RHIInterface>::Get()->GetDevice();
        if (!device->GetFeatures().m_rayTracing)
        {
            LOG_ERROR("[ReferencePathTracer] The device does not support ray tracing.");
            return false;
        }
        m_shader = Service<Resource::AssetManager>::Get()->LoadAsset<Resource::ShaderAsset>(
            Resource::AssetId::Of<Resource::ShaderAsset>("sandbox://Shader/PathTracer/ReferencePathTracer.hlsl"));
        m_displayShader = Service<Resource::AssetManager>::Get()->LoadAsset<Resource::ShaderAsset>(
            Resource::AssetId::Of<Resource::ShaderAsset>("sandbox://Shader/PathTracer/ReferencePathTracerDisplay.hlsl"));
        if (!m_shader || !m_shader->IsReady() || !m_displayShader || !m_displayShader->IsReady() || !m_scene.Init())
        {
            LOG_ERROR("[ReferencePathTracer] Failed to initialize the shader or scene.");
            return false;
        }

        m_view = Render::CreateViewEntity<Render::MainViewTag>(*RHI::RHIExecuteContext::Current());
        SetDebugMode(debugMode < DebugModeCount ? debugMode : 0);
        Update();
        CreatePasses();
        TickBus::Handler::BusConnect();
        LOG_INFO("[ReferencePathTracer] 1 spp/frame with continuous HDR accumulation, fixed primary rays, material LOD 0. Keys 1: direct lighting; 2-6: debug views; 7: path tracing (8 bounces, MIS); Esc: exit.");
        return true;
    }

    void ReferencePathTracerFeature::CreatePasses()
    {
        RHI::RenderTargetLayout targets;
        targets.m_colorAttachmentCount = 1;
        targets.m_colorFormats[0] = RHI::Format::R32G32B32A32_FLOAT;
        RHI::InputStreamLayoutBuilder input;
        input.SetTopology(RHI::PrimitiveTopology::TriangleList);
        const RHI::InputStreamLayout inputLayout = input.End();
        RHI::RenderStates states;
        states.m_depthStencilState.m_depth.m_enable = 0;
        states.m_depthStencilState.m_stencil.m_enable = 0;
        states.m_rasterState.m_cullMode = RHI::CullMode::None;

        SPARK_RENDER_PASS(*Render::PassExecuteContext::Current(), "ReferencePathTracer")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(m_shader)
            .FragmentShader(m_shader)
            .InputLayout(inputLayout)
            .RenderTargetLayout(targets)
            .RenderStates(states)
            .Binds<Render::ViewBindingTag, Render::RayTracingSceneBindingTag,
                   Render::InstanceBindingTag, Render::GeometryBindingTag, Render::MaterialBindingTag,
                   Render::MainSceneTag>()
            .RendersView<Render::MainViewTag>()
            .Build([this](Render::RenderPassScopes& p)
            {
                p.ImportShared<Render::ViewBufferTag>(RHI::AttachmentId("Views"));
                const RHI::AttachmentId accumulation("ReferencePathTracerAccumulation");
                const RHI::AttachmentId scene("ReferenceScene");
                const RHI::AttachmentId instances("ReferenceInstances");
                const RHI::AttachmentId geometries("ReferenceGeometries");
                const RHI::AttachmentId mesh("ReferenceMeshGeometry");
                const RHI::AttachmentId materials("ReferenceMaterials");
                const RHI::AttachmentId emissiveIDs("ReferenceEmissiveInstances");
                const auto size = p.GetOutputSize();
                p.CreateImage(accumulation, RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead,
                    size.x, size.y, RHI::Format::R32G32B32A32_FLOAT));
                // Try every import: short-circuiting would hide resources on startup frames.
                const bool hasScene = p.ImportShared<Render::RayTracingSceneTag>(scene);
                const bool hasInstances = p.ImportShared<Render::InstancePoolTag>(instances);
                const bool hasGeometries = p.ImportShared<Render::GeometryPoolTag>(geometries);
                const bool hasMesh = p.Import<Render::MeshGeometryPoolTag>(mesh);
                const bool hasMaterials = p.ImportShared<Render::MaterialPoolTag>(materials);
                const bool hasEmissiveIDs = p.Import<Render::EmissiveInstanceIDBufferTag>(emissiveIDs);
                uint32_t emissiveCount = 0;
                if (hasEmissiveIDs)
                {
                    for (auto [entity, count] : RHI::RHIExecuteContext::Current()->GetView<Render::EmissiveInstanceCount>().each())
                    {
                        emissiveCount = count.m_count;
                    }
                }

                RHI::AttachmentLoadStoreAction clear;
                clear.m_loadAction = RHI::AttachmentLoadAction::Clear;
                clear.m_storeAction = RHI::AttachmentStoreAction::Store;
                clear.m_clearValue = RHI::ClearValue::CreateVector4Float(0.0f, 0.0f, 0.0f, 1.0f);
                auto scope = p.Scope();
                scope.ReadBuffer(RHI::AttachmentId("Views"));
                scope.RenderTarget(accumulation, clear);
                scope.ReadPreviousImage(accumulation)
                    .Bind(RHI::InputName("g_History"));
                if (hasScene && hasInstances && hasGeometries && hasMesh && hasMaterials)
                {
                    scope.ReadAccelerationStructure(scene);
                    scope.ReadBuffer(instances);
                    scope.ReadBuffer(geometries);
                    scope.ReadBuffer(mesh).Stage(RHI::AttachmentStage::FragmentShader);
                    scope.ReadBuffer(materials);
                    if (hasEmissiveIDs)
                    {
                        scope.ReadBuffer(emissiveIDs).Stage(RHI::AttachmentStage::FragmentShader);
                    }
                    scope.Constant(RHI::InputName("debugMode"), m_debugMode);
                    scope.Constant(RHI::InputName("emissiveInstanceCount"), emissiveCount);
                    scope.Constant(RHI::InputName("sampleIndex"), m_sampleIndex++);
                    scope.Sampler(RHI::InputName("g_MaterialSampler"), RHI::SamplerState::Create(
                        RHI::FilterMode::Linear, RHI::FilterMode::Linear, RHI::AddressMode::Wrap));
                    scope.Draw(RHI::DrawLinear(3, 0));
                }
                scope.Close();
            })
            .Finalize();

        targets.m_colorFormats[0] = RHI::Format::R8G8B8A8_UNORM;
        SPARK_RENDER_PASS(*Render::PassExecuteContext::Current(), "ReferencePathTracerDisplay")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(m_displayShader)
            .FragmentShader(m_displayShader)
            .InputLayout(inputLayout)
            .RenderTargetLayout(targets)
            .RenderStates(states)
            .Binds<Render::ViewBindingTag>()
            .RendersView<Render::MainViewTag>()
            .Build([this](Render::RenderPassScopes& p)
            {
                p.ImportShared<Render::ViewBufferTag>(RHI::AttachmentId("Views"));
                const RHI::AttachmentId output("SwapChain");
                p.Import(output, p.GetCurrentSwapChainResource());
                RHI::AttachmentLoadStoreAction clear;
                clear.m_loadAction = RHI::AttachmentLoadAction::Clear;
                clear.m_storeAction = RHI::AttachmentStoreAction::Store;
                clear.m_clearValue = RHI::ClearValue::CreateVector4Float(0.0f, 0.0f, 0.0f, 1.0f);

                auto scope = p.Scope();
                scope.ReadBuffer(RHI::AttachmentId("Views"));
                scope.RenderTarget(output, clear);
                scope.ReadImage(RHI::AttachmentId("ReferencePathTracerAccumulation"))
                    .Bind(RHI::InputName("g_Accumulation"));
                scope.Constant(RHI::InputName("debugMode"), m_debugMode);
                scope.Draw(RHI::DrawLinear(3, 0));
                scope.Close();
            })
            .Finalize();
    }

    void ReferencePathTracerFeature::SetDebugMode(uint32_t mode)
    {
        m_debugMode = mode;
        // A different lighting mode starts a new reference estimate.
        m_sampleIndex = 0;
        const eastl::string title = eastl::string("ReferencePathTracer | 1 spp/frame + accumulation | ")
            + DebugModes[mode] + " | 1-7: views";
        auto* window = static_cast<GLFWwindow*>(Service<Window::IWindowSystem>::Get()->GetWindowHandle());
        glfwSetWindowTitle(window, title.c_str());
    }

    void ReferencePathTracerFeature::OnTick(const FrameTime&)
    {
        Update();
    }

    void ReferencePathTracerFeature::Update()
    {
        auto* windowSystem = Service<Window::IWindowSystem>::Get();
        auto* window = static_cast<GLFWwindow*>(windowSystem->GetWindowHandle());
        for (uint32_t i = 0; i < DebugModeCount; ++i)
        {
            if (glfwGetKey(window, GLFW_KEY_1 + i) == GLFW_PRESS && m_debugMode != i)
            {
                SetDebugMode(i);
            }
        }
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        const auto size = windowSystem->GetWindowSize();
        if (size.x <= 0 || size.y <= 0)
        {
            return;
        }
        auto& view = RHI::RHIExecuteContext::Current()->Get<Render::View>(m_view.Get());
        view = Render::MakePerspectiveView(Math::Vector3(0.0f, 1.0f, -3.2f), Math::Vector3(0.0f, 1.0f, 1.0f),
            Math::Vector3(0.0f, 1.0f, 0.0f), Math::Radians(40.0f), float(size.x) / float(size.y), 0.01f, 20.0f);
        view.m_bufferSize = size;
    }

    void ReferencePathTracerFeature::Shutdown()
    {
        TickBus::Handler::BusDisconnect();
        m_view.Reset();
        m_shader.reset();
        m_displayShader.reset();
    }
}
