#pragma once

#include <RHI/Resource/ResourcePoolDescriptor.h>
#include "ImageEnums.h"

namespace Spark::RHI
{
    class ImagePoolDescriptor: public ResourcePoolDescriptor
    {
    public:
        virtual ~ImagePoolDescriptor() = default;

        ImagePoolDescriptor() = default;

        ImageBindFlags m_bindFlags = ImageBindFlags::Color;
    };
}