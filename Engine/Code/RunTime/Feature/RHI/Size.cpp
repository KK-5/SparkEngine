/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "Size.h"

#include <EASTL/algorithm.h>

namespace Spark::RHI
{

    Size::Size(uint32_t width, uint32_t height, uint32_t depth)
        : m_width{width}
        , m_height{height}
        , m_depth{depth}
    {}

    Size Size::GetReducedMip(uint32_t mipLevel) const
    {
        Size size;
        size.m_width  = eastl::max(m_width  >> mipLevel, 1u);
        size.m_height = eastl::max(m_height >> mipLevel, 1u);
        size.m_depth  = eastl::max(m_depth  >> mipLevel, 1u);
        return size;
    }

    bool Size::operator == (const Size& rhs) const
    {
        return m_width == rhs.m_width && m_height == rhs.m_height && m_depth == rhs.m_depth;
    }

    bool Size::operator != (const Size& rhs) const
    {
        return m_width != rhs.m_width || m_height != rhs.m_height || m_depth != rhs.m_depth;
    }

    uint32_t& Size::operator [] (uint32_t idx)
    {
        uint32_t* ptr = &m_width;
        return *(ptr + idx);
    }

    uint32_t Size::operator [] (uint32_t idx) const
    {
        const uint32_t* ptr = &m_width;
        return *(ptr + idx);
    }
}
