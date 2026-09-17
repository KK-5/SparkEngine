#pragma once

#include <EASTL/string.h>

#include <imgui.h>

#include <Base.h>
#include <Math/Vector2.h>
#include <Resource/AssetLoadBatch.h>
#include <Resource/AssetTypes.h>

namespace Editor
{
    //! What the editor opens on: which project is loaded, and how far the asset preload has
    //! got. EditorUI draws this INSTEAD of the dockspace until it is dismissed, which is
    //! what keeps a panel from touching an asset that is still being built.
    class WindowChrome;

    class WelcomeScreen final
    {
    public:
        explicit WelcomeScreen(WindowChrome& windowChrome) : m_windowChrome(windowChrome) {}

        //! The client size the window is created at, so Editor::Init needs it before any UI
        //! exists.
        static Spark::Math::Vector2Int WindowSize();

        void Draw();

        bool IsDismissed() const { return m_dismissed; }

    private:
        //! First Draw, not editor setup: the mounts and the registry walk are done by then,
        //! and an icon upload can only be requested from a tick.
        void Start();

        //! Before the batch exists: OpenIcon loads synchronously, and an id the batch had
        //! already queued would come back not-ready.
        void LoadBackground();

        void ReadProject();
        void StartPreload();

        //! Hands the window over to the editor at its own size.
        void Dismiss();

        void DrawLeftColumn(const ImVec2& origin, const ImVec2& size);
        void DrawRightPane(const ImVec2& origin, const ImVec2& size);

        //! The drag strip across the top, and minimize / close over the image.
        void DrawCaption(const ImVec2& origin, float width);

        //! Bottom-anchored, so it takes its own top rather than the flowing cursor.
        void DrawPreload(const ImVec2& origin, float width);
        void DrawActions(const ImVec2& origin, float width, bool complete);

        WindowChrome& m_windowChrome;

        Spark::UniquePtr<Spark::Resource::AssetLoadBatch> m_batch;

        Spark::Resource::AssetId m_backgroundId;

        eastl::string m_projectName;
        eastl::string m_projectPath;

        bool m_started   = false;
        bool m_dismissed = false;
    };
}
