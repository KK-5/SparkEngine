/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

 /*
 * Modified by SparkEngine in 2025
 *  -- Remove tie() function
 */

#include "SamplerState.h"

namespace Spark::RHI
{
    SamplerState SamplerState::Create(
        FilterMode filterModeMinMag,
        FilterMode filterModeMip,
        AddressMode addressMode,
        BorderColor borderColor)
    {
        SamplerState state;
        state.m_filterMin = state.m_filterMag = filterModeMinMag;
        state.m_filterMip = filterModeMip;
        state.m_addressU = state.m_addressV = state.m_addressW = addressMode;
        state.m_borderColor = borderColor;
        return state;
    }

    SamplerState SamplerState::CreateAnisotropic(
        uint32_t anisotropyMax,
        AddressMode addressMode)
    {
        SamplerState state;
        state.m_anisotropyEnable = 1;
        state.m_anisotropyMax = anisotropyMax;
        state.m_addressU = state.m_addressV = state.m_addressW = addressMode;
        return state;
    }

    bool SamplerState::operator==(const SamplerState& other) const
    {
        return m_anisotropyMax == other.m_anisotropyMax &&
               m_anisotropyEnable == other.m_anisotropyEnable &&
               m_filterMin == other.m_filterMin &&
               m_filterMag == other.m_filterMag &&
               m_filterMip == other.m_filterMip &&
               m_reductionType == other.m_reductionType &&
               m_comparisonFunc == other.m_comparisonFunc &&
               m_addressU == other.m_addressU &&
               m_addressV == other.m_addressV &&
               m_addressW == other.m_addressW &&
               m_mipLodMin == other.m_mipLodMin &&
               m_mipLodMax == other.m_mipLodMax &&
               m_mipLodBias == other.m_mipLodBias &&
               m_borderColor == other.m_borderColor;
    }

    bool SamplerState::operator!=(const SamplerState& other) const
    {
        return !(*this == other);
    }
}