#pragma once

#include <ECS/ComponentTraits.h>
#include <Math/Matrix4x4.h>

namespace Spark::Camera
{
    enum CameraType : uint32_t
    {
        Perspective,
        Orthographic,
    };

    struct CameraComponent
    {
        CameraType m_type         {CameraType::Perspective};
        float      m_fov          {75.f};   // vertical FOV, degrees
        float      m_clipStart    {0.01f};
        float      m_clipEnd      {1000.f};
    };

    //! World->view transform resolved by CameraSystem from WorldTransformMatrix. The
    //! PROJECTION is intentionally absent: aspect is a render-target property unknown at
    //! this (world) layer, so ViewBindingSystem builds the projection per view from
    //! CameraComponent + the render extent. Symmetric to LightComponent -> LightRenderData.
    struct CameraViewMatrix
    {
        Math::Matrix4X4 m_viewMatrix {Math::Matrix4X4Const::IDENTITY};
    };
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(Camera::CameraComponent,
        static constexpr bool editable = true;
    )
}