#include "ImagePoolBase.h"

#include "ImageDescriptor.h"

namespace Spark::RHI
{
    ResultCode ImagePoolBase::InitImage(Image* image, const ImageDescriptor& descriptor, BackendMethod initResourceMethod)
    {
        image->SetDescriptor(descriptor);

        SetResourceState(*image, GetResourceStateFromImageBindFlags(descriptor.m_bindFlags));

        return InitResource(image, initResourceMethod);
    }
}