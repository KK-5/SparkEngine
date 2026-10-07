#pragma once

#include <Resource/AssetTypes.h>

#include <RHI/Context/UniqueRHIHandle.h>
#include <RHI/Resource/Buffer/IndexBufferView.h>

namespace Spark::Render
{
    //! A MeshComponent's geometry on the GPU, on the WORLD entity. MeshGeometrySystem writes
    //! it; the two buffer entities go when it does.
    struct MeshGeometry
    {
        //! The mesh this was made from. Stale once the MeshComponent names another.
        Resource::AssetId m_modelAssetId;
        uint32_t          m_meshIndex      = 0;
        uint32_t          m_primitiveIndex = 0;

        RHI::UniqueRHIHandle m_vertexBuffer;
        //! Empty for a mesh drawn without indices.
        RHI::UniqueRHIHandle m_indexBuffer;

        uint32_t m_vertexByteCount  = 0;
        uint32_t m_vertexByteStride = 0;

        uint32_t         m_indexCount     = 0;
        RHI::IndexFormat m_indexFormat    = RHI::IndexFormat::Unknown;
        uint32_t         m_indexByteCount = 0;

        //! Where the mesh starts in the native buffer its two buffers are parts of, as an
        //! indexed draw of that whole buffer counts: in indices, and in vertices.
        uint32_t m_firstIndex   = 0;
        uint32_t m_vertexOffset = 0;
    };

    //! On a world entity whose MeshGeometry can be drawn: the uploads of both buffers are
    //! submitted. Before that there is no fence to wait on, and a draw would read whatever
    //! the memory held, which in a pool is another mesh's data. MeshGeometrySystem puts it
    //! on; it goes with the MeshGeometry.
    struct MeshGeometryReadyTag {};

    //! Takes the MeshGeometry off every world entity whose MeshComponent is gone or names
    //! another mesh, and lets the entity be composed again. Compared every frame, so an edit
    //! to the component needs no notification.
    void RemoveStaleMeshGeometry();
}
