#include "TrianglePassFeature.h"

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
#include <RHI/Resource/Buffer/VertexInputView.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
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
        m_swapchainView = FindSwapChainView();
        if (m_swapchainView == Spark::RHI::NullHandle)
        {
            LOG_ERROR("[TrianglePassFeature] No swap chain view found in RHIContext.");
            return false;
        }

        CreateViewSRG();
        CreateVertexBuffer();
        CreateTrianglePass();

        TickBus::Handler::BusConnect();
        return true;
    }

    void TrianglePassFeature::Shutdown()
    {
        TickBus::Handler::BusDisconnect();
    }

    void TrianglePassFeature::OnTick(float /*deltaTime*/)
    {
        UpdateViewSRG();
    }

    Spark::RHI::RHIHandle TrianglePassFeature::FindSwapChainView() const
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

    void TrianglePassFeature::CreateViewSRG()
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
        m_srgLayout->SetBindingSlot(0);

        bool ok = m_srgLayout->Finalize();
        ASSERT(ok, "[TrianglePassFeature] SRG layout Finalize failed.");

        m_srg = factory->CreateShaderResource();
        if (m_srg->Init(*device, m_srgLayout) != Spark::RHI::ResultCode::Success)
        {
            LOG_ERROR("[TrianglePassFeature] ShaderResource Init failed.");
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

    void TrianglePassFeature::CreateVertexBuffer()
    {
        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();

        // Resource entity: PendingBufferInit materialized by RHIResourceSystem,
        // PendingBufferUpload submitted by AsyncUploadSystem.
        m_vbEntity = ctx.CreateEntity();
        ctx.Add<Spark::RHI::ImportedTag>(m_vbEntity);
        ctx.Add<Spark::RHI::ResourceName>(m_vbEntity,
            Spark::RHI::ResourceName{ ObjectName("TriangleVB") });

        Spark::RHI::PendingBufferInit init;
        init.m_descriptor.m_bindFlags =
            Spark::RHI::BufferBindFlags::InputAssembly | Spark::RHI::BufferBindFlags::CopyWrite;
        init.m_descriptor.m_byteCount = sizeof(g_triangleVertices);
        init.m_descriptor.m_sharedQueueMask = Spark::RHI::HardwareQueueClassMask::All;
        init.m_heapMemoryLevel = Spark::RHI::HeapMemoryLevel::Device;
        ctx.Add<Spark::RHI::PendingBufferInit>(m_vbEntity, init);

        Spark::RHI::PendingBufferUpload upload;
        upload.m_data = g_triangleVertices;
        upload.m_dataSize = sizeof(g_triangleVertices);
        upload.m_destinationOffset = 0;
        ctx.Add<Spark::RHI::PendingBufferUpload>(m_vbEntity, upload);
        ctx.Add<Spark::RHI::UploadPendingTag>(m_vbEntity);

        // View entity: needed by ImportBufferAttachment. RHIResourceSystem
        // materializes Components::BufferView from (BufferViewDescriptor +
        // ViewHierarchy) on the same OnFrameBegin tick that materializes the
        // resource. The view itself has no SRV/UAV/CBV (the buffer's bind
        // flags don't include ShaderRead/Write/Constant) — BufferView::Init
        // just records the GPU address, which is exactly what we need to let
        // the RG barrier compiler see this resource as a buffer attachment.
        m_vbViewEntity = ctx.CreateEntity();
        ctx.Add<Spark::RHI::ResourceName>(m_vbViewEntity,
            Spark::RHI::ResourceName{ ObjectName("TriangleVB.View") });
        ctx.Add<Spark::RHI::BufferViewDescriptor>(m_vbViewEntity,
            Spark::RHI::BufferViewDescriptor::CreateRaw(0, sizeof(g_triangleVertices)));
        Spark::RHI::ViewHierarchy hierarchy;
        hierarchy.m_resource = m_vbEntity;
        ctx.Add<Spark::RHI::ViewHierarchy>(m_vbViewEntity, hierarchy);
    }

    void TrianglePassFeature::CreateTrianglePass()
    {
        auto assetManager = Service<Spark::Resource::AssetManager>::Get();
        ASSERT(assetManager, "[TrianglePassFeature] AssetManager service missing.");
        Ptr<Spark::Resource::ShaderAsset> shader =
            assetManager->LoadAsset<Spark::Resource::ShaderAsset>(
                Spark::Resource::AssetId("Shader/TriangleMVP.hlsl"));
        ASSERT(shader && shader->GetStatus() == Spark::Resource::AssetStatus::Ready,
            "[TrianglePassFeature] TriangleMVP.hlsl load failed.");
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

        auto& passContext = *Spark::Render::PassExecuteContext::Current();

        SPARK_RENDER_PASS(passContext, "TrianglePass")
            .Queue(Spark::RHI::HardwareQueueClass::Graphics)
            .VertexShader(m_vertShader)
            .FragmentShader(m_fragShader)
            .InputLayout(inputLayout)
            .RenderTargetLayout(rtLayout)
            .RenderStates(renderStates)
            .ShaderResource(0, m_viewSRGEntity)
            .Build([this](Spark::Render::RenderGraphBuilder& builder)
            {
                Spark::Render::ImportedImageAttachmentBindInfo colorBind;
                colorBind.m_slot   = Spark::RHI::InputName("ColorOutput");
                colorBind.m_view   = m_swapchainView;
                colorBind.m_access = Spark::RHI::AttachmentAccess::Write;
                colorBind.m_usage  = Spark::RHI::AttachmentUsage::RenderTarget;
                colorBind.m_stage  = Spark::RHI::AttachmentStage::ColorAttachmentOutput;
                colorBind.m_action.m_clearValue  =
                    Spark::RHI::ClearValue::CreateVector4Float(0.1f, 0.1f, 0.15f, 1.f);
                colorBind.m_action.m_loadAction  = Spark::RHI::AttachmentLoadAction::Clear;
                colorBind.m_action.m_storeAction = Spark::RHI::AttachmentStoreAction::Store;
                builder.ImportImageAttachment<SPARK_PASS_TAG("TrianglePass")>(
                    Spark::RHI::AttachmentId("SwapChain"), colorBind);

                // Importing the VB as a buffer attachment is how RG sees the
                // cross-queue handoff: barrier compile picks up PendingSync
                // stamped by AsyncUploadSystem and emits queue.Wait(uploadFence)
                // + acquire barrier (Copy/COMMON → InputAssembly @ Graphics)
                // before this pass on the graphics queue.
                Spark::Render::ImportedBufferAttachmentBindInfo vbBind;
                vbBind.m_slot   = Spark::RHI::InputName("TriangleVB");
                vbBind.m_view   = m_vbViewEntity;
                vbBind.m_access = Spark::RHI::AttachmentAccess::Read;
                vbBind.m_usage  = Spark::RHI::AttachmentUsage::InputAssembly;
                vbBind.m_stage  = Spark::RHI::AttachmentStage::VertexInput;
                builder.ImportBufferAttachment<SPARK_PASS_TAG("TrianglePass")>(
                    Spark::RHI::AttachmentId("TriangleVB"), vbBind);
            })
            .Execute([this](Spark::Render::ExecuteWork& work, Spark::Render::RenderGraphExecuter&)
            {
                auto& rhiCtx = *Spark::RHI::RHIExecuteContext::Current();

                // VB is imported as a buffer attachment in Build above, so
                // ImportBufferAttachment's validation has already asserted
                // BackingBuffer is wired. Cross-queue sync (queue.Wait on the
                // upload fence + acquire barrier) is emitted by the RG barrier
                // compiler from PendingSync — no CPU wait needed here.
                auto* backing = rhiCtx.TryGet<Spark::Render::BackingBuffer>(m_vbEntity);
                ASSERT(backing && backing->m_buffer, "[TrianglePassFeature] VB has no BackingBuffer at execute.");

                auto* commandList = work.m_commandList;

                auto* window = Service<Spark::Window::IWindowSystem>::Get();
                auto windowSize = window->GetWindowSize();
                Spark::RHI::Viewport viewport(
                    0.f, (float)windowSize.first, 0.f, (float)windowSize.second);
                Spark::RHI::Scissor scissor(
                    0, 0, (int32_t)windowSize.first, (int32_t)windowSize.second);
                commandList->SetViewport(viewport);
                commandList->SetScissor(scissor);

                Spark::RHI::DrawItem drawItem;
                drawItem.m_drawArguments =
                    Spark::RHI::DrawArguments(Spark::RHI::DrawLinear(g_vertexCount, 0));
                drawItem.m_drawInstanceArgs = Spark::RHI::DrawInstanceArguments(1, 0);

                Spark::RHI::VertexInputView vbView(
                    *backing->m_buffer,
                    0,
                    sizeof(g_triangleVertices),
                    sizeof(TriangleVertex));
                drawItem.m_vertexBufferView.AddVertexInputView(vbView);

                commandList->Submit(drawItem);
            })
            .Finalize();
    }

    void TrianglePassFeature::UpdateViewSRG()
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

        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();
        ctx.AddOrReplace<Spark::RHI::ShaderResourceUpdateTag>(m_viewSRGEntity);
    }
}
