#include "MeshSystem.h"

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <Log/ILogSystem.h>
#include <Resource/AssetManagerInterface.h>
#include <Resource/Model/ModelAsset.h>
#include <Service/Service.h>

namespace Spark::Mesh
{
    void MeshSystem::InitInternal()
    {
        ComponentEventBus::Handler::BusConnect(GetTypeId<MeshComponent>());
    }

    void MeshSystem::ShutdownInternal()
    {
        ComponentEventBus::Handler::BusDisconnect();
    }

    void MeshSystem::OnComponentConstruct(Entity entity)
    {
        auto ctx = WorldExecuteContext::CurrentReference<SystemTraits>();

        auto* meshComp = ctx.TryGet<MeshComponent>(entity);
        if (!meshComp)
        {
            return;
        }

        // The component names its model and nothing more: an asset pointer on it would be a
        // second truth, and one that no file can carry -- a mesh read back from a scene had
        // only the id. Assets are preloaded, so a miss here is worth saying out loud.
        auto* assetManager = Service<Resource::AssetManager>::Get();
        Ptr<Resource::ModelAsset> model = assetManager != nullptr
            ? assetManager->FindAsset<Resource::ModelAsset>(meshComp->m_modelAssetId)
            : nullptr;
        if (!model)
        {
            LOG_ERROR("[MeshSystem] No model asset for {}; entity {} stays without geometry.",
                meshComp->m_modelAssetId.GetPath().c_str(), static_cast<uint32_t>(entity));
            return;
        }

        if (!model->IsReady())
        {
            LOG_ERROR("[MeshSystem] Model asset not ready: {}; entity {} stays without geometry.",
                meshComp->m_modelAssetId.GetPath().c_str(), static_cast<uint32_t>(entity));
            return;
        }

        UpdateStatistics(*meshComp, *model);
    }

    void MeshSystem::OnComponentUpdated(Entity entity)
    {
        OnComponentConstruct(entity);
    }

    void MeshSystem::UpdateStatistics(MeshComponent& meshComp, Resource::ModelAsset& modelAsset)
    {
        const Resource::ModelAssetData* data = modelAsset.GetModelData();
        if (!data)
        {
            LOG_ERROR("[MeshSystem] Model asset data is null.");
            return;
        }

        const Resource::Mesh* mesh = data->GetMesh(meshComp.m_meshIndex);
        if (!mesh || meshComp.m_primitiveIndex >= mesh->primitives.size())
        {
            LOG_ERROR("[MeshSystem] Mesh/Primitive index out of bounds: mesh={}, primitive={}",
                      meshComp.m_meshIndex, meshComp.m_primitiveIndex);
            return;
        }

        const Resource::Primitive& prim = mesh->primitives[meshComp.m_primitiveIndex];
        if (prim.vertexBuffer.empty())
        {
            LOG_ERROR("[MeshSystem] Primitive has no vertex data.");
            return;
        }

        const uint32_t stride = prim.layout.stride > 0 ? prim.layout.stride : 1;
        meshComp.m_vertexCount   = static_cast<uint32_t>(prim.vertexBuffer.size() / stride);
        meshComp.m_triangleCount = prim.indexCount / 3;
    }
}
