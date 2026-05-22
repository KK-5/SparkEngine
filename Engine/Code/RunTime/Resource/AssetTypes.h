#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>

#include <Base.h>
#include <EASTLEX/hash.h>
#include <Object/ObjectName.h>


namespace Spark::Resource
{
    using AssetHash = ObjectName::Hash;

    class AssetId
    {
    public:
        AssetId() = default;

        /// 用资源路径构造，hash 自动从 path 生成
        explicit AssetId(eastl::string_view path)
            : m_path(path.data(), path.size())
            , m_hash(ComputeHash(m_path))
        {}

        /// 用资源路径 + 子资产 hash 构造
        AssetId(eastl::string_view path, AssetHash subId)
            : m_path(path.data(), path.size())
            , m_hash(HashCombine(ComputeHash(m_path), subId))
        {}

        /// 用资源路径 + 子资产 名称/路径 构造
        AssetId(eastl::string_view path, eastl::string_view subName)
            : m_path(path.data(), path.size())
        {
            const eastl::string subTmp(subName.data(), subName.size());
            m_hash = HashCombine(ComputeHash(m_path), ComputeHash(subTmp));
        }

        bool operator==(const AssetId& other) const { return m_hash == other.m_hash; }
        bool operator!=(const AssetId& other) const { return m_hash != other.m_hash; }
        bool operator<(const AssetId& other) const  { return m_hash < other.m_hash; }

        AssetHash GetHash() const               { return m_hash; }
        const eastl::string& GetPath() const    { return m_path; }
        bool IsValid() const                    { return !m_path.empty(); }

    private:
        static AssetHash ComputeHash(const eastl::string& s)
        {
            if (s.empty()) { return 0; }
            return HashString(s.c_str()).value();
        }

        static AssetHash HashCombine(AssetHash a, AssetHash b)
        {
            size_t hashCombined = static_cast<size_t>(a);
            eastl::hash_combine_raw(hashCombined, static_cast<size_t>(b));
            return static_cast<AssetHash>(hashCombined);
        }

        eastl::string m_path;
        AssetHash     m_hash{0};
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
        Image,
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
