#pragma once

#include <EASTL/chrono.h>
#include <EASTL/functional.h>

#include <Base.h>
#include <ECS/WorldContext.h>
#include <Log/SpdLogSystem.h>
#include <SceneManager/SceneManager.h>
#include <EntityReaper/EntityReaper.h>
#include <Render/RenderSystem.h>
#include <Input/InputSystem.h>
#include <RHI/Backend/DX12/RHISystem.h>

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
        void Shutdown();

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

        SystemUniquePtr<Render::RenderSystem> m_renderSystem;
        UniquePtr<SpdLogSystem>               m_logSystem;
        SystemUniquePtr<Input::InputSystem>   m_inputSystem;
        SystemUniquePtr<SceneManager>         m_sceneManager;
        SystemUniquePtr<EntityReaper>         m_entityReaper;
        SystemUniquePtr<RHI::DX12::RHISystem> m_dx12Rhi;
    };
}