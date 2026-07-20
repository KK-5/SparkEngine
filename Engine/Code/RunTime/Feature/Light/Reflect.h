#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaTypeTraits.h>

#include "Components.h"

namespace Spark::Light
{
    static void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<LightType>()
            .Type("LightType")
            .Data<LightType::Directional>("Directional")
            .Data<LightType::Point>("Point")
            .Data<LightType::Spot>("Spot");

        context.Reflect<LightComponent>()
            .Type("Light").Custom<Spark::ComponentTraitsRuntime>(Spark::ComponentTraits<LightComponent>{})
            .Data<&LightComponent::m_type>("Type").Custom<Spark::EnumElement>()
            .Data<&LightComponent::m_color>("Color").Custom<Spark::ColorElement>()
            .Data<&LightComponent::m_intensity>("Intensity").Custom<Spark::FloatElement>(0.f, 100.f, 0.1f)
            .Data<&LightComponent::m_range>("Range").Custom<Spark::FloatElement>(0.f, 1000.f, 0.1f)
            .Data<&LightComponent::m_innerConeDeg>("Inner Cone").Custom<Spark::FloatElement>(0.f, 89.f, 0.5f)
            .Data<&LightComponent::m_outerConeDeg>("Outer Cone").Custom<Spark::FloatElement>(0.f, 90.f, 0.5f)
            ;

        Spark::ComponentOpertion<LightComponent>(context);
    }
}
