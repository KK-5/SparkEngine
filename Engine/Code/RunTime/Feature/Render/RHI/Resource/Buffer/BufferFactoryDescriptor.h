#pragma once

#include <Object/IObjectFactory.h>

namespace Spark::Render::RHI
{
    class Buffer;

    struct BufferFactoryDescriptor : public IObjectFactory<Buffer>::Descriptor
    {
        /* data */
    };
    
}