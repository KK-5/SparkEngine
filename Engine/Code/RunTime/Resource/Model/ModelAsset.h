#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Math/AABB.h>
#include <Math/Matrix4X4.h>
#include <Math/Vector4.h>

#include <Resource/Asset.h>
#include <Resource/AssetTypes.h>
#include <Resource/Material/MaterialState.h>   // AlphaMode

#include <RHI/Resource/Buffer/IndexBufferView.h>

#include "VertexLayout.h"

namespace Spark::Resource
{
    // === Per-instance compile configuration ===

    //! Unknown is the sentinel for "no source format decided" and never reaches an
    //! AssetId -- the format is picked from the extension when the id is built, before
    //! anything is loaded, because the descriptor is part of the id.
    enum class ModelAssetType : uint8_t
    {
        Unknown = 0,
        GLTF
    };

    class ModelAssetDescriptor : public AssetDescriptor
    {
    public:
        ModelAssetType type = ModelAssetType::GLTF;

        AssetHash Hash() const override;
    };

    // === Material ===

    //! Compiled material — factors plus the resolved image sub-asset AssetIds. Lives on
    //! the final ModelAssetData; assembled by ModelAssetBuilder from a RawMaterial.
    struct Material
    {
        Math::Vector4   baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
        float           metallicFactor{1.0f};
        float           roughnessFactor{1.0f};
        Math::Vector3   emissiveFactor{0.0f, 0.0f, 0.0f};
        float           emissiveStrength{1.0f};   // KHR_materials_emissive_strength
        float           normalScale{1.0f};        // normalTexture.scale
        float           occlusionStrength{1.0f};  // occlusionTexture.strength
        float           alphaCutoff{0.5f};
        AlphaMode       alphaMode{AlphaMode::Opaque};

        AssetId baseColorImageId;
        AssetId normalImageId;
        AssetId metallicRoughnessImageId;
        AssetId emissiveImageId;
        AssetId occlusionImageId;
    };

    //! Raw material — same factors, but each texture channel is still the glTF image
    //! index it points at (-1 = none), because image sub-assets don't exist yet at load
    //! time. Lives only on ModelAssetRawData; ModelAssetBuilder resolves the indices to
    //! AssetIds (via m_imageAssetIds) when it assembles the compiled Material.
    struct RawMaterial
    {
        Math::Vector4   baseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
        float           metallicFactor{1.0f};
        float           roughnessFactor{1.0f};
        Math::Vector3   emissiveFactor{0.0f, 0.0f, 0.0f};
        float           emissiveStrength{1.0f};
        float           normalScale{1.0f};
        float           occlusionStrength{1.0f};
        float           alphaCutoff{0.5f};
        AlphaMode       alphaMode{AlphaMode::Opaque};

        int32_t baseColorImage         = -1;
        int32_t metallicRoughnessImage = -1;
        int32_t normalImage            = -1;
        int32_t occlusionImage         = -1;
        int32_t emissiveImage          = -1;
    };

    // === Primitive ===

    struct Primitive
    {
        static constexpr uint32_t kInvalidMaterialIndex = ~0u;

        eastl::vector<uint8_t>  vertexBuffer;
        eastl::vector<uint8_t>  indexBuffer;
        uint32_t                indexCount = 0;
        RHI::IndexFormat        indexFormat = RHI::IndexFormat::UINT32;
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

    //! Loader output — the raw, pre-compile intermediate. Holds un-optimized geometry,
    //! raw materials (texture channels as glTF image indices), and the raw image entries
    //! (bytes / URIs). ModelAssetCompiler consumes it to produce ModelAssetData; it is
    //! never the asset's runtime data. Keeping raw-only fields off ModelAssetData is what
    //! makes "a compiled asset can't carry half-baked raw state" a type guarantee.
    class ModelAssetRawData : public AssetData
    {
    public:
        const Math::AABB&    GetBounds()       const { return m_bounds; }
        const eastl::string& GetResolvedPath() const { return m_resolvedPath; }

        size_t      GetMeshCount()      const { return m_meshes.size(); }
        const Mesh* GetMesh(size_t i)   const { return i < m_meshes.size() ? &m_meshes[i] : nullptr; }

        size_t      GetNodeCount()      const { return m_nodes.size(); }
        const Node* GetNode(size_t i)   const { return i < m_nodes.size() ? &m_nodes[i] : nullptr; }
        size_t      GetMaterialCount()  const { return m_rawMaterials.size(); }

        size_t               GetRawImageCount() const { return m_rawImages.size(); }
        const RawImageEntry* GetRawImage(size_t i) const
        {
            return i < m_rawImages.size() ? &m_rawImages[i] : nullptr;
        }

    private:
        friend class ModelAssetLoader;
        friend class ModelAssetCompiler;
        friend class ModelAssetBuilder;

        eastl::vector<Mesh>          m_meshes;        // un-optimized geometry
        eastl::vector<Node>          m_nodes;
        eastl::vector<RawMaterial>   m_rawMaterials;  // factors + glTF image indices
        eastl::vector<RawImageEntry> m_rawImages;     // embedded bytes / external URIs
        Math::AABB                   m_bounds;
        eastl::string                m_resolvedPath;
    };

    //! The compiled, runtime model asset (what ModelAsset holds). Optimized geometry,
    //! resolved materials (image AssetIds), and the dispatched image sub-asset ids. Carries
    //! no raw bytes — those live only on ModelAssetRawData.
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

        // Image sub-assets —— index 对齐 glTF images[]。Builder 派发后填充；dispatch 失败 /
        // 源不可用的槽位为默认构造的 AssetId（IsValid() == false），保持下标对齐供材质 by-index 引用。
        size_t          GetImageAssetCount()      const { return m_imageAssetIds.size(); }
        const AssetId&  GetImageAssetId(size_t i) const { return m_imageAssetIds[i]; }

    private:
        friend class ModelAssetCompiler;
        friend class ModelAssetBuilder;     // 派发后写 m_imageAssetIds / m_materials

        eastl::vector<Mesh>     m_meshes;         // optimized geometry
        eastl::vector<Node>     m_nodes;
        eastl::vector<Material> m_materials;      // factors + resolved image AssetIds
        eastl::vector<AssetId>  m_imageAssetIds;  // index 对齐 glTF images[]
        Math::AABB              m_bounds;
        eastl::string           m_resolvedPath;
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
