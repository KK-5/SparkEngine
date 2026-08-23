#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>

#include <Resource/Asset.h>

namespace Spark { class FileSystem; }

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

    //! Reads an asset's raw bytes. Held by a Builder as a helper.
    class BinaryAssetLoader
    {
    public:
        BinaryAssetLoader() = default;
        ~BinaryAssetLoader() = default;

        eastl::unique_ptr<AssetData> Load(const AssetId& id, const FileSystem& fileSystem);

        //! Reads a path that is already physical. The shader #include handler needs this:
        //! DXC hands over a bare name carrying no mount, so it does its own search first.
        eastl::unique_ptr<AssetData> LoadPhysicalFile(eastl::string physicalPath) const;
    };
}