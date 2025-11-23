#pragma once

#include <EASTL/chrono.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/functional.h>

#include <ECS/WorldContext.h>
#include <Log/SpdLogSystem.h>
#include <SceneManager/SceneManager.h>
#include <EntityReaper/EntityReaper.h>
#include <Feature/Render/RenderSystem.h>
#include <Feature/Input/InputSystem.h>

namespace Spark
{
    // Must initialize a IWindowSystem before engine setup
    class SparkEngine
    {
    public:
        SparkEngine() = default;
        ~SparkEngine() = default;

        SparkEngine(const SparkEngine&) = delete;
        SparkEngine& operator=(const SparkEngine&) = delete;

        void SetUp();
        void ShutDown();

        void Pause();
        void Resume();

        void Run(eastl::function<bool()> shouldQuit);

    protected:
        unsigned int CalculFPS(float deltaTime);
        float        CalculDeltaTime();

    private:
        eastl::chrono::steady_clock::time_point m_lastTickTime {eastl::chrono::steady_clock::now()};
        unsigned int m_fps  {0};
        bool         m_quit {false};

        WorldContext m_worldContext {};

        eastl::unique_ptr<Render::RenderSystem>        m_renderSystem;
        eastl::unique_ptr<SpdLogSystem>                m_logSystem;
        eastl::unique_ptr<Input::InputSystem>          m_inputSystem;
        eastl::unique_ptr<SceneManager>                m_sceneManager;
        eastl::unique_ptr<EntityReaper>                m_entityReaper;
    };
}