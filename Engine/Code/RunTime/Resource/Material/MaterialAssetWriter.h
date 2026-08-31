#pragma once

#include <EASTL/vector.h>

namespace Spark::Resource
{
    class MaterialAssetData;

    //! A material's authored values as the bytes of a `.smat` -- the write half of what
    //! MaterialAssetLoader + MaterialAssetCompiler read. Empty on failure.
    //!
    //! A free function rather than a third class beside the loader and the compiler: it has
    //! no owner and no place in the build pipeline. MaterialAssetBuilder never calls it and
    //! AssetBuildBus knows nothing about it, because writing a source file back is an editor
    //! action, never part of building one. (Serialize / Deserialize on the bus are a
    //! different thing entirely: those are the cook cache's form, which every asset type has
    //! to answer for because ProcessAsset asks every type. Nothing asks this.)
    //!
    //! Every field is written, defaults included: a `.smat` is self-describing and diffs
    //! stably, and "no texture" is an explicit null rather than an absent key.
    //!
    //! The format lives entirely in the .cpp -- callers exchange bytes.
    eastl::vector<uint8_t> WriteMaterialAsset(const MaterialAssetData& data);
}
