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
    //! A compute pass writes the arguments of a dispatch into a buffer of the render graph,
    //! and the next one dispatches by them (DispatchIndirect): a bright band on the left,
    //! over a dim pattern a direct dispatch laid first, as wide as the first pass decided.
    class IndirectDispatchFeature : public TickBus::Handler
    {
    public:
        IndirectDispatchFeature();
        ~IndirectDispatchFeature();

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
        void CreateArgsPass();
        void CreatePatternPass();
        void CreatePresentPass();

        //! The view's g_Views slot, when the window has a size and the slot exists.
        bool TryGetViewIndex(uint32_t& viewIndex) const;

        Spark::RHI::RHIHandle m_view = Spark::RHI::NullHandle;

        Ptr<Spark::Resource::ShaderAsset> m_argsShader;
        Ptr<Spark::Resource::ShaderAsset> m_patternShader;
        Ptr<Spark::Resource::ShaderAsset> m_presentShader;

        //! The window size this frame; the passes declare nothing while it is empty.
        Spark::Math::Vector2Int m_size { 0, 0 };
        float                   m_time = 0.f;

        //! Whether the passes before declared their resource this frame; the one that reads
        //! it declares only then.
        bool                    m_argsWritten    = false;
        bool                    m_patternWritten = false;
    };
}
