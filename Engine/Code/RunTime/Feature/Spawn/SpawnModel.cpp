#include "SpawnModel.h"

#include <ECS/WorldContext.h>
#include <ECS/ExecuteContext.h>
#include <Math/MathUtils.h>
#include <SceneManager/IScene.h>
#include <Service/Service.h>

#include <Transform/Components.h>
#include <Mesh/Components.h>
#include <Material/Components.h>
#include <Material/MaterialContext.h>
#include <Material/MaterialUtils.h>

#include <Resource/Model/ModelAsset.h>

namespace Spark::Spawn
{
    namespace
    {
        void DecomposeTransform(const Math::Matrix4X4& m,
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

        //! Map a model's embedded material to authored StandardPBR. Factors → scalar
        //! inputs; the 5 texture image AssetIds → their MaterialTexSlot. m_specular has no
        //! glTF counterpart (kept at default); alphaMode/alphaCutoff are render state and
        //! not part of StandardPBR yet, so they are dropped here.
        Resource::StandardPBR StandardPBRFromModel(const Resource::Material& src)
        {
            Resource::StandardPBR p;
            p.m_baseColor         = src.baseColorFactor;
            p.m_metallic          = src.metallicFactor;
            p.m_roughness         = src.roughnessFactor;
            p.m_emissive          = Math::Vector4(src.emissiveFactor, 1.0f);
            p.m_emissiveStrength  = src.emissiveStrength;
            p.m_normalScale       = src.normalScale;
            p.m_occlusionStrength = src.occlusionStrength;

            using Slot = Resource::MaterialTexSlot;
            auto set = [&](Slot slot, const Resource::AssetId& id)
            {
                if (id.IsValid())
                {
                    p.m_textures[static_cast<size_t>(slot)] = id;
                }
            };
            set(Slot::BaseColor,         src.baseColorImageId);
            set(Slot::MetallicRoughness, src.metallicRoughnessImageId);
            set(Slot::Normal,            src.normalImageId);
            set(Slot::Occlusion,         src.occlusionImageId);
            set(Slot::Emissive,          src.emissiveImageId);
            return p;
        }
    }

    void SpawnModel(Ptr<Resource::ModelAsset> model, WorldContext& context)
    {
        const Resource::ModelAssetData* modelData = model->GetModelData();
        if (!modelData)
        {
            return;
        }

        IScene* scene = Service<IScene>::Get();

        // One shared material per model material, created lazily on first use so
        // primitives referencing the same materialIndex reuse a single MaterialHandle.
        auto* matCtx = Material::MaterialExecuteContext::Current();
        eastl::vector<Material::MaterialHandle> materials(
            modelData->GetMaterialCount(), Material::NullMaterial);

        auto materialFor = [&](uint32_t materialIndex) -> Material::MaterialHandle
        {
            if (!matCtx
                || materialIndex == Resource::Primitive::kInvalidMaterialIndex
                || materialIndex >= materials.size())
            {
                return Material::NullMaterial;
            }
            if (materials[materialIndex] == Material::NullMaterial)
            {
                if (const Resource::Material* src = modelData->GetMaterial(materialIndex))
                {
                    materials[materialIndex] =
                        Material::CreateMaterial(*matCtx, StandardPBRFromModel(*src));
                }
            }
            return materials[materialIndex];
        };

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

                Mesh::MeshComponent meshComp;
                meshComp.m_modelAssetId = model->GetAssetId();
                meshComp.m_modelAsset = model;
                meshComp.m_meshIndex = static_cast<uint32_t>(node->meshIndex);
                meshComp.m_primitiveIndex = static_cast<uint32_t>(p);
                context.Add<Mesh::MeshComponent>(primEntity, meshComp);

                // Material comes entirely from the MaterialComponent — seeded here from
                // the model's embedded material, shared across primitives by materialIndex.
                const Material::MaterialHandle mat = materialFor(mesh->primitives[p].materialIndex);
                if (mat != Material::NullMaterial)
                {
                    context.Add<Material::MaterialComponent>(
                        primEntity, Material::MaterialComponent{ mat });
                }

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
}
