#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaFieldTraits.h>
#include <ECS/ComponentRuntime.h>

#include "Components.h"

namespace Spark::AmbientOcclusion
{
    static void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<AmbientOcclusionQuality>()
            .Type("AmbientOcclusionQuality")
            .Data<AmbientOcclusionQuality::Low>("Low")
            .Data<AmbientOcclusionQuality::Medium>("Medium")
            .Data<AmbientOcclusionQuality::High>("High");

        context.Reflect<AmbientOcclusionComponent>()
            .Type("Ambient Occlusion").Traits(ComponentTraits<AmbientOcclusionComponent>::flags)
            .Data<&AmbientOcclusionComponent::m_intensity>("Intensity")
                .Custom<Spark::FloatElement>(0.0f, 1.0f, 0.01f)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&AmbientOcclusionComponent::m_radius>("Radius")
                .Custom<Spark::FloatElement>(0.05f, 5.0f, 0.01f)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&AmbientOcclusionComponent::m_quality>("Quality")
                .Custom<Spark::EnumElement>()
                .Traits(MetaFieldTraits::Serializable)
            ;

        Spark::ComponentOperation<AmbientOcclusionComponent>(context);
        Spark::ComponentRuntime<AmbientOcclusionComponent>(context);
    }
}
