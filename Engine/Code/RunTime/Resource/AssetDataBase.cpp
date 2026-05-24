#include "AssetDataBase.h"


namespace Spark::Resource
{
    HashString AssetDataBase::GetName() const
    {
        return "AssetDataBase"_hs;
    }

    void AssetDataBase::ShutdownInternal()
    {
        std::unique_lock lock(m_mutex);
        m_assets.clear();
    }

    Ptr<Asset> AssetDataBase::Find(const AssetId& id) const
    {
        std::shared_lock lock(m_mutex);
        auto it = m_assets.find(id);
        if (it == m_assets.end())
        {
            return nullptr;
        }
        return it->second;
    }

    bool AssetDataBase::Contains(const AssetId& id) const
    {
        std::shared_lock lock(m_mutex);
        return m_assets.find(id) != m_assets.end();
    }

    eastl::vector<Ptr<Asset>> AssetDataBase::Snapshot() const
    {
        std::shared_lock lock(m_mutex);
        eastl::vector<Ptr<Asset>> out;
        out.reserve(m_assets.size());
        for (const auto& kv : m_assets)
        {
            out.push_back(kv.second);
        }
        return out;
    }

    Ptr<Asset> AssetDataBase::InsertOrGet(const AssetId& id, Ptr<Asset> newAsset)
    {
        std::unique_lock lock(m_mutex);
        auto [it, inserted] = m_assets.emplace(id, eastl::move(newAsset));
        return it->second;
    }

    void AssetDataBase::Remove(const AssetId& id)
    {
        std::unique_lock lock(m_mutex);
        m_assets.erase(id);
    }
}
