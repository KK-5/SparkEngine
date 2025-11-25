#pragma once

namespace Spark
{
    struct FloatElement
    {
        FloatElement() = default;
        FloatElement(float _min, float _max, float _speed)
            : min(_min), max(_max), speed(_speed), format("%.3f")
        {}

        float min = 0.0f;
        float max = 1.0f;
        float speed = 0.01f;
        const char* format = "%.3f";
    };

    struct FloatSliderElement
    {
        FloatSliderElement() = default;
        FloatSliderElement(float _min, float _max, float _speed)
            : min(_min), max(_max), speed(_speed), format("%.3f")
        {}

        float min = 0.0f;
        float max = 1.0f;
        float speed = 0.01f;
        const char* format = "%.3f";
    };

    struct IntElement
    {
        IntElement() = default;
        IntElement(int _min, int _max, int _speed)
            : min(_min), max(_max), speed(_speed)
        {}

        int min = 0;
        int max = 10;
        int speed = 1;
    };

    struct IntSliderElement
    {
        IntSliderElement() = default;
        IntSliderElement(int _min, int _max, int _speed)
            : min(_min), max(_max), speed(_speed)
        {}

        int min = 0;
        int max = 10;
        int speed = 1;
    };

    struct BoolElement
    {
    };

    struct EditTextElement
    {
        EditTextElement() = default;
        EditTextElement(size_t _maxLength, bool _multiLine)
            : maxLength(_maxLength), multiLine(_multiLine)
        {}

        size_t maxLength = 256;
        bool multiLine = false;
    };

    struct ReadonlyTextElement
    {
        ReadonlyTextElement() = default;
        ReadonlyTextElement(size_t _maxLength, bool _multiLine)
            : maxLength(_maxLength), multiLine(_multiLine)
        {}

        size_t maxLength = 256;
        bool multiLine = false;
    };

    struct Vec2Element
    {
        Vec2Element() = default;
        Vec2Element(float _speed): speed(_speed), format("%.2f")
        {}

        float speed = 0.1f;
        const char* format = "%.2f";
    };

    struct Vec3Element
    {
        Vec3Element() = default;
        Vec3Element(float _speed): speed(_speed), format("%.2f")
        {}

        float speed = 0.1f;
        const char* format = "%.2f";
    };
    
    struct ColorElement
    {
    };

    struct AssetElement
    {
    }; 

    struct EnumElement
    {
    };
    
}


