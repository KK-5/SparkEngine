#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaFieldTraits.h>
#include <ECS/ComponentRuntime.h>

#include "Components.h"

namespace Spark::AntiAliasing
{
    static void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<TemporalAAJitterSamples>()
            .Type("TemporalAAJitterSamples")
            .Data<TemporalAAJitterSamples::Four>("4")
            .Data<TemporalAAJitterSamples::Eight>("8")
            .Data<TemporalAAJitterSamples::Sixteen>("16");

        context.Reflect<TemporalAAComponent>()
            .Type("Temporal AA").Traits(ComponentTraits<TemporalAAComponent>::flags)
            .Data<&TemporalAAComponent::m_currentFrameWeight>("Current Frame Weight")
                .Custom<Spark::FloatElement>(0.02f, 0.5f, 0.005f)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&TemporalAAComponent::m_motionFrameWeight>("Motion Frame Weight")
                .Custom<Spark::FloatElement>(0.02f, 1.0f, 0.005f)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&TemporalAAComponent::m_varianceClipGamma>("Variance Clip Gamma")
                .Custom<Spark::FloatElement>(0.75f, 2.0f, 0.01f)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&TemporalAAComponent::m_filterSize>("Filter Size")
                .Custom<Spark::FloatElement>(0.5f, 2.0f, 0.01f)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&TemporalAAComponent::m_jitterSamples>("Jitter Samples")
                .Custom<Spark::EnumElement>()
                .Traits(MetaFieldTraits::Serializable)
            ;

        Spark::ComponentOperation<TemporalAAComponent>(context);
        Spark::ComponentRuntime<TemporalAAComponent>(context);
    }
}
