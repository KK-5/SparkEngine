#pragma once

#include <EASTL/functional.h>
#include <EASTL/string.h>

#include <Base.h>
#include <EBus/EBus.h>

#include <Resource/Asset.h>

namespace Editor
{
    //! Which question the dialog asks. Save takes a name nothing else holds; Open only
    //! takes a file that is there.
    enum class FileDialogMode
    {
        Save,
        Open,
    };

    //! Everything the dialog needs. It knows nothing about what is being picked: either
    //! `m_asset` is handed to AssetManager::SaveAsset as it is, or `m_onConfirm` does
    //! whatever the caller wanted with the path.
    struct FileDialogRequest
    {
        FileDialogMode m_mode = FileDialogMode::Save;

        Spark::Ptr<Spark::Resource::Asset> m_asset;

        //! Called with the chosen virtual path. False keeps the dialog open -- the caller
        //! reports why itself. Ignored when `m_asset` is set.
        eastl::function<bool(const eastl::string& path)> m_onConfirm;

        eastl::string m_title;         ///< heading, e.g. "Save Material As"
        eastl::string m_subtitle;      ///< beside the heading, e.g. "Scene"
        eastl::string m_extension;     ///< with the dot; fixed, shown beside the name field
        eastl::string m_defaultDir;    ///< virtual; the mount root when the caller has none
        eastl::string m_defaultName;   ///< without the extension
        eastl::string m_confirmLabel;  ///< the accent button; "Save" when empty
    };

    struct FileDialogEvents : public Spark::EBusTraits
    {
        //! One dialog, so connecting a second one asserts rather than picking a winner.
        static const Spark::EBusHandlerPolicy HandlerPolicy = Spark::EBusHandlerPolicy::Single;
        static const Spark::EBusAddressPolicy AddressPolicy = Spark::EBusAddressPolicy::Single;

        //! A command, hence the imperative name. Nothing comes back: a saved asset is
        //! announced on AssetBus, and anything else is the request's own callback.
        virtual void OpenFileDialog(const FileDialogRequest& request) {}
    };

    using FileDialogBus = Spark::EBus<FileDialogEvents>;
}
