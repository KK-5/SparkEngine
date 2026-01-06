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
#pragma once

#include <Math/Bit.h>
#include <EASTLEX/hash.h>
#include <RHILimits.h>

namespace Spark::RHI
{
    enum class FilterMode : uint32_t
    {
        Point,
        Linear
    };

    enum class ReductionType : uint32_t
    {
        Filter,      /// Performs filtering on samples.
        Comparison,  /// Performs comparison of samples using the supplied comparison function.
        Minimum,     /// Returns minimum of samples.
        Maximum      /// Returns maximum of samples.
    };

    enum class AddressMode : uint32_t
    {
        Wrap,
        Mirror,
        Clamp,
        Border,
        MirrorOnce
    };

    enum class ComparisonFunc : uint32_t
    {
        Never,
        Less,
        Equal,
        LessEqual,
        Greater,
        NotEqual,
        GreaterEqual,
        Always,
        Invalid
    };

    enum class BorderColor : uint32_t
    {
        OpaqueBlack,
        TransparentBlack,
        OpaqueWhite
    };

    class SamplerState
    {
        SamplerState() = default;

        static SamplerState Create(
            FilterMode filterModeMinMag,
            FilterMode filterModeMip,
            AddressMode addressMode,
            BorderColor borderColor = BorderColor::TransparentBlack);

        static SamplerState CreateAnisotropic(
            uint32_t anisotropyMax,
            AddressMode addressMode);

        uint32_t m_anisotropyMax = 1;
        uint32_t m_anisotropyEnable = 0;
        FilterMode m_filterMin = FilterMode::Point;
        FilterMode m_filterMag = FilterMode::Point;
        FilterMode m_filterMip = FilterMode::Point;
        ReductionType m_reductionType = ReductionType::Filter;
        ComparisonFunc m_comparisonFunc = ComparisonFunc::Always;
        AddressMode m_addressU = AddressMode::Wrap;
        AddressMode m_addressV = AddressMode::Wrap;
        AddressMode m_addressW = AddressMode::Wrap;
        float m_mipLodMin = 0.0f;
        float m_mipLodMax = static_cast<float>(Limits::Image::MipCountMax);
        float m_mipLodBias = 0.0f;
        BorderColor m_borderColor = BorderColor::TransparentBlack;

        bool operator==(const SamplerState& other) const;
        bool operator!=(const SamplerState& other) const;
    };

    struct SamplerStateHasher
    {
        size_t operator()(const SamplerState& samplerState)
        {
            // 这里直接用指针作为哈希seed
            size_t hash = eastl::hash<const SamplerState*>()(&samplerState);
            return hash;
        }
    };
}