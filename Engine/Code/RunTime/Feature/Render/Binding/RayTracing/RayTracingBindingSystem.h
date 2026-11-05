#pragma once

#include <RHI/Context/RHIContext.h>

namespace Spark::Render
{
    class RayTracingBindingSystem
    {
    public:
        void Init(RHI::RHIContext& rhiContext);
        //! After RayTracingSceneSystem::Update.
        void Update();
        //! After the GPU is idle, before RayTracingSceneSystem::Shutdown.
        void Shutdown(RHI::RHIContext& rhiContext);

    private:
        RHI::RHIHandle m_bindings = RHI::NullHandle;
    };
}
