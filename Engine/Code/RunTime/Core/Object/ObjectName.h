#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>

#include <HashString/HashString.h>
#include <Reflection/RTTI.h>

// Debug builds keep the original string for log/debug.
// Shipping/Release strips the string; only the hash survives — keeps ObjectName at 8 bytes.
#if defined(NDEBUG)
    #define SPARK_OBJECT_NAME_KEEP_STRING 0
#else
    #define SPARK_OBJECT_NAME_KEEP_STRING 1
#endif

namespace Spark
{
    class ObjectName final
    {
    public:
        using Hash = HashString::hash_type;

        ObjectName() = default;

        explicit ObjectName(eastl::string_view name)
            : m_hash(ComputeHash(name))
#if SPARK_OBJECT_NAME_KEEP_STRING
            , m_str(name.data(), name.size())
#endif
        {}

        ObjectName(const ObjectName&)            = default;
        ObjectName(ObjectName&&)                 = default;
        ObjectName& operator=(const ObjectName&) = default;
        ObjectName& operator=(ObjectName&&)      = default;

        ObjectName& operator=(eastl::string_view name)
        {
            m_hash = ComputeHash(name);
#if SPARK_OBJECT_NAME_KEEP_STRING
            m_str.assign(name.data(), name.size());
#endif
            return *this;
        }

        Hash GetHash() const { return m_hash; }

        bool operator==(const ObjectName& other) const { return m_hash == other.m_hash; }
        bool operator!=(const ObjectName& other) const { return m_hash != other.m_hash; }

        eastl::string_view GetStringView() const
        {
#if SPARK_OBJECT_NAME_KEEP_STRING
            return m_str;
#else
            return "[hashed]";
#endif
        }

        const char* GetCStr() const
        {
            if (IsEmpty()) { return "[unnamed]"; }
#if SPARK_OBJECT_NAME_KEEP_STRING
            return m_str.c_str();
#else
            return "[hashed]";
#endif
        }

        bool IsEmpty() const { return m_hash == 0; }

    private:
        static Hash ComputeHash(eastl::string_view name)
        {
            // entt::hashed_string expects a null-terminated C string; string_view::data() is not
            // guaranteed null-terminated (e.g. when sliced from a larger buffer), so copy once.
            if (name.empty()) { return 0; }
            const eastl::string tmp(name.data(), name.size());
            return HashString(tmp.c_str()).value();
        }

        Hash m_hash{0};
#if SPARK_OBJECT_NAME_KEEP_STRING
        eastl::string m_str;
#endif
    };
}

namespace eastl
{
    template<>
    struct hash<Spark::ObjectName>
    {
        size_t operator()(const Spark::ObjectName& name) const
        {
            return static_cast<size_t>(name.GetHash());
        }
    };
}
