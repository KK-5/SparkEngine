#pragma once

#include <Math/Vector3.h>
#include <Math/Matrix4x4.h>
#include <ECS/ComponentTraits.h>

namespace Spark::Transform
{
    struct TransformComponent 
    {
        Math::Vector3    m_position{0, 0, 0};
        Math::Vector3    m_rotation{0, 0, 0};
        Math::Vector3    m_scale   {1, 1, 1};
    };

    struct LocalTransformMatrix
    {
        Math::Matrix4X4 m_localMatrix{Math::Matrix4X4Const::IDENTITY};
    };

    struct WorldTransformMatrix
    {
        Math::Matrix4X4 m_worldMatrix{Math::Matrix4X4Const::IDENTITY};
    };
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(Transform::TransformComponent,
        static constexpr bool editable = true;
    )
}