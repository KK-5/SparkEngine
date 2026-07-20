#pragma once

#include <cstdint>

#include <Math/Vector3.h>

namespace Spark::Light
{
    enum class LightType : uint32_t
    {
        Directional = 0,
        Point       = 1,
        Spot        = 2,
    };

    //! Authoring data for a light — NON-spatial only. Position and direction come from
    //! the entity's Transform (LightSystem resolves them into LightRenderData). This
    //! component holds just what an author/editor sets.
    struct LightComponent
    {
        LightType     m_type      = LightType::Directional;
        Math::Vector3 m_color     {1.0f, 1.0f, 1.0f};
        float         m_intensity = 1.0f;
        float         m_range     = 10.0f;    // point/spot falloff radius (world units)
        float         m_innerConeDeg = 20.0f; // spot inner cone half-angle
        float         m_outerConeDeg = 30.0f; // spot outer cone half-angle
    };

    //! Per-frame render-ready light state, produced by LightSystem from LightComponent +
    //! WorldTransformMatrix. ALL spatial computation (direction/position from the transform)
    //! happens upstream in LightSystem; the render-side SceneBindingSystem only MARSHALS
    //! this into the GPU g_Lights buffer — it never touches a transform. World-layer struct,
    //! no GPU layout/padding (that lives in Render::LightData). Symmetric to how
    //! CameraSystem turns WorldTransformMatrix into CameraViewMatrix.
    struct LightRenderData
    {
        LightType     m_type            = LightType::Directional;
        Math::Vector3 m_worldDirection  {0.0f, -1.0f, 0.0f}; // direction the light shines (world)
        Math::Vector3 m_worldPosition   {0.0f, 0.0f, 0.0f};  // point/spot origin (world)
        Math::Vector3 m_color           {1.0f, 1.0f, 1.0f};
        float         m_intensity = 1.0f;
        float         m_range     = 10.0f;
        float         m_cosInner  = 1.0f;   // cos(inner cone), spot
        float         m_cosOuter  = 1.0f;   // cos(outer cone), spot
    };
}
