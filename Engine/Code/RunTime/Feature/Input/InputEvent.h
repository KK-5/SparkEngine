#pragma once

namespace Spark::Input
{
    enum class InputDevivce
    {
        Invalid,
        Mouse,
        Keyboard
    };

    enum class InputState
    {
        Press,
        Release,
        Repeat
    };

    enum class InputMode
    {
        Invalid,
        Shift,
        Control,
        Alt
    };

    enum class MouseButton
    {
        Left,
        Right,
        Middle,
    };

    enum class Key
    {
        Invalid,
        AlphanumericA,
        AlphanumericD,
        AlphanumericE,
        AlphanumericQ,
        AlphanumericR,
        AlphanumericS,
        AlphanumericW,
        Space
    };

    struct MouseButtonEvent
    {
        MouseButton button;
        InputState  state;
        InputMode   mode;
    };

    struct MouseCursorPosEvent
    {
        float xPos, yPos;
    };

    struct MouseScrollEvent
    {
        float xOffset, yOffset;
    };

    struct KeyboardEvent
    {
        Key        button;
        InputState state;
        InputMode  mode;
    };

    struct WindowCloseEvent
    {
    };

    struct WindowResizeEvent
    {
        float width, height;
    };
}