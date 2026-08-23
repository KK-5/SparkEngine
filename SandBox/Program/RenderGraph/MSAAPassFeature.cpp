#include "MSAAPassFeature.h"

#include <Log/ILogSystem.h>
#include <Math/Vector3.h>
#include <Math/Matrix4x4.h>
#include <Math/MathUtils.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/HardwareQueue.h>
#include <RHI/Fence/Fence.h>
#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/ClearValue.h>
#include <RHI/MultisampleState.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Resource/ShaderInput/ShaderInput.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/Pipeline/RenderTargetLayout.h>

#include <Resource/Asset.h>
#include <Resource/AssetManager.h>
#include <Resource/Shader/ShaderAsset.h>

#include <Pass/PassContext.h>
#include <Pass/RenderPass.h>
#include <Pass/PassTag.h>
#include <Pass/PassAccess.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphExecuter.h>
#include <View/View.h>
#include <View/ViewTags.h>
#include <Drawable/GeometrySpec.h>

#include "SampleDrawTag.h"

#include <Window/IWindowSystem.h>

#include "../Common/RenderGraphUtil.h"

namespace Spark::SandBox
{

    namespace
    {
        struct TriangleVertex
        {
            Math::Vector3 position;
            Math::Vector3 color;
        };

        static const TriangleVertex g_triangleVertices[] =
        {
            {{ 0.0f,  0.5f, 0.f}, {1.f, 0.f, 0.f}},
            {{ 0.5f, -0.5f, 0.f}, {0.f, 1.f, 0.f}},
            {{-0.5f, -0.5f, 0.f}, {0.f, 0.f, 1.f}},
        };

        constexpr uint32_t g_vertexCount = sizeof(g_triangleVertices) / sizeof(g_triangleVertices[0]);
    }

    MSAAPassFeature::MSAAPassFeature() = default;
    MSAAPassFeature::~MSAAPassFeature() = default;

    bool MSAAPassFeature::Init()
    {
        auto assetManager = Service<Spark::Resource::AssetManager>::Get();
        ASSERT(assetManager, "[MSAAPassFeature] AssetManager service missing.");
        m_shader = assetManager->LoadAsset<Spark::Resource::ShaderAsset>(
            Spark::Resource::AssetId::Of<Spark::Resource::ShaderAsset>("sandbox://Shader/TriangleMVP.hlsl"));
        ASSERT(m_shader && m_shader->GetStatus() == Spark::Resource::AssetStatus::Ready,
            "[MSAAPassFeature] TriangleMVP.hlsl load failed.");

        // The pass must exist before its per-pass bindings can be get-or-created by tag,
        // so build the pass first. UpdateViewBindings then lazily creates + fills the
        // space0 bindings (via SetPassShaderConstant).
        CreateVertexBuffer();
        CreateView();
        CreatePasses();
        BuildGeometry();

        TickBus::Handler::BusConnect();
        return true;
    }

    void MSAAPassFeature::Shutdown()
    {
        TickBus::Handler::BusDisconnect();

        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();
        auto destroyIfValid = [&](Spark::RHI::RHIHandle& handle)
        {
            if (handle != Spark::RHI::NullHandle && ctx.Valid(handle))
            {
                ctx.DestoryEntity(handle);
            }
            handle = Spark::RHI::NullHandle;
        };

        destroyIfValid(m_drawable);
        destroyIfValid(m_vertexBuffer);
        destroyIfValid(m_view);
        // Per-pass SRGs hold no member handle now — destroy them by tag (just the
        // space0 SRG here). Collected first: destroying inside the view iteration
        // would invalidate it.
        eastl::fixed_vector<Spark::RHI::RHIHandle, 4> srgEntities;
        ctx.GetView<Spark::Render::PassShaderBindingsTag>().each(
            [&](Spark::RHI::RHIHandle e) { srgEntities.push_back(e); });
        for (Spark::RHI::RHIHandle e : srgEntities)
        {
            destroyIfValid(e);
        }
    }

    void MSAAPassFeature::OnTick(float /*deltaTime*/)
    {
        Update();
    }

    void MSAAPassFeature::CreateVertexBuffer()
    {
        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();

        Spark::RHI::BufferDescriptor vbDesc;
        vbDesc.m_bindFlags =
            Spark::RHI::BufferBindFlags::InputAssembly | Spark::RHI::BufferBindFlags::CopyWrite;
        vbDesc.m_byteCount = sizeof(g_triangleVertices);
        vbDesc.m_sharedQueueMask = Spark::RHI::HardwareQueueClassMask::Graphics;

        m_vertexBuffer = Spark::RHI::CreateStaticBuffer(
            ctx, ObjectName("TriangleVB"), vbDesc);
        Spark::RHI::RequestBufferUpload(
            ctx, m_vertexBuffer, g_triangleVertices, sizeof(g_triangleVertices));
        Spark::Render::CreateStaticBufferAttachment(ctx, m_vertexBuffer,
            Spark::RHI::InputName("TriangleVB"),
            Spark::RHI::AttachmentAccess::Read,
            Spark::RHI::AttachmentUsage::InputAssembly,
            Spark::RHI::AttachmentStage::VertexInput);
    }

    void MSAAPassFeature::CreateView()
    {
        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();

        // Built by hand because ViewBindingSystem find-or-creates from world cameras and this
        // app has none. No ViewShaderBindings: TriangleMVP.hlsl declares no space1, so all
        // this view supplies is the rect that becomes viewport / scissor — here scaled by the
        // MSAA target's extent rather than the swap chain's, since that is ScenePass's
        // attachment.
        m_view = ctx.CreateEntity();
        ctx.Add<Spark::Render::View>(m_view, Spark::Render::View{});
        ctx.Add<Spark::Render::MainViewTag>(m_view);
    }

    void MSAAPassFeature::CreatePasses()
    {
        Spark::RHI::InputStreamLayoutBuilder islBuilder;
        islBuilder.Begin();
        islBuilder.SetTopology(Spark::RHI::PrimitiveTopology::TriangleList);
        islBuilder.AddBuffer()->Channel("POSITION", 0, Spark::RHI::Format::R32G32B32_FLOAT)
                              ->Channel("COLOR",    0, Spark::RHI::Format::R32G32B32_FLOAT);
        Spark::RHI::InputStreamLayout inputLayout = islBuilder.End();

        Spark::RHI::RenderTargetLayout rtLayout;
        rtLayout.m_colorAttachmentCount = 1;
        rtLayout.m_colorFormats[0]      = Spark::RHI::Format::R8G8B8A8_UNORM;

        Spark::RHI::RenderStates renderStates;
        renderStates.m_depthStencilState.m_depth.m_enable   = 0;
        renderStates.m_depthStencilState.m_stencil.m_enable = 0;
        renderStates.m_rasterState.m_cullMode               = Spark::RHI::CullMode::None;
        renderStates.m_multisampleState = RHI::MultisampleState(4, 0);

        auto* window = Service<Spark::Window::IWindowSystem>::Get();
        auto windowSize = window->GetWindowSize();

        auto& passContext = *Spark::Render::PassExecuteContext::Current();

        // ================================================================
        // Pass 1: ScenePass — render triangle to 4x MSAA transient target
        // ================================================================
        SPARK_RENDER_PASS(passContext, "ScenePass")
            .Queue(Spark::RHI::HardwareQueueClass::Graphics)
            .VertexShader(m_shader)
            .FragmentShader(m_shader)
            .InputLayout(inputLayout)
            .RenderTargetLayout(rtLayout)
            .RenderStates(renderStates)
            .Accepts<SampleDrawTag>()
            .Binds<>()
            .RendersView<Spark::Render::MainViewTag>()
            .Build([this, windowSize](Spark::Render::RenderGraphBuilder& builder)
            {
                auto imageDesc = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead,
                    windowSize.x, windowSize.y,
                    RHI::Format::R8G8B8A8_UNORM
                );
                imageDesc.m_multisampleState = RHI::MultisampleState(4, 0);

                Render::ImageAttachmentBindInfo msaaBind;
                msaaBind.m_slot   = Spark::RHI::InputName("MSAAColor");
                msaaBind.m_usage  = Spark::RHI::AttachmentUsage::RenderTarget;
                msaaBind.m_stage  = Spark::RHI::AttachmentStage::ColorAttachmentOutput;
                msaaBind.m_action.m_clearValue  =
                    Spark::RHI::ClearValue::CreateVector4Float(0.1f, 0.1f, 0.15f, 1.f);
                msaaBind.m_action.m_loadAction  = Spark::RHI::AttachmentLoadAction::Clear;
                msaaBind.m_action.m_storeAction = Spark::RHI::AttachmentStoreAction::Store;

                builder.CreateImageAttachment<SPARK_PASS_TAG("ScenePass")>(
                    RHI::AttachmentId("MSAAColor"), imageDesc, msaaBind, RHI::AttachmentAccess::Write);


                // TriangleVB is StaticImportTag — attachment declared at creation.
                // No per-frame build registration needed.
            })
            .Compile([this](Spark::Render::RenderGraphCompiler& compiler)
            {
                Spark::Render::SetPassShaderConstant<SPARK_PASS_TAG("ScenePass")>(
                    /*spaceId*/ 0, Spark::RHI::InputName("g_MVP"), m_matrix);
                Spark::Render::SetPassShaderConstant<SPARK_PASS_TAG("ScenePass")>(
                    /*spaceId*/ 0, Spark::RHI::InputName("g_Colors"), m_colors);
            })
            // Same as the default hook when .Execute() is omitted. Called once per
            // state-homogeneous run, not once per pass — N views is N calls over the same
            // draws — so submit work.m_itemHandles, never a fresh query.
            .Execute([](Spark::Render::ExecuteWork& work, Spark::Render::RenderGraphExecuter&)
            {
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();
                for (size_t i = 0; i < work.m_itemHandles.size(); ++i)
                {
                    work.m_commandList->Submit(
                        rhiCtx.Get<RHI::DrawItem>(work.m_itemHandles[i]),
                        work.m_submitBase + static_cast<uint32_t>(i));
                }
            })
            .Finalize();

        // ================================================================
        // Pass 2: ResolvePass — resolve MSAA to swapchain
        // ================================================================
        SPARK_RENDER_PASS(passContext, "ResolvePass")
            .Queue(Spark::RHI::HardwareQueueClass::Graphics)
            .CustomPipeline()
            .Build([this](Spark::Render::RenderGraphBuilder& builder)
            {
                Render::ImageAttachmentBindInfo msaaBind;
                msaaBind.m_slot  = RHI::InputName("MSAABind");
                msaaBind.m_usage = RHI::AttachmentUsage::RenderTarget;
                msaaBind.m_stage = RHI::AttachmentStage::ColorAttachmentOutput;
                msaaBind.m_action.m_loadAction = RHI::AttachmentLoadAction::Load;
                builder.ReadImageAttachment<SPARK_PASS_TAG("ResolvePass")>(
                    RHI::AttachmentId("MSAAColor"), msaaBind);

                Spark::Render::ImportedImageAttachmentBindInfo resolveBind;
                resolveBind.m_slot   = Spark::RHI::InputName("ColorOutput");
                resolveBind.m_resolveSourceSlot = RHI::InputName("MSAABind");
                resolveBind.m_image  = builder.GetCurrentSwapChainResource();
                resolveBind.m_access = Spark::RHI::AttachmentAccess::Write;
                resolveBind.m_usage  = Spark::RHI::AttachmentUsage::Resolve;
                resolveBind.m_stage  = Spark::RHI::AttachmentStage::ColorAttachmentOutput;
                resolveBind.m_action.m_loadAction  = Spark::RHI::AttachmentLoadAction::DontCare;
                resolveBind.m_action.m_storeAction = Spark::RHI::AttachmentStoreAction::Store;
                builder.ImportImageAttachment<SPARK_PASS_TAG("ResolvePass")>(
                    Spark::RHI::AttachmentId("SwapChain"), resolveBind);
            })
            .Execute([](Spark::Render::ExecuteWork&, Spark::Render::RenderGraphExecuter&)
            {
                // Empty — resolve happens at EndRenderPass via m_resolveView
            })
            .Finalize();
    }

    void MSAAPassFeature::BuildGeometry()
    {
        auto& rhiCtx = *Spark::RHI::RHIExecuteContext::Current();
        m_drawable = rhiCtx.CreateEntity();

        Render::GeometrySpec drawable;
        drawable.m_drawArgs = RHI::DrawArguments(RHI::DrawLinear(g_vertexCount, 0));
        Render::VertexStreamSpec vertex;
        vertex.m_buffer     = m_vertexBuffer;
        vertex.m_vertexBufferInfo = Render::VertexBufferInfo{0, sizeof(g_triangleVertices), sizeof(TriangleVertex)};
        vertex.m_inputSlot  = 0;
        drawable.m_streams.push_back(vertex);
        drawable.m_instanceData = Render::NoInstanceBinding{};

        rhiCtx.Add<Render::GeometrySpec>(m_drawable, eastl::move(drawable));
        rhiCtx.Add<SampleDrawTag>(m_drawable);
    }

    void MSAAPassFeature::Update()
    {
        auto* window = Service<Spark::Window::IWindowSystem>::Get();
        auto windowSize = window->GetWindowSize();
        if (windowSize.x <= 0 || windowSize.y <= 0)
        {
            return;
        }
        float aspect = (float)windowSize.x / (float)windowSize.y;

        m_rotationAngle += 0.01f;

        Math::Matrix4X4 model = Math::Rotate(
            Math::Matrix4X4Const::IDENTITY,
            m_rotationAngle,
            Math::Vector3(0.f, 0.f, 1.f));
        Math::Matrix4X4 view = Math::LookAt(
            Math::Vector3(0.f, 0.f, -2.f),
            Math::Vector3(0.f, 0.f, 0.f),
            Math::Vector3(0.f, 1.f, 0.f));
        Math::Matrix4X4 proj = Math::PerspectiveFov(
            Math::Radians(45.f), aspect, 0.1f, 100.f);
        m_matrix = proj * view * model;

        m_colorPhase += 0.01f;
        for (int i = 0; i < 3; ++i)
        {
            float phase = m_colorPhase + i * 2.0f;
            m_colors[i] = Math::Vector3(
                Math::Sin(phase) * 0.5f + 0.5f,
                Math::Sin(phase + 2.0f) * 0.5f + 0.5f,
                Math::Sin(phase + 4.0f) * 0.5f + 0.5f);
        }
    }

}

int main(int, char**)
{
    using namespace Spark;

    auto sys = SandBox::InitRenderGraphApp(1024, 576, "MSAAPass");

    Render::Pipeline pipeline("MSAAPass");
    Render::PassExecuteContext::Push(pipeline.GetPassContext());

    SandBox::MSAAPassFeature feature;
    feature.Init();

    while (!sys.m_window->ShouldClose())
    {
        TickBus::Broadcast(&TickBus::Events::OnTick, 0.f);
    }

    feature.Shutdown();
    Render::PassExecuteContext::Pop();

    return 0;
}
