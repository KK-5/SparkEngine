#include "MaterialUtils.h"

#include <Service/Service.h>

#include <Resource/AssetManagerInterface.h>
#include <Resource/Material/MaterialAsset.h>

namespace Spark::Material
{
    namespace
    {
        Ptr<Resource::MaterialAsset> AcquireAsset(const Resource::AssetId& id)
        {
            auto* assetManager = Service<Resource::AssetManager>::Get();
            if (!assetManager)
            {
                return nullptr;
            }

            // A sub-asset is published by its parent's build and refuses to be built on
            // its own, so asking for a load could only produce an error.
            Ptr<Resource::Asset> asset =
                id.IsSubAsset() ? assetManager->FindAsset(id) : assetManager->LoadAsset(id);

            if (!asset || asset->GetAssetType() != Resource::AssetType::Material || !asset->IsReady())
            {
                return nullptr;
            }
            return Ptr<Resource::MaterialAsset>(static_cast<Resource::MaterialAsset*>(asset.get()));
        }
    }

    MaterialHandle Resolve(MaterialContext& mc, const Resource::AssetId& id)
    {
        if (!id.IsValid())
        {
            return NullMaterial;
        }

        for (MaterialHandle h : mc.GetView<MaterialAssetRef>())
        {
            if (mc.Get<MaterialAssetRef>(h).m_id == id)
            {
                return h;
            }
        }

        Ptr<Resource::MaterialAsset> asset = AcquireAsset(id);
        if (!asset)
        {
            return NullMaterial;
        }
        const Resource::MaterialAssetData* data = asset->GetMaterialData();
        if (!data)
        {
            return NullMaterial;
        }

        const MaterialHandle h = CreateMaterial(mc, data->GetParams(), data->GetState());
        mc.Add<MaterialAssetRef>(h, MaterialAssetRef{id});
        return h;
    }
}
