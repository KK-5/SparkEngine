#include "MSAAPassFeature.h"

#include <Log/SpdLogSystem.h>
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
#include <RHI/Context/RHIContext.h>
#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/ClearValue.h>
#include <RHI/MultisampleState.h>
#include <RHI/Resource/Buffer/VertexInputView.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/ShaderResource/InputStreamLayoutBuilder.h>
#include <RHI/Resource/ShaderResource/ShaderResource.h>
#include <RHI/Resource/ShaderResource/ShaderResourceLayout.h>
#include <RHI/Resource/ShaderResource/ShaderResourceDescriptor.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/Pipeline/RenderTargetLayout.h>

#include <Resource/Asset.h>
#include <Resource/AssetManager.h>
#include <Resource/Shader/ShaderAsset.h>

#include <Pass/PassContext.h>
#include <Pass/PassBuilder.h>
#include <Pass/PassTag.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphExecuter.h>

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
        m_swapchainView = FindSwapChainView();
        if (m_swapchainView == Spark::RHI::NullHandle)
        {
            LOG_ERROR("[MSAAPassFeature] No swap chain view found in RHIContext.");
            return false;
        }

        auto* window = Service<Spark::Window::IWindowSystem>::Get();
        auto windowSize = window->GetWindowSize();
        Spark::RHI::Viewport viewport(
            0.f, (float)windowSize.first, 0.f, (float)windowSize.second);
        Spark::RHI::Scissor scissor(
            0, 0, (int32_t)windowSize.first, (int32_t)windowSize.second);
        m_viewport = viewport;
        m_scissor = scissor;

        CreateViewSRG();
        CreateVertexBuffer();
        CreatePasses();

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
        destroyIfValid(m_drawItemEntity);
        destroyIfValid(m_vbViewEntity);
        destroyIfValid(m_vbEntity);
        destroyIfValid(m_viewSRGEntity);
    }

    void MSAAPassFeature::OnTick(float /*deltaTime*/)
    {
        UpdateViewSRG();
        BuildDrawItemEntity();
    }

    Spark::RHI::RHIHandle MSAAPassFeature::FindSwapChainView() const
    {
        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();
        Spark::RHI::RHIHandle found = Spark::RHI::NullHandle;
        ctx.GetView<Spark::RHI::ImportedTag, Spark::Render::SwapChainViews>().each(
            [&](Spark::RHI::RHIHandle h, const Spark::Render::SwapChainViews&)
            {
                if (found == Spark::RHI::NullHandle)
                {
                    found = h;
                }
            });
        return found;
    }

    void MSAAPassFeature::CreateViewSRG()
    {
        auto* rhi = Service<Spark::RHI::RHIInterface>::Get();
        auto* factory = rhi->GetRHIFactory();
        auto* device  = rhi->GetDevice();

        m_srgLayout = factory->CreateShaderResourceLayout();

        Spark::RHI::ShaderInputConstantDescriptor mvpConstant(
            Spark::RHI::InputName("g_MVP"),
            0,
            sizeof(Math::Matrix4X4),
            0,
            0);
        m_srgLayout->AddShaderInput(mvpConstant);

        Spark::RHI::ShaderInputConstantDescriptor vColor
        (
            Spark::RHI::InputName("g_Color"),
            sizeof(Math::Matrix4X4),
            sizeof(Math::Vector3) * 3,
            0,
            0
        );
        m_srgLayout->AddShaderInput(vColor);

        bool ok = m_srgLayout->Finalize();
        ASSERT(ok, "[MSAAPassFeature] SRG layout Finalize failed.");

        m_srg = factory->CreateShaderResource();
        if (m_srg->Init(*device, m_srgLayout) != Spark::RHI::ResultCode::Success)
        {
            LOG_ERROR("[MSAAPassFeature] ShaderResource Init failed.");
            return;
        }

        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();
        m_viewSRGEntity = ctx.CreateEntity();
        ctx.Add<Spark::RHI::ImportedTag>(m_viewSRGEntity);
        ctx.Add<Spark::RHI::ShaderResourceTag>(m_viewSRGEntity);
        ctx.Add<Spark::RHI::ResourceName>(m_viewSRGEntity,
            Spark::RHI::ResourceName{ ObjectName("ViewSRG") });
        ctx.Add<Spark::RHI::Components::ShaderResourceLayout>(m_viewSRGEntity,
            Spark::RHI::Components::ShaderResourceLayout{ m_srgLayout });
        ctx.Add<Spark::RHI::Components::ShaderResource>(m_viewSRGEntity,
            Spark::RHI::Components::ShaderResource{ m_srg });
    }

    void MSAAPassFeature::CreateVertexBuffer()
    {
        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();

        m_vbEntity = ctx.CreateEntity();
        ctx.Add<Spark::RHI::ImportedTag>(m_vbEntity);
        ctx.Add<Spark::RHI::ResourceName>(m_vbEntity,
            Spark::RHI::ResourceName{ ObjectName("TriangleVB") });

        Spark::RHI::PendingBufferInit init;
        init.m_descriptor.m_bindFlags =
            Spark::RHI::BufferBindFlags::InputAssembly | Spark::RHI::BufferBindFlags::CopyWrite;
        init.m_descriptor.m_byteCount = sizeof(g_triangleVertices);
        init.m_descriptor.m_sharedQueueMask = Spark::RHI::HardwareQueueClassMask::Graphics;
        init.m_heapMemoryLevel = Spark::RHI::HeapMemoryLevel::Device;
        ctx.Add<Spark::RHI::PendingBufferInit>(m_vbEntity, init);

        Spark::RHI::PendingBufferUpload upload;
        upload.m_data = g_triangleVertices;
        upload.m_dataSize = sizeof(g_triangleVertices);
        upload.m_destinationOffset = 0;
        ctx.Add<Spark::RHI::PendingBufferUpload>(m_vbEntity, upload);
        ctx.Add<Spark::RHI::UploadPendingTag>(m_vbEntity);

        m_vbViewEntity = ctx.CreateEntity();
        ctx.Add<Spark::RHI::ResourceName>(m_vbViewEntity,
            Spark::RHI::ResourceName{ ObjectName("TriangleVB.View") });
        ctx.Add<Spark::RHI::BufferViewDescriptor>(m_vbViewEntity,
            Spark::RHI::BufferViewDescriptor::CreateRaw(0, sizeof(g_triangleVertices)));
        Spark::RHI::ViewHierarchy hierarchy;
        hierarchy.m_resource = m_vbEntity;
        ctx.Add<Spark::RHI::ViewHierarchy>(m_vbViewEntity, hierarchy);
    }

    void MSAAPassFeature::CreatePasses()
    {
        auto assetManager = Service<Spark::Resource::AssetManager>::Get();
        ASSERT(assetManager, "[MSAAPassFeature] AssetManager service missing.");
        Ptr<Spark::Resource::ShaderAsset> shader =
            assetManager->LoadAsset<Spark::Resource::ShaderAsset>(
                Spark::Resource::AssetId("Shader/TriangleMVP.hlsl"));
        ASSERT(shader && shader->GetStatus() == Spark::Resource::AssetStatus::Ready,
            "[MSAAPassFeature] TriangleMVP.hlsl load failed.");
        m_vertShader = shader;
        m_fragShader = shader;

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
            .VertexShader(m_vertShader)
            .FragmentShader(m_fragShader)
            .InputLayout(inputLayout)
            .RenderTargetLayout(rtLayout)
            .RenderStates(renderStates)
            .ShaderResource(0, m_viewSRGEntity)
            .Build([this, windowSize](Spark::Render::RenderGraphBuilder& builder)
            {
                auto imageDesc = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::Color | RHI::ImageBindFlags::ShaderRead,
                    windowSize.first, windowSize.second,
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


                Spark::Render::ImportedBufferAttachmentBindInfo vbBind;
                vbBind.m_slot   = Spark::RHI::InputName("TriangleVB");
                vbBind.m_view   = m_vbViewEntity;
                vbBind.m_access = Spark::RHI::AttachmentAccess::Read;
                vbBind.m_usage  = Spark::RHI::AttachmentUsage::InputAssembly;
                vbBind.m_stage  = Spark::RHI::AttachmentStage::VertexInput;
                builder.ImportBufferAttachment<SPARK_PASS_TAG("ScenePass")>(
                    Spark::RHI::AttachmentId("TriangleVB"), vbBind);
            })
            .Execute([this](Spark::Render::ExecuteWork& work, Spark::Render::RenderGraphExecuter&)
            {
                auto& rhiCtx = *RHI::RHIExecuteContext::Current();
                auto* cmdList = work.m_commandList;
                auto& view = rhiCtx.GetView<SPARK_PASS_TAG("ScenePass"), RHI::DrawItem>();
                view.each([&](RHI::RHIHandle handle, const RHI::DrawItem& drawItem) {
                    cmdList->Submit(drawItem);
                });
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
                resolveBind.m_view   = m_swapchainView;
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

    void MSAAPassFeature::BuildDrawItemEntity()
    {
        if (m_drawItemEntity != Spark::RHI::NullHandle)
        {
            return;
        }

        auto& rhiCtx = *Spark::RHI::RHIExecuteContext::Current();

        auto* vertexBuffer = rhiCtx.TryGet<Spark::RHI::Components::Buffer>(m_vbEntity);
        if (vertexBuffer && vertexBuffer->m_buffer)
        {
            m_drawItemEntity = rhiCtx.CreateEntity();
            Spark::RHI::DrawItem drawItem;
            drawItem.m_drawArguments =
                Spark::RHI::DrawArguments(Spark::RHI::DrawLinear(g_vertexCount, 0));
            drawItem.m_drawInstanceArgs = Spark::RHI::DrawInstanceArguments(1, 0);

            Spark::RHI::VertexInputView vbView(
                *vertexBuffer->m_buffer,
                0,
                sizeof(g_triangleVertices),
                sizeof(TriangleVertex));
            drawItem.m_vertexBufferView.AddVertexInputView(vbView);
            drawItem.m_viewportsCount = 1;
            drawItem.m_scissorsCount = 1;
            drawItem.m_viewports.push_back(m_viewport);
            drawItem.m_scissors.push_back(m_scissor);

            rhiCtx.Add<Spark::RHI::DrawItem>(m_drawItemEntity, eastl::move(drawItem));
            rhiCtx.Add<SPARK_PASS_TAG("ScenePass")>(m_drawItemEntity);
        }
    }

    void MSAAPassFeature::UpdateViewSRG()
    {
        if (!m_srg || !m_srgLayout || m_viewSRGEntity == Spark::RHI::NullHandle)
        {
            return;
        }

        auto* window = Service<Spark::Window::IWindowSystem>::Get();
        auto windowSize = window->GetWindowSize();
        if (windowSize.first <= 0 || windowSize.second <= 0)
        {
            return;
        }
        float aspect = (float)windowSize.first / (float)windowSize.second;

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
        Math::Matrix4X4 mvp = proj * view * model;

        Spark::RHI::ShaderInputIndex mvpIdx =
            m_srgLayout->FindShaderInputConstantIndex(Spark::RHI::InputName("g_MVP"));
        m_srg->SetConstantRaw(mvpIdx, &mvp, sizeof(mvp));

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

        Spark::RHI::ShaderInputIndex colorIdx =
            m_srgLayout->FindShaderInputConstantIndex(Spark::RHI::InputName("g_Color"));
        m_srg->SetConstantRaw(colorIdx, colors, sizeof(colors));

        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();
        ctx.AddOrReplace<Spark::RHI::ShaderResourceUpdateTag>(m_viewSRGEntity);
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
