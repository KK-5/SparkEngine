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
        static constexpr uint32_t kInvalidMaterialIndex = ~0u;

        eastl::vector<uint8_t>  vertexBuffer;
        eastl::vector<uint8_t>  indexBuffer;
        VertexLayout            layout;
        uint32_t                materialIndex{kInvalidMaterialIndex};
        Math::AABB              bounds;
    };

    // === Raw image entry (intermediate; Builder consumes & clears after sub-asset dispatch) ===

    struct RawImageEntry
    {
        eastl::vector<uint8_t> data;          ///< 内嵌图字节（已 copy 自源数据）；空 = 外部
        eastl::string          externalUri;   ///< 外部图相对路径（相对 glTF 所在目录）；空 = 内嵌
        eastl::string          name;          ///< glTF image.name；用于子资产 subLabel；空时回落到索引
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

        // embed image
        size_t               GetRawImageCount() const { return m_rawImages.size(); }
        const RawImageEntry* GetRawImage(size_t i) const
        {
            return i < m_rawImages.size() ? &m_rawImages[i] : nullptr;
        }

        // Image sub-assets —— index 对齐 glTF images[]。Builder 派发后填充到
        // compiledData 上；dispatch 失败 / 源不可用的槽位为默认构造的 AssetId
        // （IsValid() == false），保持下标对齐供材质阶段 by-index 引用。
        size_t          GetImageAssetCount()      const { return m_imageAssetIds.size(); }
        const AssetId&  GetImageAssetId(size_t i) const { return m_imageAssetIds[i]; }

    private:
        friend class ModelAssetLoader;
        friend class ModelAssetCompiler;
        friend class ModelAssetBuilder;     // 派发完允许 clear + 写 m_imageAssetIds

        eastl::vector<Mesh>          m_meshes;
        eastl::vector<Material>      m_materials;       // v1 不填充
        eastl::vector<Node>          m_nodes;
        eastl::vector<RawImageEntry> m_rawImages;       // 仅 raw 阶段非空
        eastl::vector<AssetId>       m_imageAssetIds;   // 仅 compiled 阶段非空，index 对齐 glTF images[]
        Math::AABB                   m_bounds;
        eastl::string                m_resolvedPath;
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
