#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaTypeTraits.h>

#include "Components.h"

namespace Spark::Transform
{
    static void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<TransformComponent>()
            .Type("Transform").Custom<ComponentTraitsRuntime>(ComponentTraits<TransformComponent>{})
            .Data<&TransformComponent::m_position>("Position").Custom<Spark::Vec3Element>(-10000, 10000, 0.01)
            .Data<&TransformComponent::m_rotation>("Rotation").Custom<Spark::Vec3Element>(0, 360, 0.1)
            .Data<&TransformComponent::m_scale>("Scale").Custom<Spark::Vec3Element>(-1000, 1000, 0.01)
            ;

        Spark::ComponentOpertion<TransformComponent>(context);
    }
}