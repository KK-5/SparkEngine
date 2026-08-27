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

        context.Reflect<ShadowFilterWidth>()
            .Type("ShadowFilterWidth")
            .Data<ShadowFilterWidth::W3>("3x3")
            .Data<ShadowFilterWidth::W5>("5x5")
            .Data<ShadowFilterWidth::W7>("7x7");

        context.Reflect<LightComponent>()
            .Type("Light").Custom<Spark::ComponentTraitsRuntime>(Spark::ComponentTraits<LightComponent>{})
            .Data<&LightComponent::m_type>("Type").Custom<Spark::EnumElement>()
            .Data<&LightComponent::m_color>("Color").Custom<Spark::ColorElement>()
            .Data<&LightComponent::m_intensity>("Intensity").Custom<Spark::FloatElement>(0.f, 100.f, 0.1f)
            .Data<&LightComponent::m_range>("Range").Custom<Spark::FloatElement>(0.f, 1000.f, 0.1f)
            .Data<&LightComponent::m_innerConeDeg>("Inner Cone").Custom<Spark::FloatElement>(0.f, 89.f, 0.5f)
            .Data<&LightComponent::m_outerConeDeg>("Outer Cone").Custom<Spark::FloatElement>(0.f, 90.f, 0.5f)
            .Data<&LightComponent::m_castShadow>("Cast Shadow").Custom<Spark::BoolElement>()
            .Data<&LightComponent::m_shadowBias>("Shadow Bias").Custom<Spark::FloatElement>(0.f, 0.01f, 0.0001f, "%.5f")
            .Data<&LightComponent::m_shadowNormalOffsetTexels>("Shadow Normal Offset").Custom<Spark::FloatElement>(0.f, 8.f, 0.1f, "%.1f")
            .Data<&LightComponent::m_shadowFilterWidth>("Shadow Filter").Custom<Spark::EnumElement>()
            .Data<&LightComponent::m_shadowDistance>("Shadow Distance").Custom<Spark::FloatElement>(1.f, 500.f, 1.f, "%.1f")
            ;

        Spark::ComponentOperation<LightComponent>(context);
    }
}
