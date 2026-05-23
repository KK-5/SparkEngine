#pragma once

#include <EASTL/string.h>
#include <EASTL/string_view.h>

#include <Base.h>
#include <EASTLEX/hash.h>
#include <Object/Object.h>
#include <Object/ObjectName.h>


namespace Spark::Resource
{
    using AssetHash = ObjectName::Hash;

    /// Per-asset-type compile-time configuration (compression, mip count, ...).
    /// Concrete derived types provide field storage and implement Hash() over
    /// those fields. Equals is hash-based: two descriptors are equal iff their
    /// hashes are equal. In practice an AssetId never carries cross-type
    /// descriptors against the same path, so cross-type hash collisions cannot
    /// reach Equals.
    class AssetDescriptor : public Object
    {
    public:
        ~AssetDescriptor() override = default;

        virtual AssetHash Hash() const = 0;

        bool Equals(const AssetDescriptor& other) const { return Hash() == other.Hash(); }
    };

    class AssetId
    {
    public:
        AssetId() = default;

        /// Build an AssetId for asset type T with T's default descriptor.
        template<typename T>
        static AssetId Of(eastl::string_view path)
        {
            return AssetId(path, T::DefaultDescriptor());
        }

        /// Build an AssetId for asset type T with a caller-supplied descriptor.
        template<typename T>
        static AssetId Of(eastl::string_view path, const typename T::Descriptor& desc)
        {
            return AssetId(path, Ptr<AssetDescriptor>(new typename T::Descriptor(desc)));
        }

        bool operator==(const AssetId& other) const { return m_hash == other.m_hash; }
        bool operator!=(const AssetId& other) const { return m_hash != other.m_hash; }
        bool operator<(const AssetId& other) const  { return m_hash < other.m_hash; }

        AssetHash              GetHash() const        { return m_hash; }
        const eastl::string&   GetPath() const        { return m_path; }
        const AssetDescriptor* GetDescriptor() const  { return m_descriptor.get(); }
        bool                   IsValid() const        { return !m_path.empty(); }

    private:
        AssetId(eastl::string_view path, Ptr<AssetDescriptor> descriptor)
            : m_path(path.data(), path.size())
            , m_descriptor(eastl::move(descriptor))
            , m_hash(ComputeHash(m_path, m_descriptor.get()))
        {}

        static AssetHash ComputeHash(const eastl::string& path, const AssetDescriptor* desc)
        {
            if (path.empty())
            {
                return 0;
            }
            AssetHash pathHash = HashString(path.c_str()).value();
            if (!desc)
            {
                return pathHash;
            }
            size_t combined = static_cast<size_t>(pathHash);
            eastl::hash_combine_raw(combined, static_cast<size_t>(desc->Hash()));
            return static_cast<AssetHash>(combined);
        }

        eastl::string         m_path;
        Ptr<AssetDescriptor>  m_descriptor;
        AssetHash             m_hash{0};
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
