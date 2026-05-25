#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Math/AABB.h>
#include <Math/Matrix4X4.h>
#include <Math/Vector4.h>

#include <Resource/Asset.h>
#include <Resource/AssetTypes.h>

#include "VertexLayout.h"

namespace Spark::Resource
{
    // === Per-instance compile configuration ===

    enum class ModelAssetType : uint8_t
    {
        Unknown = 0,
        GLTF
    };

    class ModelAssetDescriptor : public AssetDescriptor
    {
    public:
        ModelAssetType type = ModelAssetType::Unknown;

        AssetHash Hash() const override;
    };

    // === Material ===

    enum class AlphaMode : uint8_t
    {
        Opaque,
        Mask,
        Blend,
    };

    struct Material
    {
        Math::Vector4   baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
        float           metallicFactor{1.0f};
        float           roughnessFactor{1.0f};
        Math::Vector3   emissiveFactor{0.0f, 0.0f, 0.0f};
        float           alphaCutoff{0.5f};
        AlphaMode       alphaMode{AlphaMode::Opaque};

        AssetId baseColorImageId;
        AssetId normalImageId;
        AssetId metallicRoughnessImageId;
        AssetId emissiveImageId;
        AssetId occlusionImageId;
    };

    // === Primitive ===

    struct Primitive
    {
        eastl::vector<uint8_t>  vertexBuffer;
        eastl::vector<uint8_t>  indexBuffer;
        VertexLayout            layout;
        uint32_t                materialIndex{0};
        Math::AABB              bounds;
    };

    // === Mesh (named group of primitives sharing a transform) ===

    struct Mesh
    {
        eastl::vector<Primitive> primitives;
        eastl::string            name;
    };

    // === Node (flat hierarchy, parent index semantics) ===

    struct Node
    {
        int32_t         parent{-1};
        Math::Matrix4X4 localTransform{1.0f};
        int32_t         meshIndex{-1};
        eastl::string   name;
    };

    // === Pipeline data ===

    class ModelAssetData : public AssetData
    {
    public:
        const Math::AABB&    GetBounds()       const { return m_bounds; }
        const eastl::string& GetResolvedPath() const { return m_resolvedPath; }

        // Mesh & Primitive
        size_t      GetMeshCount()      const { return m_meshes.size(); }
        const Mesh* GetMesh(size_t i)   const { return i < m_meshes.size() ? &m_meshes[i] : nullptr; }
        size_t      GetPrimitiveCount() const;

        // Node
        size_t      GetNodeCount()      const { return m_nodes.size(); }
        const Node* GetNode(size_t i)   const { return i < m_nodes.size() ? &m_nodes[i] : nullptr; }

        // Material
        size_t          GetMaterialCount()      const { return m_materials.size(); }
        const Material* GetMaterial(size_t i)   const { return i < m_materials.size() ? &m_materials[i] : nullptr; }

    private:
        friend class ModelAssetLoader;
        friend class ModelAssetCompiler;

        eastl::vector<Mesh>       m_meshes;
        eastl::vector<Material>   m_materials;       // v1 不填充
        eastl::vector<Node>       m_nodes;            // v1 不填充
        Math::AABB                m_bounds;
        eastl::string             m_resolvedPath;
    };

    // === Asset ===

    class ModelAsset : public Asset
    {
    public:
        using Descriptor = ModelAssetDescriptor;

        static constexpr AssetType GetAssetTypeStatic() { return AssetType::Model; }
        static Ptr<AssetDescriptor> DefaultDescriptor();

        explicit ModelAsset(AssetId id);

        const ModelAssetData* GetModelData() const;
    };
}
