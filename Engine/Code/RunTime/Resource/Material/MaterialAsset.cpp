#include "MaterialAsset.h"

#include <HashString/HashString.h>

namespace Spark::Resource
{
    AssetHash MaterialAssetDescriptor::Hash() const
    {
        // No fields, so the seed is the whole hash -- and it has to be the type's own name
        // for the same reason the others seed with theirs: two empty descriptors of
        // different types would otherwise contribute identically to an AssetId's hash.
        return static_cast<AssetHash>(HashString("MaterialAssetDescriptor").value());
    }

    Ptr<AssetDescriptor> MaterialAsset::DefaultDescriptor()
    {
        static Ptr<AssetDescriptor> instance(new MaterialAssetDescriptor{});
        return instance;
    }

    MaterialAsset::MaterialAsset(AssetId id)
        : Asset(eastl::move(id))
    {}

    const MaterialAssetData* MaterialAsset::GetMaterialData() const
    {
        return GetData<MaterialAssetData>();
    }
}
