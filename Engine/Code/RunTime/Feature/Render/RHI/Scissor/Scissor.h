/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#pragma once

#include <EASTL/numeric_limits.h>

namespace Spark::RHI
{
    struct Scissor
    {
        Scissor() = default;
        Scissor(int32_t minX, int32_t minY, int32_t maxX, int32_t maxY);

        Scissor GetScaled(
            float normalizedMinX,
            float normalizedMinY,
            float normalizedMaxY,
            float normalizedMaxX) const;

        static Scissor CreateNull();

        bool IsNull() const;

        static const int32_t DefaultScissorMin = 0;
        static const int32_t DefaultScissorMax = eastl::numeric_limits<int32_t>::max();

        int32_t m_minX = DefaultScissorMin;
        int32_t m_minY = DefaultScissorMin;
        int32_t m_maxX = DefaultScissorMax;
        int32_t m_maxY = DefaultScissorMax;
    };
}