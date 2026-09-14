#pragma once

namespace Editor
{
    float WindowButtonsWidth(bool maximizable);

    //! Minimize, maximize/restore when `maximizable`, and close, laid out to end at `right`.
    //! Screen coordinates, in the current window.
    void DrawWindowButtons(float right, float top, float height, bool maximizable);
}
