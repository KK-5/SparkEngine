#include "SceneCommands.h"

#include <ECS/ExecuteContext.h>
#include <ECS/WorldContext.h>
#include <Log/ILogSystem.h>
#include <Tick/TickOrder.h>

#include <Material/MaterialContext.h>
#include <Scene/SceneSerializer.h>

namespace Editor
{
    using namespace Spark;

    void SceneCommandSystem::InitInternal()
    {
        TickBus::Handler::BusConnect();
    }

    void SceneCommandSystem::ShutdownInternal()
    {
        TickBus::Handler::BusDisconnect();
    }

    void SceneCommandSystem::OnTick(float)
    {
        if (!m_save.empty())
        {
            const eastl::string path = eastl::move(m_save);
            m_save.clear();
            if (Scene::SaveScene(path))
            {
                LOG_INFO("[SceneCommands] Scene saved to {}.", path.c_str());
            }
        }

        if (m_new)
        {
            m_new = false;
            auto* world     = WorldExecuteContext::Current();
            auto* materials = Material::MaterialExecuteContext::Current();
            if (world != nullptr && materials != nullptr)
            {
                Scene::ClearScene(*world, *materials);
            }
        }

        if (!m_open.empty())
        {
            const eastl::string path = eastl::move(m_open);
            m_open.clear();
            if (Scene::OpenScene(path))
            {
                LOG_INFO("[SceneCommands] Scene opened from {}.", path.c_str());
            }
        }
    }

    unsigned int SceneCommandSystem::GetTickOrder() const
    {
        // After EntityReaper: what the user deleted this frame is gone and the links it was in
        // are patched, so a save writes a scene that reads back as itself.
        return static_cast<unsigned int>(TickOrder::TICK_LAST + 1);
    }

    void SceneCommandSystem::Save(eastl::string_view virtualPath)
    {
        m_save.assign(virtualPath.data(), virtualPath.size());
    }

    void SceneCommandSystem::Open(eastl::string_view virtualPath)
    {
        m_open.assign(virtualPath.data(), virtualPath.size());
    }

    void SceneCommandSystem::New()
    {
        m_new = true;
    }
}
