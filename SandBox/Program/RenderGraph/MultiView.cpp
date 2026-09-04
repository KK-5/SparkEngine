#include "MultiView.h"

#include <Log/ILogSystem.h>
#include <Math/Vector3.h>
#include <Math/Matrix4x4.h>
#include <Math/MathUtils.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Device/Device.h>
#include <RHI/Component/Component.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>
#include <RHI/Command/CommandList.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/ClearValue.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Resource/Image/ImageSubResource.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>
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
#include <RenderGraph/RenderGraphUtils.h>
#include <Drawable/GeometrySpec.h>

#include "SampleDrawTag.h"
#include <View/View.h>
#include <View/ViewTags.h>
#include <View/ViewComponents.h>
#include <View/ViewFactory.h>

#include <Window/IWindowSystem.h>

#include "../Common/RenderGraphUtil.h"

namespace Spark::SandBox
{
    namespace
    {
        //! Normalized against the pass target: a 2x2 grid, in ViewRect's minX/maxX/minY/maxY
        //! order. The executer scales the target viewport by these, so nothing here is in pixels.
        constexpr Spark::Render::ViewRect kViewRects[] = {
            { 0.0f, 0.5f, 0.0f, 0.5f },
            { 0.5f, 1.0f, 0.0f, 0.5f },
            { 0.0f, 0.5f, 0.5f, 1.0f },
            { 0.5f, 1.0f, 0.5f, 1.0f },
        };
    }

    MultiView::MultiView() = default;
    MultiView::~MultiView() = default;

    bool MultiView::Init()
    {
        LoadAsset();
        CreateImage();
        CreateVertexBuffer();
        CreateViews();
        CreatePasses();

        BuildGeometry();

        TickBus::Handler::BusConnect();
        return true;
    }

    void MultiView::Shutdown()
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
        // Order: GeometrySpec -> VB/IB/Image -> views.

        destroyIfValid(m_drawable);
        destroyIfValid(m_vertexBuffer);
        destroyIfValid(m_indexBuffer);
        destroyIfValid(m_baseColor);

        // Each view owns its SRG entity; drop that before the view itself.
        for (Spark::RHI::RHIHandle& view : m_views)
        {
            if (view != Spark::RHI::NullHandle && ctx.Valid(view))
            {
                if (auto* bindings = ctx.TryGet<Spark::Render::ViewShaderBindings>(view))
                {
                    destroyIfValid(bindings->m_bindings);
                }
            }
            destroyIfValid(view);
        }

        eastl::fixed_vector<Spark::RHI::RHIHandle, 4> srgEntities;
        ctx.GetView<Spark::Render::PassShaderBindingsTag>().each(
            [&](Spark::RHI::RHIHandle e) { srgEntities.push_back(e); });
        for (Spark::RHI::RHIHandle e : srgEntities)
        {
            destroyIfValid(e);
        }
    }

    void MultiView::OnTick(float /*deltaTime*/)
    {
        Update();
    }

    void MultiView::LoadAsset()
    {
        auto assetManager = Service<Spark::Resource::AssetManager>::Get();
        ASSERT(assetManager, "[MultiView] AssetManager service missing.");
        m_shader = assetManager->LoadAsset<Spark::Resource::ShaderAsset>(
            Spark::Resource::AssetId::Of<Spark::Resource::ShaderAsset>("sandbox://Shader/CubeTextured.hlsl"));
        ASSERT(m_shader && m_shader->GetStatus() == Spark::Resource::AssetStatus::Ready,
            "[MultiView] CubeTextured.hlsl load failed.");

        m_model = assetManager->LoadAsset<Resource::ModelAsset>(
            Resource::AssetId::Of<Resource::ModelAsset>("sandbox://Model/CubeTextured.glb"));
        ASSERT(m_model && m_model->GetStatus() == Spark::Resource::AssetStatus::Ready,
            "[MultiView] Model/CubeTextured.glb load failed.");

        auto* modelAssetData = m_model->GetModelData();
        if (modelAssetData->GetImageAssetCount() > 0)
        {
            m_image = assetManager->LoadAsset<Resource::ImageAsset>(modelAssetData->GetImageAssetId(0));
            ASSERT(m_image && m_image->GetStatus() == Spark::Resource::AssetStatus::Ready,
                "[MultiView] embeded image load failed.");
        }
    }

    void MultiView::CreateVertexBuffer()
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

        m_vertexBuffer = Spark::RHI::CreateStaticBuffer(ctx, ObjectName("CubeVertex"), vbDesc);
        Spark::RHI::RequestBufferUpload(
            ctx, m_vertexBuffer, primitive.vertexBuffer.data(), primitive.vertexBuffer.size());
        Spark::Render::CreateStaticBufferAttachment(ctx, m_vertexBuffer,
            Spark::RHI::InputName("CubeVertex"),
            Spark::RHI::AttachmentAccess::Read,
            Spark::RHI::AttachmentUsage::InputAssembly,
            Spark::RHI::AttachmentStage::VertexInput);

        Spark::RHI::BufferDescriptor ibDesc;
        ibDesc.m_bindFlags =
            Spark::RHI::BufferBindFlags::InputAssembly | Spark::RHI::BufferBindFlags::CopyWrite;
        ibDesc.m_byteCount = primitive.indexBuffer.size();
        ibDesc.m_sharedQueueMask = Spark::RHI::HardwareQueueClassMask::Graphics;

        m_indexBuffer = Spark::RHI::CreateStaticBuffer(ctx, ObjectName("CubeIndex"), ibDesc);
        Spark::RHI::RequestBufferUpload(
            ctx, m_indexBuffer, primitive.indexBuffer.data(), primitive.indexBuffer.size());
        Spark::Render::CreateStaticBufferAttachment(ctx, m_indexBuffer,
            Spark::RHI::InputName("CubeIndex"),
            Spark::RHI::AttachmentAccess::Read,
            Spark::RHI::AttachmentUsage::InputAssembly,
            Spark::RHI::AttachmentStage::VertexInput);
    }

    void MultiView::CreateImage()
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

        m_baseColor = RHI::CreateStaticImage(
            ctx,
            ObjectName("BaseColorImage"),
            desc,
            Spark::RHI::HeapMemoryLevel::Device,
            Spark::RHI::HostMemoryAccess::Write
        );

        RHI::RequestImageUpload(
            ctx,
            m_baseColor,
            m_image->GetImageData()->GetTextureBytes().data(),
            m_image->GetImageData()->GetTextureBytes().size(),
            RHI::ImageSubresourceRange(desc),
            RHI::Origin(),
            m_image->GetFormat()
        );

        Render::CreateStaticImageAttachment(
            ctx,
            m_baseColor,
            Spark::RHI::InputName("BaseColorImage"),
            Spark::RHI::AttachmentAccess::Read,
            Spark::RHI::AttachmentUsage::Shader,
            Spark::RHI::AttachmentStage::FragmentShader
        );

        m_baseColorViewDesc =
            RHI::ImageViewDescriptor::Create(m_image->GetFormat(), 0, m_image->GetMipLevels() - 1);
    }

    void MultiView::CreateViews()
    {
        auto& ctx = *Spark::RHI::RHIExecuteContext::Current();

        static_assert(sizeof(kViewRects) / sizeof(kViewRects[0]) == kViewCount,
            "[MultiView] One rect per view.");

        // Four independent view instances, all carrying MainViewTag — that tag is the whole
        // link to the pass, which collects by it. Usable because CubeTextured.hlsl includes
        // ViewBindings.hlsl, so its space1 group matches the layout each view's SRG was built
        // from; a shader declaring its own space1 could not take a view's SRG.
        for (uint32_t i = 0; i < kViewCount; ++i)
        {
            m_views[i] = Spark::Render::CreateViewEntity<Spark::Render::MainViewTag>(ctx);
            ASSERT(m_views[i] != Spark::RHI::NullHandle, "[MultiView] Failed to create view {}.", i);
            ctx.Get<Spark::Render::View>(m_views[i]).m_rect = kViewRects[i];
        }
    }

    void MultiView::CreatePasses()
    {
        auto* mesh = m_model->GetModelData()->GetMesh(0);
        ASSERT(mesh->primitives.size() > 0, "No primitive in mesh.");
        const Resource::Primitive& primitive = mesh->primitives[0];

        Spark::RHI::InputStreamLayoutBuilder islBuilder;
        islBuilder.Begin();
        islBuilder.SetTopology(Spark::RHI::PrimitiveTopology::TriangleList);
        auto* bufferBuilder = islBuilder.AddBuffer();
        for (const Resource::VertexAttribute& vert : primitive.layout.attributes)
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

        auto& passContext = *Spark::Render::PassExecuteContext::Current();

        // One render pass for all four panels. Clear and depth are full-target and happen once
        // at BeginRenderPass; each view's scissor is what keeps its draws inside its quadrant.
        SPARK_RENDER_PASS(passContext, "ScenePass")
            .Queue(Spark::RHI::HardwareQueueClass::Graphics)
            .VertexShader(m_shader)
            .FragmentShader(m_shader)
            .InputLayout(inputLayout)
            .RenderTargetLayout(rtLayout)
            .RenderStates(renderStates)
            .Accepts<SampleDrawTag>()
            .Binds<>()
            .RendersView<Render::MainViewTag>()
            .Build([this](Spark::Render::RenderGraphBuilder& builder)
            {
                const auto renderSize = builder.GetRenderSize();

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
                builder.ImportImageAttachment<SPARK_PASS_TAG("ScenePass")>(
                    Spark::RHI::AttachmentId("SwapChain"), colorBind);

                auto depthDesc = RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::DepthStencil,
                    renderSize.x, renderSize.y,
                    RHI::Format::D32_FLOAT
                );

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
            .Compile([this](Spark::Render::RenderGraphCompiler& compiler)
            {
                auto& rhiCtx = *Spark::RHI::RHIExecuteContext::Current();

                using namespace Spark::Render;

                // Only what is view-independent: the cameras live in space1, one SRG per view.
                SetPassShaderConstant<SPARK_PASS_TAG("ScenePass")>(
                    /*spaceId*/ 0, Spark::RHI::InputName("g_Model"), m_modelMatrix);

                if (IsResourceReady(rhiCtx, m_baseColor))
                {
                    auto image = rhiCtx.Get<RHI::Components::Image>(m_baseColor);
                    auto* view = Spark::RHI::GetOrCreateImageView(
                        rhiCtx, m_baseColor, *image.m_image, m_baseColorViewDesc);
                    if (view)
                    {
                        SetPassShaderImage<SPARK_PASS_TAG("ScenePass")>(
                            /*spaceId*/ 0, Spark::RHI::InputName("g_Texture"), view);
                    }
                }

                SetPassShaderSampler<SPARK_PASS_TAG("ScenePass")>(
                    /*spaceId*/ 0, Spark::RHI::InputName("g_Sampler"), m_samplerState);
            })
            // Called once per state-homogeneous run, so four times here — once per view, each
            // time over the same one draw. Submit work.m_itemHandles, never a fresh query.
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
    }

    void MultiView::BuildGeometry()
    {
        auto& rhiCtx = *Spark::RHI::RHIExecuteContext::Current();
        m_drawable = rhiCtx.CreateEntity();

        auto* mesh = m_model->GetModelData()->GetMesh(0);
        const Resource::Primitive& primitive = mesh->primitives[0];

        Render::GeometrySpec drawable;
        drawable.m_drawArgs = RHI::DrawArguments(RHI::DrawIndexed(0, primitive.indexCount, 0));
        Render::VertexStreamSpec vertex;
        vertex.m_buffer = m_vertexBuffer;
        vertex.m_vertexBufferInfo = Render::VertexBufferInfo{
            0, static_cast<uint32_t>(primitive.vertexBuffer.size()), primitive.layout.stride};
        vertex.m_inputSlot = 0;
        drawable.m_streams.push_back(vertex);

        drawable.m_index.m_indexBuffer = m_indexBuffer;
        drawable.m_index.m_indexInfo   = Render::IndexBufferInfo{
            0, static_cast<uint32_t>(primitive.indexBuffer.size()), primitive.indexFormat };

        drawable.m_instanceData = Render::NoInstanceBinding{};

        rhiCtx.Add<Render::GeometrySpec>(m_drawable, eastl::move(drawable));
        rhiCtx.Add<SampleDrawTag>(m_drawable);
    }

    void MultiView::Update()
    {
        auto* window = Service<Spark::Window::IWindowSystem>::Get();
        auto windowSize = window->GetWindowSize();
        if (windowSize.x <= 0 || windowSize.y <= 0)
        {
            return;
        }

        m_rotationAngle += 0.01f;
        m_modelMatrix = Math::Rotate(
            Math::Matrix4X4Const::IDENTITY,
            m_rotationAngle,
            Math::Vector3(0.f, 1.f, 0.f));

        // Producers only write the View; ViewBindingSystem stages every one into its own SRG
        // and the executer binds that per DrawList. Same split CameraViewSystem lives on.
        auto& rhiCtx = *Spark::RHI::RHIExecuteContext::Current();
        for (uint32_t i = 0; i < kViewCount; ++i)
        {
            Render::View& view = rhiCtx.Get<Render::View>(m_views[i]);

            // Four fixed points on one orbit — the panels are meant to be unmistakably
            // different, so a broken per-view SRG bind shows up as repeated images. Not 90
            // degrees apart: a cube is 4-fold symmetric about Y, so that spacing would leave
            // the four silhouettes identical and only the texture telling them apart.
            const float orbit = Math::Radians(50.f) * static_cast<float>(i);
            view.m_worldToView = Math::LookAt(
                Math::Vector3(Math::Sin(orbit) * 6.f, 3.f, -Math::Cos(orbit) * 6.f),
                Math::Vector3(0.f, 0.f, 0.f),
                Math::Vector3(0.f, 1.f, 0.f));

            // From the view's own rect, not the window: each panel is a quarter of the target.
            const Render::ViewRect& rect = view.m_rect;
            const float aspect = (windowSize.x * (rect.m_maxX - rect.m_minX)) /
                                 (windowSize.y * (rect.m_maxY - rect.m_minY));
            view.m_viewToClip = Math::PerspectiveFov(Math::Radians(45.f), aspect, 0.1f, 100.f);
        }
    }
}

int main(int, char**)
{
    using namespace Spark;

    auto sys = SandBox::InitRenderGraphApp(1024, 576, "MultiView");

    Render::Pipeline pipeline("MultiView");
    Render::PassExecuteContext::Push(pipeline.GetPassContext());

    SandBox::MultiView feature;
    feature.Init();

    while (!sys.m_window->ShouldClose())
    {
        TickBus::Broadcast(&TickBus::Events::OnTick, 0.f);
    }

    feature.Shutdown();
    Render::PassExecuteContext::Pop();

    return 0;
}
