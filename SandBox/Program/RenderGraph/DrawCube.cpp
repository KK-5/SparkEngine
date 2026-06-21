#include "DrawCube.h"

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
#include <RHI/Resource/Buffer/VertexInputView.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImageSubResource.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
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
#include <Pass/RenderPass.h>
#include <Pass/PassTag.h>
#include <Pass/PassAccess.h>
#include <Pass/Component/PassComponents.h>
#include <Pass/Component/RHIComponents.h>
#include <RenderGraph/RenderGraphBuilder.h>
#include <RenderGraph/RenderGraphExecuter.h>
#include <Request/DrawRequest.h>
#include <View/View.h>

#include <Window/IWindowSystem.h>

#include "../Common/RenderGraphUtil.h"

namespace Spark::SandBox
{

    DrawCube::DrawCube() = default;
    DrawCube::~DrawCube() = default;

    bool DrawCube::Init()
    {
        auto* window = Service<Spark::Window::IWindowSystem>::Get();
        auto windowSize = window->GetWindowSize();
        Spark::RHI::Viewport viewport(
            0.f, (float)windowSize.x, 0.f, (float)windowSize.y);
        Spark::RHI::Scissor scissor(
            0, 0, (int32_t)windowSize.x, (int32_t)windowSize.y);
        m_viewport = viewport;
        m_scissor  = scissor;

        // Pass must exist before CreatePassShaderBindings can look it up by tag.
        LoadAsset();
        CreateImage();
        CreateVertexBuffer();
        CreatePasses();
        CreateViewBindings();

        BuildDrawRequest();

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
        destroyIfValid(m_vbEntity);
        destroyIfValid(m_indexEntity);
        destroyIfValid(m_imageEntity);
        destroyIfValid(m_viewBindingsEntity);

        m_viewBindings.reset();
    }

    void DrawCube::OnTick(float /*deltaTime*/)
    {
        UpdateViewBindings();
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

        // View is no longer a separate entity — it is resolved on demand from the
        // image resource's view cache. Just record the descriptor (the cache key).
        m_baseColorViewDesc =
            RHI::ImageViewDescriptor::Create(m_image->GetFormat(), 0, m_image->GetMipLevels() - 1);
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
            .ViewportScissor(m_viewport, m_scissor)
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

                auto depthDesc = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::DepthStencil,
                    windowSize.x, windowSize.y,
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
            .ViewportScissor(m_viewport, m_scissor)
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

    void DrawCube::BuildDrawRequest()
    {
        auto& rhiCtx = *Spark::RHI::RHIExecuteContext::Current();
        m_drawItemEntity = rhiCtx.CreateEntity();

        auto* mesh = m_model->GetModelData()->GetMesh(0);
        const Resource::Primitive& primitive = mesh->primitives[0];

        Render::DrawRequest req;
        req.m_drawArguments    = RHI::DrawArguments(RHI::DrawIndexed(0, primitive.indexCount, 0));
        req.m_drawInstanceArgs = RHI::DrawInstanceArguments(1, 0);
        req.m_vertexBufferInfo = Render::VertexBufferInfo{
            0, static_cast<uint32_t>(primitive.vertexBuffer.size()), primitive.layout.stride};
        req.m_vertexBuffer = m_vbEntity;
        req.m_indexBufferInfo  = Render::IndexBufferInfo{
            0, static_cast<uint32_t>(primitive.indexBuffer.size()), primitive.indexFormat};
        req.m_indexBuffer  = m_indexEntity;

        rhiCtx.Add<Render::DrawRequest>(m_drawItemEntity, eastl::move(req));
        rhiCtx.Add<SPARK_PASS_TAG("ScenePass")>(m_drawItemEntity);
    }

    void DrawCube::UpdateViewBindings()
    {
        if (!m_viewBindings || m_viewBindingsEntity == Spark::RHI::NullHandle)
        {
            return;
        }

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
            Math::Vector3(0.f, 1.f, 0.f));   // spin around world up

        Render::View camera = Render::MakePerspectiveView(
            Math::Vector3(0.f, 5.f, -5.f),   // eye
            Math::Vector3(0.f, 0.f, 0.f),    // target
            Math::Vector3(0.f, 1.f, 0.f),    // up
            Math::Radians(45.f), aspect, 0.1f, 100.f);

        // View owns view+proj and writes g_ViewProjection; the per-object model
        // is the feature's and goes into g_Model separately.
        Spark::Render::WriteViewConstants(camera, m_viewBindingsEntity);

        auto* modelInput = m_viewBindings->FindConstantInput(Spark::RHI::InputName("g_Model"));
        ASSERT(modelInput, "No g_Model shader input.");
        modelInput->SetData(&model, sizeof(model));

        auto& rhiCtx = *Spark::RHI::RHIExecuteContext::Current();
        if (auto* imgComp = rhiCtx.TryGet<Spark::RHI::Components::Image>(m_imageEntity);
            imgComp && imgComp->m_image)
        {
            auto* view = Spark::RHI::GetOrCreateImageView(
                rhiCtx, m_imageEntity, *imgComp->m_image, m_baseColorViewDesc);
            if (view)
            {
                auto* texInput = m_viewBindings->FindImageInput(Spark::RHI::InputName("g_Texture"));
                ASSERT(texInput, "No g_Texture shader input.");
                texInput->SetView(/*arrayIndex*/0, view);
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
