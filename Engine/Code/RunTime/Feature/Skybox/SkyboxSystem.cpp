#include "SkyboxSystem.h"

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <Log/ILogSystem.h>

namespace Spark::Skybox
{
    void SkyboxSystem::InitInternal()
    {
        ComponentEventBus::Handler::BusConnect(GetTypeId<SkyboxComponent>());
        auto& world = *WorldExecuteContext::Current();
        world.RegisterEventOnEntityRemove<SkyboxComponent>();
    }

    void SkyboxSystem::ShutdownInternal()
    {
        ComponentEventBus::Handler::BusDisconnect();
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<ActiveSkyboxTag>();
        }
    }

    void SkyboxSystem::OnComponentConstruct(Entity entity)
    {
        auto& world = *WorldExecuteContext::Current();
        const auto active = world.GetView<ActiveSkyboxTag>();
        if (!active.empty())
        {
            LOG_WARN("[SkyboxSystem] Skybox on entity {} is inactive: entity {} already owns the active skybox.",
                static_cast<uint32_t>(entity), static_cast<uint32_t>(active.front()));
            return;
        }
        world.Add<ActiveSkyboxTag>(entity);
    }

    void SkyboxSystem::OnComponentDestory(Entity entity)
    {
        auto& world = *WorldExecuteContext::Current();
        if (world.Has<ActiveSkyboxTag>(entity))
        {
            world.Remove<ActiveSkyboxTag>(entity);
        }
    }
}
