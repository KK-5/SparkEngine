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

        //! The cook cache's two format halves. No context and no id on purpose -- a format
        //! must not reach the database or the file system. `identity` is opaque: store it
        //! verbatim, hand back what comes out. It is what catches a key collision before
        //! one asset gets another's payload.

        //! Empty = declined, nothing is written.
        virtual eastl::vector<uint8_t> Serialize(const AssetData& compiled, eastl::string_view identity)
        {
            return {};
        }

        //! Null = rejected; the caller rebuilds, which overwrites the entry.
        virtual UniquePtr<AssetData> Deserialize(const uint8_t* bytes, size_t size, eastl::string_view identity)
        {
            return nullptr;
        }
    };

    using AssetBuildBus = EBus<AssetBuildEvents>;
}
