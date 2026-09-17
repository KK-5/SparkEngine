#pragma once

#include <imgui.h>

//! Every fixed icon the editor draws, in one table.
//!
//! A part names an icon instead of spelling a path. A misspelled path shows nothing and says
//! nothing -- the art here is a mix of `Console.svg` and `folder.svg`, so getting the case
//! wrong is easy -- while a misspelled name does not compile. The table is also the only
//! answer to "what art does the editor need", which matters the day the set is redrawn.
//!
//! Each icon is opened once here, so two parts drawing the same one share a single GPU image
//! rather than uploading it twice.
//!
//! An image whose path is only known at runtime -- a project's background art, an asset
//! thumbnail -- is not an icon. Those still go through IconManagerInterface directly.
namespace Editor::Icons
{
    enum class Icon
    {
        App,        // the logo, in the menu bar and on the welcome screen
        Folder,
        Console,
        Assets,
        Search,
        Unload,
        Loading,
        Edit,       // open the material editor from a slot
        Override,
        Revert,
        Clear,      // empty an asset field

        Count,
    };

    //! Opens every icon. Call once the icon manager exists; anything not ready by then is
    //! retried by Get.
    void Load();

    //! The icon's texture, or ImTextureID_Invalid while it is not ready. Callers have to
    //! expect the invalid one: a panel can draw before the art has arrived.
    ImTextureID Get(Icon icon);
}
