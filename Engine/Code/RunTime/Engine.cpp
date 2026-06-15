
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

        m_worldCtxGuard = eastl::make_unique<WorldExecuteContextGuard>(m_worldContext);

        LogConfig logConfig{};
        logConfig.m_showTimeStamp = true;
        m_logSystem = eastl::make_unique<SpdLogSystem>(logConfig);

        m_entityReaper = CreateSystem<EntityReaper>();
        m_entityReaper->Init();

        m_sceneManager = CreateSystem<SceneManager>();
        m_sceneManager->Init();

        m_transformSystem = CreateSystem<Transform::TransformSystem>();
        m_transformSystem->Init();

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
        
        m_assetManager = CreateSystem<Resource::SparkAssetManager>();
        m_assetManager->Init();
        m_assetManager->AddSearchPath("Engine/Asset");
        m_assetManager->AssetRegistry();

        m_renderSystem = CreateSystem<Render::RenderSystem>();
        m_renderSystem->Init();

        m_iconManager = CreateSystem<UI::IconManager>();
        m_iconManager->Init();

        m_meshSystem = CreateSystem<Mesh::MeshSystem>();
        m_meshSystem->Init();

        m_meshResolver = eastl::make_unique<Mesh::MeshResolver>();
        m_meshResolver->Init();

        m_rhiResourceSystem = CreateSystem<RHI::RHIResourceSystem>();
        m_rhiResourceSystem->Init();

        m_asyncUploadSystem = CreateSystem<RHI::AsyncUploadSystem>();
        m_asyncUploadSystem->Init();

        m_initialized = true;
    }

    SparkEngine::~SparkEngine()
    {
        Shutdown();
    }

    void SparkEngine::Shutdown()
    {
        if (!m_initialized)
        {
            return;
        }
        m_initialized = false;
    }

    void SparkEngine::Run(eastl::function<bool()> shouldQuit)
    {
        while (!shouldQuit())
        {
            // Fetch asset resolve request
            Resource::AssetResolveBus::ExecuteQueuedEvents();


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