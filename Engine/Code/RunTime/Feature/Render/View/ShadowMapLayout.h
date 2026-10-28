#pragma once

#include <cstdint>

#include <RHI/Format.h>

#include "View.h"

namespace Spark::Render
{
    //! A shadow map a shadow view, each a layer of one reserved 2D array. A view renders into
    //! the top left of its shadow map, as far as its resolution, and only that much of it is
    //! backed by memory. Each is as wide as the coarsest level.
    inline constexpr uint32_t kShadowMapResolution = 2048;

    //! Sixteen lights of up to six shadow maps: ShadowMask holds no more lights than that.
    inline constexpr uint32_t kShadowMapCount = 96;

    //! The levels a light may be granted, level l a resolution of 4096 >> l. Level 0 is out of
    //! reach: no light is worth every other light's shadow.
    inline constexpr uint32_t kShadowCoarsestLevel = 1;   // 2048 texels
    inline constexpr uint32_t kShadowFinestLevel   = 3;   //  512 texels

    inline constexpr uint32_t ShadowResolution(uint32_t level)
    {
        return 4096u >> level;
    }

    inline constexpr uint32_t kShadowCubeFaceCount = 6;

    //! One resource, two views: D32 DSV for ShadowPass, R32_FLOAT SRV for the passes sampling it.
    inline constexpr RHI::Format kShadowMapFormat = RHI::Format::D32_FLOAT;

    //! No level: the light holds no shadow, so there is nothing to be hysteretic about, and
    //! nothing an allocation could be granted at.
    inline constexpr uint32_t kNoShadowLevel = ~0u;

    //! The shadow map array, owned by ShadowViewSystem. How ShadowPass locates it.
    struct ShadowMapTag {};

    //! How far a shadow map of a level reaches across its layer, in UV: it is rendered from the
    //! top left, so its UV runs over [0, extent] on both axes.
    inline float ShadowMapExtent(uint32_t level)
    {
        return static_cast<float>(ShadowResolution(level)) / static_cast<float>(kShadowMapResolution);
    }

    //! Clip -> UV of a shadow map reaching `extent` across its layer. Left-multiply a view's
    //! worldToClip to get world -> UV. Y is flipped, as UV runs down; Z is left alone — LH_ZO
    //! clip depth is the 0..1 the comparison wants.
    inline Math::Matrix4X4 ShadowClipToUV(float extent)
    {
        Math::Matrix4X4 m = Math::Matrix4X4Const::IDENTITY;
        m[0][0] =  0.5f * extent;   m[3][0] = 0.5f * extent;
        m[1][1] = -0.5f * extent;   m[3][1] = 0.5f * extent;
        return m;
    }
}
