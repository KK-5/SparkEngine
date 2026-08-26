#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <Base.h>

#include "Asset.h"
#include "AssetTypes.h"


namespace Spark { class FileSystem; }

namespace Spark::Resource
{
    class AssetDataBase;

    class AssetBuildContext
    {
    public:
        AssetId   id;

        AssetId   parentId;

        UniquePtr<AssetData> rawData;        ///< Load 输出 / Compile 输入
        UniquePtr<AssetData> compiledData;   ///< Compile 输出

        const uint8_t* sourceData = nullptr; ///< 非空时 Load 从内存解码（不读磁盘）
        size_t         sourceSize = 0;

        const FileSystem*            fileSystem{nullptr};
        AssetDataBase*               db{nullptr};   ///< Builder 注册子资产用

        AssetBuildContext() = default;
        AssetBuildContext(const AssetBuildContext&) = delete;
        AssetBuildContext& operator=(const AssetBuildContext&) = delete;
        AssetBuildContext(AssetBuildContext&&) = default;
        AssetBuildContext& operator=(AssetBuildContext&&) = default;

        eastl::string ResolvePath(eastl::string_view virtualPath) const;

        AssetBuildContext MakeChild(AssetId subId) const;
    };

    //! Resolve `relative` against the directory of `virtualPath`, lexically. Used for a
    //! glTF's external texture URIs, which are relative to the model file.
    eastl::string ResolveSiblingVirtualPath(eastl::string_view virtualPath,
                                            eastl::string_view relative);
}
