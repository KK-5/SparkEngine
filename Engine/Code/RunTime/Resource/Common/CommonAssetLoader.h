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

    /// 通用二进制文件 Loader，按搜索路径查找并读取文件原始字节
    class BinaryAssetLoader : public AssetLoader
    {
    public:
        BinaryAssetLoader();
        virtual ~BinaryAssetLoader();

        void SetSearchPaths(const eastl::vector<eastl::string> searchPaths) override;

        eastl::unique_ptr<AssetData> Load(const AssetId& id) override;

        void OnAssetSearchPathsChange(const eastl::vector<eastl::string>& paths) override;

    private:
        eastl::string ResolvePath(const AssetId& id) const;

        eastl::vector<eastl::string> m_searchPaths;
    };
}