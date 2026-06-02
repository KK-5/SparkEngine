#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>

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

        //! Resolve a raw file path (asset-relative or absolute) against the
        //! search paths and read its bytes. Used by the shader #include handler
        //! so includes resolve through the same roots as regular assets.
        eastl::unique_ptr<AssetData> LoadFile(eastl::string_view path) const;

    private:
        eastl::string ResolvePath(const AssetId& id) const;
        eastl::string ResolvePathStr(eastl::string_view path) const;
        eastl::unique_ptr<AssetData> ReadResolved(eastl::string resolvedPath) const;

        eastl::vector<eastl::string> m_searchPaths;
    };
}