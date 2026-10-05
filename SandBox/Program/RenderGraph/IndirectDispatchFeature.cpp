#include "IndirectDispatchFeature.h"

#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/HardwareQueue.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Attachment/AttachmentEnums.h>
#include <RHI/Attachment/AttachmentLoadStoreAction.h>
#include <RHI/Command/DrawItem.h>
#include <RHI/Command/IndirectCommands.h>
#include <RHI/Resource/Buffer/BufferDescriptor.h>
#include <RHI/Resource/Buffer/BufferViewDescriptor.h>
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
#include <Binding/View/ViewBinding.h>
#include <View/View.h>
#include <View/ViewTags.h>

#include <Window/IWindowSystem.h>

#include "../Common/RenderGraphUtil.h"

namespace Spark::SandBox
{
    namespace
    {
        const RHI::AttachmentId kDispatchArgs { "DispatchArgs" };
        const RHI::AttachmentId kPattern      { "Pattern" };
        const RHI::AttachmentId kSwapChain    { "SwapChain" };

        Ptr<Resource::ShaderAsset> LoadShader(const char* path)
        {
            auto* assetManager = Service<Resource::AssetManager>::Get();
            ASSERT(assetManager, "[IndirectDispatchFeature] AssetManager service missing.");
            auto shader = assetManager->LoadAsset<Resource::ShaderAsset>(Resource::AssetId::Of<Resource::ShaderAsset>(path));
            ASSERT(shader && shader->GetStatus() == Resource::AssetStatus::Ready,
                "[IndirectDispatchFeature] {} failed to load.", path);
            return shader;
        }
    }

    IndirectDispatchFeature::IndirectDispatchFeature() = default;
    IndirectDispatchFeature::~IndirectDispatchFeature() = default;

    bool IndirectDispatchFeature::Init()
    {
        m_argsShader    = LoadShader("sandbox://Shader/IndirectDispatchArgs.hlsl");
        m_patternShader = LoadShader("sandbox://Shader/IndirectDispatchPattern.hlsl");
        m_presentShader = LoadShader("sandbox://Shader/PresentIndexed.hlsl");

        CreateView();
        // Declared in dependency order: each pass reads what the one before writes.
        CreateArgsPass();
        CreatePatternPass();
        CreatePresentPass();

        TickBus::Handler::BusConnect();
        return true;
    }

    void IndirectDispatchFeature::Shutdown()
    {
        TickBus::Handler::BusDisconnect();

        auto& ctx = *RHI::RHIExecuteContext::Current();
        if (m_view != RHI::NullHandle && ctx.Valid(m_view))
        {
            ctx.DestoryEntity(m_view);
        }
        m_view = RHI::NullHandle;
    }

    void IndirectDispatchFeature::OnTick(const FrameTime& /*time*/)
    {
        auto* window = Service<Window::IWindowSystem>::Get();
        m_size  = window->GetWindowSize();
        m_time += 0.016f;

        // Before RenderSystem's tick, so ViewBindingSystem encodes this frame's size.
        auto& ctx = *RHI::RHIExecuteContext::Current();
        ctx.Get<Render::View>(m_view).m_bufferSize = m_size;
    }

    void IndirectDispatchFeature::CreateView()
    {
        // By hand, as in ComputePass: no camera. The compute passes read the view's size from
        // g_Views; the present pass only takes its rect as the viewport.
        auto& ctx = *RHI::RHIExecuteContext::Current();
        m_view = ctx.CreateEntity();
        ctx.Add<Render::View>(m_view, Render::View{});
        ctx.Add<Render::MainViewTag>(m_view);
    }

    bool IndirectDispatchFeature::TryGetViewIndex(uint32_t& viewIndex) const
    {
        if (m_size.x <= 0 || m_size.y <= 0)
        {
            return false;
        }
        // The view picked here rather than by the executer: a compute pass renders none.
        return Render::TryGetViewIndex(*RHI::RHIExecuteContext::Current(), m_view, viewIndex);
    }

    void IndirectDispatchFeature::CreateArgsPass()
    {
        auto& passContext = *Render::PassExecuteContext::Current();

        SPARK_COMPUTE_PASS(passContext, "ArgsPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(m_argsShader)
            .Binds<Render::ViewBindingTag>()
            .Build([this](Render::ComputePassScopes& p)
            {
                m_argsWritten = false;
                uint32_t viewIndex = 0;
                if (!TryGetViewIndex(viewIndex))
                {
                    return;
                }
                m_argsWritten = true;

                // One record, written by a shader and read as the arguments of a dispatch.
                RHI::BufferDescriptor desc;
                desc.m_byteCount = sizeof(RHI::DispatchIndirectCommand);
                desc.m_bindFlags = RHI::BufferBindFlags::ShaderReadWrite | RHI::BufferBindFlags::Indirect;
                p.CreateBuffer(kDispatchArgs, desc);

                auto s = p.Scope();
                s.WriteBuffer(kDispatchArgs)
                    .View(RHI::BufferViewDescriptor::CreateStructured(0, 3, sizeof(uint32_t)))
                    .Bind(RHI::InputName("g_Args"));
                s.Constant(RHI::InputName("time"), m_time);
                s.Constant(RHI::InputName("viewIndex"), viewIndex);
                s.Dispatch(1);
                s.Close();
            })
            .Finalize();
    }

    void IndirectDispatchFeature::CreatePatternPass()
    {
        auto& passContext = *Render::PassExecuteContext::Current();

        SPARK_COMPUTE_PASS(passContext, "PatternPass")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .ComputeShader(m_patternShader)
            .Binds<Render::ViewBindingTag>()
            .Build([this](Render::ComputePassScopes& p)
            {
                m_patternWritten = false;
                uint32_t viewIndex = 0;
                if (!m_argsWritten || !TryGetViewIndex(viewIndex))
                {
                    return;
                }
                m_patternWritten = true;
                const auto width  = static_cast<uint32_t>(m_size.x);
                const auto height = static_cast<uint32_t>(m_size.y);

                p.CreateImage(kPattern, RHI::ImageDescriptor::Create2D(
                    RHI::ImageBindFlags::ShaderReadWrite, width, height, RHI::Format::R8G8B8A8_UNORM));

                // The whole view, dimly: what the indirect dispatch does not reach would
                // otherwise show whatever the transient memory held.
                {
                    auto s = p.Scope();
                    s.WriteImage(kPattern).BindIndex(RHI::InputName("outputIndex"));
                    s.Constant(RHI::InputName("time"), m_time);
                    s.Constant(RHI::InputName("viewIndex"), viewIndex);
                    s.Constant(RHI::InputName("brightness"), 0.25f);
                    s.Dispatch(width, height);
                    s.Close();
                }

                // Brightly, over as much as ArgsPass wrote into the buffer.
                {
                    auto s = p.Scope();
                    auto arguments = s.IndirectArguments(kDispatchArgs);
                    s.ReadWriteImage(kPattern).BindIndex(RHI::InputName("outputIndex"));
                    s.Constant(RHI::InputName("time"), m_time);
                    s.Constant(RHI::InputName("viewIndex"), viewIndex);
                    s.Constant(RHI::InputName("brightness"), 1.0f);
                    s.DispatchIndirect(arguments);
                    s.Close();
                }
            })
            .Finalize();
    }

    void IndirectDispatchFeature::CreatePresentPass()
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
                if (!m_patternWritten)
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
                s.ReadImage(kPattern).Stage(RHI::AttachmentStage::FragmentShader).BindIndex(RHI::InputName("inputIndex"));
                s.Draw(RHI::DrawLinear(3, 0));
                s.Close();
            })
            .Finalize();
    }
}

int main(int, char**)
{
    using namespace Spark;

    auto sys = SandBox::InitRenderGraphApp(1024, 576, "IndirectDispatch");

    Render::Pipeline pipeline("IndirectDispatch");
    Render::PassExecuteContext::Push(pipeline.GetPassContext());

    SandBox::IndirectDispatchFeature feature;
    feature.Init();

    while (!sys.m_window->ShouldClose())
    {
        TickBus::Broadcast(&TickBus::Events::OnTick, FrameTime{});
    }

    feature.Shutdown();
    Render::PassExecuteContext::Pop();

    return 0;
}
