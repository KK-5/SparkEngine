#include "ShaderSemantic.h"

#include <EASTL/functional.h>

namespace Spark::RHI
{
    explicit ShaderSemantic::ShaderSemantic(const ObjectName& name, size_t index)
        :m_name(name), m_index(index)
    {}

    explicit ShaderSemantic::ShaderSemantic(eastl::string_view name, size_t index)
        : ShaderSemantic{ ObjectName{name}, index }
    {}

    bool ShaderSemantic::operator==(const ShaderSemantic& rhs) const
    {
        return this->m_index == rhs.m_index && this->m_name == rhs.m_name;
    }

    size_t ShaderSemantic::GetHash() const
    {
        size_t h1 = m_name.GetHash();
        size_t h2 = eastl::hash<uint32_t>()(m_index);

        size_t combinedHash = h1;
        combinedHash ^= h2 + 0x9e3779b9 + (combinedHash << 6) + (combinedHash >> 2);
        return combinedHash;
    }

    eastl::string ShaderSemantic::ToString() const
    {
        m_name.GetCStr() + eastl::to_string(m_index);
    }
}