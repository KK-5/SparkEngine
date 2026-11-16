#pragma once

#include <Base.h>

#include <RHI/Pipeline/PipelineLayoutDescriptor.h>
#include <RHI/Resource/ShaderInput/ShaderBindings.h>

namespace Spark::Render
{
    //! A group's ShaderBindings, over the inputs the space declares. The declaration is the
    //! group's layout, not any shader's reflection: DXC drops every resource an entry point does
    //! not read, so no shader reflects a group whole. It must agree with the group's .hlsli in
    //! registers and types; every pass reading the group checks its shaders against it, and takes
    //! the group's space from it whole (BuildPassPipelineLayout).
    //!
    //! Stage masks stay None: a group is not tied to stages, the pass that reads it is.
    //! Called by SharedBindingPool; null, logged, on failure.
    Ptr<RHI::ShaderBindings> CreateSharedBindings(
        const RHI::ShaderInputList& inputs, uint32_t spaceId, const char* owner);
}
