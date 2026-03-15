/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include "Scissor.h"

#include <EASTL/numeric.h>

namespace Spark::RHI
{
    Scissor::Scissor(
        int32_t minX,
        int32_t minY,
        int32_t maxX,
        int32_t maxY)
        : m_minX(minX)
        , m_minY(minY)
        , m_maxX(maxX)
        , m_maxY(maxY)
    {}

    Scissor Scissor::GetScaled(
        float normalizedMinX,
        float normalizedMinY,
        float normalizedMaxX,
        float normalizedMaxY) const
    {
        Scissor scissor;
        scissor.m_minX = static_cast<int32_t>(eastl::lerp(static_cast<float>(m_minX), static_cast<float>(m_maxX), normalizedMinX));
        scissor.m_maxX = static_cast<int32_t>(eastl::lerp(static_cast<float>(m_minX), static_cast<float>(m_maxX), normalizedMaxX));
        scissor.m_minY = static_cast<int32_t>(eastl::lerp(static_cast<float>(m_minY), static_cast<float>(m_maxY), normalizedMinY));
        scissor.m_maxY = static_cast<int32_t>(eastl::lerp(static_cast<float>(m_minY), static_cast<float>(m_maxY), normalizedMaxY));
        return scissor;
    }

    Scissor Scissor::CreateNull()
    {
        return Scissor{0, 0, -1, -1};
    }

    bool Scissor::IsNull() const
    {
        return ((m_minX > m_maxX) || (m_minY > m_maxY));
    }
}