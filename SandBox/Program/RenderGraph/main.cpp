#include <Log/SpdLogSystem.h>
#include <Base.h>

#include <Input/InputSystem.h>

#include <RHI/Backend/DX12/RHISystem.h>
#include <RHI/System/RHIResourceSystem.h>
#include <RHI/System/AsyncUploadSystem.h>
#include <RHI/Device/DeviceDescriptor.h>

#include <Resource/Asset.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/AssetManager.h>
#include <Resource/Common/CommonAssetLoader.h>

#include <RenderSystem.h>
#include <Pass/PassContext.h>

#include "../Common/SimpleGlfwWindow.h"
#include "TrianglePassFeature.h"

int main(int argc, char** argv)
{
    using namespace Spark;

    LogConfig logConfig{};
    logConfig.m_showTimeStamp = true;
    UniquePtr<ILogSystem<SpdLogSystem>> s_logger = eastl::make_unique<SpdLogSystem>(logConfig);

    // Foundation systems (no RHI deps).
    auto glfwWindow = CreateSystem<Spark::SandBox::SimpleGlfwWindow>(1024, 576, "TrianglePass");
    glfwWindow->Init();

    auto inputSystem = CreateSystem<Spark::Input::InputSystem>();
    inputSystem->Init();

    // RHI backend: factory + RHIContext are ready after Init; Device is not.
    auto rhiSystem = CreateSystem<Spark::RHI::DX12::RHISystem>();
    rhiSystem->Init();

    // User-driven Device init. Anything below this point that needs a Device
    // (RHIResourceSystem, AsyncUploadSystem, RenderSystem) can now resolve it
    // via Service<RHIInterface>::Get()->GetDevice().
    {
        auto* rhi = Service<Spark::RHI::RHIInterface>::Get();
        auto devs = rhi->EnumeratePhysicalDevices();
        ASSERT(!devs.empty(), "[main] No physical device available.");

        Spark::RHI::DeviceDescriptor deviceDesc;
        deviceDesc.m_frameCountMax = 3;
        Spark::RHI::ResultCode r = rhi->InitDevice(*devs.front(), deviceDesc);
        ASSERT(r == Spark::RHI::ResultCode::Success, "[main] Init device failed.");
    }

    // RHI resource infrastructure. Init order matters for FrameEventBus dispatch:
    // EBus is LIFO (push_front on BusConnect, iterate via begin()), so connecting
    // AsyncUploadSystem first means RHIResourceSystem::OnFrameBegin runs *first*
    // (materialize buffers), and AsyncUploadSystem::OnFrameBegin runs *after*
    // (pick up the freshly materialized buffer and submit the upload).
    auto asyncUploadSystem = CreateSystem<Spark::RHI::AsyncUploadSystem>();
    asyncUploadSystem->Init();

    auto rhiResourceSystem = CreateSystem<Spark::RHI::RHIResourceSystem>();
    rhiResourceSystem->Init();

    // Asset manager (consumed by RenderSystem / feature code).
    auto assetManager = CreateSystem<Spark::Resource::SparkAssetManager>();
    assetManager->Init();
    assetManager->AddSearchPath(SHADER_ASSET_DIR);

    // Renderer: owns SwapChain + RenderGraph entities. Depends on Device + pools.
    auto renderSystem = CreateSystem<Spark::Render::RenderSystem>();
    renderSystem->Init();

    // Sample-specific pipeline overrides the RenderSystem default. OnTick reads
    // PassExecuteContext::Current(), so whichever context is on top is what runs.
    Spark::Render::Pipeline triPipeline("Triangle");
    Spark::Render::PassExecuteContext::Push(triPipeline.GetPassContext());

    Spark::SandBox::TrianglePassFeature triFeature;
    triFeature.Init();

    while (!glfwWindow->ShouldClose())
    {
        TickBus::Broadcast(&TickBus::Events::OnTick, 0.f);
    }

    // Game-level teardown. Each owner destroys what it created — TrianglePassFeature
    // disposes of its RHIContext entities, RenderSystem (via its dtor below) drains
    // the GPU and tears down swap chain + render graph entities, then the resource
    // systems release their pools, and RHISystem finally releases factory + device.
    // Scope exit handles the rest in reverse declaration order: renderSystem →
    // assetManager → rhiResourceSystem → asyncUploadSystem → rhiSystem → input → window.
    triFeature.Shutdown();
    Spark::Render::PassExecuteContext::Pop();

    return 0;
}
