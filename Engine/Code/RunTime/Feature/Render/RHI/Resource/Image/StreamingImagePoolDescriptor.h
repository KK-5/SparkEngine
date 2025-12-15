#pragma once

#include <Resource/ResourcePoolDescriptor.h>

namespace Spark::Render::RHI
{
    class StreamingImagePoolDescriptor : public ResourcePoolDescriptor
    {
    public:
        StreamingImagePoolDescriptor() = default;
        virtual ~StreamingImagePoolDescriptor() = default;

        // Currently empty.
    };
}