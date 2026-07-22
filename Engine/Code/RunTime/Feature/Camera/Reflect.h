#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaTypeTraits.h>

#include "Components.h"

namespace Spark::Camera
{
    static void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<CameraType>()
            .Type("CameraType")
            .Data<CameraType::Prespective>("Prespective")
            .Data<CameraType::Orthographic>("Orthographic");

        context.Reflect<CameraComponent>()
            .Type("Camera").Custom<Spark::ComponentTraitsRuntime>(Spark::ComponentTraits<CameraComponent>{})
            .Data<&CameraComponent::m_type>("Type").Custom<Spark::EnumElement>()
            .Data<&CameraComponent::m_fov>("FOV").Custom<Spark::FloatElement>(1.f, 179.f, 1.f)
            .Data<&CameraComponent::m_clipStart>("Near").Custom<Spark::FloatElement>(0.0001f, 100.f, 0.01f)
            .Data<&CameraComponent::m_clipEnd>("Far").Custom<Spark::FloatElement>(1.f, 100000.f, 10.f)
            ;

        Spark::ComponentOpertion<CameraComponent>(context);
    }
}
