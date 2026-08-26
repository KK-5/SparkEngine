#pragma once

#include <EASTL/string.h>
#include <EASTL/vector.h>

#include <Base.h>
#include <Resource/Asset.h>

#include "EnvironmentBaker.h"

namespace Spark::Resource
{
    //! What Load handed to Compile. The raw states its own kind because usage cannot: a
    //! Texture2D arrives as either decoded pixels or an authored container.
    class ImageRawData : public AssetData
    {
    public:
        enum class Kind : uint8_t
        {
            Pixels,   //!< a .png / .jpg / .hdr / .svg, or a glTF's embedded bytes
            Encoded,  //!< an authored .ktx2's bytes, still to be parsed
            Baked,    //!< a GPU bake's cube faces, still to be assembled
        };

        Kind GetKind() const { return m_kind; }

    protected:
        explicit ImageRawData(Kind kind) : m_kind(kind) {}

    private:
        Kind m_kind;
    };

    //! An authored .ktx2, unparsed. A source format like any other: parsing it is its Compile.
    class ImageEncodedRawData final : public ImageRawData
    {
    public:
        ImageEncodedRawData(eastl::vector<uint8_t> bytes, eastl::string resolvedPath)
            : ImageRawData(Kind::Encoded)
            , m_bytes(eastl::move(bytes))
            , m_resolvedPath(eastl::move(resolvedPath))
        {}

        const eastl::vector<uint8_t>& GetBytes()        const { return m_bytes; }
        const eastl::string&          GetResolvedPath() const { return m_resolvedPath; }

    private:
        eastl::vector<uint8_t> m_bytes;
        eastl::string          m_resolvedPath;
    };

    //! One bake product, unassembled. A wrapper rather than making BakedCubemap an AssetData:
    //! AssetData deletes its copy and declares no move, so BakedEnvironment could not return
    //! its three cubes by value.
    class ImageBakedRawData final : public ImageRawData
    {
    public:
        explicit ImageBakedRawData(BakedCubemap cube)
            : ImageRawData(Kind::Baked)
            , m_cube(eastl::move(cube))
        {}

        BakedCubemap&       GetCube()       { return m_cube; }
        const BakedCubemap& GetCube() const { return m_cube; }

    private:
        BakedCubemap m_cube;
    };
}
