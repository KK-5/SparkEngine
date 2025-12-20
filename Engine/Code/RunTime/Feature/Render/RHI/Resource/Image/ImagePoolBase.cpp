#include "ImagePoolBase.h"

namespace Spark::RHI
{
    ResultCode ImagePoolBase::InitImage(Image* image, const ImageDescriptor& descriptor, BackendMethod initResourceMethod)
    {
        image->SetDescriptor(descriptor);

        return InitResource(image, initResourceMethod);
    }
}