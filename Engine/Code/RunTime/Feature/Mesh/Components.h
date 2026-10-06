#pragma once

#include <ECS/ComponentTraits.h>
#include <Resource/AssetTypes.h>
#include <Resource/Model/ModelAsset.h>

namespace Spark::Mesh
{
    struct MeshComponent
    {
        Resource::AssetId m_modelAssetId;
        uint32_t m_meshIndex      = 0;
        uint32_t m_primitiveIndex = 0;

        // Runtime statistics, filled by MeshSystem after asset load
        uint32_t m_vertexCount   = 0;
        uint32_t m_triangleCount = 0;
    };
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(Mesh::MeshComponent,
        static constexpr ComponentFlags flags = ComponentFlags::Editable | ComponentFlags::Persistent;
        static constexpr ComponentEventMask componentEvents = ComponentEventMask::All;
    )
}
