#include "MaterialAssetLoader.h"

#include <EASTL/vector.h>

#include <Log/ILogSystem.h>
#include <VFS/FileSystem.h>

#include "MaterialRawTypes.h"

namespace Spark::Resource
{
    UniquePtr<AssetData> MaterialAssetLoader::Load(const AssetId& id,
                                                  const FileSystem& fileSystem) const
    {
        eastl::vector<uint8_t> bytes;
        if (!fileSystem.ReadFile(id.GetPath(), bytes))
        {
            LOG_ERROR("[MaterialAssetLoader] Failed to read '{}'.", id.GetPath().c_str());
            return nullptr;
        }

        return MakeUnique<MaterialEncodedRawData>(eastl::move(bytes));
    }
}
