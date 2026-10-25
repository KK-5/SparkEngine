#pragma once

#include <ECS/WorldContext.h>
#include <Math/AABB.h>

#include <Resource/AssetTypes.h>

#include <RHI/Resource/Buffer/IndexBufferView.h>

#include <Suballocation/Suballocator.h>

namespace Spark::Render
{
    //! A MeshComponent's geometry on the GPU, on the WORLD entity: its vertices and indices,
    //! each a range of the geometry pool. MeshGeometrySystem writes it; the ranges go back to
    //! the pool when it goes.
    struct MeshGeometry
    {
        //! The mesh this was made from. Stale once the MeshComponent names another.
        Resource::AssetId m_modelAssetId;
        uint32_t          m_meshIndex      = 0;
        uint32_t          m_primitiveIndex = 0;

        Suballocation m_vertices;
        //! Empty for a mesh drawn without indices.
        Suballocation m_indices;

        uint32_t m_vertexByteStride = 0;

        uint32_t         m_indexCount  = 0;
        RHI::IndexFormat m_indexFormat = RHI::IndexFormat::Unknown;

        //! Where the mesh starts in the pool, as an indexed draw of the whole pool counts: in
        //! indices, and in vertices.
        uint32_t m_firstIndex   = 0;
        uint32_t m_vertexOffset = 0;

        //! Around the mesh's vertices, in the space they are stored in.
        Math::AABB m_localBounds;
    };

    //! On a world entity whose MeshGeometry still has bytes to go into the pool: its vertices,
    //! then its indices, as one run, of which m_uploadedBytes are in. The data is the model
    //! asset's, which the asset database never evicts. It goes with the MeshGeometry.
    struct MeshGeometryUpload
    {
        const uint8_t* m_vertexData      = nullptr;
        uint64_t       m_vertexByteCount = 0;
        const uint8_t* m_indexData       = nullptr;
        uint64_t       m_indexByteCount  = 0;
        uint64_t       m_uploadedBytes   = 0;

        uint64_t GetRemainingBytes() const { return m_vertexByteCount + m_indexByteCount - m_uploadedBytes; }
    };

    //! On a world entity whose MeshGeometry can be drawn: all of it is in the pool, or is
    //! copied there this frame ahead of every draw. MeshGeometrySystem puts it on; it goes with
    //! the MeshGeometry.
    struct MeshGeometryReadyTag {};

    //! On the RHIContext entity of the geometry pool's buffer, which a pass drawing meshes
    //! imports as its vertex and index stream.
    struct MeshGeometryPoolTag {};

    //! Takes the MeshGeometry off every world entity whose MeshComponent is gone or names
    //! another mesh, so the next one is made. Compared every frame, so an edit to the
    //! component needs no notification.
    void RemoveStaleMeshGeometry();

    //! Visits each upload this frame's `budget` reaches, with how many of its bytes, in the
    //! order they go: the ones already begun, then new ones, until the budget is spent. A new
    //! one begins only once every begun one is through, so at most one is ever left halfway.
    //! visit: void(Entity, MeshGeometryUpload&, uint64_t byteCount). It may take the upload
    //! off the entity: a view stays valid when the entity it is on is modified.
    template<typename Visit>
    void ForEachUploadChunk(WorldContext& world, uint64_t budget, Visit&& visit)
    {
        auto take = [&](Entity entity, MeshGeometryUpload& upload)
        {
            if (budget == 0)
            {
                return;
            }
            const uint64_t byteCount = eastl::min(upload.GetRemainingBytes(), budget);
            budget -= byteCount;
            visit(entity, upload, byteCount);
        };

        auto view = world.GetView<MeshGeometryUpload>();
        // The one left halfway last frame goes on first.
        view.each([&](Entity entity, MeshGeometryUpload& upload)
        {
            if (upload.m_uploadedBytes > 0)
            {
                take(entity, upload);
            }
        });
        // Then new ones, while the budget lasts.
        view.each([&](Entity entity, MeshGeometryUpload& upload)
        {
            if (upload.m_uploadedBytes == 0)
            {
                take(entity, upload);
            }
        });
    }
}
