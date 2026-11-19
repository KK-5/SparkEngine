#pragma once

#include <Pass/Component/RHIComponents.h>

namespace Spark::Render
{
    //! A resource selected by ImportShared for Direct binding in this graph frame.
    struct SharedBindingImportTag {};

    //! A resource entity's shader usage: its binding mode, fixed input location for Direct,
    //! and default view. The object stays on the resource's own components. Bindless Image
    //! usage publishes the view; the consumer resolves its index and delivers it to shaders.

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
