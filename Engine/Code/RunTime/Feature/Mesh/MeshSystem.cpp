#include "MeshSystem.h"

#include <ECS/ExecuteContext.h>
#include <Log/ILogSystem.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <Resource/Model/ModelAsset.h>

namespace Spark::Mesh
{
    void MeshSystem::InitInternal()
    {
        ComponentEventBus::Handler::BusConnect(GetTypeId<MeshComponent>());

        if (auto* world = WorldExecuteContext::Current())
        {
            world->RegisterEventOnEntityRemove<MeshComponent>();
        }
    }

    void MeshSystem::ShutdownInternal()
    {
        auto ctx = WorldExecuteContext::CurrentReference<SystemTraits>();
        ctx.GetView<MeshGPUComponent>().each([&](Entity entity, MeshGPUComponent&)
        {
            CleanupGPUResources(entity);
        });

        ComponentEventBus::Handler::BusDisconnect();
    }

    void MeshSystem::OnComponentConstruct(Entity entity)
    {
        auto ctx = WorldExecuteContext::CurrentReference<SystemTraits>();

        auto* meshComp = ctx.TryGet<MeshComponent>(entity);
        if (!meshComp || !meshComp->m_modelAsset)
        {
            return;
        }

        if (!meshComp->m_modelAsset->IsReady())
        {
            LOG_WARN("[MeshSystem] Model asset not ready, skipping GPU build: {}",
                      meshComp->m_modelAssetId.GetPath().c_str());
            return;
        }

        BuildGPUResources(entity, *meshComp->m_modelAsset);
    }

    void MeshSystem::OnComponentUpdated(Entity entity)
    {
        CleanupGPUResources(entity);
        OnComponentConstruct(entity);
    }

    void MeshSystem::OnComponentDestory(Entity entity)
    {
        CleanupGPUResources(entity);
    }

    void MeshSystem::BuildGPUResources(Entity entity, Resource::ModelAsset& modelAsset)
    {
        auto ctx = WorldExecuteContext::CurrentReference<SystemTraits>();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();
        if (!rhiCtx)
        {
            LOG_ERROR("[MeshSystem] RHI context not available, cannot create GPU resources.");
            return;
        }

        auto* meshComp = ctx.TryGet<MeshComponent>(entity);
        if (!meshComp)
        {
            return;
        }

        const Resource::ModelAssetData* data = modelAsset.GetModelData();
        if (!data)
        {
            LOG_ERROR("[MeshSystem] Model asset data is null.");
            return;
        }

        const Resource::Mesh* mesh = data->GetMesh(meshComp->m_meshIndex);
        if (!mesh || meshComp->m_primitiveIndex >= mesh->primitives.size())
        {
            LOG_ERROR("[MeshSystem] Mesh/Primitive index out of bounds: mesh={}, primitive={}",
                      meshComp->m_meshIndex, meshComp->m_primitiveIndex);
            return;
        }

        const Resource::Primitive& prim = mesh->primitives[meshComp->m_primitiveIndex];
        if (prim.vertexBuffer.empty())
        {
            LOG_ERROR("[MeshSystem] Primitive has no vertex data.");
            return;
        }

        // Create vertex buffer RHI entity
        RHI::RHIHandle vbEntity = rhiCtx->CreateEntity();
        {
            RHI::BufferDescriptor desc;
            desc.m_bindFlags = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;
            desc.m_byteCount = prim.vertexBuffer.size();

            RHI::PendingBufferInit init;
            init.m_descriptor = desc;
            rhiCtx->Add<RHI::PendingBufferInit>(vbEntity, init);

            RHI::PendingBufferUpload upload;
            upload.m_data = prim.vertexBuffer.data();
            upload.m_dataSize = prim.vertexBuffer.size();
            rhiCtx->Add<RHI::PendingBufferUpload>(vbEntity, upload);
            rhiCtx->Add<RHI::UploadPendingTag>(vbEntity);
        }

        // Create index buffer RHI entity
        RHI::RHIHandle ibEntity = RHI::NullHandle;
        if (!prim.indexBuffer.empty())
        {
            ibEntity = rhiCtx->CreateEntity();

            RHI::BufferDescriptor desc;
            desc.m_bindFlags = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;
            desc.m_byteCount = prim.indexBuffer.size();

            RHI::PendingBufferInit init;
            init.m_descriptor = desc;
            rhiCtx->Add<RHI::PendingBufferInit>(ibEntity, init);

            RHI::PendingBufferUpload upload;
            upload.m_data = prim.indexBuffer.data();
            upload.m_dataSize = prim.indexBuffer.size();
            rhiCtx->Add<RHI::PendingBufferUpload>(ibEntity, upload);
            rhiCtx->Add<RHI::UploadPendingTag>(ibEntity);
        }

        // Build InputStreamLayout from VertexLayout
        RHI::InputStreamLayoutBuilder layoutBuilder;
        auto* bufferBuilder = layoutBuilder.AddBuffer();
        for (const auto& attr : prim.layout.attributes)
        {
            bufferBuilder->Channel(attr.semantic.c_str(), attr.semanticIndex, attr.format);
        }
        RHI::InputStreamLayout inputLayout = layoutBuilder.End();

        // Write MeshGPUComponent
        MeshGPUComponent gpuComp;
        gpuComp.m_vertexBuffer = vbEntity;
        gpuComp.m_indexBindings = ibEntity;
        gpuComp.m_inputLayout = eastl::move(inputLayout);
        gpuComp.m_indexCount = prim.indexCount;
        gpuComp.m_indexFormat = prim.indexFormat;

        ctx.AddOrReplace<MeshGPUComponent>(entity, eastl::move(gpuComp));

        // Update statistics
        const uint32_t stride = prim.layout.stride > 0 ? prim.layout.stride : 1;
        meshComp->m_vertexCount = static_cast<uint32_t>(prim.vertexBuffer.size() / stride);
        meshComp->m_triangleCount = prim.indexCount / 3;
    }

    void MeshSystem::CleanupGPUResources(Entity entity)
    {
        auto ctx = WorldExecuteContext::CurrentReference<SystemTraits>();
        auto* rhiCtx = RHI::RHIExecuteContext::Current();

        auto* gpuComp = ctx.TryGet<MeshGPUComponent>(entity);
        if (!gpuComp)
        {
            return;
        }

        if (rhiCtx)
        {
            auto destroyBuffer = [&](RHI::RHIHandle handle)
            {
                if (handle != RHI::NullHandle && rhiCtx->Valid(handle))
                {
                    rhiCtx->DestoryEntity(handle);
                }
            };
            destroyBuffer(gpuComp->m_vertexBuffer);
            destroyBuffer(gpuComp->m_indexBindings);
        }

        ctx.Remove<MeshGPUComponent>(entity);
    }
}
