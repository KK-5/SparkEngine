#include "MeshUtils.h"

#include <ECS/WorldContext.h>
#include <Math/MathUtils.h>
#include <SceneManager/IScene.h>
#include <Service/Service.h>
#include <Transform/Components.h>
#include <Resource/Model/ModelAsset.h>

#include "Components.h"

namespace Spark::Mesh
{

static void DecomposeTransform(const Math::Matrix4X4& m,
                               Math::Vector3& position,
                               Math::Vector3& rotation,
                               Math::Vector3& scale)
{
    position = Math::Vector3(m[3].x, m[3].y, m[3].z);

    Math::Vector3 col0(m[0].x, m[0].y, m[0].z);
    Math::Vector3 col1(m[1].x, m[1].y, m[1].z);
    Math::Vector3 col2(m[2].x, m[2].y, m[2].z);

    scale = Math::Vector3(Math::Length(col0), Math::Length(col1), Math::Length(col2));

    Math::Matrix3X3 rotMat(1.0f);
    if (scale.x > 0)
    {
        rotMat[0] = col0 / scale.x;
    }
    if (scale.y > 0)
    {
        rotMat[1] = col1 / scale.y;
    }
    if (scale.z > 0)
    {
        rotMat[2] = col2 / scale.z;
    }
    rotation = Math::QuaternionToEuler(Math::QuaternionFromMatrix3X3(rotMat));
}

void ExtractMeshToWorld(Ptr<Resource::ModelAsset> model, WorldContext& context)
{
    const Resource::ModelAssetData* modelData = model->GetModelData();
    if (!modelData)
    {
        return;
    }

    IScene* scene = Service<IScene>::Get();

    const size_t nodeCount = modelData->GetNodeCount();
    eastl::vector<Entity> nodeEntities(nodeCount, NullEntity);

    // First pass: create node entities and primitive child entities
    for (size_t i = 0; i < nodeCount; ++i)
    {
        const Resource::Node* node = modelData->GetNode(i);
        if (!node)
        {
            continue;
        }

        Math::Vector3 position, rotation, scale;
        DecomposeTransform(node->localTransform, position, rotation, scale);

        Transform::TransformComponent transformComp;
        transformComp.m_position = position;
        transformComp.m_rotation = rotation;
        transformComp.m_scale = scale;

        Entity nodeEntity = context.CreateEntity(node->name);
        context.Add<Transform::TransformComponent>(nodeEntity, transformComp);
        nodeEntities[i] = nodeEntity;

        if (scene)
        {
            scene->AddEntity(nodeEntity);
        }

        if (node->meshIndex < 0)
        {
            continue;
        }

        const Resource::Mesh* mesh = modelData->GetMesh(static_cast<size_t>(node->meshIndex));
        if (!mesh)
        {
            continue;
        }

        const size_t primCount = mesh->primitives.size();
        for (size_t p = 0; p < primCount; ++p)
        {
            eastl::string primName = p == 0 ?  mesh->name : mesh->name + "." + eastl::to_string(p);
            Entity primEntity = context.CreateEntity(primName);

            MeshComponent meshComp;
            meshComp.m_modelAssetId = model->GetAssetId();
            meshComp.m_modelAsset = model;
            meshComp.m_meshIndex = static_cast<uint32_t>(node->meshIndex);
            meshComp.m_primitiveIndex = static_cast<uint32_t>(p);
            context.Add<MeshComponent>(primEntity, meshComp);

            if (scene)
            {
                scene->SetParent(primEntity, nodeEntity);
            }
        }
    }

    // Second pass: wire up node-to-node parent relationships
    if (scene)
    {
        for (size_t i = 0; i < nodeCount; ++i)
        {
            const Resource::Node* node = modelData->GetNode(i);
            if (!node || node->parent < 0)
            {
                continue;
            }

            Entity childEntity = nodeEntities[i];
            Entity parentEntity = nodeEntities[static_cast<size_t>(node->parent)];
            if (childEntity != NullEntity && parentEntity != NullEntity)
            {
                scene->SetParent(childEntity, parentEntity);
            }
        }
    }
}

} // namespace Spark::Mesh
