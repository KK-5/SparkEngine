#pragma once

#include <cstdint>

#include <Math/Vector3.h>

namespace Spark::Render
{
    //! Per-light GPU record, one element of the global g_Lights StructuredBuffer (space0).
    //! HLSL mirror lives in SceneBindings.hlsli — the two MUST stay byte-for-byte identical
    //! (the static_assert below is the only automatic guard).
    //!
    //! Assembled each frame by SceneBindingSystem from Light::LightRenderData — a pure
    //! field-by-field marshal, no computation (direction/position were resolved upstream by
    //! LightSystem). m_direction is where the light shines (world space); the lighting
    //! shader negates it to point toward the light.
    struct LightData
    {
        Math::Vector3 m_direction;             // dir/spot: direction the light shines (world)
        float         m_intensity = 0.0f;
        Math::Vector3 m_color;                 // rgb radiance tint
        uint32_t      m_type      = 0;         // Light::LightType (0=dir, 1=point, 2=spot)
        Math::Vector3 m_position;              // point/spot: world origin
        float         m_invRange  = 0.0f;      // 1/range for attenuation (0 = directional)
        float         m_cosInner  = 1.0f;      // spot cone
        float         m_cosOuter  = 1.0f;
        int32_t       m_shadowIndex = -1;      // first g_ShadowViews row; -1 = casts no shadow

        //! Rows the light owns from m_shadowIndex on, one per cube face. Above 1 the shader
        //! adds a face index to m_shadowIndex; a face with no tile this frame still owns its
        //! row, which reads as unshadowed.
        uint32_t      m_shadowFaceCount = 1;

        //! ShadowMask slot, granted per frame by ShadowMaskSystem; -1 = sample no mask.
        //! A different index space from m_shadowIndex: that one addresses an atlas row, this
        //! one is dense across the lights that were granted a mask.
        int32_t       m_shadowMaskIndex = -1;

        //! To the next 16B boundary, and where the next field goes.
        uint32_t      m_padding[3] = {};
    };

    // 80B. StructuredBuffer elements are tightly C-packed, so sizeof must match the HLSL
    // struct in SceneBindings.hlsli; add padding deliberately when introducing new fields.
    static_assert(sizeof(LightData) == 80,
        "LightData must stay 80 bytes to match SceneBindings.hlsli.");
}
