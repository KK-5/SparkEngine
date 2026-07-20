#pragma once

#include <cstdint>

#include <Math/Vector3.h>

namespace Spark::Render
{
    //! Per-light GPU record, one element of the global g_Lights StructuredBuffer (space0).
    //! HLSL mirror lives in SceneBindings.hlsl — the two MUST stay byte-for-byte identical
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
        float         m_pad0      = 0.0f;
        float         m_pad1      = 0.0f;
    };

    // 64B. StructuredBuffer elements are tightly C-packed, so sizeof must match the HLSL
    // struct in SceneBindings.hlsl; add padding deliberately when introducing new fields.
    static_assert(sizeof(LightData) == 64,
        "LightData must stay 64 bytes to match SceneBindings.hlsl.");
}
