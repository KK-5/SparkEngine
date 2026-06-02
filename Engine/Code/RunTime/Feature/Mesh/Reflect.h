#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaTypeTraits.h>

#include "Components.h"

namespace Spark::Mesh
{
    static void Reflect(Spark::ReflectContext& context)
    {
        context.Reflect<MeshComponent>()
            .Type("Mesh").Custom<ComponentTraitsRuntime>(ComponentTraits<MeshComponent>{})
            .Data<&MeshComponent::m_modelAssetId>("Model Asset").Custom<Spark::AssetElement>()
            .Data<&MeshComponent::m_meshIndex>("Mesh Index").Custom<Spark::IntElement>(0, 255, 1)
            .Data<&MeshComponent::m_primitiveIndex>("Primitive Index").Custom<Spark::IntElement>(0, 255, 1)
            ;

        Spark::ComponentOpertion<MeshComponent>(context);
    }
}