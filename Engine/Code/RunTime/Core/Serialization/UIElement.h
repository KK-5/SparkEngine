#pragma once

#include <cstdint>

#include "EASTL/string.h"

namespace Spark
{
    struct FloatElement
    {
        FloatElement() = default;
        FloatElement(float _min, float _max, float _speed, bool _readOnly = false)
            : min(_min), max(_max), speed(_speed), format("%.3f"), readOnly(_readOnly)
        {}

        FloatElement(float _min, float _max, float _speed, const char* _format, bool _readOnly = false)
            : min(_min), max(_max), speed(_speed), format(_format), readOnly(_readOnly)
        {}

        float min = 0.0f;
        float max = 1.0f;
        float speed = 0.1f;
        eastl::string format = "%.3f";
        bool          readOnly = false;
    };

    struct FloatSliderElement
    {
        FloatSliderElement() = default;
        FloatSliderElement(float _min, float _max, float _speed, bool _readOnly = false)
            : min(_min), max(_max), speed(_speed), format("%.3f"), readOnly(_readOnly)
        {}
        FloatSliderElement(float _min, float _max, float _speed, const char* _format, bool _readOnly = false)
            : min(_min), max(_max), speed(_speed), format(_format), readOnly(_readOnly)
        {}

        float min = 0.0f;
        float max = 1.0f;
        float speed = 0.1f;
        eastl::string format = "%.3f";
        bool          readOnly = false;
    };

    struct IntElement
    {
        IntElement() = default;
        IntElement(int _min, int _max, float _speed, bool _readOnly = false)
            : min(_min), max(_max), speed(_speed), readOnly(_readOnly)
        {}

        int min = 0;
        int max = 10;
        float speed = 1.f;
        bool  readOnly = false;
    };

    struct IntSliderElement
    {
        IntSliderElement() = default;
        IntSliderElement(int _min, int _max, bool _readOnly = false)
            : min(_min), max(_max), readOnly(_readOnly)
        {}

        int min = 0;
        int max = 10;
        bool readOnly = false;
    };

    struct UIntElement
    {
        UIntElement() = default;
        UIntElement(uint32_t _min, uint32_t _max, float _speed, bool _readOnly = false)
            : min(_min), max(_max), speed(_speed), readOnly(_readOnly)
        {}

        uint32_t min = 0;
        uint32_t max = 10;
        float speed = 1.f;
        bool  readOnly = false;
    };

    struct UIntSliderElement
    {
        UIntSliderElement() = default;
        UIntSliderElement(uint32_t _min, uint32_t _max, bool _readOnly = false)
            : min(_min), max(_max), readOnly(_readOnly)
        {}

        uint32_t min = 0;
        uint32_t max = 10;
        bool readOnly = false;
    };

    struct BoolElement
    {
        BoolElement() = default;
        explicit BoolElement(bool _readOnly) : readOnly(_readOnly) {}
        bool readOnly = false;
    };

    struct EditTextElement
    {
        EditTextElement() = default;
        EditTextElement(size_t _maxLength, bool _readOnly = false)
            : maxLength(_maxLength), readOnly(_readOnly)
        {}

        size_t maxLength = 256;
        bool   readOnly = false;
    };

    struct ReadonlyTextElement
    {
        ReadonlyTextElement() = default;
        ReadonlyTextElement(size_t _maxLength, bool _readOnly = false)
            : maxLength(_maxLength), readOnly(_readOnly)
        {}

        size_t maxLength = 256;
        bool   readOnly = false;
    };

    struct Vec2Element
    {
        Vec2Element() = default;
        Vec2Element(float _min, float _max, float _speed, bool _readOnly = false)
            : min(_min), max(_max), speed(_speed), format("%.2f"), readOnly(_readOnly)
        {}

        float min = 0.0f;
        float max = 1.0f;
        float speed = 0.1f;
        eastl::string format = "%.2f";
        bool          readOnly = false;
    };

    struct Vec3Element
    {
        Vec3Element() = default;
        Vec3Element(float _min, float _max, float _speed, bool _readOnly = false)
            : min(_min), max(_max), speed(_speed), format("%.2f"), readOnly(_readOnly)
        {}

        float min = 0.0f;
        float max = 1.0f;
        float speed = 0.1f;
        eastl::string format = "%.2f";
        bool          readOnly = false;
    };

    struct ColorElement
    {
        ColorElement() = default;
        explicit ColorElement(bool _readOnly) : readOnly(_readOnly) {}
        bool readOnly = false;
    };

    struct AssetElement
    {
        AssetElement() = default;
        explicit AssetElement(bool _readOnly, uint32_t _expectType = 0)
            : readOnly(_readOnly), expectType(_expectType) {}
        bool     readOnly = false;
        //! Accepted asset type for drag-drop, as static_cast<uint32_t>(Resource::AssetType).
        //! 0 == accept any. Kept as a raw uint to avoid a Core→Resource dependency.
        uint32_t expectType = 0;
    };

    struct TextureElement
    {
        TextureElement() = default;
        explicit TextureElement(bool _readOnly, uint32_t _usageHint = 0)
            : readOnly(_readOnly), usageHint(_usageHint) {}
        bool     readOnly = false;
        //! static_cast<uint32_t>(Resource::ImageUsage). 0 == Texture2D (sRGB), the default.
        uint32_t usageHint = 0;
    };

    struct EnumElement
    {
        EnumElement() = default;
        explicit EnumElement(bool _readOnly) : readOnly(_readOnly) {}
        bool readOnly = false;
    };

    //! Marks a field that holds a reference (handle) to another reflected object,
    //! rendered by following the reference and expanding the target's reflected
    //! fields inline — as opposed to AssetElement, which only shows a reference slot.
    //! First user: a MaterialComponent's MaterialHandle, expanded to the referenced
    //! material's StandardPBR. Kept as a bare marker (no handle type) to avoid a
    //! Core -> Material dependency; the editor branch knows how to resolve it.
    struct MaterialRefElement
    {
        MaterialRefElement() = default;
        explicit MaterialRefElement(bool _readOnly) : readOnly(_readOnly) {}
        bool readOnly = false;
    };

}
