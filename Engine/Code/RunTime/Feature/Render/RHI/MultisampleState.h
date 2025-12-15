/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/array.h>

#include <Math/Bit.h>
#include "RHILimits.h"

namespace Spark::Render::RHI
{
    struct SamplePosition
    {
        SamplePosition() = default;
        SamplePosition(uint8_t x, uint8_t y);

        bool operator==(const SamplePosition& other) const;
        bool operator!=(const SamplePosition& other) const;

        uint8_t m_x = 0;
        uint8_t m_y = 0;
    };

    struct MultisampleState
    {
        MultisampleState() = default;
        MultisampleState(uint16_t samples, uint16_t quality);

        bool operator==(const MultisampleState& other) const;
        bool operator!=(const MultisampleState& other) const;

        eastl::array<SamplePosition, Limits::Pipeline::MultiSampleCustomLocationsCountMax> m_customPositions{};
        uint32_t m_customPositionsCount = 0;
        uint16_t m_samples = 1;
        uint16_t m_quality = 0;
    };

}