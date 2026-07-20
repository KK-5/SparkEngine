#pragma once

#include <ECS/ISystem.h>
#include <Tick/TickOrder.h>
#include <Tick/TickBus.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/RenderTargetContext/RenderTargetContext.h>

#include "Pass/Pipeline.h"

#include "RenderGraph/RenderGraph.h"

#include "Feature/UI/RenderUI.h"
#include "Feature/UI/UIProcessFeature.h"
#include "Feature/DepthPre/DepthPreProcessor.h"
#include "Feature/GBuffer/GBufferProcessor.h"
#include "Feature/Lighting/LightingProcessor.h"
#include "Feature/Skybox/SkyboxProcessor.h"
#include "Feature/Tonemap/TonemapProcessor.h"
#include "View/ViewBindingSystem.h"
#include "SceneBind/SceneBindingSystem.h"
#include "Instance/InstanceBindingSystem.h"
#include "MaterialBind/MaterialTextureSystem.h"
#include "MaterialBind/MaterialBindingSystem.h"
#include "Drawable/DrawableComposer.h"

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

        void SetUpDefaultPipeline();

    private:
        bool InitRHIData();
        bool InitRenderUI();

        Ptr<RHI::SwapChain> m_swapChain;
        
        RenderUI m_rednerUI;
        UIProcessFeature m_uiProcessFeature;
        DepthPreProcessor m_depthPreProcessor;
        GBufferProcessor m_gbufferProcessor;
        LightingProcessor m_lightingProcessor;
        SkyboxProcessor m_skyboxProcessor;
        TonemapProcessor m_tonemapProcessor;
        ViewBindingSystem m_viewBindingSystem;
        SceneBindingSystem m_sceneBindingSystem;
        MaterialTextureSystem m_materialTextureSystem;
        MaterialBindingSystem m_materialBindingSystem;
        InstanceBindingSystem m_instanceBindingSystem;
        DrawableComposer m_drawableComposer;

        Pipeline    m_pipeline {"default"};
        RenderGraph m_renderGraph;
    };
}