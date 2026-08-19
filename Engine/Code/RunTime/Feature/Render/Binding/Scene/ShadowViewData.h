#pragma once

#include <cstdint>

#include <Math/Matrix4x4.h>
#include <Math/Vector4.h>

namespace Spark::Render
{
    //! One shadow view's sampling record, indexed by LightData::m_shadowIndex — which is the
    //! atlas tile slot, so this buffer has holes wherever a tile is unallocated. HLSL mirror
    //! lives in Lib/Shadow/ShadowViewData.hlsli; the two MUST stay byte-for-byte identical.
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
        //!
        //! Inverted by default, and deliberately: min above max is a rect no uv can fall
        //! inside, so a row nobody has written reads as unshadowed without the shader
        //! testing for it.
        Math::Vector4   m_uvMinMax {1.0f, 1.0f, 0.0f, 0.0f};

        float           m_depthBias = 0.0f;

        //! In texels, and turned into world units by the field below. See
        //! LightComponent::m_shadowNormalOffsetTexels for why the unit is not negotiable.
        float           m_normalOffsetTexels = 0.0f;

        //! Half the footprint authored as LightComponent::m_shadowFilterWidth, in texels.
        //!
        //! A half-width and not a tap pattern, which is what lets the filter be replaced
        //! without touching anything outside Lib/Shadow: the bicubic reconstruction reads it
        //! as its kernel half-width and picks the 4 / 9 / 16 tap variant from it. The same
        //! number sizes the fov a point light's faces are padded by, so the two cannot drift.
        float           m_pcfRadiusTexels = 2.5f;

        //! World size of one shadow texel, per unit of clip w. A perspective light's texels
        //! grow with distance and its w carries that distance; an orthographic light's do
        //! not, and its w is 1 — so one multiply covers both and no projection type reaches
        //! the shader.
        float           m_texelWorldSizePerW = 0.0f;
    };

    static_assert(sizeof(ShadowViewData) == 96,
        "ShadowViewData must stay 96 bytes to match ShadowViewData.hlsli.");
}
