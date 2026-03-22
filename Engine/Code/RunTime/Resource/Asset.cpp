#include "Asset.h"

#include <Service.h>
#include "AssetManagerInterface.h"

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
    }

    void AssetManager::SetAssetData(Asset& asset, eastl::unique_ptr<AssetData> data)
    {
        asset.SetData(eastl::move(data));
    }
}
