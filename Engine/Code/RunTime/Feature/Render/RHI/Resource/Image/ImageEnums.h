/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <Math/Bit.h>

namespace Spark::Render::RHI
{
    enum class ImageBindFlags : uint32_t
    {
        None        =    0,
        ShaderRead  = BIT(0),
        ShaderWrite = BIT(1),
        ShaderReadWrite = ShaderRead | ShaderWrite,
        Color       = BIT(2),
        Depth       = BIT(3),
        Stencil     = BIT(4),
        DepthStencil = Depth | Stencil,
        CopyRead    = BIT(5),
        CopyWrite   = BIT(6),
        ShadingRate = BIT(7),
    };

    enum class ImageDimension
    {
        Image1D = 1,
        Image2D = 2,
        Image3D = 3,
    };

    enum class ImageAspect : uint32_t
    {
        Color = 0,  //< Represents the color aspect of an image
        Depth,      //< Represents the depth aspect of an image
        Stencil,    //< Represents the stencil aspect of an image
        Count
    };

    static const uint32_t ImageAspectCount = static_cast<uint32_t>(ImageAspect::Count);

    enum class ImageAspectFlags : uint32_t
    {
        None    = 0,
        Color   = BIT(static_cast<uint32_t>(ImageAspect::Color)),
        Depth   = BIT(static_cast<uint32_t>(ImageAspect::Depth)),
        Stencil = BIT(static_cast<uint32_t>(ImageAspect::Stencil)),
        DepthStencil = Depth | Stencil,
        All = ~uint32_t(0)
    };

    ImageAspectFlags GetImageAspectFlags(Format format);
}