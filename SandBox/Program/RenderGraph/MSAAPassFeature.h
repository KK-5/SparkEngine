#pragma once

#include <Base.h>
#include <Tick/TickBus.h>
#include <Pass/Pass.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>

namespace Spark::RHI
{
    class ShaderResource;
    class ShaderResourceLayout;
    class Fence;
}

namespace Spark::Resource
{
    class ShaderAsset;
}

namespace Spark::SandBox
{
    class MSAAPassFeature : public TickBus::Handler
    {
    public:
        MSAAPassFeature();
        ~MSAAPassFeature();

        bool Init();
        void Shutdown();

        // TickBus
        void OnTick(float deltaTime) override;
        unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(Spark::RenderSystemTickOrder) - 1;
        }

    private:
        void CreateViewSRG();
        void CreateVertexBuffer();
        void CreatePasses();
        void UpdateViewSRG();
        void BuildDrawItemEntity();

        Spark::RHI::RHIHandle FindSwapChainView() const;

        // ViewSRG (in RHIContext)
        Spark::RHI::RHIHandle  m_viewSRGEntity = Spark::RHI::NullHandle;
        Ptr<Spark::RHI::ShaderResourceLayout> m_srgLayout;
        Ptr<Spark::RHI::ShaderResource>       m_srg;

        // Vertex buffer (in RHIContext)
        Spark::RHI::RHIHandle m_vbEntity     = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_vbViewEntity = Spark::RHI::NullHandle;

        // Swap chain view (looked up from RHIContext at Init time)
        Spark::RHI::RHIHandle m_swapchainView = Spark::RHI::NullHandle;

        Spark::RHI::RHIHandle m_drawItemEntity = Spark::RHI::NullHandle;

        // Shader assets
        Ptr<Spark::Resource::ShaderAsset> m_vertShader;
        Ptr<Spark::Resource::ShaderAsset> m_fragShader;

        Spark::RHI::Viewport m_viewport;
        Spark::RHI::Scissor  m_scissor;

        // Transform
        float m_rotationAngle = 0.f;
        float m_colorPhase = 0.f;
    };
}
