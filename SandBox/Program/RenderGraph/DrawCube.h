#pragma once

#include <Base.h>
#include <Tick/TickBus.h>
#include <Pass/Pass.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/Resource/Sampler/SamplerState.h>
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
    class ModelAsset;
    class ImageAsset;
}

namespace Spark::SandBox
{
    class DrawCube : public TickBus::Handler
    {
    public:
        DrawCube();
        ~DrawCube();

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
        void LoadAsset();
        void CreateImage();

        Spark::RHI::RHIHandle FindSwapChainView() const;

        // ViewSRG (in RHIContext)
        Spark::RHI::RHIHandle  m_viewSRGEntity = Spark::RHI::NullHandle;
        Ptr<Spark::RHI::ShaderResourceLayout> m_srgLayout;
        Ptr<Spark::RHI::ShaderResource>       m_srg;

        // Vertex buffer (in RHIContext)
        Spark::RHI::RHIHandle m_vbEntity        = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_vbViewEntity    = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_indexEntity     = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_indexViewEntity = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_imageEntity     = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_imageViewEntity = Spark::RHI::NullHandle;

        // Swap chain view (looked up from RHIContext at Init time)
        Spark::RHI::RHIHandle m_swapchainView = Spark::RHI::NullHandle;

        Spark::RHI::RHIHandle m_drawItemEntity = Spark::RHI::NullHandle;

        // Shader assets
        Ptr<Spark::Resource::ShaderAsset> m_shader;

        Ptr<Spark::Resource::ModelAsset> m_model;
        Ptr<Spark::Resource::ImageAsset> m_image;

        Spark::RHI::Viewport m_viewport;
        Spark::RHI::Scissor  m_scissor;

        // Transform
        float m_rotationAngle = 0.f;

        // Sampler
        Spark::RHI::SamplerState m_samplerState = Spark::RHI::SamplerState::Create(
            Spark::RHI::FilterMode::Linear,
            Spark::RHI::FilterMode::Linear,
            Spark::RHI::AddressMode::Wrap);
    };
}
