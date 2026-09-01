#pragma once

namespace Editor
{
    class SceneView final
    {
    public:
        void Draw();

        //! Whether the cursor is over the rendered scene, as imgui sees it -- occlusion by
        //! any floating window included. This is what tells a mouse event meant for the
        //! camera from one meant for a panel sitting over the viewport.
        //!
        //! Answered by imgui rather than by a rectangle test, which is why the scene window
        //! takes mouse input at all (see Draw). A rectangle cannot know what is on top of it.
        bool IsHovered() const { return m_hovered; }

    private:
        bool m_hovered = false;
    };
}
