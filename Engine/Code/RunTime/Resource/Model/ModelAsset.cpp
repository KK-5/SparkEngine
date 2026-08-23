#include "ModelAsset.h"

#include <EASTLEX/hash.h>
#include <HashString/HashString.h>

namespace Spark::Resource
{
    // === ModelAssetDescriptor ===

    AssetHash ModelAssetDescriptor::Hash() const
    {
        // Seeded with the descriptor's own type: without it a one-field hash collides with
        // any other type's one-field hash (ModelAssetType::GLTF and ShaderBackend::SPIRV are
        // both 1), and AssetId::operator== compares hashes alone.
        size_t h = static_cast<size_t>(HashString("ModelAssetDescriptor").value());
        eastl::hash_combine(h, static_cast<size_t>(type));
        return static_cast<AssetHash>(h);
    }

    // === ModelAssetData ===

    size_t ModelAssetData::GetPrimitiveCount() const
    {
        size_t count = 0;
        for (const auto& mesh : m_meshes)
        {
            count += mesh.primitives.size();
        }
        return count;
    }

    // === ModelAsset ===

    Ptr<AssetDescriptor> ModelAsset::DefaultDescriptor()
    {
        static Ptr<AssetDescriptor> instance(new ModelAssetDescriptor{});
        return instance;
    }

    ModelAsset::ModelAsset(AssetId id)
        : Asset(eastl::move(id))
    {}

    const ModelAssetData* ModelAsset::GetModelData() const
    {
        return GetData<ModelAssetData>();
    }
}
