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
    class NRDSystem
    {
    public:
        NRDSystem();
        ~NRDSystem();

        void Init(RHI::Factory& factory, RHI::Device& device);
        void PrepareFrame(const RHI::RHIContext& rhiContext, const FrameTime& time);
        void Shutdown();

    private:
        struct State;
        UniquePtr<State> m_state;
    };
}
