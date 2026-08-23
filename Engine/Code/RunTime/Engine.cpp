
#include "Engine.h"

#include <Tick/TickBus.h>
#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <ECS/Common.h>
#include <Reflection/TypeRegistry.h>
#include <Reflect.h>

#include <Feature/Transform/Reflect.h>
#include <Feature/Camera/Reflect.h>
#include <Feature/Mesh/Reflect.h>
#include <Feature/Material/Reflect.h>
#include <Feature/Skybox/Reflect.h>
#include <Feature/Light/Reflect.h>

namespace Spark
{
    void SparkEngine::SetUp()
    {
        TypeRegistry::Register(Spark::Reflect);
        TypeRegistry::Register(Spark::Transform::Reflect);
        TypeRegistry::Register(Spark::Camera::Reflect);
        TypeRegistry::Register(Spark::Mesh::Reflect);
        TypeRegistry::Register(Spark::Material::Reflect);
        TypeRegistry::Register(Spark::Skybox::Reflect);
        TypeRegistry::Register(Spark::Light::Reflect);
        TypeRegistry::RegisterAll();

        m_worldCtxGuard = eastl::make_unique<WorldExecuteContextGuard>(m_worldContext);

        LogConfig logConfig{};
        logConfig.m_showTimeStamp = true;
        m_logSystem = eastl::make_unique<SpdLogSystem>(logConfig);

        m_vfs = CreateSystem<VFSSystem>();
        m_vfs->Init();
        m_vfs->Mount("engine", "Engine/Asset");

        m_entityReaper = CreateSystem<EntityReaper>();
        m_entityReaper->Init();

        m_sceneManager = CreateSystem<SceneManager>();
        m_sceneManager->Init();

        m_transformSystem = CreateSystem<Transform::TransformSystem>();
        m_transformSystem->Init();

        m_cameraSystem = CreateSystem<Camera::CameraSystem>();
        m_cameraSystem->Init();

        m_inputSystem = CreateSystem<Input::InputSystem>();
        m_inputSystem->Init();

        m_dx12Rhi = CreateSystem<RHI::DX12::RHISystem>();
        m_dx12Rhi->Init();

        {
            // Create RHI Device
            auto* rhi = Service<RHI::RHIInterface>::Get();
            auto devs = rhi->EnumeratePhysicalDevices();
            ASSERT(!devs.empty(), "[Engine] No physical device available.");

            // Prefer discrete GPU: dedicated video memory is the most reliable
            // heuristic — discrete GPUs have their own VRAM while integrated
            // GPUs share system memory and report 0 or very little.
            eastl::sort(devs.begin(), devs.end(), [](const Ptr<RHI::PhysicalDevice>& a,
                                                      const Ptr<RHI::PhysicalDevice>& b)
            {
                const auto& da = a->GetDescriptor();
                const auto& db = b->GetDescriptor();
                return da.m_heapSizePerLevel[static_cast<size_t>(RHI::HeapMemoryLevel::Device)]
                     > db.m_heapSizePerLevel[static_cast<size_t>(RHI::HeapMemoryLevel::Device)];
            });

            RHI::DeviceDescriptor deviceDesc;
            deviceDesc.m_frameCountMax = 3;
            RHI::ResultCode r = rhi->InitDevice(*devs.front(), deviceDesc);
            ASSERT(r == RHI::ResultCode::Success, "[Engine] Init RHI device failed.");
        }
        
        m_assetManager = CreateSystem<Resource::SparkAssetManager>();
        m_assetManager->Init();
        m_assetManager->AssetRegistry();
        m_assetManager->InitEnvironmentBaker();

        // Before RenderSystem: the material store must be pushed so the render-side
        // GPU binding (later phase) and any consumer can resolve MaterialComponent
        // references. No RHI/asset dependency for the entity layer itself.
        m_materialSystem = CreateSystem<Material::MaterialSystem>();
        m_materialSystem->Init();

        // Before RenderSystem: resolves each light's Transform into LightRenderData (which
        // the render-side SceneBindingSystem marshals) and creates the default directional
        // light. Ticks at TICK_PRE_RENDER, after TransformSystem.
        m_lightSystem = CreateSystem<Light::LightSystem>();
        m_lightSystem->Init();

        m_renderSystem = CreateSystem<Render::RenderSystem>();
        m_renderSystem->Init();
        m_renderSystem->SetUpDefaultPipeline();

        m_iconManager = CreateSystem<UI::IconManager>();
        m_iconManager->Init();

        m_meshSystem = CreateSystem<Mesh::MeshSystem>();
        m_meshSystem->Init();

        m_spawnResolver = eastl::make_unique<Spawn::ModelSpawnResolver>();
        m_spawnResolver->Init();

        m_skyboxSystem = CreateSystem<Skybox::SkyboxSystem>();
        m_skyboxSystem->Init();

        m_rhiResourceSystem = CreateSystem<RHI::RHIResourceSystem>();
        m_rhiResourceSystem->Init();

        m_asyncUploadSystem = CreateSystem<RHI::AsyncUploadSystem>();
        m_asyncUploadSystem->Init();

        m_rhiHandleClearSystem = CreateSystem<RHI::RHIHandleClearSystem>();
        m_rhiHandleClearSystem->Init();

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