#pragma once

#include <EASTL/string.h>

#include <Base.h>
#include <EBus/EBus.h>

#include <Resource/Asset.h>

namespace Editor
{
    //! Everything the dialog needs. It knows nothing about what is being saved: `m_asset`
    //! is whatever the requester wants written, held by the dialog until the user answers,
    //! and handed to AssetManager::SaveAsset as it is.
    struct SaveAssetRequest
    {
        Spark::Ptr<Spark::Resource::Asset> m_asset;

        eastl::string m_title;         ///< heading, e.g. "Save Material As"
        eastl::string m_extension;     ///< with the dot; fixed, shown beside the name field
        eastl::string m_defaultDir;    ///< virtual; the mount root when the caller has none
        eastl::string m_defaultName;   ///< without the extension
    };

    struct SaveAssetDialogEvents : public Spark::EBusTraits
    {
        //! One dialog, so connecting a second one asserts rather than picking a winner.
        static const Spark::EBusHandlerPolicy HandlerPolicy = Spark::EBusHandlerPolicy::Single;
        static const Spark::EBusAddressPolicy AddressPolicy = Spark::EBusAddressPolicy::Single;

        //! A command, hence the imperative name. Nothing comes back: the dialog saves what
        //! it was given, and whoever wanted it learns from AssetBus::OnAssetSaved.
        virtual void OpenSaveAssetDialog(const SaveAssetRequest& request) {}
    };

    using SaveAssetDialogBus = Spark::EBus<SaveAssetDialogEvents>;
}
