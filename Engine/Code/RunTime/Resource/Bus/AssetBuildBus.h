#pragma once

#include <EASTL/string_view.h>
#include <EASTL/vector.h>

#include <EBus/EBus.h>

#include <Base.h>

#include <Resource/Asset.h>
#include <Resource/AssetTypes.h>


namespace Spark::Resource
{
    class AssetBuildContext;

    struct AssetBuildEvents : public EBusTraits
    {
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Single;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ById;

        using BusIdType = AssetType;

        //! The main thread (LoadAsset, RegisterFile) and the asset worker dispatch this at
        //! once. Builders connect before the worker starts and disconnect after it joins,
        //! which is the condition this asks for.
        static constexpr bool LocklessDispatch = true;

        virtual Ptr<Asset> CreateAsset(const AssetId& id) = 0;
        virtual void       Load(AssetBuildContext& ctx) = 0;
        virtual void       Compile(AssetBuildContext& ctx) = 0;

        //! A type's format, both halves. No context and no id on purpose -- a format must
        //! not reach the database or the file system. `identity` is the cook cache's, and
        //! opaque: store it verbatim, hand back what comes out. It is what catches a key
        //! collision before one asset gets another's payload.
        //!
        //! Two users: AssetCache writes an entry, SaveAsset writes a source file. A type
        //! that declines the cache can still have a format.

        //! Empty = declined, nothing is written.
        virtual eastl::vector<uint8_t> Serialize(const AssetData& compiled, eastl::string_view identity)
        {
            return {};
        }

        //! Refuse a source write, or do what has to happen before it. The data is mutable
        //! for sub-asset extraction, which will rewrite the ids it saved; writing those
        //! sibling files needs a way in that does not exist yet, and will be a parameter
        //! here when it does.
        //!
        //! False = refused, nothing is written; the reason goes in the log.
        virtual bool PrepareToSave(AssetData& data, eastl::string_view virtualPath)
        {
            return true;
        }

        //! Null = rejected; the caller rebuilds, which overwrites the entry.
        virtual UniquePtr<AssetData> Deserialize(const uint8_t* bytes, size_t size, eastl::string_view identity)
        {
            return nullptr;
        }
    };

    using AssetBuildBus = EBus<AssetBuildEvents>;
}
