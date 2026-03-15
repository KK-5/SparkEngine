/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "Viewport.h"

#include <EASTL/numeric.h>

namespace Spark::RHI
{
    Viewport::Viewport(
        float minX,
        float maxX,
        float minY,
        float maxY,
        float minZ,
        float maxZ)
        : m_minX(minX)
        , m_maxX(maxX)
        , m_minY(minY)
        , m_maxY(maxY)
        , m_minZ(minZ)
        , m_maxZ(maxZ)
    {}

    Viewport Viewport::GetScaled(
        float normalizedMinX,
        float normalizedMaxX,
        float normalizedMinY,
        float normalizedMaxY,
        float normalizedMinZ,
        float normalizedMaxZ) const
    {
        Viewport viewport;
        viewport.m_minX = eastl::lerp(m_minX, m_maxX, normalizedMinX);
        viewport.m_maxX = eastl::lerp(m_minX, m_maxX, normalizedMaxX);
        viewport.m_minY = eastl::lerp(m_minY, m_maxY, normalizedMinY);
        viewport.m_maxY = eastl::lerp(m_minY, m_maxY, normalizedMaxY);
        viewport.m_minZ = eastl::lerp(m_minZ, m_maxZ, normalizedMinZ);
        viewport.m_maxZ = eastl::lerp(m_minZ, m_maxZ, normalizedMaxZ);
        return viewport;
    }

    Viewport Viewport::CreateNull()
    {
        return Viewport{0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
    }

    bool Viewport::IsNull() const
    {
        return ((m_minX >= m_maxX) ||
            (m_minY >= m_maxY) ||
            (m_minZ >= m_maxZ));
    }
}