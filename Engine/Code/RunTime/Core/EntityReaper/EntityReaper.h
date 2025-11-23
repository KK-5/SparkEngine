#pragma once

#include <ECS/ISystem.h>
#include <Tick/TickBus.h>
#include <CoreComponents/Tags.h>

namespace Spark
{
    class EntityReaper final : public ISystem,
                               public TickBus::Handler
    {
    public:
        // ISystem
        void Initialize() override
        {
            TickBus::Handler::BusConnect();
        }

        void ShutDown() override
        {
            TickBus::Handler::BusDisconnect();
        }

        eastl::vector<HashString> Request() const override
        {
            return {};
        }

        HashString GetName() const override
        {
            return "EntityReaper"_hs;
        }

        // TickBus
        void OnTick(WorldContext& context, float deltaTime) override
        {
            auto view = context.GetView<DeadTag>();
            context.DestoryEntity(view.begin(), view.end());
        }

        unsigned int GetTickOrder() const
        {
            return static_cast<unsigned int>(TickOrder::TICK_LAST);
        }

    };
}