#include "RayTracingBindingSystem.h"

#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Device/Device.h>

#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>
#include <Binding/SharedGroupLayouts.h>
#include <RayTracing/RayTracingScene.h>

namespace Spark::Render
{
    void RayTracingBindingSystem::Init(RHI::RHIContext& rhiContext)
    {
        const RHI::Device& device = *Service<RHI::RHIInterface>::Get()->GetDevice();
        if (!device.GetFeatures().m_rayTracing)
        {
            return;
        }

        // The layout this space declares, independent of what publishes into it this frame.
        const RHI::InputName sceneInput(SharedInput::RayTracingScene);

    }

    void RayTracingBindingSystem::Shutdown(RHI::RHIContext& rhiContext)
    {
    }
}
