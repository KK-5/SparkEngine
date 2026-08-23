#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/type_traits.h>

#include <ECS/ISystem.h>
#include <Base.h>
#include "AssetTypes.h"


namespace Spark::Resource
{
    class Asset;

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

        virtual Ptr<Asset> FindAsset(const AssetId& id) const = 0;

        virtual AssetType GetSupportAssetType(eastl::string_view file) = 0;

        //! Takes a virtual path (`mount://relative`). Mount points are registered on the
        //! FileSystem service, not here.
        virtual AssetId MakeAssetId(eastl::string_view virtualPath) = 0;

        virtual void AssetRegistry() = 0;

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
        virtual void ReleaseAsset(const AssetId& id) = 0;
    };
}
