#pragma once

namespace Spark::UI
{
    struct FloatElement
    {
        float min = 0.0f;
        float max = 1.0f;
        float speed = 0.01f;
        const char* format = "%.3f";
    };

    struct FloatSliderElement
    {
        float min = 0.0f;
        float max = 1.0f;
        float speed = 0.01f;
        const char* format = "%.3f";
    };

    struct IntElement
    {
        int min = 0;
        int max = 10;
        int speed = 1;
    };

    struct IntSliderElement
    {
        int min = 0;
        int max = 10;
        int speed = 1;
    };

    struct BoolElement
    {
    };

    struct EditTextElement
    {
        size_t maxLength = 256;
        bool multiLine = false;
    };

    struct ReadonlyTextElement
    {
        size_t maxLength = 256;
        bool multiLine = false;
    };

    struct Vec2Element
    {
        float speed = 0.1f;
        const char* format = "%.2f";
    };

    struct Vec3Element
    {
        float speed = 0.1f;
        const char* format = "%.2f";
    };
    
    struct ColorElement
    {
    };

    struct FileElement
    {
    }; 

    struct EnumElement
    {
    };
    
}


