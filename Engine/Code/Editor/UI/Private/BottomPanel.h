#pragma once

namespace Editor
{
    class BottomPanel final
    {
    public:
        void Draw();

        enum class Tab
        {
            CONSILE,
            ASSETS
        };

    private:
        void DrawConsole();
        void DrawAssets();

        Tab currentTab = Tab::CONSILE;
    };
}