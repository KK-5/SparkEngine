#pragma once

namespace Editor
{
    //! The row under the menu bar: transform tools, play / build, the viewport's own
    //! controls, and what the frame cost.
    //!
    //! Every control here holds its own state and nothing reads it yet -- the surface is
    //! built before the systems behind it exist.
    class Toolbar final
    {
    public:
        //! Height in screen pixels, so the caller can leave room for it.
        static float Height();

        void Draw();

    private:
        enum class Tool
        {
            Select,
            Move,
            Rotate,
            Scale,
        };

        enum class Space
        {
            World,
            Local,
        };

        enum class ViewMode
        {
            Lit,
            Unlit,
            Wireframe,
            Normals,
            Occlusion,
            Complexity,
        };

        //! What the viewport draws on top of the scene.
        struct Overlays
        {
            bool m_grid      = true;
            bool m_collision = false;
            bool m_icons     = true;
            bool m_stats     = false;
        };

        Tool     m_tool     = Tool::Move;
        Space    m_space    = Space::World;
        ViewMode m_viewMode = ViewMode::Lit;
        Overlays m_overlays;
        float    m_cameraSpeed = 10.f;
    };
}
