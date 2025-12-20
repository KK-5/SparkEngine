/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/numeric_limits.h>
#include <EASTL/array.h>
#include "Format.h"

namespace Spark::RHI
{
    enum class ClearValueType : uint32_t
    {
        Vector4Float = 0,
        Vector4Uint,
        DepthStencil
    };

    struct ClearDepthStencil
    {
        bool operator==(const ClearDepthStencil& other) const
        {
            return
                abs(m_depth - other.m_depth) < eastl::numeric_limits<float>::epsilon() &&
                m_stencil == other.m_stencil;
        }
            
        float m_depth = 0.0f;
        uint8_t m_stencil = 0;
    };

    struct ClearValue
    {
        ClearValue();

        static ClearValue CreateDepth(float depth);
        static ClearValue CreateStencil(uint8_t stencil);
        static ClearValue CreateDepthStencil(float depth, uint8_t stencil);
        static ClearValue CreateVector4Float(float x, float y, float z, float w);
        static ClearValue CreateVector4Uint(uint32_t x, uint32_t y, uint32_t z, uint32_t w);

        bool operator==(const ClearValue& other) const;
            
        ClearValueType m_type;
        ClearDepthStencil m_depthStencil;
        eastl::array<float, 4> m_vector4Float;
        eastl::array<uint32_t, 4> m_vector4Uint;
    };
}