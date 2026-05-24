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
        // ===== 身份 =====
        AssetId   id;
        AssetType type{AssetType::Image};

        // ===== 父资产链接（root 为空 AssetId） =====
        AssetId   parentId;

        // ===== 流转中的数据 =====
        UniquePtr<AssetData> rawData;        ///< Load 输出 / Compile 输入；外部预置时跳过 Load
        UniquePtr<AssetData> compiledData;   ///< Compile 输出

        const uint8_t* sourceData = nullptr; ///< 非空时 Load 从内存解码（不读磁盘）
        size_t         sourceSize = 0;

        // ===== 环境（manager 在构造 ctx 时填入；子 ctx 由 MakeChild 继承） =====
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
}
