#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaFieldTraits.h>
#include <ECS/ComponentRuntime.h>

#include "Components.h"

namespace Spark::Bloom
{
    static void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<BloomComponent>()
            .Type("Bloom").Traits(ComponentTraits<BloomComponent>::flags)
            .Data<&BloomComponent::m_intensity>("Intensity")
                .Custom<Spark::FloatElement>(0.0f, 1.0f, 0.005f)
                .Traits(MetaFieldTraits::Serializable)
            ;

        Spark::ComponentOperation<BloomComponent>(context);
        Spark::ComponentRuntime<BloomComponent>(context);
    }
}
