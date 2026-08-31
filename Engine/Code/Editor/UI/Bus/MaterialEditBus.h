#pragma once

#include <EBus/EBus.h>

#include <Material/MaterialHandle.h>

namespace Editor
{
    struct MaterialEditEvents : public Spark::EBusTraits
    {
        //! One material window, so one handler.
        static const Spark::EBusHandlerPolicy HandlerPolicy = Spark::EBusHandlerPolicy::Single;
        static const Spark::EBusAddressPolicy AddressPolicy = Spark::EBusAddressPolicy::Single;

        //! Show this material in the material editor window, opening it if it is closed.
        //! A command rather than a notification, hence the imperative name -- compare
        //! AssetEditBus::OnAssetDragToComponent, which reports that something happened.
        //! Named for the window, not the material: what it opens is a piece of UI.
        //!
        //! Sent from the material slot's Open button in Component View -- the main entry,
        //! because a material a model brought in (`Chair.glb:material/0`) is a sub-asset and
        //! cannot be selected in the Browser at all -- and from double-clicking a `.smat`.
        virtual void OpenMaterialEditor(Spark::Material::MaterialHandle handle) {}
    };

    using MaterialEditBus = Spark::EBus<MaterialEditEvents>;
}
