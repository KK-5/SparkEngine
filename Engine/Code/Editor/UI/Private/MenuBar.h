#pragma once

#include <Resource/Asset.h>

namespace Editor
{
    class WindowChrome;

    //! The editor's top row: brand, menus, and -- once the OS title bar is gone -- the drag
    //! area and window buttons.
    class MenuBar final
    {
    public:
        void Draw(WindowChrome& chrome);

    private:
        void DrawBrand(float top, float height);
        void DrawMenus();

        Spark::Resource::AssetId m_logoId;
    };
}
