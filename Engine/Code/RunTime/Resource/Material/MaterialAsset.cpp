#include "MaterialAsset.h"

#include <HashString/HashString.h>
#include <Log/ILogSystem.h>

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

    AssetId MaterialAsset::MakeSubId(const AssetId& parentId, eastl::string_view subLabel)
    {
        // A sub-asset of a sub-asset would drop the parent's own subLabel and could collide.
        ASSERT(!parentId.IsSubAsset(),
            "[MaterialAsset] MakeSubId: parent is itself a sub-asset ('{}'); the sub id "
            "would lose its label", parentId.GetPath().c_str());

        const eastl::string& parentPath = parentId.GetPath();
        return AssetId::OfSub<MaterialAsset>(
            eastl::string_view(parentPath.c_str(), parentPath.size()), subLabel);
    }

    MaterialAsset::MaterialAsset(AssetId id)
        : Asset(eastl::move(id))
    {}

    const MaterialAssetData* MaterialAsset::GetMaterialData() const
    {
        return GetData<MaterialAssetData>();
    }
}
