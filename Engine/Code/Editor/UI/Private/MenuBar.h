#pragma once

#include <EASTL/string.h>

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

        void OpenScene();
        void SaveScene(bool askForPath);

        Spark::Resource::AssetId m_logoId;

        //! Empty until a scene has been given a file, which is what makes Save ask for one.
        eastl::string m_scenePath;
    };
}
