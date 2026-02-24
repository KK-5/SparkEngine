/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

namespace Spark::RHI
{
    struct Viewport
    {
        Viewport() = default;
        Viewport(
            float minX,
            float maxX,
            float minY,
            float maxY,
            float minZ = 0.0f,
            float maxZ = 1.0f);

        Viewport GetScaled(
            float normalizedMinX,
            float normalizedMaxX,
            float normalizedMinY,
            float normalizedMaxY,
            float normalizedMinZ = 0.0f,
            float normalizedMaxZ = 1.0f) const;

        static Viewport CreateNull();

        bool IsNull() const;

        float m_minX = 0.0f;
        float m_maxX = 0.0f;
        float m_minY = 0.0f;
        float m_maxY = 0.0f;
        float m_minZ = 0.0f;
        float m_maxZ = 1.0f;

        float GetWidth() const;
        float GetHeight() const;
        float GetDepth() const;
    };

    inline float Viewport::GetWidth() const
    {
        return m_maxX - m_minX;
    }

    inline float Viewport::GetHeight() const
    {
        return m_maxY - m_minY;
    }

    inline float Viewport::GetDepth() const
    {
        return m_maxZ - m_minZ;
    }
}