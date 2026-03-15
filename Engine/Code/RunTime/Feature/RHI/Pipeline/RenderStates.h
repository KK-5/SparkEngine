/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */
#pragma once

#include <EASTL/array.h>
#include <RHI/RHILimits.h>
#include <RHI/MultisampleState.h>
#include <RHI/Resource/Sampler/SamplerState.h>

namespace Spark::RHI
{
    enum class CullMode : uint32_t
    {
        None,
        Front,
        Back,
        Invalid
    };

    enum class FillMode : uint32_t
    {
        Solid,
        Wireframe,
        Invalid
    };

    enum class DepthWriteMask : uint32_t
    {
        Zero,
        All,
        Invalid
    };

    enum class StencilOp :uint32_t
    {
        Keep,
        Zero,
        Replace,
        IncrementSaturate,
        DecrementSaturate,
        Invert,
        Increment,
        Decrement,
        Invalid
    };

    enum class BlendFactor : uint32_t
    {
        Zero,
        One,
        ColorSource,
        ColorSourceInverse,
        AlphaSource,
        AlphaSourceInverse,
        AlphaDest,
        AlphaDestInverse,
        ColorDest,
        ColorDestInverse,
        AlphaSourceSaturate,
        Factor,
        FactorInverse,
        ColorSource1,
        ColorSource1Inverse,
        AlphaSource1,
        AlphaSource1Inverse,
        Invalid
    };

    enum class BlendOp : uint32_t
    {
        Add,               //!< Result = Source + Destination
        Subtract,          //!< Result = Source - Destination
        SubtractReverse,   //!< Result = Destination - Source
        Minimum,           //!< Result = MIN(Source, Destination)
        Maximum,           //!< Result = MAX(Source, Destination)
        Invalid
    };

    struct RasterState
    {
        bool operator == (const RasterState& rhs) const;

        FillMode m_fillMode = FillMode::Solid;
        CullMode m_cullMode = CullMode::Back;
        int32_t m_depthBias = 0;
        float m_depthBiasClamp = 0.0f;
        float m_depthBiasSlopeScale = 0.0f;
        uint32_t m_multisampleEnable = 0;
        uint32_t m_depthClipEnable = 1;
        uint32_t m_conservativeRasterEnable = 0;
        uint32_t m_forcedSampleCount = 0;
    };

    struct DepthState
    {
        bool operator == (const DepthState& rhs) const;

        uint32_t m_enable = 1;
        DepthWriteMask m_writeMask = DepthWriteMask::All;
        ComparisonFunc m_func = ComparisonFunc::Less;
    };

    struct StencilOpState
    {
        bool operator == (const StencilOpState& rhs) const;

        StencilOp m_failOp = StencilOp::Keep;
        StencilOp m_depthFailOp = StencilOp::Keep;
        StencilOp m_passOp = StencilOp::Keep;
        ComparisonFunc m_func = ComparisonFunc::Always;
    };

    struct StencilState
    {
        bool operator == (const StencilState& rhs) const;

        uint32_t m_enable = 0;
        uint32_t m_readMask = 0xFF;
        uint32_t m_writeMask = 0xFF;
        StencilOpState m_frontFace;
        StencilOpState m_backFace;
    };

    struct DepthStencilState
    {
        bool operator == (const DepthStencilState& rhs) const;

        static DepthStencilState CreateDepth();
        static DepthStencilState CreateReverseDepth();
        static DepthStencilState CreateDisabled();

        DepthState m_depth;
        StencilState m_stencil;
    };

    enum class WriteChannelMask : uint8_t
    {
        ColorWriteMaskNone = 0,
        ColorWriteMaskRed = BIT(0),
        ColorWriteMaskGreen = BIT(1),
        ColorWriteMaskBlue = BIT(2),
        ColorWriteMaskAlpha = BIT(3),
        ColorWriteMaskAll = ColorWriteMaskRed | ColorWriteMaskGreen | ColorWriteMaskBlue | ColorWriteMaskAlpha
    };
    
    struct TargetBlendState
    {
        bool operator == (const TargetBlendState& rhs) const;

        uint32_t m_enable = 0;
        uint32_t m_writeMask = 0xF;
        BlendFactor m_blendSource = BlendFactor::One;
        BlendFactor m_blendDest = BlendFactor::Zero;
        BlendOp m_blendOp = BlendOp::Add;
        BlendFactor m_blendAlphaSource = BlendFactor::One;
        BlendFactor m_blendAlphaDest = BlendFactor::Zero;
        BlendOp m_blendAlphaOp = BlendOp::Add;
    };

    struct BlendState
    {
        bool operator == (const BlendState& rhs) const;

        uint32_t m_alphaToCoverageEnable = 0;
        uint32_t m_independentBlendEnable = 0;
        eastl::array<TargetBlendState, Limits::Pipeline::AttachmentColorCountMax> m_targets;
    };

    struct RenderStates
    {
        size_t GetHash(size_t seed = 0 ) const;

        bool operator == (const RenderStates& rhs) const;

        MultisampleState m_multisampleState;
        RasterState m_rasterState;
        BlendState m_blendState;
        DepthStencilState m_depthStencilState;
    };

    static constexpr uint32_t RenderStates_InvalidBool = eastl::numeric_limits<uint32_t>::max();
    static constexpr uint32_t RenderStates_InvalidUInt16 = eastl::numeric_limits<uint16_t>::max();
    static constexpr uint32_t RenderStates_InvalidUInt = eastl::numeric_limits<uint32_t>::max();
    static constexpr int32_t RenderStates_InvalidInt = eastl::numeric_limits<int32_t>::max();
    static constexpr float RenderStates_InvalidFloat = eastl::numeric_limits<float>::max();
}