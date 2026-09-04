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
        virtual Ptr<Asset> LoadAsset(const AssetId& id) = 0;

        /// 异步请求加载，立即返回（状态为 Queued/Loading），完成后状态变为 Ready；
        /// 加载完成时触发 AssetBus 的 OnAssetReady 事件
        virtual Ptr<Asset> RequestAsset(const AssetId& id) = 0;

        virtual Ptr<Asset> FindAsset(const AssetId& id) const = 0;

        virtual AssetType GetSupportAssetType(eastl::string_view file) = 0;

        //! Takes a virtual path (`mount://relative`). Mount points are registered on the
        //! FileSystem service, not here.
        virtual AssetId MakeAssetId(eastl::string_view virtualPath) = 0;

        virtual void AssetRegistry() = 0;

        //! Bytes at a path, as an asset loadable before this returns -- registration cannot
        //! wait for the file watcher when an editor action needs the result this frame.
        //! Overwriting is normal. Invalid id on failure, reason logged.
        virtual AssetId WriteAssetFile(eastl::string_view virtualPath,
                                       const uint8_t* data, size_t size) = 0;

        //! Typed forms. The id already carries its type, so these only add the downcast --
        //! and the check that the caller asked for the type the id actually names.
        template<typename T>
        Ptr<T> LoadAsset(const AssetId& id)
        {
            static_assert(eastl::is_base_of_v<Asset, T>, "T must derive from Asset");
            ValidateAssetType(id, T::GetAssetTypeStatic());
            Ptr<Asset> asset = LoadAsset(id);
            return Ptr<T>(static_cast<T*>(asset.get()));
        }

        template<typename T>
        Ptr<T> RequestAsset(const AssetId& id)
        {
            static_assert(eastl::is_base_of_v<Asset, T>, "T must derive from Asset");
            ValidateAssetType(id, T::GetAssetTypeStatic());
            Ptr<Asset> asset = RequestAsset(id);
            return Ptr<T>(static_cast<T*>(asset.get()));
        }

    protected:
        //! Drop `self` from the database as it is destroyed. Takes the instance, not just
        //! the id: a build creates an asset before it knows whether one is already
        //! registered under that id, and the loser of that race must not evict the winner.
        virtual void ReleaseAsset(const AssetId& id, const Asset* self) = 0;
    };
}
