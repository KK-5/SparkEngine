#pragma once

#include <EASTL/string_view.h>
#include <EASTL/type_traits.h>

#include <ECS/ISystem.h>
#include <Base.h>
#include "AssetTypes.h"


namespace Spark::Resource
{
    class Asset;

    /// 外部使用方接口：异步请求资产、搜索路径管理。
    /// 注册表的读写在 AssetDataBase；构建逻辑在 AssetBuildBus 上的 Builder。
    class AssetManager : public ISystem
    {
        friend class Asset;  // 用于 Asset Shutdown 时回调 ReleaseAsset
    public:
        virtual ~AssetManager() = default;

        /// 同步加载资产，阻塞直到 Ready 或 Error
        virtual Ptr<Asset> LoadAsset(const AssetId& id, AssetType type) = 0;

        /// 异步请求加载，立即返回（状态为 Queued/Loading），完成后状态变为 Ready；
        /// 加载完成时触发 AssetBus 的 OnAssetReady 事件
        virtual Ptr<Asset> RequestAsset(const AssetId& id, AssetType type) = 0;

        /// 查找已注册的资产；返回的 Asset 可能尚未 Ready
        virtual Ptr<Asset> FindAsset(const AssetId& id) const = 0;

        /// 资产搜索路径（如 "Assets/Shaders", "Assets/Textures"）
        virtual void AddSearchPath(eastl::string_view path) = 0;
        virtual void RemoveSearchPath(eastl::string_view path) = 0;

        // ===== 模板便捷方法 =====

        template<typename T>
        Ptr<T> LoadAsset(const AssetId& id)
        {
            static_assert(eastl::is_base_of_v<Asset, T>, "T must derive from Asset");
            Ptr<Asset> asset = LoadAsset(id, T::GetAssetTypeStatic());
            return Ptr<T>(static_cast<T*>(asset.get()));
        }

        template<typename T>
        Ptr<T> RequestAsset(const AssetId& id)
        {
            static_assert(eastl::is_base_of_v<Asset, T>, "T must derive from Asset");
            Ptr<Asset> asset = RequestAsset(id, T::GetAssetTypeStatic());
            return Ptr<T>(static_cast<T*>(asset.get()));
        }

    protected:
        /// 由 Asset::Shutdown 调用，从 DataBase 注册表移除
        virtual void ReleaseAsset(const AssetId& id) = 0;
    };
}
