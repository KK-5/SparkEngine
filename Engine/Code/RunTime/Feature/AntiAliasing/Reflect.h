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
            .Data<&TemporalAAComponent::m_minVarianceGamma>("Min Variance Gamma")
                .Custom<Spark::FloatElement>(0.25f, 4.0f, 0.01f)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&TemporalAAComponent::m_maxVarianceGamma>("Max Variance Gamma")
                .Custom<Spark::FloatElement>(0.25f, 4.0f, 0.01f)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&TemporalAAComponent::m_motionRejectionPixels>("Motion Rejection Pixels (1080p)")
                .Custom<Spark::FloatElement>(1.0f, 1024.0f, 1.0f)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&TemporalAAComponent::m_depthRejectionBias>("Depth Rejection Bias (Far Fraction)")
                .Custom<Spark::FloatElement>(0.0f, 0.01f, 0.00001f)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&TemporalAAComponent::m_jitterSamples>("Jitter Samples")
                .Custom<Spark::EnumElement>()
                .Traits(MetaFieldTraits::Serializable)
            ;

        Spark::ComponentOperation<TemporalAAComponent>(context);
        Spark::ComponentRuntime<TemporalAAComponent>(context);
    }
}
