#include "MeshGeometrySystem.h"

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Factory.h>
#include <RHI/Component/Component.h>
#include <RHI/Device/Device.h>
#include <RHI/HardwareQueue.h>
#include <RHI/ResourceBuilder.h>
#include <RHI/Resource/Buffer/Buffer.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Model/ModelAsset.h>

#include <Mesh/Components.h>

#include <Drawable/GeometrySpec.h>

namespace Spark::Render
{
    namespace
    {
        constexpr RHI::BufferBindFlags GeometryBindFlags =
            RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;

        //! The primitive a MeshComponent names, or null while there is none to draw. Silent:
        //! MeshSystem reports a component that names nothing, once, where this runs every frame.
        const Resource::Primitive* FindPrimitive(
            Resource::AssetManager& assetManager, const Mesh::MeshComponent& meshComp)
        {
            Ptr<Resource::ModelAsset> model =
                assetManager.FindAsset<Resource::ModelAsset>(meshComp.m_modelAssetId);
            if (!model || !model->IsReady())
            {
                return nullptr;
            }

            const Resource::ModelAssetData* data = model->GetModelData();
            if (!data)
            {
                return nullptr;
            }

            const Resource::Mesh* mesh = data->GetMesh(meshComp.m_meshIndex);
            if (!mesh || meshComp.m_primitiveIndex >= mesh->primitives.size())
            {
                return nullptr;
            }

            const Resource::Primitive& primitive = mesh->primitives[meshComp.m_primitiveIndex];
            return primitive.vertexBuffer.empty() ? nullptr : &primitive;
        }
    }

    void RemoveStaleMeshGeometry()
    {
        auto* world = WorldExecuteContext::Current();
        if (!world)
        {
            return;
        }

        // Collected first: MeshGeometry is the pool being iterated.
        eastl::vector<Entity> stale;
        world->GetView<MeshGeometry>().each([&](Entity entity, const MeshGeometry& geometry)
        {
            const auto* meshComp = world->TryGet<Mesh::MeshComponent>(entity);
            if (!meshComp
                || meshComp->m_meshIndex != geometry.m_meshIndex
                || meshComp->m_primitiveIndex != geometry.m_primitiveIndex
                || meshComp->m_modelAssetId != geometry.m_modelAssetId)
            {
                stale.push_back(entity);
            }
        });

        for (Entity entity : stale)
        {
            world->Remove<MeshGeometry>(entity);
            // The spec composed from the old buffers is reaped with them, and nothing
            // composes the next one while the entity still says it is composed.
            if (world->Has<WorldComposedTag>(entity))
            {
                world->Remove<WorldComposedTag>(entity);
            }
        }
    }

    void MeshGeometrySystem::Init(RHI::RHIContext& /*rhiCtx*/)
    {
        auto* rhi = Service<RHI::RHIInterface>::Get();

        // Owned by the Graphics queue: the Copy queue takes each buffer for its one upload
        // and hands it back.
        RHI::BufferPoolDescriptor desc;
        desc.m_heapMemoryLevel = RHI::HeapMemoryLevel::Device;
        desc.m_bindFlags       = GeometryBindFlags;
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;
        desc.m_budgetInBytes   = PoolBudgetInBytes;

        m_pool = rhi->GetRHIFactory()->CreateBufferPool();
        m_pool->SetName(ObjectName("MeshGeometryPool"));
        if (m_pool->Init(*rhi->GetDevice(), desc) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[MeshGeometrySystem] BufferPool::Init failed; no mesh will have geometry.");
            m_pool.reset();
        }
    }

    void MeshGeometrySystem::Update()
    {
        auto* world        = WorldExecuteContext::Current();
        auto* rhiCtx       = RHI::RHIExecuteContext::Current();
        auto* assetManager = Service<Resource::AssetManager>::Get();
        if (!world || !rhiCtx || !assetManager || !m_pool)
        {
            return;
        }

        // First, so a component that names another mesh now gets its geometry below.
        RemoveStaleMeshGeometry();

        // Structural write inside iteration: MeshGeometry is only in the exclude set.
        world->GetView<Mesh::MeshComponent>(Exclude<MeshGeometry, DeadTag>).each(
            [&](Entity entity, const Mesh::MeshComponent& meshComp)
        {
            const Resource::Primitive* primitive = FindPrimitive(*assetManager, meshComp);
            if (!primitive)
            {
                return;
            }

            // Unique suffix so per-entity ResourceNames don't collide as AttachmentIds.
            const eastl::string idSuffix = eastl::to_string(static_cast<uint32_t>(entity));

            // The uploads borrow the asset's arrays and the copies can be several frames out.
            // What keeps them alive is the asset database, which never evicts.
            MeshGeometry geometry;
            geometry.m_modelAssetId  = meshComp.m_modelAssetId;
            geometry.m_meshIndex      = meshComp.m_meshIndex;
            geometry.m_primitiveIndex = meshComp.m_primitiveIndex;
            geometry.m_vertexBuffer = RHI::UniqueRHIHandle(CreateBuffer(
                *rhiCtx, ObjectName(eastl::string("MeshVB_") + idSuffix),
                primitive->vertexBuffer.data(), primitive->vertexBuffer.size(),
                primitive->layout.stride));
            if (!geometry.m_vertexBuffer.IsValid())
            {
                return;
            }
            geometry.m_vertexByteCount  = static_cast<uint32_t>(primitive->vertexBuffer.size());
            geometry.m_vertexByteStride = primitive->layout.stride;

            if (!primitive->indexBuffer.empty())
            {
                geometry.m_indexBuffer = RHI::UniqueRHIHandle(CreateBuffer(
                    *rhiCtx, ObjectName(eastl::string("MeshIB_") + idSuffix),
                    primitive->indexBuffer.data(), primitive->indexBuffer.size(),
                    RHI::GetIndexFormatSize(primitive->indexFormat)));
                if (!geometry.m_indexBuffer.IsValid())
                {
                    // geometry goes here and takes the vertex buffer with it.
                    return;
                }
                geometry.m_indexCount     = primitive->indexCount;
                geometry.m_indexFormat    = primitive->indexFormat;
                geometry.m_indexByteCount = static_cast<uint32_t>(primitive->indexBuffer.size());
            }

            world->Add<MeshGeometry>(entity, eastl::move(geometry));
        });
    }

    void MeshGeometrySystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<MeshGeometry>();
        }

        if (!m_pool)
        {
            return;
        }

        // The pool's buffers have to be gone before it is. Destroyed outright: no tick
        // follows to reap the DeadTag the clear above left on them.
        eastl::vector<RHI::RHIHandle> buffers;
        rhiCtx.GetView<RHI::Components::Buffer>().each(
            [&](RHI::RHIHandle entity, const RHI::Components::Buffer& buffer)
        {
            if (buffer.m_buffer && buffer.m_buffer->GetPool() == m_pool.get())
            {
                buffers.push_back(entity);
            }
        });
        rhiCtx.DestoryEntity(buffers.begin(), buffers.end());

        m_pool->Shutdown();
        m_pool.reset();
    }

    RHI::RHIHandle MeshGeometrySystem::CreateBuffer(
        RHI::RHIContext& rhiCtx,
        const ObjectName& name,
        const void* data,
        size_t byteCount,
        uint32_t alignment)
    {
        // The offset of a part is a multiple of its element size, so it converts to the
        // vertex or index a draw of the whole native buffer starts from.
        RHI::BufferDescriptor desc;
        desc.m_bindFlags       = GeometryBindFlags;
        desc.m_byteCount       = byteCount;
        desc.m_alignment       = alignment;
        desc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        Ptr<RHI::Buffer> buffer = Service<RHI::Factory>::Get()->CreateBuffer();
        buffer->SetName(name);
        if (m_pool->InitBuffer(RHI::BufferInitRequest{ *buffer, desc }) != RHI::ResultCode::Success)
        {
            LOG_ERROR("[MeshGeometrySystem] InitBuffer failed for {}; its mesh stays without geometry.",
                name.GetCStr());
            return RHI::NullHandle;
        }

        // What RHIResourceSystem would put on a static buffer it made from a PendingBufferInit.
        RHI::RHIHandle entity = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::StaticImportTag>(entity);
        rhiCtx.Add<RHI::ResourceName>(entity, RHI::ResourceName{ name });
        rhiCtx.Add<RHI::Components::Buffer>(entity, RHI::Components::Buffer{ buffer });
        RHI::RequestBufferUpload(rhiCtx, entity, data, byteCount);
        return entity;
    }
}
