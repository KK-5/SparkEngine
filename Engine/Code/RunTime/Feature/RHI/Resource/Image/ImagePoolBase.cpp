#include "ImagePoolBase.h"

#include "ImageDescriptor.h"

namespace Spark::RHI
{
    ResultCode ImagePoolBase::InitImage(Image* image, const ImageDescriptor& descriptor, BackendMethod initResourceMethod)
    {
        SetResourceState(*image, RHI::ResourceState{});

        image->SetDescriptor(descriptor);

        return InitResource(image, initResourceMethod);
    }
}