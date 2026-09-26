#pragma once

#include <Base.h>
#include <Math/Vector2.h>
#include <Tick/TickBus.h>

#include <RHI/Context/RHIHandle.h>

namespace Spark::Resource
{
    class ShaderAsset;
}

namespace Spark::SandBox
{
    //! A compute pass writes a transient image, a render pass copies it to the swap chain.
    //! Both reach the image by heap index (.BindIndex) and take their parameters as root
    //! constants.
    class ComputePassFeature : public TickBus::Handler
    {
    public:
        ComputePassFeature();
        ~ComputePassFeature();

        bool Init();
        void Shutdown();

        // TickBus
        void OnTick(const FrameTime& time) override;
        unsigned int GetTickOrder() const override
        {
            return static_cast<unsigned int>(Spark::RenderSystemTickOrder) - 1;
        }

    private:
        void CreateView();
        void CreatePatternPass();
        void CreatePresentPass();

        Spark::RHI::RHIHandle m_view = Spark::RHI::NullHandle;

        Ptr<Spark::Resource::ShaderAsset> m_patternShader;
        Ptr<Spark::Resource::ShaderAsset> m_presentShader;

        //! The window size this frame; both passes declare nothing while it is empty.
        Spark::Math::Vector2Int m_size { 0, 0 };
        float                   m_time = 0.f;
    };
}
