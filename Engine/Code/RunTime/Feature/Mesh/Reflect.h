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
            .Data<&MeshComponent::m_modelAssetId>("Model Asset")
                .Custom<Spark::AssetElement>(true)
            .Data<&MeshComponent::m_meshIndex>("Mesh Index")
                .Custom<Spark::UIntElement>(0, 255, 1, true)
            .Data<&MeshComponent::m_primitiveIndex>("Primitive Index")
                .Custom<Spark::UIntElement>(0, 255, 1, true)
            .Data<&MeshComponent::m_vertexCount>("Vertex Count")
                .Custom<Spark::UIntElement>(0, static_cast<uint32_t>(-1), 1, true)
            .Data<&MeshComponent::m_triangleCount>("Triangle Count")
                .Custom<Spark::UIntElement>(0, static_cast<uint32_t>(-1), 1, true)
            ;

        Spark::ComponentOpertion<MeshComponent>(context);
    }
}