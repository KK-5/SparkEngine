#include "ComputePassFeature.h"

#include <EASTL/array.h>

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Resource/Image/ImageDescriptor.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/Pipeline/RenderStates.h>
#include <RHI/Pipeline/RenderTargetLayout.h>

#include <Resource/Asset.h>
#include <Resource/AssetManager.h>
#include <Resource/Shader/ShaderAsset.h>

#include <Pass/PassContext.h>
#include <Pass/ComputePass.h>
#include <Pass/RenderPass.h>
#include <RenderGraph/PassScopes.h>
#include <View/View.h>
#include <View/ViewTags.h>

#include <Window/IWindowSystem.h>

#include "../Common/RenderGraphUtil.h"

namespace Spark::SandBox
{
    namespace
    {
        const RHI::AttachmentId kPattern   { "Pattern" };
        const RHI::AttachmentId kSwapChain { "SwapChain" };

        Ptr<Resource::ShaderAsset> LoadShader(const char* path)
        {
            auto* assetManager = Service<Resource::AssetManager>::Get();
            ASSERT(assetManager, "[ComputePassFeature] AssetManager service missing.");
            auto shader = assetManager->LoadAsset<Resource::ShaderAsset>(Resource::AssetId::Of<Resource::ShaderAsset>(path));
            ASSERT(shader && shader->GetStatus() == Resource::AssetStatus::Ready,
                "[ComputePassFeature] {} failed to load.", path);
            return shader;
        }
    }

    ComputePassFeature::ComputePassFeature() = default;
    ComputePassFeature::~ComputePassFeature() = default;

    bool ComputePassFeature::Init()
    {
        m_patternShader = LoadShader("sandbox://Shader/ComputePattern.hlsl");
        m_presentShader = LoadShader("sandbox://Shader/PresentIndexed.hlsl");

        CreateView();
        // Declared in dependency order: the present pass reads what the pattern pass writes.
        CreatePatternPass();
        CreatePresentPass();

        TickBus::Handler::BusConnect();
        return true;
    }

    void ComputePassFeature::Shutdown()
    {
        TickBus::Handler::BusDisconnect();

        auto& ctx = *RHI::RHIExecuteContext::Current();
        if (m_view != RHI::NullHandle && ctx.Valid(m_view))
        {
            ctx.DestoryEntity(m_view);
        }
        m_view = RHI::NullHandle;
    }

    void ComputePassFeature::OnTick(const FrameTime& /*time*/)
    {
        auto* window = Service<Window::IWindowSystem>::Get();
        m_size  = window->GetWindowSize();
        m_time += 0.016f;
    }

    void ComputePassFeature::CreateView()
    {
        // By hand, as in TrianglePass: no camera, and no space1 in the shaders. The view only
        // supplies the rect that becomes the present pass's viewport.
        auto& ctx = *RHI::RHIExecuteContext::Current();
        m_view = ctx.CreateEntity();
        ctx.Add<Render::View>(m_view, Render::View{});
        ctx.Add<Render::MainViewTag>(m_view);
    }

    void ComputePassFeature::CreatePatternPass()
    {
        auto& passContext = *Render::PassExecuteContext::Current();

        SPARK_COMPUTE_PASS(passContext, "PatternPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(m_patternShader)
            .Build([this](Render::ComputePassScopes& p)
            {
                if (m_size.x <= 0 || m_size.y <= 0)
                {
                    return;
                }
                const auto width  = static_cast<uint32_t>(m_size.x);
                const auto height = static_cast<uint32_t>(m_size.y);

                p.CreateImage(kPattern, RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::ShaderReadWrite, width, height, RHI::Format::R8G8B8A8_UNORM));

                auto s = p.Scope();
                s.Write(kPattern).BindIndex(RHI::InputName("outputIndex"));
                s.Constant(RHI::InputName("time"), m_time);
                s.Constant(RHI::InputName("size"), eastl::array<uint32_t, 2>{ width, height });
                s.Dispatch(width, height);
            })
            .Finalize();
    }

    void ComputePassFeature::CreatePresentPass()
    {
        RHI::InputStreamLayoutBuilder islBuilder;
        islBuilder.Begin();
        islBuilder.SetTopology(RHI::PrimitiveTopology::TriangleList);
        RHI::InputStreamLayout inputLayout = islBuilder.End();

        RHI::RenderTargetLayout rtLayout;
        rtLayout.m_colorAttachmentCount = 1;
        rtLayout.m_colorFormats[0]      = RHI::Format::R8G8B8A8_UNORM;

        RHI::RenderStates renderStates;
        renderStates.m_depthStencilState.m_depth.m_enable   = 0;
        renderStates.m_depthStencilState.m_stencil.m_enable = 0;
        renderStates.m_rasterState.m_cullMode               = RHI::CullMode::None;

        auto& passContext = *Render::PassExecuteContext::Current();

        SPARK_RENDER_PASS(passContext, "PresentPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .VertexShader(m_presentShader)
            .FragmentShader(m_presentShader)
            .InputLayout(inputLayout)
            .RenderTargetLayout(rtLayout)
            .RenderStates(renderStates)
            .Binds<>()
            .RendersView<Render::MainViewTag>()
            .Build([this](Render::RenderPassScopes& p)
            {
                if (m_size.x <= 0 || m_size.y <= 0)
                {
                    return;
                }
                p.Import(kSwapChain, p.GetCurrentSwapChainResource());

                // Every pixel is overwritten.
                RHI::AttachmentLoadStoreAction overwrite;
                overwrite.m_loadAction  = RHI::AttachmentLoadAction::DontCare;
                overwrite.m_storeAction = RHI::AttachmentStoreAction::Store;

                auto s = p.Scope();
                s.RenderTarget(kSwapChain, overwrite);
                s.Read(kPattern).Stage(RHI::AttachmentStage::FragmentShader).BindIndex(RHI::InputName("inputIndex"));
                s.Draw(RHI::DrawLinear(3, 0));
            })
            .Finalize();
    }
}

int main(int, char**)
{
    using namespace Spark;

    auto sys = SandBox::InitRenderGraphApp(1024, 576, "ComputePass");

    Render::Pipeline pipeline("ComputePass");
    Render::PassExecuteContext::Push(pipeline.GetPassContext());

    SandBox::ComputePassFeature feature;
    feature.Init();

    while (!sys.m_window->ShouldClose())
    {
        TickBus::Broadcast(&TickBus::Events::OnTick, FrameTime{});
    }

    feature.Shutdown();
    Render::PassExecuteContext::Pop();

    return 0;
}
