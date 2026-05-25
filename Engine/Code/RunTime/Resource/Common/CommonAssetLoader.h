#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <Resource/Asset.h>

namespace Spark::Resource
{
    class BinaryAssetData : public AssetData
    {
    public:
        BinaryAssetData(eastl::vector<uint8_t> bytes, eastl::string resolvedPath);

        const eastl::vector<uint8_t>& GetBytes() const { return m_bytes; }
        const eastl::string& GetResolvedPath() const { return m_resolvedPath; }

    private:
        eastl::vector<uint8_t> m_bytes;
        eastl::string m_resolvedPath;
    };

    /// 通用二进制文件 Loader，按搜索路径查找并读取文件原始字节。
    /// 由具体的 Builder 持有作为辅助（不再走 AssetCatalogBus 注册）。
    class BinaryAssetLoader
    {
    public:
        BinaryAssetLoader() = default;
        ~BinaryAssetLoader() = default;

        void SetSearchPaths(const eastl::vector<eastl::string>& searchPaths)
        {
            m_searchPaths = searchPaths;
        }

        eastl::unique_ptr<AssetData> Load(const AssetId& id);

    private:
        eastl::string ResolvePath(const AssetId& id) const;

        eastl::vector<eastl::string> m_searchPaths;
    };
}