#pragma once

#include <ECS/ISystem.h>
#include <Tick/TickOrder.h>
#include <Tick/TickBus.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Command/CommandQueueContext.h>
#include <RHI/RenderTargetContext/RenderTargetContext.h>

#include "Pass/Pipeline.h"
#include "Pass/RHIContext.h"
#include "Feature/UI/RenderUI.h"

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
        bool InitRHIData();
        bool InitRenderUI();

        void BuildPipeline();


        void ExecutePipeline(Pipeline& pipeline);

        struct RHIData
        {
            RHI::Factory* m_factory;
            Ptr<RHI::Device> m_device;
            RHI::CommandQueueContext m_commandQueuecontext;
            Ptr<RHI::SwapChain> m_swapChain;
            // eastl::vector<Ptr<RHI::ImageView>>
        };

        RHIData m_rhiData;
        RenderUI m_rednerUI;

        RHIContext m_rhiContext;
        Pipeline m_pipeline {"default"};
    };
}