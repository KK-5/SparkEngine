
#include "Engine.h"

#include <Tick/TickBus.h>
#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <Reflection/TypeRegistry.h>
#include <Reflect.h>

#include <Feature/Transform/Reflect.h>
#include <Feature/Mesh/Reflect.h>

namespace Spark
{
    void SparkEngine::SetUp()
    {
        TypeRegistry::Register(Spark::Reflect);
        TypeRegistry::Register(Spark::Transform::Reflect);
        TypeRegistry::Register(Spark::Mesh::Reflect);
        TypeRegistry::RegisterAll();

        WorldExecuteContext::Push(m_worldContext);

        LogConfig logConfig{};
        logConfig.m_showTimeStamp = true;
        m_logSystem = eastl::make_unique<SpdLogSystem>(logConfig);

        m_entityReaper = CreateSystem<EntityReaper>();
        m_entityReaper->Init();

        m_sceneManager = CreateSystem<SceneManager>();
        m_sceneManager->Init();

        m_inputSystem = CreateSystem<Input::InputSystem>();
        m_inputSystem->Init();

        m_dx12Rhi = CreateSystem<RHI::DX12::RHISystem>();
        m_dx12Rhi->Init();

        {
            // Create RHI Device
            auto* rhi = Service<RHI::RHIInterface>::Get();
            auto devs = rhi->EnumeratePhysicalDevices();
            ASSERT(!devs.empty(), "[Engine] No physical device available.");

            RHI::DeviceDescriptor deviceDesc;
            deviceDesc.m_frameCountMax = 3;
            RHI::ResultCode r = rhi->InitDevice(*devs.front(), deviceDesc);
            ASSERT(r == RHI::ResultCode::Success, "[Engine] Init RHI device failed.");
        }

        m_renderSystem = CreateSystem<Render::RenderSystem>();
        m_renderSystem->Init();

        m_assetManager = CreateSystem<Resource::SparkAssetManager>();
        m_assetManager->Init();
        m_assetManager->AddSearchPath("Engine/Asset");

        m_iconManager = CreateSystem<UI::IconManager>();
        m_iconManager->Init();

        m_rhiResourceSystem = CreateSystem<RHI::RHIResourceSystem>();
        m_rhiResourceSystem->Init();

        m_asyncUploadSystem = CreateSystem<RHI::AsyncUploadSystem>();
        m_asyncUploadSystem->Init();
    }

    void SparkEngine::Shutdown()
    {
        WorldExecuteContext::Pop();
    }

    void SparkEngine::Run(eastl::function<bool()> shouldQuit)
    {
        while (!shouldQuit())
        {
            float deltaTime = CalculDeltaTime();
            TickBus::Broadcast(&TickBus::Events::OnTick, deltaTime);
        }
    }

    float SparkEngine::CalculDeltaTime()
    {
        float deltaTime {0};
        {
            using namespace eastl::chrono;
            steady_clock::time_point now = steady_clock::now();
            duration<float> span = now - m_lastTickTime;
            deltaTime = span.count();
            m_lastTickTime = now;
        }
        return deltaTime;
    }
}