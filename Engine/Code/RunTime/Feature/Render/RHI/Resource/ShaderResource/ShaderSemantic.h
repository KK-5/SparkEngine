#pragma once

#include <EASTL/string.h>

#include <Object/ObjectName.h>

namespace Spark::RHI
{
    class ShaderSemantic
    {
    public:
        static constexpr const char UvStreamSemantic[] = "UV";

        ShaderSemantic() = default;
        explicit ShaderSemantic(const ObjectName& name, size_t index = 0);
        explicit ShaderSemantic(eastl::string_view name, size_t index = 0);

        bool operator==(const ShaderSemantic& rhs) const;

        size_t GetHash() const;

        eastl::string ToString() const;

        ObjectName m_name;
        uint32_t m_index = 0;
    };
}