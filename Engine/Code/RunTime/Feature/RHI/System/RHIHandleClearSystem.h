#pragma once

#include <ECS/ISystem.h>
#include <ECS/SystemTraits.h>
#include <Tick/TickBus.h>
#include <CoreComponents/Tags.h>
#include <RHI/Context/RHIContext.h>

namespace Spark::RHI
{
    class RHIHandleClearSystem final : public ISystem,
                                       public TickBus::Handler
    {
    public:
        eastl::vector<HashString> Request() const override
        {
            return {};
        }

        HashString GetName() const override
        {
            return "RHIHandleClearSystem"_hs;
        }

        void InitInternal() override
        {
            TickBus::Handler::BusConnect();
        }

        void ShutdownInternal() override
        {
            TickBus::Handler::BusDisconnect();
        }

        void OnTick(float /*deltaTime*/) override
        {
            auto* rhiCtx = RHIExecuteContext::Current();
            if (!rhiCtx)
            {
                return;
            }

            auto view = rhiCtx->GetView<DeadTag>();
            rhiCtx->DestoryEntity(view.begin(), view.end());
        }

        unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(TickOrder::TICK_LAST) - 1;
        }
    };
}
