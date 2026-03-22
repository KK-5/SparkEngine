#pragma once

#include <EASTL/string_view.h>

#include <ECS/ISystem.h>
#include <Object/Base.h>
#include "AssetTypes.h"


namespace Spark::Resource
{
    class Asset;
    class AssetLoader;
    class AssetCompiler;

    class AssetManager : public ISystem
    {
        friend class Asset;
    public:
        virtual ~AssetManager() = default;

        /// @brief Find or create asset from AssetManager, if asset does not exist, return null pointer.
        /// @param id Asset id
        /// @param type Asset type
        /// @return Pointer of the asset or null pointer for invalid asset.
        virtual Ptr<Asset> FindAsset(const AssetId& id, AssetType type) const = 0;

        /// 同步加载资产，阻塞直到 Ready 或 Error
        virtual Ptr<Asset> LoadAsset(const AssetId& id, AssetType type) = 0;

        /// 异步请求加载，立即返回（状态为 Queued/Loading），完成后状态变为 Ready
        virtual Ptr<Asset> RequestAsset(const AssetId& id, AssetType type) = 0;

        /// 注册资产搜索路径（如 "Assets/Shaders", "Assets/Textures"）
        virtual void AddSearchPath(eastl::string_view path) = 0;

        virtual void RemoveSearchPath(eastl::string_view path) = 0;

        virtual void RegisterAssetLoader(eastl::unique_ptr<AssetLoader> loader, AssetType type) = 0;

        virtual void RegisterAssetCompiler(eastl::unique_ptr<AssetCompiler> compiler, AssetType type) = 0;

    protected:
        /// @brief Called when Asset Shutdown
        /// @param id Asset id
        virtual void ReleaseAsset(const AssetId& id) = 0;

        /// 供子类访问 Asset 的 protected 成员
        static void SetAssetStatus(Asset& asset, AssetStatus status);
        static void SetAssetData(Asset& asset, eastl::unique_ptr<AssetData> data);
    };
}