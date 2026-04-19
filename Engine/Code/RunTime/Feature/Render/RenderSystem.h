#pragma once

#include <ECS/ISystem.h>
#include <Tick/TickOrder.h>
#include <Tick/TickBus.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Command/CommandQueueContext.h>
#include <RHI/RenderTargetContext/RenderTargetContext.h>

namespace Spark::Render
{
    class RenderSystem final: 
        public ISystem,
        public TickBus::Handler
    {
    public:
        // ISystem
        void InitInternal() override;
        void ShutdownInternal() override;

        eastl::vector<HashString> Request() const override
        {
            return {"LogSystem"_hs, "WindowSystem"_hs, "InputSystem"_hs};
        }

        HashString GetName() const override
        {
            return "RenderSystem"_hs;
        }

        // TickBus
        void OnTick(float deltaTime) override;
        
        inline unsigned int GetTickOrder() const override 
        {
            return static_cast<unsigned int>(RenderSystemTickOrder);
        }

    private:
        struct RHIContext
        {
            RHI::Factory* m_factory;
            Ptr<RHI::Device> m_device;
            RHI::CommandQueueContext m_commandQueuecontext;
            RHI::RenderTargetContext m_rtContext;
        };

        RHIContext m_rhiContext;
    };
}