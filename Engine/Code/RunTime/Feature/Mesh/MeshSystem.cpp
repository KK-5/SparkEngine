#include "MeshSystem.h"

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <CoreComponents/Tags.h>
#include <Log/ILogSystem.h>
#include <RHI/Context/RHIContext.h>
#include <RHI/Component/Component.h>
#include <RHI/Pipeline/InputStreamLayoutBuilder.h>
#include <RHI/ResourceBuilder.h>
#include <Resource/Model/ModelAsset.h>

#include <EASTL/string.h>

namespace Spark::Mesh
{
    void MeshSystem::InitInternal()
    {
        ComponentEventBus::Handler::BusConnect(GetTypeId<MeshComponent>());

        auto& world = *WorldExecuteContext::Current();
        world.RegisterEventOnEntityRemove<MeshComponent>();
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

        // Unique suffix so per-entity ResourceNames don't collide as AttachmentIds.
        const eastl::string idSuffix = eastl::to_string(static_cast<uint32_t>(entity));

        // Vertex buffer: static-import create + upload only. The render-graph
        // static-import attachment (which drives the upload→VB barrier + copy→graphics
        // fence) is registered by the render-side consumer (MeshDrawableComposer), not
        // here — the feature produces the resource; render decides its usage.
        RHI::BufferDescriptor vbDesc;
        vbDesc.m_bindFlags       = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;
        vbDesc.m_byteCount       = prim.vertexBuffer.size();
        vbDesc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

        const eastl::string vbName = eastl::string("MeshVB_") + idSuffix;
        RHI::RHIHandle vbEntity = RHI::CreateStaticBuffer(*rhiCtx, ObjectName(vbName), vbDesc);
        RHI::RequestBufferUpload(
            *rhiCtx, vbEntity, prim.vertexBuffer.data(), prim.vertexBuffer.size());

        // Index buffer: same static-import create + upload; attachment likewise
        // registered by the render consumer (MeshDrawableComposer).
        RHI::RHIHandle ibEntity = RHI::NullHandle;
        if (!prim.indexBuffer.empty())
        {
            RHI::BufferDescriptor ibDesc;
            ibDesc.m_bindFlags       = RHI::BufferBindFlags::InputAssembly | RHI::BufferBindFlags::CopyWrite;
            ibDesc.m_byteCount       = prim.indexBuffer.size();
            ibDesc.m_sharedQueueMask = RHI::HardwareQueueClassMask::Graphics;

            const eastl::string ibName = eastl::string("MeshIB_") + idSuffix;
            ibEntity = RHI::CreateStaticBuffer(*rhiCtx, ObjectName(ibName), ibDesc);
            RHI::RequestBufferUpload(
                *rhiCtx, ibEntity, prim.indexBuffer.data(), prim.indexBuffer.size());
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
        gpuComp.m_vertexBuffer     = vbEntity;
        gpuComp.m_indexBindings    = ibEntity;
        gpuComp.m_inputLayout      = eastl::move(inputLayout);
        gpuComp.m_indexCount       = prim.indexCount;
        gpuComp.m_indexFormat      = prim.indexFormat;
        gpuComp.m_vertexByteCount  = static_cast<uint32_t>(prim.vertexBuffer.size());
        gpuComp.m_vertexByteStride = prim.layout.stride;
        gpuComp.m_indexByteCount   = static_cast<uint32_t>(prim.indexBuffer.size());

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
            if (rhiCtx->Valid(gpuComp->m_vertexBuffer))
            {
                rhiCtx->Add<DeadTag>(gpuComp->m_vertexBuffer);
            }
            if (rhiCtx->Valid(gpuComp->m_indexBindings))
            {
                rhiCtx->Add<DeadTag>(gpuComp->m_indexBindings);
            }
        }

        ctx.Remove<MeshGPUComponent>(entity);
    }
}
