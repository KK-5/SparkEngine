#pragma once

#include <EASTL/vector.h>

namespace Spark::Resource
{
    class MaterialAssetData;

    //! A material's authored values as the bytes of a `.smat` -- the write half of what
    //! MaterialAssetLoader + MaterialAssetCompiler read. Empty on failure.
    //!
    //! A free function rather than a third class beside the loader and the compiler: it has
    //! no owner and no state. MaterialAssetBuilder::Serialize is what puts it on the bus.
    //!
    //! Every field is written, defaults included: a `.smat` is self-describing and diffs
    //! stably, and "no texture" is an explicit null rather than an absent key.
    //!
    //! The format lives entirely in the .cpp -- callers exchange bytes.
    eastl::vector<uint8_t> WriteMaterialAsset(const MaterialAssetData& data);
}
