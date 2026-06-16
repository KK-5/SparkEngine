#pragma once

#include <ECS/ComponentTraits.h>
#include <Math/Matrix4x4.h>

namespace Spark::Camera
{
    enum CameraType : uint32_t
    {
        Prespective,
        Orthographic,
    };

    struct CameraComponent
    {
        CameraType m_type         {CameraType::Prespective};
        float      m_fov          {75.f};
        float      m_aspectRatio  {16.f / 9.f};
        float      m_clipStart    {0.01f};
        float      m_clipEnd      {1000.f};
    };

    struct CameraViewMatrix
    {
        Math::Matrix4X4 m_viewMatrix           {Math::Matrix4X4Const::IDENTITY};
        Math::Matrix4X4 m_projectionMatrix     {Math::Matrix4X4Const::IDENTITY};
        Math::Matrix4X4 m_viewProjectionMatrix {Math::Matrix4X4Const::IDENTITY};
    }; 
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(Camera::CameraComponent,
        static constexpr bool editable = true;
    )
}