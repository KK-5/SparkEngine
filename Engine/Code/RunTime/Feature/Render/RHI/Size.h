/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include "Base.h"

namespace Spark::Render::RHI
{
    struct Size
    {
        Size() = default;
        Size(uint32_t width, uint32_t height, uint32_t depth);

        //! Returns the mip level size, assuming this size is mip 0. A value of 1 is
        //! half sized, 2 quarter sized, etc. Clamps at 1.
        Size GetReducedMip(uint32_t mipLevel) const;

        uint32_t m_width = 1;
        uint32_t m_height = 1;
        uint32_t m_depth = 1;

        bool operator == (const Size& rhs) const;
        bool operator != (const Size& rhs) const;

        uint32_t& operator [] (uint32_t idx);
        uint32_t operator [] (uint32_t idx) const;
    };
}
