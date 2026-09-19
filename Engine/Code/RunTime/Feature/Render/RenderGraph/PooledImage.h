#pragma once

#include <RHI/Context/RHIContext.h>
#include <RHI/Resource/Image/ImageDescriptor.h>

#include <Pass/Component/RHIComponents.h>

namespace Spark::RHI
{
    class ImagePool;
    struct ClearValue;
}

namespace Spark::Render
{
    //! Only what forces a different allocation — a differing clear value or queue mask does not.
    bool IsSameImageStorage(const RHI::ImageDescriptor& a, const RHI::ImageDescriptor& b);

    //! An idle pooled image matching `desc`, or a new one allocated from `pool`. Marked
    //! PooledImageActiveTag, so nothing else takes it this frame.
    RHIHandle AcquirePooledImage(
        RHIContext&                 ctx,
        RHI::ImagePool&             pool,
        const RHI::ImageDescriptor& desc,
        const RHI::ClearValue*      optimizedClearValue,
        const ObjectName&           debugName);
}
