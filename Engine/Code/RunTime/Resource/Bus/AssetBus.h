#pragma once

#include <EASTL/vector.h>
#include <EASTL/string.h>

#include <EBus/EBus.h>

#include <Resource/AssetTypes.h>

namespace Spark::Resource
{
    class Asset;

    // NOTE: OnAssetReady / OnAssetError are dispatched from whichever thread drove the
    // processing — the AssetManager worker (ProcessThread) or the caller of LoadAsset.
    // Handlers of those two MUST NOT perform write operations on ECS contexts
    // (WorldContext, RHIContext, etc.) — reading is unsafe too unless the owning system has
    // explicit synchronization in place. To mutate ECS state in response to them, use
    // AssetResolveBus instead, which has EnableEventQueue = true and fires handlers on the
    // main thread after ExecuteQueuedEvents().
    //
    // OnAssetSaved is the exception: saving is an editor action, so it fires on the thread
    // that called SaveAsset — the main thread — and its handlers may touch ECS.
    struct AssetBusTraits : public EBusTraits
    {
        static const EBusHandlerPolicy HandlerPolicy = EBusHandlerPolicy::Multiple;
        static const EBusAddressPolicy AddressPolicy = EBusAddressPolicy::ById;

        using BusIdType = AssetType;

        //! Both threads above can be dispatching at once. Handlers connect at editor setup
        //! and disconnect at teardown, never mid-dispatch.
        static constexpr bool LocklessDispatch = true;

        // These fire on the AssetManager worker thread.
        virtual void OnAssetReady(Asset& asset) {}
        virtual void OnAssetError(Asset& asset) {}

        //! An asset's source file was written and `id` is registered. Whoever asked for the
        //! save learns of it here, since whoever performs one does not know who wanted it.
        //! Cancelling and failing send nothing -- neither leaves anything to undo.
        virtual void OnAssetSaved(const AssetId& id) {}
    };

    using AssetBus = EBus<AssetBusTraits>;
}