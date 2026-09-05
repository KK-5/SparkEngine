#pragma once

#include <EASTL/string.h>

#include <EBus/EBus.h>

namespace Editor
{
    //! Everything the dialog needs to ask its question. It knows nothing about what is
    //! being saved -- title, extension, directory and name is all its callers (Save As,
    //! New, the Browser's create later) have in common.
    struct SaveAssetRequest
    {
        eastl::string m_title;         ///< heading, e.g. "Save Material As"
        eastl::string m_extension;     ///< with the dot; fixed, shown beside the name field
        eastl::string m_defaultDir;    ///< virtual; the mount root when the caller has none
        eastl::string m_defaultName;   ///< without the extension
    };

    struct SaveAssetDialogEvents : public Spark::EBusTraits
    {
        static const Spark::EBusHandlerPolicy HandlerPolicy = Spark::EBusHandlerPolicy::Multiple;
        static const Spark::EBusAddressPolicy AddressPolicy = Spark::EBusAddressPolicy::Single;

        //! A command to the one dialog, hence the imperative name -- compare the
        //! notification below.
        virtual void OpenSaveAssetDialog(const SaveAssetRequest& request) {}

        //! The user said go. `virtualPath` carries the extension and names a place that
        //! can be written; writing there is the requester's business.
        //!
        //! Cancelling sends nothing -- that is why this is a confirmation and not "a path
        //! was chosen": nothing was going to happen, and no requester holds state that
        //! would need unwinding. Nor is there a requester id: the dialog is modal, so
        //! exactly one caller can be waiting. A second one only becomes possible once
        //! saving moves out of the requester, and this event goes away with it.
        virtual void OnSaveAssetConfirmed(eastl::string virtualPath) {}
    };

    using SaveAssetDialogBus = Spark::EBus<SaveAssetDialogEvents>;
}
