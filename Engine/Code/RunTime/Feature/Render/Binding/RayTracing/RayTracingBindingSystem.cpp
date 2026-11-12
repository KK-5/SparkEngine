#include "RayTracingBindingSystem.h"
#include "RayTracingBinding.h"

#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Component/Component.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Device/Device.h>

#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>
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
        const RHI::InputName sceneInput(TopLevelSceneInputName);
        RHI::ShaderInputList inputs;
        inputs.m_accelerationStructures.push_back(
            RHI::ShaderInputAccelerationStructureDescriptor(sceneInput, 1, 0, kRayTracingSpaceId));
        Ptr<RHI::ShaderBindings> bindings = CreateSharedBindings(inputs, kRayTracingSpaceId, "RayTracingBindingSystem");
        if (!bindings)
        {
            return;
        }

        m_bindings = rhiContext.CreateEntity();
        rhiContext.Add<RHI::Components::ShaderBindings>(m_bindings, RHI::Components::ShaderBindings{ bindings });
        rhiContext.Add<RayTracingSceneBindingTag>(m_bindings);
    }

    void RayTracingBindingSystem::Shutdown(RHI::RHIContext& rhiContext)
    {
        if (m_bindings != RHI::NullHandle)
        {
            rhiContext.DestoryEntity(m_bindings);
            m_bindings = RHI::NullHandle;
        }
    }
}
