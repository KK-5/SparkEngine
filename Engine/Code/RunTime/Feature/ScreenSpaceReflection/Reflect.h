#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaFieldTraits.h>
#include <ECS/ComponentRuntime.h>

#include "Components.h"

namespace Spark::ScreenSpaceReflection
{
    static void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<ScreenSpaceReflectionQuality>()
            .Type("ScreenSpaceReflectionQuality")
            .Data<ScreenSpaceReflectionQuality::Low>("Low")
            .Data<ScreenSpaceReflectionQuality::Medium>("Medium")
            .Data<ScreenSpaceReflectionQuality::High>("High");

        context.Reflect<ScreenSpaceReflectionComponent>()
            .Type("Screen Space Reflection").Traits(ComponentTraits<ScreenSpaceReflectionComponent>::flags)
            .Data<&ScreenSpaceReflectionComponent::m_intensity>("Intensity")
                .Custom<Spark::FloatElement>(0.0f, 1.0f, 0.01f)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&ScreenSpaceReflectionComponent::m_maxRoughness>("Max Roughness")
                .Custom<Spark::FloatElement>(0.05f, 1.0f, 0.01f)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&ScreenSpaceReflectionComponent::m_quality>("Quality")
                .Custom<Spark::EnumElement>()
                .Traits(MetaFieldTraits::Serializable)
            ;

        Spark::ComponentOperation<ScreenSpaceReflectionComponent>(context);
        Spark::ComponentRuntime<ScreenSpaceReflectionComponent>(context);
    }
}
