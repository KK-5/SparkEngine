#pragma once

#include <Base.h>
#include <RHI/Context/RHIContext.h>

namespace Spark
{
    struct FrameTime;
}

namespace Spark::RHI
{
    class Device;
    class Factory;
}

namespace Spark::Render
{
    class PassContext;

    class NRDSystem
    {
    public:
        NRDSystem();
        ~NRDSystem();

        void Init(RHI::Factory& factory, RHI::Device& device);
        void SetUp(PassContext& passes);
        void PrepareFrame(RHI::RHIContext& rhiContext, const FrameTime& time);
        //! After the render graph has made the GPU idle.
        void Shutdown(RHI::RHIContext& rhiContext);

    private:
        struct State;
        UniquePtr<State> m_state;
    };
}
