#pragma once

#include <Base.h>
#include <Math/Matrix4x4.h>
#include <Tick/TickBus.h>
#include <Pass/Pass.h>
#include <RHI/Context/RHIHandle.h>
#include <RHI/Resource/Sampler/SamplerState.h>
#include <RHI/Resource/Image/ImageViewDescriptor.h>
#include <View/View.h>

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
        void CreateVertexBuffer();
        void CreateView();
        void CreatePasses();
        void Update();
        void BuildDrawable();
        void LoadAsset();
        void CreateImage();

        // Vertex buffer (in RHIContext)
        Spark::RHI::RHIHandle m_vertexBuffer    = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_indexBuffer     = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_baseColor       = Spark::RHI::NullHandle;
        Spark::RHI::ImageViewDescriptor m_baseColorViewDesc {};

        Spark::RHI::RHIHandle m_drawable = Spark::RHI::NullHandle;

        // Owns the camera and its space1 SRG; Update writes both.
        Spark::RHI::RHIHandle m_view = Spark::RHI::NullHandle;

        // Shader assets
        Ptr<Spark::Resource::ShaderAsset> m_shader;

        Ptr<Spark::Resource::ModelAsset> m_model;
        Ptr<Spark::Resource::ImageAsset> m_image;

        // Transform
        float m_rotationAngle = 0.f;

        // Per-frame CPU data computed in Update(), consumed by the ScenePass Compile hook.
        Spark::Math::Matrix4X4 m_modelMatrix;

        // Sampler
        Spark::RHI::SamplerState m_samplerState = Spark::RHI::SamplerState::Create(
            Spark::RHI::FilterMode::Linear,
            Spark::RHI::FilterMode::Linear,
            Spark::RHI::AddressMode::Wrap);
    };
}
