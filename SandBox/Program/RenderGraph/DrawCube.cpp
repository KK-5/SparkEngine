#include "DrawCube.h"

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
#include <RHI/ResourceBuilder.h>
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
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImageSubResource.h>
#include <RHI/Resource/ShaderResource/InputStreamLayoutBuilder.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
#include <RHI/Resource/ShaderInput/ShaderInput.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/Pipeline/RenderTargetLayout.h>

#include <Resource/Asset.h>
#include <Resource/AssetManager.h>
#include <Resource/Shader/ShaderAsset.h>
#include <Resource/Model/ModelAsset.h>
#include <Resource/Image/ImageAsset.h>

#include <Pass/PassContext.h>
#include <Pass/PassBuilder.h>
#include <Pass/PassTag.h>
#include <Pass/PassAccess.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphExecuter.h>

#include <Window/IWindowSystem.h>

#include "../Common/RenderGraphUtil.h"

namespace Spark::SandBox
{

    DrawCube::DrawCube() = default;
    DrawCube::~DrawCube() = default;

    bool DrawCube::Init()
    {
        m_swapchainView = FindSwapChainView();
        if (m_swapchainView == Spark::RHI::NullHandle)
        {
            LOG_ERROR("[DrawCube] No swap chain view found in RHIContext.");
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

        // Pass must exist before CreatePassShaderBindings can look it up by tag.
        LoadAsset();
        CreateImage();
        CreateVertexBuffer();
        CreatePasses();
        CreateViewBindings();

        TickBus::Handler::BusConnect();
        return true;
    }

    void DrawCube::Shutdown()
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
        destroyIfValid(m_indexViewEntity);
        destroyIfValid(m_imageViewEntity);
        destroyIfValid(m_vbEntity);
        destroyIfValid(m_indexEntity);
        destroyIfValid(m_imageEntity);
        destroyIfValid(m_viewBindingsEntity);

        m_viewBindings.reset();
    }

    void DrawCube::OnTick(float /*deltaTime*/)
    {
        UpdateViewBindings();
        BuildDrawItemEntity();
    }

    Spark::RHI::RHIHandle DrawCube::FindSwapChainView() const
    {
        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();
        Spark::RHI::RHIHandle found = Spark::RHI::NullHandle;
        ctx.GetView<Spark::Render::SwapChainViews>().each(
            [&](Spark::RHI::RHIHandle h, const Spark::Render::SwapChainViews&)
            {
                if (found == Spark::RHI::NullHandle)
                {
                    found = h;
                }
            });
        return found;
    }

    void DrawCube::LoadAsset()
    {
        auto assetManager = Service<Spark::Resource::AssetManager>::Get();
        ASSERT(assetManager, "[DrawCube] AssetManager service missing.");
        m_shader = assetManager->LoadAsset<Spark::Resource::ShaderAsset>(
            Spark::Resource::AssetId::Of<Spark::Resource::ShaderAsset>("Shader/CubeTextured.hlsl"));
        ASSERT(m_shader && m_shader->GetStatus() == Spark::Resource::AssetStatus::Ready,
            "[DrawCube] CubeTextured.hlsl load failed.");

        m_model = assetManager->LoadAsset<Resource::ModelAsset>(Resource::AssetId::Of<Resource::ModelAsset>("Model/CubeTextured.glb"));
        ASSERT(m_model && m_model->GetStatus() == Spark::Resource::AssetStatus::Ready,
            "[DrawCube] Model/CubeTextured.glb load failed.");

        auto* modelAssetData = m_model->GetModelData();
        if (modelAssetData->GetImageAssetCount() > 0)
        {
            m_image = assetManager->LoadAsset<Resource::ImageAsset>(modelAssetData->GetImageAssetId(0));
            ASSERT(m_image && m_image->GetStatus() == Spark::Resource::AssetStatus::Ready,
                "[DrawCube] embeded image load failed.");
        }
    }

    void DrawCube::CreateViewBindings()
    {
        auto& passCtx = *Spark::Render::PassExecuteContext::Current();
        auto& rhiCtx  = *Spark::RHI::RHIExecuteContext::Current();

        auto handle = Spark::Render::CreatePassShaderBindings<SPARK_PASS_TAG("ScenePass")>(
            passCtx, rhiCtx, /*spaceId*/ 0);
        if (!handle.m_bindings)
        {
            LOG_ERROR("[DrawCube] CreatePassShaderBindings failed.");
            return;
        }

        m_viewBindings       = handle.m_bindings;
        m_viewBindingsEntity = handle.m_entity;

        Spark::Render::AttachShaderBindings<SPARK_PASS_TAG("ScenePass")>(
            passCtx, /*spaceId*/ 0, m_viewBindings);
    }

    void DrawCube::CreateVertexBuffer()
    {
        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();

        auto* mesh = m_model->GetModelData()->GetMesh(0);
        ASSERT(mesh->primitives.size() > 0, "No primitive in mesh.");
        const Resource::Primitive& primitive = mesh->primitives[0];

        Spark::RHI::BufferDescriptor vbDesc;
        vbDesc.m_bindFlags =
            Spark::RHI::BufferBindFlags::InputAssembly | Spark::RHI::BufferBindFlags::CopyWrite;
        vbDesc.m_byteCount = primitive.vertexBuffer.size();
        vbDesc.m_sharedQueueMask = Spark::RHI::HardwareQueueClassMask::Graphics;

        m_vbEntity = Spark::RHI::CreateStaticBuffer(ctx, ObjectName("CubeVertex"), vbDesc);
        Spark::RHI::RequestBufferUpload(
            ctx, m_vbEntity, primitive.vertexBuffer.data(), primitive.vertexBuffer.size());
        Spark::Render::CreateStaticBufferAttachment(ctx, m_vbEntity,
            Spark::RHI::InputName("CubeVertex"),
            Spark::RHI::AttachmentAccess::Read,
            Spark::RHI::AttachmentUsage::InputAssembly,
            Spark::RHI::AttachmentStage::VertexInput);

        m_vbViewEntity = Spark::RHI::CreateBufferView(ctx, m_vbEntity,
            ObjectName("CubeVertex.View"),
            Spark::RHI::BufferViewDescriptor::CreateRaw(0, primitive.vertexBuffer.size()));

        Spark::RHI::BufferDescriptor ibDesc;
        ibDesc.m_bindFlags =
            Spark::RHI::BufferBindFlags::InputAssembly | Spark::RHI::BufferBindFlags::CopyWrite;
        ibDesc.m_byteCount = primitive.indexBuffer.size();
        ibDesc.m_sharedQueueMask = Spark::RHI::HardwareQueueClassMask::Graphics;

        m_indexEntity = Spark::RHI::CreateStaticBuffer(ctx, ObjectName("CubeIndex"), ibDesc);
        Spark::RHI::RequestBufferUpload(
            ctx, m_indexEntity, primitive.indexBuffer.data(), primitive.indexBuffer.size());
        Spark::Render::CreateStaticBufferAttachment(ctx, m_indexEntity,
            Spark::RHI::InputName("CubeIndex"),
            Spark::RHI::AttachmentAccess::Read,
            Spark::RHI::AttachmentUsage::InputAssembly,
            Spark::RHI::AttachmentStage::VertexInput);

        m_indexViewEntity = Spark::RHI::CreateBufferView(ctx, m_indexEntity,
            ObjectName("CubeIndex.View"),
            Spark::RHI::BufferViewDescriptor::CreateRaw(0, primitive.indexBuffer.size()));
    }

    void DrawCube::CreateImage()
    {
        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();

        RHI::ImageDescriptor desc = RHI::ImageDescriptor::Create2D(
            RHI::ImageBindFlags::ShaderRead, 
            m_image->GetWidth(), 
            m_image->GetHeight(), 
            m_image->GetFormat());
        desc.m_sharedQueueMask = Spark::RHI::HardwareQueueClassMask::Graphics;
        desc.m_mipLevels = m_image->GetMipLevels();
        desc.m_bindFlags = RHI::ImageBindFlags::ShaderRead | RHI::ImageBindFlags::CopyWrite;

        m_imageEntity = RHI::CreateStaticImage(
            ctx,
            ObjectName("BaseColorImage"),
            desc,
            Spark::RHI::HeapMemoryLevel::Device,
            Spark::RHI::HostMemoryAccess::Write
        );

        RHI::RequestImageUpload(
            ctx,
            m_imageEntity,
            m_image->GetImageData()->GetTextureBytes().data(),
            m_image->GetImageData()->GetTextureBytes().size(),
            RHI::ImageSubresourceRange(desc),
            RHI::Origin(),
            m_image->GetFormat()
        );

        Render::CreateStaticImageAttachment(
            ctx,
            m_imageEntity,
            Spark::RHI::InputName("BaseColorImage"),
            Spark::RHI::AttachmentAccess::Read,
            Spark::RHI::AttachmentUsage::Shader,
            Spark::RHI::AttachmentStage::FragmentShader
        );

        m_imageViewEntity = RHI::CreateImageView(
            ctx,
            m_imageEntity,
            ObjectName("BaseColorImage.View"),
            RHI::ImageViewDescriptor::Create(m_image->GetFormat(), 0, m_image->GetMipLevels() - 1)
        );
    }

    void DrawCube::CreatePasses()
    {
        auto* mesh = m_model->GetModelData()->GetMesh(0);
        ASSERT(mesh->primitives.size() > 0, "No primitive in mesh.");
        Resource::Primitive primitive = mesh->primitives[0];

        Spark::RHI::InputStreamLayoutBuilder islBuilder;
        islBuilder.Begin();
        islBuilder.SetTopology(Spark::RHI::PrimitiveTopology::TriangleList);
        auto* bufferBuilder = islBuilder.AddBuffer();
        for(const Resource::VertexAttribute& vert : primitive.layout.attributes)
        {
            bufferBuilder->Channel(vert.semantic, vert.semanticIndex, vert.format);
        }
        Spark::RHI::InputStreamLayout inputLayout = islBuilder.End();

        Spark::RHI::RenderTargetLayout rtLayout;
        rtLayout.m_colorAttachmentCount = 1;
        rtLayout.m_colorFormats[0]      = Spark::RHI::Format::R8G8B8A8_UNORM;
        rtLayout.m_depthStencilFormat   = Spark::RHI::Format::D32_FLOAT;

        Spark::RHI::RenderStates renderStates;
        renderStates.m_depthStencilState.m_depth.m_enable    = 1;
        renderStates.m_depthStencilState.m_depth.m_writeMask = Spark::RHI::DepthWriteMask::All;
        renderStates.m_depthStencilState.m_depth.m_func      = Spark::RHI::ComparisonFunc::Less;
        renderStates.m_depthStencilState.m_stencil.m_enable  = 0;
        renderStates.m_rasterState.m_cullMode                = Spark::RHI::CullMode::Back;
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

                auto depthDesc = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::DepthStencil,
                    windowSize.first, windowSize.second,
                    RHI::Format::D32_FLOAT
                );
                depthDesc.m_multisampleState = RHI::MultisampleState(4, 0);

                Render::ImageAttachmentBindInfo depthBind;
                depthBind.m_slot   = Spark::RHI::InputName("SceneDepth");
                depthBind.m_usage  = Spark::RHI::AttachmentUsage::DepthStencil;
                depthBind.m_stage  = Spark::RHI::AttachmentStage::EarlyFragmentTest |
                                     Spark::RHI::AttachmentStage::LateFragmentTest;
                depthBind.m_action.m_clearValue  = Spark::RHI::ClearValue::CreateDepth(1.0f);
                depthBind.m_action.m_loadAction  = Spark::RHI::AttachmentLoadAction::Clear;
                depthBind.m_action.m_storeAction = Spark::RHI::AttachmentStoreAction::DontCare;

                builder.CreateImageAttachment<SPARK_PASS_TAG("ScenePass")>(
                    RHI::AttachmentId("SceneDepth"), depthDesc, depthBind, RHI::AttachmentAccess::Write);
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

    void DrawCube::BuildDrawItemEntity()
    {
        if (m_drawItemEntity != Spark::RHI::NullHandle)
        {
            return;
        }

        auto& rhiCtx = *Spark::RHI::RHIExecuteContext::Current();

        auto* vb = rhiCtx.TryGet<Spark::RHI::Components::Buffer>(m_vbEntity);
        auto* ib = rhiCtx.TryGet<Spark::RHI::Components::Buffer>(m_indexEntity);
        if (vb && vb->m_buffer && ib && ib->m_buffer)
        {
            auto* mesh = m_model->GetModelData()->GetMesh(0);
            const Resource::Primitive& primitive = mesh->primitives[0];

            const uint32_t indexCount =
                static_cast<uint32_t>(primitive.indexBuffer.size() / sizeof(uint32_t));

            m_drawItemEntity = rhiCtx.CreateEntity();
            Spark::RHI::DrawItem drawItem;
            drawItem.m_drawArguments =
                Spark::RHI::DrawArguments(Spark::RHI::DrawIndexed(0, indexCount, 0));
            drawItem.m_drawInstanceArgs = Spark::RHI::DrawInstanceArguments(1, 0);

            Spark::RHI::VertexInputView vbView(
                *vb->m_buffer,
                0,
                static_cast<uint32_t>(primitive.vertexBuffer.size()),
                primitive.layout.stride);
            drawItem.m_vertexBufferView.AddVertexInputView(vbView);

            drawItem.m_indexBufferView = Spark::RHI::IndexBufferView(
                *ib->m_buffer,
                0,
                static_cast<uint32_t>(primitive.indexBuffer.size()),
                Spark::RHI::IndexFormat::UINT32);

            drawItem.m_viewportsCount = 1;
            drawItem.m_scissorsCount = 1;
            drawItem.m_viewports.push_back(m_viewport);
            drawItem.m_scissors.push_back(m_scissor);

            rhiCtx.Add<Spark::RHI::DrawItem>(m_drawItemEntity, eastl::move(drawItem));
            rhiCtx.Add<SPARK_PASS_TAG("ScenePass")>(m_drawItemEntity);
        }
    }

    void DrawCube::UpdateViewBindings()
    {
        if (!m_viewBindings || m_viewBindingsEntity == Spark::RHI::NullHandle)
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
            Math::Vector3(0.f, 1.f, 0.f));   // spin around world up
        Math::Matrix4X4 view = Math::LookAt(
            Math::Vector3(0.f, 5.f, -5.f),
            Math::Vector3(0.f, 0.f, 0.f),
            Math::Vector3(0.f, 1.f, 0.f));
        Math::Matrix4X4 proj = Math::PerspectiveFov(
            Math::Radians(45.f), aspect, 0.1f, 100.f);
        Math::Matrix4X4 mvp = proj * view * model;

        auto* mvpInput = m_viewBindings->FindConstantInput(Spark::RHI::InputName("g_MVP"));
        ASSERT(mvpInput, "No g_MVP shader input.");
        mvpInput->SetData(&mvp, sizeof(mvp));

        auto& rhiCtx = *Spark::RHI::RHIExecuteContext::Current();
        if (auto* iv = rhiCtx.TryGet<Spark::RHI::Components::ImageView>(m_imageViewEntity))
        {
            if (iv->m_view)
            {
                auto* texInput = m_viewBindings->FindImageInput(Spark::RHI::InputName("g_Texture"));
                ASSERT(texInput, "No g_Texture shader input.");
                texInput->SetView(/*arrayIndex*/0, iv->m_view.get());
            }
        }

        auto* samplerInput = m_viewBindings->FindSamplerInput(Spark::RHI::InputName("g_Sampler"));
        ASSERT(samplerInput, "No g_Sampler shader input.");
        samplerInput->SetState(/*arrayIndex*/0, m_samplerState);

        Spark::Render::MarkShaderBindingsUpdate(rhiCtx, m_viewBindingsEntity);
    }

}

int main(int, char**)
{
    using namespace Spark;

    auto sys = SandBox::InitRenderGraphApp(1024, 576, "MSAAPass");

    Render::Pipeline pipeline("MSAAPass");
    Render::PassExecuteContext::Push(pipeline.GetPassContext());

    SandBox::DrawCube feature;
    feature.Init();

    while (!sys.m_window->ShouldClose())
    {
        TickBus::Broadcast(&TickBus::Events::OnTick, 0.f);
    }

    feature.Shutdown();
    Render::PassExecuteContext::Pop();

    return 0;
}
