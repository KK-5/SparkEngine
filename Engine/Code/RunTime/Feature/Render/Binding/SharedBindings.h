#pragma once

#include <Base.h>

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
}
