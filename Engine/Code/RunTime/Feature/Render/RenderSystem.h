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
#include "View/ViewBindingSystem.h"
#include "View/CameraViewSystem.h"
#include "View/ShadowViewSystem.h"
#include "SceneBind/SceneBindingSystem.h"
#include "Binding/Instance/InstanceBindingSystem.h"
#include "Binding/Material/MaterialBindingSystem.h"
#include "Drawable/MeshDrawableComposer.h"
#include "Drawable/DrawItemRouter.h"

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
        // Producers first, then the one encoding step, which serves all of them.
        CameraViewSystem  m_cameraViewSystem;
        ShadowViewSystem  m_shadowViewSystem;
        ViewBindingSystem m_viewBindingSystem;
        SceneBindingSystem m_sceneBindingSystem;
        MaterialBindingSystem m_materialBindingSystem;
        InstanceBindingSystem m_instanceBindingSystem;
        MeshDrawableComposer m_meshDrawableComposer;
        DrawItemRouter       m_drawItemRouter;

        Pipeline    m_pipeline {"default"};
        RenderGraph m_renderGraph;
    };
}