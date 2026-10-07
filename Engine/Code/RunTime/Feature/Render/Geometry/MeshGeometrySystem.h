#pragma once

#include <RHI/Context/RHIContext.h>
#include <RHI/Resource/Buffer/BufferPool.h>

#include "MeshGeometry.h"

namespace Spark::Render
{
    //! Puts the geometry of every MeshComponent on the GPU and hands the world entity a
    //! MeshGeometry naming it. The Mesh feature only says which mesh it means.
    //!
    //! Owns the pool the vertex and index buffers come from: one native buffer they are all
    //! parts of, so draws of different meshes can share a vertex and an index binding. How
    //! a mesh is placed in it is known here only; MeshGeometry says it to everything else.
    //!
    //! Plain helper, not ISystem — owned by RenderSystem, ticked before GeometryBindingSystem
    //! and InstanceBindingSystem, which give a slot to what has a MeshGeometry.
    class MeshGeometrySystem
    {
    public:
        void Init(RHI::RHIContext& rhiCtx);
        void Update();
        //! After the GPU is idle: the native buffer goes here.
        void Shutdown(RHI::RHIContext& rhiCtx);

    private:
        //! Size of the native buffer, taken whole at Init. A request that finds no room fails;
        //! the pool does not grow.
        static constexpr uint64_t PoolBudgetInBytes = 256ull * 1024 * 1024;

        //! A buffer entity of the pool holding a copy of data, or NullHandle if the pool has
        //! no room. data must outlive the upload.
        RHI::RHIHandle CreateBuffer(
            RHI::RHIContext& rhiCtx,
            const ObjectName& name,
            const void* data,
            size_t byteCount,
            uint32_t alignment);

        Ptr<RHI::BufferPool> m_pool;

        //! Components::Buffer — the pool's whole native buffer, for a draw that binds it.
        RHI::RHIHandle m_baseBuffer = RHI::NullHandle;
    };
}
