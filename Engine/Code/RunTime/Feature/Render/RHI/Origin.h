/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <cstdint>

namespace Spark::Render::RHI
{
    struct Origin
    {
        Origin() = default;
        Origin(uint32_t left, uint32_t top, uint32_t front)
            : m_left{left}
            , m_top{top}
            , m_front{front}
        {}

        uint32_t m_left = 0;
        uint32_t m_top = 0;
        uint32_t m_front = 0;

        bool operator == (const Origin& rhs) const
        {
            return m_left == rhs.m_left && m_top == rhs.m_top && m_front == rhs.m_front;
        }

        bool operator != (const Origin& rhs) const
        {
            return m_left != rhs.m_left || m_top != rhs.m_top || m_front != rhs.m_front;
        }
    };
}