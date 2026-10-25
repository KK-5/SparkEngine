#pragma once

#include <EASTL/vector.h>

#include <RHI/Context/RHIContext.h>

#include <Pass/PassContext.h>
#include <Staging/StagingBuffer.h>
#include <Suballocation/ReservedBuffer.h>
#include <Suballocation/Suballocator.h>

#include "MeshGeometry.h"

namespace Spark::Resource
{
    struct Primitive;
}

namespace Spark::Render
{
    //! Puts the geometry of every MeshComponent on the GPU and hands the world entity a
    //! MeshGeometry naming it. The Mesh feature only says which mesh it means.
    //!
    //! Owns the pool the vertices and indices go in: ranges of one reserved buffer, backed
    //! page by page as they are taken, so draws of different meshes share a vertex and an
    //! index binding. Writes them through a staging buffer of its own and a copy pass, a
    //! fixed amount a frame; a mesh can be drawn from the frame its last bytes are copied.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked before GeometryBindingSystem
    //! and InstanceBindingSystem, which give a slot to what has a MeshGeometry.
    class MeshGeometrySystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        //! Registers the copy pass, ahead of every pass that draws meshes.
        void SetUp(PassContext& passContext);
        void Update();
        //! After the GPU is idle.
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Address space of the pool: memory is taken only for the pages meshes are on.
        static constexpr uint64_t PoolCapacityInBytes = 1024ull * 1024 * 1024;
        static constexpr uint64_t PageSizeInBytes     = 2ull * 1024 * 1024;
        //! What goes into the pool a frame. Meshes can wait, so the rest goes next frame.
        static constexpr uint64_t UploadBytesPerFrame = 8ull * 1024 * 1024;

        //! Ranges for a new mesh, and its upload; false, leaving the entity as it is, when the
        //! pool is full.
        bool CreateGeometry(const Resource::Primitive& primitive, MeshGeometry& geometry,
                            MeshGeometryUpload& upload);

        //! This frame's bytes of every upload the budget reaches, into the staging buffer, a
        //! copy each run they make in the pool.
        void StageUploads(WorldContext& world);

        struct Copy
        {
            uint64_t m_stagingOffset = 0;
            uint64_t m_poolOffset    = 0;
            uint64_t m_byteCount     = 0;
        };

        Ptr<Suballocator> m_allocator;
        ReservedBuffer    m_pool;
        //! The pool's buffer as the graph imports it.
        RHI::RHIHandle    m_poolResource = RHI::NullHandle;
        StagingBuffer     m_staging;

        eastl::vector<Suballocator::PageChange> m_pageChanges;
        //! This frame's, for the copy pass.
        eastl::vector<Copy> m_copies;
    };
}
