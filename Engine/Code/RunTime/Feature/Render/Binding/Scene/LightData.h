#pragma once

#include <cstdint>

#include <Math/Vector3.h>

namespace Spark::Render
{
    inline constexpr uint32_t InvalidLightType = 0;

    //! Per-light GPU parameters for one frame.
    //! HLSL mirror lives in LightData.hlsli — the two MUST stay byte-for-byte identical
    //! (the static_assert below is the only automatic guard).
    //!
    //! Assembled each frame by SceneBindingSystem from Light::LightRenderData — a pure
    //! field-by-field marshal, no computation (direction/position were resolved upstream by
    //! LightSystem). m_direction is where the light shines (world space); the lighting
    //! shader negates it to point toward the light.
    struct LightData
    {
        Math::Vector3 m_direction{0.0f, 0.0f, 0.0f}; // dir/spot: direction the light shines (world)
        float         m_intensity = 0.0f;      // rect: radiance (LightSystem divided by the area)
        Math::Vector3 m_color{0.0f, 0.0f, 0.0f}; // rgb radiance tint
        uint32_t      m_type = InvalidLightType; // Light::LightType + 1 for a present light
        Math::Vector3 m_position{0.0f, 0.0f, 0.0f}; // point/spot: world origin
        float         m_invRange  = 0.0f;      // 1/range for attenuation (0 = directional)
        float         m_cosInner  = 1.0f;      // spot cone
        float         m_cosOuter  = 1.0f;
        int32_t       m_shadowIndex = -1;      // first shadow map, and g_ShadowViews row; -1 = no shadow map

        //! Shadow maps the light holds from m_shadowIndex on, a cube's being its faces. Above 1
        //! the shader adds a face index to m_shadowIndex; a face not committed this frame still
        //! owns its row, which reads as unshadowed.
        uint32_t      m_shadowMapCount = 1;

        //! Rect or directional emitter: unit right axis perpendicular to m_direction.
        Math::Vector3 m_right{0.0f, 0.0f, 0.0f};

        //! ShadowMask slot, granted per frame by ShadowMaskSystem; -1 = sample no mask.
        //! A different index space from m_shadowIndex: that one addresses an atlas row, this
        //! one is dense across the lights that were granted a mask.
        int32_t       m_shadowMaskIndex = -1;

        float         m_halfWidth  = 0.0f;     // rect, world units
        float         m_halfHeight = 0.0f;

        //! Authoring switch, independent of shadow-map allocation. 0 skips RT visibility.
        uint32_t      m_castShadow = 1;

        //! Directional emitter: 1 - cos(source half-angle).
        float         m_directionalConeHeight = 0.0f;
    };

    // 96B. StructuredBuffer elements are tightly C-packed, so sizeof must match the HLSL
    // struct in LightData.hlsli; add padding deliberately when introducing new fields.
    static_assert(sizeof(LightData) == 96,
        "LightData must stay 96 bytes to match LightData.hlsli.");

    //! One persistent light slot in g_Lights, with independent presence in each frame.
    struct LightRecord
    {
        LightData m_current;
        LightData m_previous;
    };

    static_assert(sizeof(LightRecord) == 2 * sizeof(LightData));
}
