#pragma once

namespace Spark::Resource
{
    //! The three top-level keys of a `.smat`, shared by the read side
    //! (MaterialAssetCompiler) and the write side (WriteMaterialAsset) so the two cannot
    //! drift apart. They are written and read by hand, lowerCamel like `path` / `sub` /
    //! `desc`; what lives INSIDE state and properties are reflected names, and those are
    //! handed to the serializer.
    //!
    //! Just the names. Which text format spells them is the business of the two .cpp files
    //! and of nothing that includes this.
    inline constexpr const char* kMaterialShadingModelKey = "shadingModel";
    inline constexpr const char* kMaterialStateKey        = "state";
    inline constexpr const char* kMaterialPropertiesKey   = "properties";

    //! Cache entries only: a model's material sub-asset carries the cache's identity here.
    inline constexpr const char* kMaterialIdentityKey     = "identity";
}
