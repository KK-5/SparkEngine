#include <Log/SpdLogSystem.h>
#include <Base.h>

#include <Input/InputSystem.h>

#include <RHI/Backend/DX12/RHISystem.h>

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

    auto glfwWindow = CreateSystem<Spark::SandBox::SimpleGlfwWindow>(1024, 576, "TrianglePass");
    glfwWindow->Init();

    auto inputSystem = CreateSystem<Spark::Input::InputSystem>();
    inputSystem->Init();

    auto rhiSystem = CreateSystem<Spark::RHI::DX12::RHISystem>();
    rhiSystem->Init();

    auto assetManager = CreateSystem<Spark::Resource::SparkAssetManager>();
    assetManager->Init();
    assetManager->AddSearchPath(SHADER_ASSET_DIR);

    auto renderSystem = CreateSystem<Spark::Render::RenderSystem>();
    renderSystem->Init();

    // Push our own pipeline to override the default UIPass pipeline.
    // OnTick reads PassExecuteContext::Current(), so whichever context is on top
    // of the stack gets executed.
    Spark::Render::Pipeline triPipeline("Triangle");
    Spark::Render::PassExecuteContext::Push(triPipeline.GetPassContext());

    Spark::SandBox::TrianglePassFeature triFeature;
    triFeature.Init();

    while (!glfwWindow->ShouldClose())
    {
        glfwWindow->PollEvents();
        TickBus::Broadcast(&TickBus::Events::OnTick, 0.f);
    }

    triFeature.Shutdown();
    Spark::Render::PassExecuteContext::Pop();

    return 0;
}
