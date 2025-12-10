#include "ImageEnums.h"

#include <Format.h>

namespace Spark::Render::RHI
{
    ImageAspectFlags GetImageAspectFlags(Format format)
    {
        switch (format)
        {
        case Format::D32_FLOAT_S8X24_UINT:
        case Format::D16_UNORM_S8_UINT:
        case Format::D24_UNORM_S8_UINT:
            return ImageAspectFlags::DepthStencil;
        case Format::D32_FLOAT:
        case Format::D16_UNORM:
            return ImageAspectFlags::Depth;
        default:
            return ImageAspectFlags::Color;
        }
    }
}