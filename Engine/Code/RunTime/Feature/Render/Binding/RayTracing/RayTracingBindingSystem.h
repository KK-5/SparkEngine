#pragma once

#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    //! Publishes the space7 group its resource is seen through. What it holds is bound by the
    //! passes that read it, at compile.
    class RayTracingBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiContext);
        //! After the GPU is idle, before RayTracingSceneSystem::Shutdown.
        void Shutdown(RHI::RHIContext& rhiContext);

    private:
        RHI::RHIHandle m_bindings = RHI::NullHandle;
    };
}
