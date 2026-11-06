#pragma once

#include <EASTL/sort.h>

#include <ECS/Common.h>
#include <ECS/WorldContext.h>
#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <Base.h>
#include <Reflect.h>
#include <Reflection/TypeRegistry.h>
#include <VFS/VFSSystem.h>

#include <Input/InputSystem.h>

#include <RHI/RHIInterface.h>
#include <RHI/Backend/DX12/RHISystem.h>
#include <RHI/System/RHIResourceSystem.h>
#include <RHI/System/AsyncUploadSystem.h>
#include <RHI/System/RHIHandleClearSystem.h>
#include <RHI/Device/DeviceDescriptor.h>

#include <Resource/Asset.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/AssetManager.h>
#include <Resource/Common/CommonAssetLoader.h>
#include <Resource/Reflect.h>

#include <RenderSystem.h>
#include <Pass/PassContext.h>

#include "SimpleGlfwWindow.h"

namespace Spark::SandBox
{
    struct RenderGraphSystems
    {
        RenderGraphSystems() = default;
        RenderGraphSystems(RenderGraphSystems&&) = default;

        ~RenderGraphSystems()
        {
            // Upload requests must release their targets before renderer-owned pools go.
            m_asyncUpload.reset();
            m_render.reset();
            if (m_worldContext)
            {
                m_worldContext->Clear();
            }
            if (m_rhi && m_rhi->IsInitialized())
            {
                RHI::RHIHandleClearSystem::Collect(m_rhi->GetContext());
                if (auto* device = m_rhi->GetDevice())
                {
                    device->FlushReleaseQueue();
                }
            }
        }

        UniquePtr<ILogSystem>                       m_logger;
        // Stable addresses when this bundle is returned by value; outlive every system.
        UniquePtr<WorldContext>                     m_worldContext;
        UniquePtr<WorldExecuteContextGuard>         m_worldCtxGuard;
        SystemUniquePtr<VFSSystem>                  m_vfs;
        SystemUniquePtr<SimpleGlfwWindow>           m_window;
        SystemUniquePtr<Input::InputSystem>         m_input;
        SystemUniquePtr<RHI::DX12::RHISystem>       m_rhi;
        // Declared after the resource system, so it shuts down first: its requests hold the
        // resources they upload into, which have to go before the pools they came from.
        SystemUniquePtr<RHI::RHIResourceSystem>     m_rhiResource;
        SystemUniquePtr<RHI::AsyncUploadSystem>     m_asyncUpload;
        SystemUniquePtr<Resource::SparkAssetManager> m_assetManager;
        SystemUniquePtr<Render::RenderSystem>       m_render;
    };

    inline RenderGraphSystems InitRenderGraphApp(int width, int height, const char* title)
    {
        using namespace Spark;

        RenderGraphSystems sys;
        sys.m_worldContext = MakeUnique<WorldContext>();
        sys.m_worldCtxGuard = MakeUnique<WorldExecuteContextGuard>(*sys.m_worldContext);

        // Asset ids are cached as JSON, which runs off reflection.
        TypeRegistry::Register(Spark::Reflect);
        TypeRegistry::Register(Spark::Resource::Reflect);
        TypeRegistry::RegisterAll();

        // Logger
        LogConfig logConfig{};
        logConfig.m_showTimeStamp = true;
        sys.m_logger = MakeUnique<SpdLogSystem>(logConfig);

        // Window + Input
        sys.m_window = CreateSystem<SimpleGlfwWindow>(width, height, title);
        sys.m_window->Init();

        sys.m_input = CreateSystem<Input::InputSystem>();
        sys.m_input->Init();

        // RHI backend
        sys.m_rhi = CreateSystem<RHI::DX12::RHISystem>();
        sys.m_rhi->Init();

        // Device
        {
            auto* rhi = Service<RHI::RHIInterface>::Get();
            auto devs = rhi->EnumeratePhysicalDevices();
            ASSERT(!devs.empty(), "[InitRenderGraphApp] No physical device available.");

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
            ASSERT(r == RHI::ResultCode::Success, "[InitRenderGraphApp] Init device failed.");
        }

        // RHI resource infrastructure
        sys.m_rhiResource = CreateSystem<RHI::RHIResourceSystem>();
        sys.m_rhiResource->Init();

        sys.m_asyncUpload = CreateSystem<RHI::AsyncUploadSystem>();
        sys.m_asyncUpload->Init();

        // Asset manager. engine:// is the same mount the editor uses, so engine-relative ids
        // resolve -- notably the shared engine shader headers the samples include.
        sys.m_vfs = CreateSystem<VFSSystem>();
        sys.m_vfs->Init();
        sys.m_vfs->Mount("sandbox", SHADER_ASSET_DIR);
        sys.m_vfs->Mount("engine", ENGINE_ASSET_DIR);

        sys.m_assetManager = CreateSystem<Resource::SparkAssetManager>();
        sys.m_assetManager->Init();

        // Renderer
        sys.m_render = CreateSystem<Render::RenderSystem>();
        sys.m_render->Init();

        return sys;
    }

} // namespace Spark::SandBox
