#pragma once

#include <Resource/ResourcePoolDescriptor.h>
#include "ImageEnums.h"

namespace Spark::Render::RHI
{
    class ImagePoolDescriptor: public ResourcePoolDescriptor
    {
    public:
        virtual ~ImagePoolDescriptor() = default;

        ImagePoolDescriptor() = default;

        ImageBindFlags m_bindFlags = ImageBindFlags::Color;
    };
}