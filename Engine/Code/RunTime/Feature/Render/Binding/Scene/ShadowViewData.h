#pragma once

#include <cstdint>

#include <Math/Matrix4x4.h>

namespace Spark::Render
{
    //! One shadow view's sampling record, indexed by its shadow map's index — LightData::
    //! m_shadowIndex plus the light's map — so this buffer has holes wherever no shadow map is
    //! held. HLSL mirror lives in Lib/Shadow/ShadowViewData.hlsli; the two MUST stay
    //! byte-for-byte identical.
    //!
    //! Assembled each frame by SceneBindingSystem from two sources: the geometry from the
    //! shadow view entity, the authored bias from the light.
    struct ShadowViewData
    {
        //! World -> UV of the shadow map's layer. One mul, no layout in the shader.
        Math::Matrix4X4 m_worldToShadowUV = Math::Matrix4X4Const::IDENTITY;

        //! The shadow map covers [0, m_uvMax] of its layer on both axes; sampling clamps to it.
        //! Negative by default, and deliberately: no uv falls inside, so a row nobody has
        //! written reads as unshadowed without the shader testing for it.
        float           m_uvMax = -1.0f;

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

        float           m_padding0 = 0.0f;
        float           m_padding1 = 0.0f;
        float           m_padding2 = 0.0f;
    };

    static_assert(sizeof(ShadowViewData) == 96,
        "ShadowViewData must stay 96 bytes to match ShadowViewData.hlsli.");
}
