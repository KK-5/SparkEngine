#include "TrianglePassFeature.h"

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
#include <Request/DrawRequest.h>
#include <Shader/ShaderBindingsUtils.h>
#include <View/View.h>
#include <Drawable/Drawable.h>

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

    TrianglePassFeature::TrianglePassFeature() = default;
    TrianglePassFeature::~TrianglePassFeature() = default;

    bool TrianglePassFeature::Init()
    {
        auto* window = Service<Spark::Window::IWindowSystem>::Get();
        auto windowSize = window->GetWindowSize();
        Spark::RHI::Viewport viewport(
            0.f, (float)windowSize.x, 0.f, (float)windowSize.y);
        Spark::RHI::Scissor scissor(
            0, 0, (int32_t)windowSize.x, (int32_t)windowSize.y);
        m_viewport = viewport;
        m_scissor = scissor;

        auto assetManager = Service<Spark::Resource::AssetManager>::Get();
        ASSERT(assetManager, "[TrianglePassFeature] AssetManager service missing.");
        m_shader = assetManager->LoadAsset<Spark::Resource::ShaderAsset>(
            Spark::Resource::AssetId::Of<Spark::Resource::ShaderAsset>("Shader/TriangleMVP.hlsl"));
        ASSERT(m_shader && m_shader->GetStatus() == Spark::Resource::AssetStatus::Ready,
            "[TrianglePassFeature] TriangleMVP.hlsl load failed.");

        // The pass must exist before its per-pass SRG can be get-or-created by tag,
        // so build the pass first. UpdateViewBindings then lazily creates + fills the
        // space0 SRG (via SetPassShaderConstant) before BuildDrawRequest injects it
        // by tag (AddShaderBindings).
        CreateVertexBuffer();
        CreateTrianglePass();
        UpdateViewBindings();
        BuildDrawRequest();

        TickBus::Handler::BusConnect();
        return true;
    }

    void TrianglePassFeature::Shutdown()
    {
        TickBus::Handler::BusDisconnect();

        // Entities live in RHIContext (owned by RHISystem), not in any Ptr<>
        // member here — RAII can't reach them, so the owner has to destroy
        // them explicitly while the resource pools they reference are still
        // alive. Order: DrawRequest (consumer) → Drawable → VB → ViewSRG;
        // Components on the entity (Components::Buffer / ShaderResource / ...)
        // tear down with the entity, releasing their Ptr<> refs.
        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();
        auto destroyIfValid = [&](Spark::RHI::RHIHandle& handle)
        {
            if (handle != Spark::RHI::NullHandle && ctx.Valid(handle))
            {
                ctx.DestoryEntity(handle);
            }
            handle = Spark::RHI::NullHandle;
        };
        destroyIfValid(m_drawItem);
        destroyIfValid(m_drawable);
        destroyIfValid(m_vertexBuffer);
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

    void TrianglePassFeature::OnTick(float /*deltaTime*/)
    {
        UpdateViewBindings();
    }

    void TrianglePassFeature::CreateVertexBuffer()
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

    void TrianglePassFeature::CreateTrianglePass()
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

        auto& passContext = *Spark::Render::PassExecuteContext::Current();

        SPARK_RENDER_PASS(passContext, "TrianglePass")
            .Queue(Spark::RHI::HardwareQueueClass::Graphics)
            .VertexShader(m_shader)
            .FragmentShader(m_shader)
            .InputLayout(inputLayout)
            .RenderTargetLayout(rtLayout)
            .RenderStates(renderStates)
            .ViewportScissor(m_viewport, m_scissor)
            .Build([this](Spark::Render::RenderGraphBuilder& builder)
            {
                Spark::Render::ImportedImageAttachmentBindInfo colorBind;
                colorBind.m_slot   = Spark::RHI::InputName("ColorOutput");
                colorBind.m_image  = builder.GetCurrentSwapChainResource();
                colorBind.m_access = Spark::RHI::AttachmentAccess::Write;
                colorBind.m_usage  = Spark::RHI::AttachmentUsage::RenderTarget;
                colorBind.m_stage  = Spark::RHI::AttachmentStage::ColorAttachmentOutput;
                colorBind.m_action.m_clearValue  =
                    Spark::RHI::ClearValue::CreateVector4Float(0.1f, 0.1f, 0.15f, 1.f);
                colorBind.m_action.m_loadAction  = Spark::RHI::AttachmentLoadAction::Clear;
                colorBind.m_action.m_storeAction = Spark::RHI::AttachmentStoreAction::Store;
                builder.ImportImageAttachment<SPARK_PASS_TAG("TrianglePass")>(
                    Spark::RHI::AttachmentId("SwapChain"), colorBind);

            })
            .Execute([this](Spark::Render::ExecuteWork& work, Spark::Render::RenderGraphExecuter&)
            {
                auto& rhiCtx = *Spark::RHI::RHIExecuteContext::Current();
                auto* commandList = work.m_commandList;

                auto& view = rhiCtx.GetView<SPARK_PASS_TAG("TrianglePass"), Spark::RHI::DrawItem>();
                view.each([&](Spark::RHI::RHIHandle handle, const Spark::RHI::DrawItem& drawItem){
                    commandList->Submit(drawItem);
                });

            })
            .Finalize();
    }

    void TrianglePassFeature::BuildDrawRequest()
    {
        auto& rhiCtx = *Spark::RHI::RHIExecuteContext::Current();
        m_drawItem = rhiCtx.CreateEntity();
        m_drawable = rhiCtx.CreateEntity();

        Render::Drawable drawable;
        drawable.m_drawArgs = RHI::DrawArguments(RHI::DrawLinear(g_vertexCount, 0));
        Render::VertexStreamSpec vertex;
        vertex.m_buffer = m_vertexBuffer;
        vertex.m_byteCount = sizeof(g_triangleVertices);
        vertex.m_byteOffset = 0;
        vertex.m_byteStride = sizeof(TriangleVertex);
        vertex.m_inputSlot  = 0;
        drawable.m_streams.push_back(vertex);
        drawable.m_instanceData = Render::DirectInstanceBinding{ RHI::NullHandle };

        rhiCtx.Add<Render::Drawable>(m_drawable, drawable);

        Render::DrawRequest req;
        req.m_drawable = m_drawable;
        // space0 SRG injected by tag (get-or-created + filled in UpdateViewBindings,
        // which ran before this in Init).
        Render::AddShaderBindings<SPARK_PASS_TAG("TrianglePass")>(req, rhiCtx);

        rhiCtx.Add<Render::DrawRequest>(m_drawItem, eastl::move(req));
        rhiCtx.Add<SPARK_PASS_TAG("TrianglePass")>(m_drawItem);
    }

    void TrianglePassFeature::UpdateViewBindings()
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

        Render::View camera = Render::MakePerspectiveView(
            Math::Vector3(0.f, 0.f, -2.f),   // eye
            Math::Vector3(0.f, 0.f, 0.f),    // target
            Math::Vector3(0.f, 1.f, 0.f),    // up
            Math::Radians(45.f), aspect, 0.1f, 100.f);

        Math::Matrix4X4 mvp = camera.GetWorldToClip() * model;

        Spark::Render::SetPassShaderConstant<SPARK_PASS_TAG("TrianglePass")>(
            /*spaceId*/ 0, Spark::RHI::InputName("g_MVP"), mvp);

        m_colorPhase += 0.01f;
        Math::Vector3 colors[3];
        for (int i = 0; i < 3; ++i)
        {
            float phase = m_colorPhase + i * 2.0f;
            colors[i] = Math::Vector3(
                sinf(phase) * 0.5f + 0.5f,
                sinf(phase + 2.0f) * 0.5f + 0.5f,
                sinf(phase + 4.0f) * 0.5f + 0.5f);
        }

        Spark::Render::SetPassShaderConstant<SPARK_PASS_TAG("TrianglePass")>(
            /*spaceId*/ 0, Spark::RHI::InputName("g_Colors"), colors);
    }
}

int main(int, char**)
{
    using namespace Spark;

    auto sys = SandBox::InitRenderGraphApp(1024, 576, "TrianglePass");

    // Pipeline + Feature setup
    Render::Pipeline pipeline("Triangle");
    Render::PassExecuteContext::Push(pipeline.GetPassContext());

    SandBox::TrianglePassFeature feature;
    feature.Init();

    while (!sys.m_window->ShouldClose())
    {
        TickBus::Broadcast(&TickBus::Events::OnTick, 0.f);
    }

    feature.Shutdown();
    Render::PassExecuteContext::Pop();

    return 0;
}
