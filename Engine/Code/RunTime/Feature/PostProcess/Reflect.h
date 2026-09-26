#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaFieldTraits.h>
#include <ECS/ComponentRuntime.h>

#include "Components.h"

namespace Spark::PostProcess
{
    static void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<PostProcessVolumeComponent>()
            .Type("Post Process Volume").Traits(ComponentTraits<PostProcessVolumeComponent>::flags)
            .Data<&PostProcessVolumeComponent::m_priority>("Priority")
                .Custom<Spark::IntElement>(-100, 100, 0.1f)
                .Traits(MetaFieldTraits::Serializable)
            ;

        Spark::ComponentOperation<PostProcessVolumeComponent>(context);
        Spark::ComponentRuntime<PostProcessVolumeComponent>(context);
    }
}
