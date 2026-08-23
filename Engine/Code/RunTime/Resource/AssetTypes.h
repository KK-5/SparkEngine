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

    enum class AssetType : uint32_t
    {
        Unknown,
        Shader,
        Image,
        Model,
    };

    /// Per-asset-type compile-time configuration (compression, mip count, ...).
    /// Concrete derived types provide field storage and implement Hash() over
    /// those fields. Equals is hash-based: two descriptors are equal iff their
    /// hashes are equal. In practice an AssetId never carries cross-type
    /// descriptors against the same path, so cross-type hash collisions cannot
    /// reach Equals.
    class AssetDescriptor : public Object
    {
    public:
        AssetDescriptor() = default;
        ~AssetDescriptor() override = default;

        // Object holds a non-copyable intrusive refcount, which would otherwise delete
        // every descriptor's copy ctor and break AssetId::Of<T>(path, desc). Copying a
        // descriptor yields a fresh, unreferenced object (the clone gets its own count),
        // which is exactly what Of wants when it clones a caller's stack descriptor.
        AssetDescriptor(const AssetDescriptor&) : Object() {}
        AssetDescriptor& operator=(const AssetDescriptor&) { return *this; }

        virtual AssetHash Hash() const = 0;

        bool Equals(const AssetDescriptor& other) const { return Hash() == other.Hash(); }
    };

    //! Fires when a path is not of the form `mount://relative`, or when a non-empty path
    //! carries no asset type. Either way the id resolves to nothing, and would otherwise
    //! only surface as a load failure far from the call site that made it. Defined out of
    //! line to keep the log headers out of everything that includes this one.
    void ValidateAssetId(const eastl::string& path, AssetType type);

    class AssetId;

    //! Fires when a caller asks for an id as one asset type while the id names another.
    void ValidateAssetType(const AssetId& id, AssetType expected);

    class AssetId
    {
    public:
        AssetId() = default;

        /// Build an AssetId for asset type T with T's default descriptor.
        /// Prefer MakeAssetId() on AssetManager unless you can guarantee the path
        /// is canonical (full, unique across search paths). This factory does NOT
        /// validate existence or resolve the path against search paths.
        template<typename T>
        static AssetId Of(eastl::string_view path)
        {
            return AssetId(path, {}, T::GetAssetTypeStatic(), T::DefaultDescriptor());
        }

        /// Build an AssetId for asset type T with a caller-supplied descriptor.
        /// Same caveats as above: caller must ensure path is valid and unique.
        template<typename T>
        static AssetId Of(eastl::string_view path, const typename T::Descriptor& desc)
        {
            return AssetId(path, {}, T::GetAssetTypeStatic(),
                           Ptr<AssetDescriptor>(new typename T::Descriptor(desc)));
        }

        /// Build a sub-asset AssetId. Path is the parent's path; subLabel identifies
        /// the sub-object within (UE style: "parent.gltf:image/3").
        template<typename T>
        static AssetId OfSub(eastl::string_view parentPath, eastl::string_view subLabel)
        {
            return AssetId(parentPath, subLabel, T::GetAssetTypeStatic(), T::DefaultDescriptor());
        }

        template<typename T>
        static AssetId OfSub(eastl::string_view parentPath, eastl::string_view subLabel,
                             const typename T::Descriptor& desc)
        {
            return AssetId(parentPath, subLabel, T::GetAssetTypeStatic(),
                           Ptr<AssetDescriptor>(new typename T::Descriptor(desc)));
        }

        /// The one factory that takes the type as a value rather than as T: for the
        /// deserialization path, where the type is read out of the file, and for callers
        /// holding an already-prepared descriptor Ptr (ImageAsset::DescriptorForUsage).
        /// Every other path goes through Of<T>/OfSub<T> -- there is no way to build an
        /// AssetId without naming its type.
        static AssetId Of(eastl::string_view path, eastl::string_view subLabel,
                          AssetType type, Ptr<AssetDescriptor> descriptor)
        {
            return AssetId(path, subLabel, type, eastl::move(descriptor));
        }

        bool operator==(const AssetId& other) const { return m_hash == other.m_hash; }
        bool operator!=(const AssetId& other) const { return m_hash != other.m_hash; }
        bool operator<(const AssetId& other) const  { return m_hash < other.m_hash; }

        AssetHash              GetHash() const        { return m_hash; }
        const eastl::string&   GetPath() const        { return m_path; }
        const eastl::string&   GetSubLabel() const    { return m_subLabel; }
        AssetType              GetAssetType() const   { return m_type; }
        bool                   IsSubAsset() const     { return !m_subLabel.empty(); }
        const AssetDescriptor* GetDescriptor() const  { return m_descriptor.get(); }
        bool                   IsValid() const        { return !m_path.empty()
                                                           && m_type != AssetType::Unknown
                                                           && m_hash != 0; }

        /// Returns a new id with the same path, sub-label and type but a different
        /// descriptor. The descriptor is part of the identity hash, so this yields a
        /// distinct asset (e.g. the same image file re-tagged as a linear normal map).
        /// Used when a drop target field dictates which usage-variant of a texture to bind.
        AssetId WithDescriptor(Ptr<AssetDescriptor> descriptor) const
        {
            return AssetId(eastl::string_view(m_path.c_str(), m_path.size()),
                           eastl::string_view(m_subLabel.c_str(), m_subLabel.size()),
                           m_type,
                           eastl::move(descriptor));
        }

    private:
        AssetId(eastl::string_view path, eastl::string_view subLabel, AssetType type,
                Ptr<AssetDescriptor> descriptor)
            : m_path(path.data(), path.size())
            , m_subLabel(subLabel.data(), subLabel.size())
            , m_descriptor(eastl::move(descriptor))
            , m_type(type)
            , m_hash(ComputeHash(m_path, m_subLabel, m_type, m_descriptor.get()))
        {
            ValidateAssetId(m_path, m_type);
        }

        static AssetHash ComputeHash(const eastl::string& path, const eastl::string& subLabel,
                                     AssetType type, const AssetDescriptor* desc)
        {
            if (path.empty())
            {
                return 0;
            }
            size_t combined = static_cast<size_t>(HashString(path.c_str()).value());
            eastl::hash_combine_raw(combined, static_cast<size_t>(type));
            if (!subLabel.empty())
            {
                eastl::hash_combine_raw(combined, static_cast<size_t>(HashString(subLabel.c_str()).value()));
            }
            if (desc)
            {
                eastl::hash_combine_raw(combined, static_cast<size_t>(desc->Hash()));
            }
            return static_cast<AssetHash>(combined);
        }

        eastl::string         m_path;
        eastl::string         m_subLabel;
        Ptr<AssetDescriptor>  m_descriptor;
        AssetType             m_type{AssetType::Unknown};
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
