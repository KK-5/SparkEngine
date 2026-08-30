#pragma once

#include <EASTL/vector.h>

#include <Resource/Asset.h>

#include "MaterialState.h"
#include "StandardPBR.h"

namespace Spark::Resource
{
    //! Both forms reach Compile through the same slot, so the raw states which it is.
    class MaterialRawData : public AssetData
    {
    public:
        enum class Kind : uint8_t
        {
            Encoded,   //!< a `.smat`'s bytes, still to be parsed
            Decoded,   //!< the values already, assembled by a parent's Compile
        };

        Kind GetKind() const { return m_kind; }

    protected:
        explicit MaterialRawData(Kind kind) : m_kind(kind) {}

    private:
        Kind m_kind;
    };

    class MaterialEncodedRawData final : public MaterialRawData
    {
    public:
        explicit MaterialEncodedRawData(eastl::vector<uint8_t> bytes)
            : MaterialRawData(Kind::Encoded)
            , m_bytes(eastl::move(bytes))
        {}

        const eastl::vector<uint8_t>& GetBytes() const { return m_bytes; }

    private:
        eastl::vector<uint8_t> m_bytes;
    };

    //! Compile still runs on it: a sub-asset is not a payload arriving finished.
    class MaterialDecodedRawData final : public MaterialRawData
    {
    public:
        MaterialDecodedRawData(StandardPBR params, MaterialState state)
            : MaterialRawData(Kind::Decoded)
            , m_params(eastl::move(params))
            , m_state(state)
        {}

        const StandardPBR&   GetParams() const { return m_params; }
        const MaterialState& GetState()  const { return m_state; }

    private:
        StandardPBR   m_params;
        MaterialState m_state;
    };
}
