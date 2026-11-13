#pragma once

#include <Base.h>

#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

namespace Spark::Render
{
    //! The ShaderBindings of a shared group (scene / view / material / ...) over the inputs
    //! its system declares. The declaration is the group's layout, not any shader's
    //! reflection: DXC drops every resource an entry point does not read, so no shader
    //! reflects a group whole. It must agree with the group's .hlsli in registers and
    //! types; every pass binding the group checks what its shaders read against it, and
    //! takes the group's space from it whole (BuildPassPipelineLayout).
    //!
    //! Stage masks stay None: a group is not tied to stages, the pass that binds it is.
    //! Null, logged, on failure.
    Ptr<RHI::ShaderBindings> CreateSharedBindings(
        const RHI::ShaderInputList& inputs, uint32_t spaceId, const char* owner);

    //! The group whose space is `spaceId`, or null. A shared space has at most one group, so
    //! this is how a pass finds the group its shaders read without naming it: what a pass uses
    //! is its shaders' reflection, and the group that fills a space is found by that space.
    inline RHI::ShaderBindings* FindSharedBindings(RHI::RHIContext& context, uint32_t spaceId)
    {
        for (auto [entity, component] : context.GetView<RHI::Components::ShaderBindings>().each())
        {
            if (component.m_bindings != nullptr && component.m_bindings->GetSpaceId() == spaceId)
            {
                return component.m_bindings.get();
            }
        }
        return nullptr;
    }
}
