#pragma once

#include <EASTL/chrono.h>
#include <EASTL/functional.h>

#include <Base.h>
#include <ECS/WorldContext.h>
#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <SceneManager/SceneManager.h>
#include <EntityReaper/EntityReaper.h>
#include <Render/RenderSystem.h>
#include <Input/InputSystem.h>
#include <RHI/Backend/DX12/RHISystem.h>
#include <RHI/System/RHIResourceSystem.h>
#include <RHI/System/AsyncUploadSystem.h>
#include <RHI/System/RHIHandleClearSystem.h>
#include <Resource/AssetManager.h>
#include <UI/ImGui/IconManager.h>
#include <Mesh/MeshSystem.h>
#include <Mesh/MeshResolver.h>
#include <Material/MaterialSystem.h>
#include <Light/LightSystem.h>
#include <Skybox/SkyboxSystem.h>
#include <Resource/Bus/AssetResolveBus.h>
#include <Feature/Transform/TransformSystem.h>
#include <Feature/Camera/CameraSystem.h>

namespace Spark
{
    // Must initialize a IWindowSystem before engine setup
    class SparkEngine
    {
    public:
        SparkEngine() = default;
        ~SparkEngine();

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
        bool         m_quit       {false};
        bool         m_initialized{false};

        WorldContext m_worldContext {};
        UniquePtr<WorldExecuteContextGuard>           m_worldCtxGuard;

        UniquePtr<SpdLogSystem>                      m_logSystem;
        SystemUniquePtr<Input::InputSystem>          m_inputSystem;
        SystemUniquePtr<SceneManager>                m_sceneManager;
        SystemUniquePtr<Transform::TransformSystem>  m_transformSystem;
        SystemUniquePtr<Camera::CameraSystem>        m_cameraSystem;
        SystemUniquePtr<EntityReaper>                m_entityReaper;
        SystemUniquePtr<RHI::DX12::RHISystem>        m_dx12Rhi;
        SystemUniquePtr<RHI::RHIResourceSystem>      m_rhiResourceSystem;
        SystemUniquePtr<RHI::AsyncUploadSystem>      m_asyncUploadSystem;
        SystemUniquePtr<RHI::RHIHandleClearSystem>   m_rhiHandleClearSystem;
        SystemUniquePtr<Render::RenderSystem>        m_renderSystem;
        SystemUniquePtr<Resource::SparkAssetManager> m_assetManager;
        SystemUniquePtr<UI::IconManager>             m_iconManager;
        SystemUniquePtr<Mesh::MeshSystem>            m_meshSystem;
        UniquePtr<Mesh::MeshResolver>                 m_meshResolver;
        SystemUniquePtr<Material::MaterialSystem>    m_materialSystem;
        SystemUniquePtr<Light::LightSystem>          m_lightSystem;
        SystemUniquePtr<Skybox::SkyboxSystem>        m_skyboxSystem;
    };
}