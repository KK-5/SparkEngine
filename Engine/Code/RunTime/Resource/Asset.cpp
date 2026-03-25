#include "Asset.h"

#include <Service/Service.h>
#include "AssetManagerInterface.h"
#include "EBus/AssetBus.h"

namespace Spark::Resource
{
    void Asset::SetData(eastl::unique_ptr<AssetData> data)
    {
        m_data = eastl::move(data);
        m_status = m_data ? AssetStatus::Ready : AssetStatus::Error;
    }

    void Asset::Shutdown()
    {
        if (auto* manager = Service<AssetManager>::Get())
        {
            manager->ReleaseAsset(m_id);
        }
        delete this;
    }

    void AssetManager::SetAssetStatus(Asset& asset, AssetStatus status)
    {
        asset.SetStatus(status);

        if (status == AssetStatus::Error)
        {
            AssetBus::Event(asset.GetAssetType(), &AssetBus::Events::OnAssetError, asset);
        }
        else if (status == AssetStatus::Ready)
        {
            AssetBus::Event(asset.GetAssetType(), &AssetBus::Events::OnAssetReady, asset);
        }
    }

    void AssetManager::SetAssetData(Asset& asset, eastl::unique_ptr<AssetData> data)
    {
        asset.SetData(eastl::move(data));
    }
}
