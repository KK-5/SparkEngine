#pragma once

#include <Reflection/ReflectContext.h>
#include <Reflection/TypeRegistry.h>
#include <Reflection/Utility.h>
#include <Serialization/UIElement.h>
#include <Serialization/MetaFieldTraits.h>
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
                .Traits(MetaFieldTraits::Serializable)
            .Data<&MeshComponent::m_meshIndex>("Mesh Index")
                .Custom<Spark::UIntElement>(0, 255, 1, true)
                .Traits(MetaFieldTraits::Serializable)
            .Data<&MeshComponent::m_primitiveIndex>("Primitive Index")
                .Custom<Spark::UIntElement>(0, 255, 1, true)
                .Traits(MetaFieldTraits::Serializable)
            // Vertex/Triangle Count stay unmarked: both are derived from the model asset,
            // so persisting them would put a second source of truth on disk.
            .Data<&MeshComponent::m_vertexCount>("Vertex Count")
                .Custom<Spark::UIntElement>(0, static_cast<uint32_t>(-1), 1, true)
            .Data<&MeshComponent::m_triangleCount>("Triangle Count")
                .Custom<Spark::UIntElement>(0, static_cast<uint32_t>(-1), 1, true)
            ;

        Spark::ComponentOperation<MeshComponent>(context);
    }
}