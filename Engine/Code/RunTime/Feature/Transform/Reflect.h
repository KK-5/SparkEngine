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
            .Data<&TransformComponent::m_position>("Position").Custom<Spark::Vec3Element>()
            .Data<&TransformComponent::m_rotation>("Rotation").Custom<Spark::Vec3Element>()
            .Data<&TransformComponent::m_scale>("Scale").Custom<Spark::Vec3Element>()
            ;

        Spark::ComponentOpertion<TransformComponent>(context);
    }
}