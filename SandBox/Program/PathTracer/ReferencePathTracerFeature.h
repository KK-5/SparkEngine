#pragma once

#include <Base.h>
#include <RHI/Context/UniqueRHIHandle.h>
#include <Tick/TickBus.h>
#include <Tick/TickOrder.h>

#include "CornellBoxScene.h"

namespace Spark::Resource
{
    class ShaderAsset;
}

namespace Spark::SandBox
{
    class ReferencePathTracerFeature : public TickBus::Handler
    {
    public:
        ReferencePathTracerFeature();
        ~ReferencePathTracerFeature();

        bool Init(uint32_t debugMode = 0);
        void Shutdown();
        void OnTick(const FrameTime&) override;
        unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(RenderSystemTickOrder) - 1;
        }

    private:
        void CreatePass();
        void Update();
        void SetDebugMode(uint32_t mode);

        CornellBoxScene m_scene;
        Ptr<Resource::ShaderAsset> m_shader;
        RHI::UniqueRHIHandle m_view;
        uint32_t m_debugMode = 0;
    };
}
