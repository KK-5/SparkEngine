#pragma once

#include <EASTL/sort.h>

#include <Log/ILogSystem.h>
#include <Log/SpdLogSystem.h>
#include <Base.h>

#include <Input/InputSystem.h>

#include <RHI/RHIInterface.h>
#include <RHI/Backend/DX12/RHISystem.h>
#include <RHI/System/RHIResourceSystem.h>
#include <RHI/System/AsyncUploadSystem.h>
#include <RHI/Device/DeviceDescriptor.h>

#include <Resource/Asset.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/AssetManager.h>
#include <Resource/Common/CommonAssetLoader.h>

#include <RenderSystem.h>
#include <Pass/PassContext.h>

#include "SimpleGlfwWindow.h"

namespace Spark::SandBox
{
    struct RenderGraphSystems
    {
        SystemUniquePtr<SimpleGlfwWindow>           m_window;
        SystemUniquePtr<Input::InputSystem>         m_input;
        SystemUniquePtr<RHI::DX12::RHISystem>       m_rhi;
        SystemUniquePtr<RHI::AsyncUploadSystem>     m_asyncUpload;
        SystemUniquePtr<RHI::RHIResourceSystem>     m_rhiResource;
        SystemUniquePtr<Resource::SparkAssetManager> m_assetManager;
        SystemUniquePtr<Render::RenderSystem>       m_render;
        UniquePtr<ILogSystem>                     m_logger;
    };

    inline RenderGraphSystems InitRenderGraphApp(int width, int height, const char* title)
    {
        using namespace Spark;

        RenderGraphSystems sys;

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
        sys.m_asyncUpload = CreateSystem<RHI::AsyncUploadSystem>();
        sys.m_asyncUpload->Init();

        sys.m_rhiResource = CreateSystem<RHI::RHIResourceSystem>();
        sys.m_rhiResource->Init();

        // Asset manager
        sys.m_assetManager = CreateSystem<Resource::SparkAssetManager>();
        sys.m_assetManager->Init();
        sys.m_assetManager->AddSearchPath(SHADER_ASSET_DIR);
        sys.m_assetManager->AddSearchPath(ENGINE_SHADER_DIR);   // shared engine shader headers (#include <...>)

        // Renderer
        sys.m_render = CreateSystem<Render::RenderSystem>();
        sys.m_render->Init();

        return sys;
    }

} // namespace Spark::SandBox
