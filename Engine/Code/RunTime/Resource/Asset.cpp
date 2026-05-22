#include "Asset.h"

#include <filesystem>

#include <Service/Service.h>
#include "AssetManagerInterface.h"
#include "EBus/AssetBus.h"

namespace Spark::Resource
{
    eastl::string AssetLoader::ResolvePath(const AssetId& id) const
    {
        const eastl::string& path = id.GetPath();
        for (const auto& searchPath : m_searchPaths)
        {
            std::filesystem::path full = std::filesystem::path(searchPath.c_str()) / path.c_str();
            if (std::filesystem::exists(full))
            {
                auto str = full.string();
                return eastl::string(str.c_str(), str.size());
            }
        }
        return {};
    }

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
