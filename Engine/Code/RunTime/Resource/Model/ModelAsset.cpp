#include "ModelAsset.h"

#include <EASTLEX/hash.h>

namespace Spark::Resource
{
    // === ModelAssetDescriptor ===

    AssetHash ModelAssetDescriptor::Hash() const
    {
        size_t h = 0;
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
        : Asset(eastl::move(id), AssetType::Model)
    {}

    const ModelAssetData* ModelAsset::GetModelData() const
    {
        return GetData<ModelAssetData>();
    }
}
