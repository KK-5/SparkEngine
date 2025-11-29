#pragma once

#include "Reflection/ReflectContext.h"
#include "Reflection/Utility.h"
#include "HashString/HashString.h"
#include "CoreComponents/Name.h"
#include "ECS/WorldContext.h"
#include "Math/Vector2.h"
#include "Math/Vector3.h"
#include "Math/Vector4.h"
#include "Math/Quaternion.h"
#include "Serialization/UIElement.h"
#include "Serialization/MetaTypeTraits.h"

namespace Spark
{
    static void Reflect(ReflectContext& context)
    {

        context.Reflect<Math::Vector2>().Type("Vector2")
            .Data<&Math::Vector2::x>("x")
            .Data<&Math::Vector2::y>("y");

        context.Reflect<Math::Vector3>().Type("Vector3")
            .Data<&Math::Vector3::x>("x")
            .Data<&Math::Vector3::y>("y")
            .Data<&Math::Vector3::z>("z");

        context.Reflect<Math::Vector4>().Type("Vector4")
            .Data<&Math::Vector4::x>("x")
            .Data<&Math::Vector4::y>("y")
            .Data<&Math::Vector4::z>("z")
            .Data<&Math::Vector4::w>("w");

        context.Reflect<Math::Quaternion>().Type("Quaternion")
            .Data<&Math::Quaternion::x>("x")
            .Data<&Math::Quaternion::y>("y")
            .Data<&Math::Quaternion::z>("z")
            .Data<&Math::Quaternion::w>("w");


        context.Reflect<Name>().Type("Name").Traits(MetaTypeTraits::Editable)
            .Data<&Name::name>("name").Custom<EditTextElement>();
            
        ComponentOpertion<Name>(context);
    }
}