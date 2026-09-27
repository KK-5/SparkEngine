#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaFieldTraits.h>
#include <ECS/ComponentRuntime.h>

#include "Components.h"

namespace Spark::Tonemap
{
    static void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<AgXLook>()
            .Type("AgXLook")
            .Data<AgXLook::BaseContrast>("Base Contrast")
            .Data<AgXLook::VeryHighContrast>("Very High Contrast")
            .Data<AgXLook::HighContrast>("High Contrast")
            .Data<AgXLook::MediumHighContrast>("Medium High Contrast")
            .Data<AgXLook::MediumLowContrast>("Medium Low Contrast")
            .Data<AgXLook::LowContrast>("Low Contrast")
            .Data<AgXLook::VeryLowContrast>("Very Low Contrast")
            .Data<AgXLook::Greyscale>("Greyscale");

        context.Reflect<TonemapComponent>()
            .Type("Tonemap").Traits(ComponentTraits<TonemapComponent>::flags)
            .Data<&TonemapComponent::m_look>("Look")
                .Custom<Spark::EnumElement>()
                .Traits(MetaFieldTraits::Serializable)
            ;

        Spark::ComponentOperation<TonemapComponent>(context);
        Spark::ComponentRuntime<TonemapComponent>(context);
    }
}
