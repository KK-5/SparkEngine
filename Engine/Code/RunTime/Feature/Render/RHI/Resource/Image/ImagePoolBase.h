#pragma once

#include <Resource/ResourcePool.h>
#include "Image.h"

namespace Spark::Render::RHI
{
    class ImagePoolBase : public ResourcePool
    {
    public:
        virtual ~ImagePoolBase() override = default;

    protected:
        ImagePoolBase() = default;

        ResultCode InitImage(
            Image* image,
            const ImageDescriptor& descriptor,
            BackendMethod initResourceMethod);
    
    private:
        using ResourcePool::InitResource;
    };
}