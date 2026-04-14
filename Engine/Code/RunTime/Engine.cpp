
#include "Engine.h"

#include <Tick/TickBus.h>
#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <Reflection/TypeRegistry.h>
#include <Reflect.h>

namespace Spark
{
    void SparkEngine::SetUp()
    {
        TypeRegistry::Register(Spark::Reflect);
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

        m_renderSystem = CreateSystem<Render::RenderSystem>();
        m_renderSystem->Init();
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