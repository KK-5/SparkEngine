#pragma once

#include <Base.h>
#include <Tick/TickBus.h>
#include <Pass/Pass.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <RHI/Viewport/Viewport.h>
#include <RHI/Scissor/Scissor.h>

namespace Spark::RHI
{
    class ShaderBindings;
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
        void CreateViewBindings();
        void CreateVertexBuffer();
        void CreatePasses();
        void UpdateViewBindings();
        void BuildDrawRequest();
        void LoadAsset();
        void CreateImage();

        // Per-pass ShaderBindings (new ShaderInput path).
        Spark::RHI::RHIHandle           m_viewBindingsEntity = Spark::RHI::NullHandle;
        Ptr<Spark::RHI::ShaderBindings> m_viewBindings;

        // Vertex buffer (in RHIContext)
        Spark::RHI::RHIHandle m_vbEntity        = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_indexEntity     = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_imageEntity     = Spark::RHI::NullHandle;
        Spark::RHI::ImageViewDescriptor m_baseColorViewDesc {};

        Spark::RHI::RHIHandle m_drawItemEntity = Spark::RHI::NullHandle;

        // Shader assets
        Ptr<Spark::Resource::ShaderAsset> m_shader;

        Ptr<Spark::Resource::ModelAsset> m_model;
        Ptr<Spark::Resource::ImageAsset> m_image;

        // Transform
        float m_rotationAngle = 0.f;

        // Pass-level viewport / scissor (stored here so CreatePasses can pick them up)
        Spark::RHI::Viewport m_viewport;
        Spark::RHI::Scissor  m_scissor;

        // Sampler
        Spark::RHI::SamplerState m_samplerState = Spark::RHI::SamplerState::Create(
            Spark::RHI::FilterMode::Linear,
            Spark::RHI::FilterMode::Linear,
            Spark::RHI::AddressMode::Wrap);
    };
}
