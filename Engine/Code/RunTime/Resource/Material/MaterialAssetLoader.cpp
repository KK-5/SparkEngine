#include "MaterialAssetLoader.h"

#include <EASTL/vector.h>

#include <Log/ILogSystem.h>
#include <VFS/FileSystem.h>

#include "MaterialRawTypes.h"

namespace Spark::Resource
{
    UniquePtr<AssetData> MaterialAssetLoader::Load(const AssetId& id,
                                                  const FileSystem& fileSystem,
                                                  LoadFailure& failure) const
    {
        eastl::vector<uint8_t> bytes;
        if (!fileSystem.ReadFile(id.GetPath(), bytes))
        {
            failure = fileSystem.Exists(id.GetPath()) ? LoadFailure::Unavailable
                                                      : LoadFailure::Missing;
            return nullptr;
        }

        return MakeUnique<MaterialEncodedRawData>(eastl::move(bytes));
    }
}
