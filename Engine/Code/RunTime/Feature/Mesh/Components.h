#pragma once

#include <ECS/ComponentTraits.h>
#include <Resource/AssetTypes.h>

namespace Spark::Mesh
{
    struct MeshComponent
    {
        Resource::AssetId  m_modelAssetId;
        uint32_t m_meshIndex      = 0;
        uint32_t m_primitiveIndex = 0;

        // Runtime statistics, filled by MeshSystem after asset load
        uint32_t m_vertexCount   = 0;
        uint32_t m_triangleCount = 0;
    };


    /*
    struct MeshGPUComponent {
        RHI::RHIHandle m_vertexBuffer{NullHandle};
        RHI::RHIHandle m_shaderBindings{NullHandle};
        RHI::InputStreamLayout m_inputLayout;
        uint32_t       m_indexCount = 0;
        RHI::IndexFormat m_indexFormat;
    };
    */
    
}

namespace Spark
{
    SPARK_COMPONENT_TRAITS(Mesh::MeshComponent,
        static constexpr bool editable = true;
        static constexpr ComponentEventMask componentEvents = ComponentEventMask::All;
    )
}