#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <EASTL/unique_ptr.h>
#include <EASTL/vector.h>

#include <Base.h>

#include "Asset.h"
#include "AssetTypes.h"


namespace Spark::Resource
{
    class AssetDataBase;

    class AssetBuildContext
    {
    public:
        AssetId   id;
        AssetType type{AssetType::Image};

        AssetId   parentId;

        UniquePtr<AssetData> rawData;        ///< Load 输出 / Compile 输入；外部预置时跳过 Load
        UniquePtr<AssetData> compiledData;   ///< Compile 输出

        const uint8_t* sourceData = nullptr; ///< 非空时 Load 从内存解码（不读磁盘）
        size_t         sourceSize = 0;

        eastl::vector<eastl::string> searchPaths;
        AssetDataBase*               db{nullptr};   ///< Builder 注册子资产用

        AssetBuildContext() = default;
        AssetBuildContext(const AssetBuildContext&) = delete;
        AssetBuildContext& operator=(const AssetBuildContext&) = delete;
        AssetBuildContext(AssetBuildContext&&) = default;
        AssetBuildContext& operator=(AssetBuildContext&&) = default;

        eastl::string ResolvePath(eastl::string_view relative) const;

        AssetBuildContext MakeChild(AssetId subId, AssetType subType) const;
    };

    /// 统一路径解析：先检查 path 是否直接存在，再遍历 searchPaths 拼接查找
    eastl::string ResolveAssetPath(eastl::string_view path,
                                   const eastl::vector<eastl::string>& searchPaths);
}
