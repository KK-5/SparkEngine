#pragma once

#include <ECS/ISystem.h>
#include <ECS/ExecuteContext.h>
#include <ECS/WorldContext.h>
#include <ECS/Common.h>
#include <Tick/TickBus.h>
#include <CoreComponents/Tags.h>

namespace Spark
{
    class EntityReaper final : public ISystem,
                               public TickBus::Handler
    {
    public:
        // ISystem
        void InitInternal() override
        {
            TickBus::Handler::BusConnect();
        }

        void ShutdownInternal() override
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
        void OnTick(float deltaTime) override
        {
            auto curContext = WorldExecuteContext::Current();
            auto view = curContext->GetView<DeadTag>();
            curContext->DestoryEntity(view.begin(), view.end());
        }

        unsigned int GetTickOrder() const
        {
            return static_cast<unsigned int>(TickOrder::TICK_LAST);
        }

    };
}