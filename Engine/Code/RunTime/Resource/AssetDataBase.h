#pragma once

#include <shared_mutex>

#include <EASTL/unordered_map.h>
#include <EASTL/vector.h>

#include <Base.h>
#include <ECS/ISystem.h>
#include <Service/Service.h>

#include "Asset.h"
#include "AssetTypes.h"


namespace Spark::Resource
{
    class AssetDataBase : public ISystem
    {
    public:
        AssetDataBase() = default;
        ~AssetDataBase() override = default;

        // ISystem
        eastl::vector<HashString> Request() const override { return {}; }
        HashString GetName() const override;

        // ===== 读（共享锁） =====
        Ptr<Asset>                Find(const AssetId& id) const;
        bool                      Contains(const AssetId& id) const;
        eastl::vector<Ptr<Asset>> Snapshot() const;

        // ===== 写（独占锁） =====
        /// 幂等插入：如果 id 已存在，返回现有 Ptr（忽略传入的 newAsset）；
        /// 否则插入并返回 newAsset。
        Ptr<Asset> InsertOrGet(const AssetId& id, Ptr<Asset> newAsset);

        void Remove(const AssetId& id);

    private:
        void InitInternal() override {}
        void ShutdownInternal() override;

        mutable std::shared_mutex                      m_mutex;
        eastl::unordered_map<AssetId, Ptr<Asset>>      m_assets;
    };
}
