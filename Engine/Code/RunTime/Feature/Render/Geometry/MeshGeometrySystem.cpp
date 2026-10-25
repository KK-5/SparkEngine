#include "MeshGeometrySystem.h"

#include <EASTL/vector.h>

#include <ECS/Common.h>
#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <Service/Service.h>

#include <RHI/RHIInterface.h>
#include <RHI/Component/Component.h>
#include <RHI/Device/Device.h>
#include <RHI/HardwareQueue.h>
#include <RHI/Resource/Buffer/Buffer.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Model/ModelAsset.h>

#include <Mesh/Components.h>

#include <Pass/CopyPass.h>

namespace Spark::Render
{
    namespace
    {
        const RHI::AttachmentId StagingName("MeshGeometryStaging");
        const RHI::AttachmentId PoolName("MeshGeometry");

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
            world->Remove<MeshGeometry, MeshGeometryUpload, MeshGeometryReadyTag>(entity);
        }
    }

    void MeshGeometrySystem::Init(RHI::RHIContext& rhiCtx)
    {
        RHI::Device& device = *Service<RHI::RHIInterface>::Get()->GetDevice();

        // Read as vertices and indices on the Graphics queue, where the copy pass writes it.
        ReservedBuffer::Descriptor poolDesc;
        poolDesc.m_capacity  = PoolCapacityInBytes;
        poolDesc.m_pageSize  = PageSizeInBytes;
        poolDesc.m_bindFlags = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;
        poolDesc.m_name      = ObjectName("MeshGeometryPool");
        if (!m_pool.Init(device, poolDesc))
        {
            LOG_ERROR("[MeshGeometrySystem] The geometry pool failed to init; no mesh will have geometry.");
            return;
        }

        StagingBuffer::Descriptor stagingDesc;
        stagingDesc.m_name            = ObjectName("MeshGeometryStaging");
        stagingDesc.m_bindFlags       = RHI::BufferBindFlags::CopyRead;
        stagingDesc.m_initialCapacity = UploadBytesPerFrame;
        if (!m_staging.Init(device, stagingDesc))
        {
            LOG_ERROR("[MeshGeometrySystem] The staging buffer failed to init; no mesh will have geometry.");
            m_pool.Shutdown();
            return;
        }

        m_allocator = new Suballocator(device, PoolCapacityInBytes, PageSizeInBytes);

        m_poolResource = rhiCtx.CreateEntity();
        rhiCtx.Add<RHI::Components::Buffer>(m_poolResource, RHI::Components::Buffer{ Ptr<RHI::Buffer>(m_pool.GetBuffer()) });
        rhiCtx.Add<RHI::ImportedTag>(m_poolResource);
        rhiCtx.Add<RHI::ResourceName>(m_poolResource, RHI::ResourceName{ poolDesc.m_name });
        rhiCtx.Add<MeshGeometryPoolTag>(m_poolResource);
    }

    void MeshGeometrySystem::SetUp(PassContext& passContext)
    {
        SPARK_COPY_PASS(passContext, "MeshGeometryUpload")
            .Queue(RHI::HardwareQueueClass::Graphics)
            .Build([this](CopyPassScopes& p)
            {
                if (m_copies.empty())
                {
                    return;
                }

                p.Import(StagingName, m_staging.GetResource());
                p.Import(PoolName, m_poolResource);

                auto s = p.Scope();
                const Attachment source      = s.CopySource(StagingName);
                const Attachment destination = s.CopyDestination(PoolName);
                for (const Copy& copy : m_copies)
                {
                    s.CopyBuffer(source, copy.m_stagingOffset, destination, copy.m_poolOffset, copy.m_byteCount);
                }
                s.Close();
            })
            .Finalize();
    }

    void MeshGeometrySystem::Update()
    {
        auto* world        = WorldExecuteContext::Current();
        auto* assetManager = Service<Resource::AssetManager>::Get();
        if (!world || !assetManager || !m_allocator)
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

            // What lets every mesh be a part of one vertex binding: the compiler lets no
            // other format through.
            ASSERT(Resource::IsStandardVertexLayout(primitive->layout),
                "[MeshGeometrySystem] A primitive of {} is not in the standard vertex format.",
                meshComp.m_modelAssetId.GetPath().c_str());

            MeshGeometry geometry;
            geometry.m_modelAssetId   = meshComp.m_modelAssetId;
            geometry.m_meshIndex      = meshComp.m_meshIndex;
            geometry.m_primitiveIndex = meshComp.m_primitiveIndex;
            geometry.m_localBounds    = primitive->bounds;

            MeshGeometryUpload upload;
            if (!CreateGeometry(*primitive, geometry, upload))
            {
                return;
            }
            world->Add<MeshGeometry>(entity, eastl::move(geometry));
            world->Add<MeshGeometryUpload>(entity, upload);
        });

        // After the ranges taken above; the ones handed back went at the last frame end.
        m_allocator->TakePageChanges(m_pageChanges);
        m_pool.Update(m_pageChanges);

        StageUploads(*world);
    }

    void MeshGeometrySystem::Shutdown(RHI::RHIContext& rhiCtx)
    {
        if (auto* world = WorldExecuteContext::Current())
        {
            world->Clear<MeshGeometry, MeshGeometryUpload, MeshGeometryReadyTag>();
        }
        m_copies.clear();

        if (m_poolResource != RHI::NullHandle)
        {
            rhiCtx.DestoryEntity(m_poolResource);
            m_poolResource = RHI::NullHandle;
        }
        m_staging.Shutdown();
        m_pool.Shutdown();
        // The ranges just cleared hold it until the device hands them back.
        m_allocator.reset();
    }

    bool MeshGeometrySystem::CreateGeometry(
        const Resource::Primitive& primitive, MeshGeometry& geometry, MeshGeometryUpload& upload)
    {
        // Each range starts at a whole element, so its offset converts to the vertex or index a
        // draw of the whole pool starts from.
        const uint32_t stride = primitive.layout.stride;
        geometry.m_vertices = m_allocator->Allocate(primitive.vertexBuffer.size(), stride);
        if (!geometry.m_vertices.IsValid())
        {
            LOG_ERROR("[MeshGeometrySystem] The geometry pool has no room for {} bytes of vertices.",
                primitive.vertexBuffer.size());
            return false;
        }
        geometry.m_vertexByteStride = stride;
        geometry.m_vertexOffset     = static_cast<uint32_t>(geometry.m_vertices.GetOffset() / stride);
        upload.m_vertexData         = primitive.vertexBuffer.data();
        upload.m_vertexByteCount    = primitive.vertexBuffer.size();

        if (!primitive.indexBuffer.empty())
        {
            const uint32_t indexByteSize = RHI::GetIndexFormatSize(primitive.indexFormat);
            geometry.m_indices = m_allocator->Allocate(primitive.indexBuffer.size(), indexByteSize);
            if (!geometry.m_indices.IsValid())
            {
                LOG_ERROR("[MeshGeometrySystem] The geometry pool has no room for {} bytes of indices.",
                    primitive.indexBuffer.size());
                return false;
            }
            geometry.m_indexCount   = primitive.indexCount;
            geometry.m_indexFormat  = primitive.indexFormat;
            geometry.m_firstIndex   = static_cast<uint32_t>(geometry.m_indices.GetOffset() / indexByteSize);
            upload.m_indexData      = primitive.indexBuffer.data();
            upload.m_indexByteCount = primitive.indexBuffer.size();
        }
        return true;
    }

    void MeshGeometrySystem::StageUploads(WorldContext& world)
    {
        m_copies.clear();

        // Begin wants the frame's total before anything is written. The budget is spent whole,
        // so that is what is left to go, up to the budget.
        uint64_t remainingBytes = 0;
        world.GetView<MeshGeometryUpload>().each([&](Entity, const MeshGeometryUpload& upload)
        {
            remainingBytes += upload.GetRemainingBytes();
        });
        m_staging.Begin(eastl::min(remainingBytes, UploadBytesPerFrame));

        auto stage = [&](const uint8_t* data, uint64_t poolOffset, uint64_t size)
        {
            const StagingBuffer::Allocation staged = m_staging.Allocate(size);
            if (!staged.m_data)
            {
                return;
            }
            memcpy(staged.m_data, data, size);
            m_copies.push_back(Copy{ staged.m_offset, poolOffset, size });
        };

        ForEachUploadChunk(world, UploadBytesPerFrame,
            [&](Entity entity, MeshGeometryUpload& upload, uint64_t chunk)
        {
            const MeshGeometry& geometry = world.Get<MeshGeometry>(entity);
            const uint64_t begin = upload.m_uploadedBytes;
            const uint64_t end   = begin + chunk;

            // The part of [begin, end) that falls in this segment of the run.
            auto stageSegment = [&](const uint8_t* data, uint64_t poolOffset, uint64_t segmentBegin, uint64_t segmentSize)
            {
                const uint64_t first = eastl::max(begin, segmentBegin);
                const uint64_t last  = eastl::min(end, segmentBegin + segmentSize);
                if (first < last)
                {
                    stage(data + (first - segmentBegin), poolOffset + (first - segmentBegin), last - first);
                }
            };
            // The run is the vertices, then the indices: the chunk may hold some of each.
            stageSegment(upload.m_vertexData, geometry.m_vertices.GetOffset(), 0, upload.m_vertexByteCount);
            stageSegment(upload.m_indexData, geometry.m_indices.GetOffset(), upload.m_vertexByteCount, upload.m_indexByteCount);

            upload.m_uploadedBytes = end;
            if (upload.GetRemainingBytes() == 0)
            {
                // Copied this frame ahead of every pass that reads the pool.
                world.Remove<MeshGeometryUpload>(entity);
                world.Add<MeshGeometryReadyTag>(entity);
            }
        });
    }
}
