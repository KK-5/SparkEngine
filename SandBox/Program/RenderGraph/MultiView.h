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
}

namespace Spark::Resource
{
    class ShaderAsset;
    class ModelAsset;
    class ImageAsset;
}

namespace Spark::SandBox
{
    //! One cube, one pass, four views. The pass declares .RendersView<MainViewTag>(), so the
    //! executer emits one DrawList per live view and replays the pass's draws under each —
    //! the sample itself never loops over views at record time.
    class MultiView : public TickBus::Handler
    {
    public:
        MultiView();
        ~MultiView();

        bool Init();
        void Shutdown();

        // TickBus
        void OnTick(float deltaTime) override;
        unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(Spark::RenderSystemTickOrder) - 1;
        }

    private:
        //! A 2x2 grid; raising this only needs another entry in kViewRects.
        static constexpr uint32_t kViewCount = 4;

        void LoadAsset();
        void CreateImage();
        void CreateVertexBuffer();
        void CreateViews();
        void CreatePasses();
        void BuildDrawable();
        void Update();

        Spark::RHI::RHIHandle m_vertexBuffer = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_indexBuffer  = Spark::RHI::NullHandle;
        Spark::RHI::RHIHandle m_baseColor    = Spark::RHI::NullHandle;
        Spark::RHI::ImageViewDescriptor m_baseColorViewDesc {};

        Spark::RHI::RHIHandle m_drawable = Spark::RHI::NullHandle;

        //! Each owns a camera and its own space1 SRG, so the panels differ on screen exactly
        //! because the executer rebinds space1 when it crosses a DrawList boundary.
        Spark::RHI::RHIHandle m_views[kViewCount] = {
            Spark::RHI::NullHandle, Spark::RHI::NullHandle,
            Spark::RHI::NullHandle, Spark::RHI::NullHandle };

        Ptr<Spark::Resource::ShaderAsset> m_shader;
        Ptr<Spark::Resource::ModelAsset>  m_model;
        Ptr<Spark::Resource::ImageAsset>  m_image;

        float m_rotationAngle = 0.f;

        // Computed in Update(), consumed by the ScenePass Compile hook.
        Spark::Math::Matrix4X4 m_modelMatrix;

        Spark::RHI::SamplerState m_samplerState = Spark::RHI::SamplerState::Create(
            Spark::RHI::FilterMode::Linear,
            Spark::RHI::FilterMode::Linear,
            Spark::RHI::AddressMode::Wrap);
    };
}
