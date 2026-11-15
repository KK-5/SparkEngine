#pragma once

#include <Pass/Component/RHIComponents.h>

namespace Spark::Render
{
    //! A resource entity's shader usage: where shaders can see it. Only the location — and, for
    //! a kind a resource can be seen through several ways, the default view — is published; the
    //! object stays on the resource's own components.

    struct ShaderAccelerationStructureUsage
    {
        ShaderInputBinding m_binding;
    };

    struct ShaderBufferUsage
    {
        ShaderInputBinding         m_binding;
        RHI::BufferViewDescriptor m_view;
    };

    struct ShaderImageUsage
    {
        ShaderInputBinding        m_binding;
        RHI::ImageViewDescriptor m_view;
    };
}
