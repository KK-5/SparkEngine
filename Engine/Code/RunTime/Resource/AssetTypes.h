#pragma once

#include <EASTL/string.h>

#include <Base.h>
#include <EASTLEX/hash.h>


namespace Spark::Resource
{
    using AssetHash = ObjectName::Hash;

    class AssetId
    {
    public:
        AssetId() = default;

        /// 用资源路径构造，hash 自动从 name 生成
        explicit AssetId(eastl::string_view name)
            : m_name(name)
            , m_hash(m_name.GetHash())
        {}

        /// 用资源路径 + 子资产 hash 构造
        AssetId(eastl::string_view name, AssetHash subId)
            : m_name(name)
            , m_hash(HashCombine(m_name.GetHash(), subId))
        {}

        /// 用资源路径 + 子资产 名称/路径 构造
        AssetId(eastl::string_view name, eastl::string_view subName)
            : m_name(name)
            , m_hash(HashCombine(m_name.GetHash(), ObjectName(subName).GetHash()))
        {}

        bool operator==(const AssetId& other) const { return m_hash == other.m_hash; }
        bool operator!=(const AssetId& other) const { return m_hash != other.m_hash; }
        bool operator<(const AssetId& other) const  { return m_hash < other.m_hash; }

        AssetHash GetHash() const          { return m_hash; }
        const ObjectName& GetName() const  { return m_name; }
        bool IsValid() const               { return !m_name.IsEmpty(); }

    private:
        static AssetHash HashCombine(AssetHash a, AssetHash b)
        {
            size_t hashCombined = static_cast<size_t>(a);
            eastl::hash_combine_raw(hashCombined, static_cast<size_t>(b));
            return static_cast<AssetHash>(hashCombined);
        }

        ObjectName m_name;
        AssetHash m_hash{0};
    };

    enum class AssetStatus : uint32_t
    {
        NotLoaded,          ///< Asset has not been loaded, and is not in the process of loading.
        Queued,             ///< Asset has a job created for loading it which has not begun processing.
        Loading,            ///< Asset is currently in the process of loading.
        Compiling,          ///< Asset is compiling for use.
        Ready,              ///< Asset is loaded and ready for use.
        Error,              ///< Asset attempted to load, but it or a strict dependency failed.
    };

    enum class AssetType : uint32_t
    {
        Shader,
    };
}

namespace eastl
{
    template<>
    struct hash<Spark::Resource::AssetId>
    {
        size_t operator()(const Spark::Resource::AssetId& id) const
        {
            return static_cast<size_t>(id.GetHash());
        }
    };
}
