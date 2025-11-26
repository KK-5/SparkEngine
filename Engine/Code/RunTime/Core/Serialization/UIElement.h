#pragma once

#include "EASTL/string.h"

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
        float speed = 0.1f;
        eastl::string format = "%.3f";
    };

    struct FloatSliderElement
    {
        FloatSliderElement() = default;
        FloatSliderElement(float _min, float _max, float _speed)
            : min(_min), max(_max), speed(_speed), format("%.3f")
        {}

        float min = 0.0f;
        float max = 1.0f;
        float speed = 0.1f;
        eastl::string format = "%.3f";
    };

    struct IntElement
    {
        IntElement() = default;
        IntElement(int _min, int _max, float _speed)
            : min(_min), max(_max), speed(_speed)
        {}

        int min = 0;
        int max = 10;
        float speed = 1.f;
    };

    struct IntSliderElement
    {
        IntSliderElement() = default;
        IntSliderElement(int _min, int _max)
            : min(_min), max(_max)
        {}

        int min = 0;
        int max = 10;
    };

    struct BoolElement
    {
    };

    struct EditTextElement
    {
        EditTextElement() = default;
        EditTextElement(size_t _maxLength)
            : maxLength(_maxLength)
        {}

        size_t maxLength = 256;
    };

    struct ReadonlyTextElement
    {
        ReadonlyTextElement() = default;
        ReadonlyTextElement(size_t _maxLength)
            : maxLength(_maxLength)
        {}

        size_t maxLength = 256;
    };

    struct Vec2Element
    {
        Vec2Element() = default;
        Vec2Element(float _min, float _max, float _speed): min(_min), max(_max), speed(_speed), format("%.2f")
        {}

        float min = 0.0f;
        float max = 1.0f;
        float speed = 0.1f;
        eastl::string format = "%.2f";
    };

    struct Vec3Element
    {
        Vec3Element() = default;
        Vec3Element(float _min, float _max, float _speed): min(_min), max(_max), speed(_speed), format("%.2f")
        {}

        float min = 0.0f;
        float max = 1.0f;
        float speed = 0.1f;
        eastl::string format = "%.2f";
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


