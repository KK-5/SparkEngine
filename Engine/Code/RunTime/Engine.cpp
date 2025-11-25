
#include "Engine.h"

#include <Tick/TickBus.h>
#include <ECS/WorldContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflect.h>

namespace Spark
{
    void SparkEngine::SetUp()
    {
        TypeRegistry::Register(Spark::Reflect);
        TypeRegistry::RegisterAll();

        LogConfig logConfig{};
        logConfig.m_showTimeStamp = true;
        m_logSystem = eastl::make_unique<SpdLogSystem>(logConfig);

        m_entityReaper = eastl::make_unique<EntityReaper>();
        m_entityReaper->Initialize();

        m_sceneManager = eastl::make_unique<SceneManager>(m_worldContext);
        m_sceneManager->Initialize();

        m_inputSystem = eastl::make_unique<Input::InputSystem>();
        m_inputSystem->Initialize();

        m_renderSystem = eastl::make_unique<Render::RenderSystem>();
        m_renderSystem->Initialize();
    }

    void SparkEngine::ShutDown()
    {
        m_renderSystem->ShutDown();
        m_inputSystem->ShutDown();
        m_sceneManager->ShutDown();
        m_entityReaper->ShutDown();
    }

    void SparkEngine::Run(eastl::function<bool()> shouldQuit)
    {
        while (!shouldQuit())
        {
            float deltaTime = CalculDeltaTime();
            TickBus::Broadcast(&TickBus::Events::OnTick, m_worldContext, deltaTime);
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