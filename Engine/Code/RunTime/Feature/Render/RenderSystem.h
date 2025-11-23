#pragma once

#include <ECS/ISystem.h>
#include <Tick/TickOrder.h>
#include <Tick/TickBus.h>

namespace Spark::Render
{
    class RenderSystem final: 
        public ISystem,
        public TickBus::Handler
    {
    public:
        // ISystem
        void Initialize() override;
        void ShutDown() override;

        eastl::vector<HashString> Request() const override
        {
            return {"LogSystem"_hs, "WindowSystem"_hs, "InputSystem"_hs};
        }

        HashString GetName() const override
        {
            return "RenderSystem"_hs;
        }

        // TickBus
        void OnTick(WorldContext& context, float deltaTime) override;
        
        inline unsigned int GetTickOrder() const override 
        {
            return static_cast<unsigned int>(RenderSystemTickOrder);
        }

    private:

    };
}