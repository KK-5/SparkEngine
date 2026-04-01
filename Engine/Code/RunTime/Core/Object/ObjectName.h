#pragma once

#include <EASTL/string_view.h>

#include <HashString/HashString.h>
#include <Reflection/RTTI.h>

namespace Spark
{
    class ObjectName final
    {
    public:
        using Hash = HashString::hash_type;

        ObjectName() = default;

        explicit ObjectName(eastl::string_view name)
        {
            m_name = HashString(name.data());
        }

        ObjectName(const ObjectName& other) = default;
        ObjectName(ObjectName&& name) = default;
        ObjectName& operator=(const ObjectName& other) = default;
        ObjectName& operator=(ObjectName&& other) = default;

        ObjectName& operator=(eastl::string_view name)
        {
            m_name = HashString(name.data());
            return *this;
        }

        Hash GetHash() const
        {
            return m_name.value();
        }

        bool operator==(const ObjectName& other) const
        {
            return GetHash() == other.GetHash();
        }

        bool operator!=(const ObjectName& other) const
        {
            return GetHash() != other.GetHash();
        }

        eastl::string_view GetStringView() const
        {
           return m_name.data();
        }

        const char* GetCStr() const
        {
            if (IsEmpty())
            {
                return "[unnamed]";
            }
            return m_name.data();
        }

        bool IsEmpty() const
        {
            return m_name.data() == nullptr;
        }

    private:
        HashString m_name;
    };
}

namespace eastl
{
    template<>
    struct hash<Spark::ObjectName> {
        size_t operator()(const Spark::ObjectName& name) const {
            return static_cast<size_t>(name.GetHash());
        }
    };
}