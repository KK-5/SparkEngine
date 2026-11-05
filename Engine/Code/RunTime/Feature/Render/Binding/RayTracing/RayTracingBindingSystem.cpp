#include "RayTracingBindingSystem.h"
#include "RayTracingBinding.h"

#include <CoreComponents/Tags.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Component/Component.h>
#include <RHI/Device/Device.h>

#include <Binding/BindingSpaces.h>
#include <Binding/SharedBindings.h>
#include <RayTracing/RayTracingScene.h>
#include <Shader/ShaderBindingsUtils.h>

namespace Spark::Render
{
    namespace
    {
        const RHI::InputName SceneInput("g_RayTracingScene");
    }

    void RayTracingBindingSystem::Init(RHI::RHIContext& rhiContext)
    {
        const RHI::Device& device = *Service<RHI::RHIInterface>::Get()->GetDevice();
        if (!device.GetFeatures().m_rayTracing)
        {
            return;
        }

        RHI::ShaderInputList inputs;
        inputs.m_accelerationStructures.push_back(
            RHI::ShaderInputAccelerationStructureDescriptor(SceneInput, 1, 0, kRayTracingSpaceId));
        Ptr<RHI::ShaderBindings> bindings = CreateSharedBindings(inputs, kRayTracingSpaceId, "RayTracingBindingSystem");
        if (!bindings)
        {
            return;
        }

        m_bindings = rhiContext.CreateEntity();
        rhiContext.Add<RHI::Components::ShaderBindings>(m_bindings, RHI::Components::ShaderBindings{ bindings });
        rhiContext.Add<RayTracingSceneBindingTag>(m_bindings);
    }

    void RayTracingBindingSystem::Update()
    {
        auto* rhiContext = RHI::RHIExecuteContext::Current();
        if (!rhiContext || m_bindings == RHI::NullHandle)
        {
            return;
        }

        const RHI::AccelerationStructure* structure = nullptr;
        for (auto [entity, scene] : rhiContext->GetView<RayTracingSceneTag, RayTracingScene>(Exclude<DeadTag>).each())
        {
            structure = scene.m_topLevel.get();
        }
        // Binding can precede GPU construction; tracing scopes declare the AS read dependency.
        SetShaderAccelerationStructure(m_bindings, SceneInput, structure);
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
