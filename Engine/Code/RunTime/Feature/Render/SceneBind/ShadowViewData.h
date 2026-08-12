#pragma once

#include <cstdint>

#include <Math/Matrix4x4.h>
#include <Math/Vector4.h>

namespace Spark::Render
{
    //! One shadow view's sampling record, indexed by LightData::m_shadowIndex — which is the
    //! atlas tile slot, so this buffer has holes wherever a tile is unallocated. HLSL mirror
    //! lives in ShadowViewData.hlsli; the two MUST stay byte-for-byte identical.
    //!
    //! Assembled each frame by SceneBindingSystem from two sources: the geometry from the
    //! shadow view entity, the authored bias from the light.
    struct ShadowViewData
    {
        //! World -> atlas UV, tile transform already folded in. One mul, no tile layout in
        //! the shader.
        Math::Matrix4X4 m_worldToShadowUV = Math::Matrix4X4Const::IDENTITY;

        //! (minU, minV, maxU, maxV) of the tile's inset rect. Sampling clamps to it so a PCF
        //! tap at the edge cannot reach the neighbouring tile.
        Math::Vector4   m_uvMinMax {0.0f, 0.0f, 1.0f, 1.0f};

        float           m_depthBias    = 0.0f;
        float           m_normalOffset = 0.0f;
        float           m_pad0         = 0.0f;
        float           m_pad1         = 0.0f;
    };

    static_assert(sizeof(ShadowViewData) == 96,
        "ShadowViewData must stay 96 bytes to match ShadowViewData.hlsli.");
}
