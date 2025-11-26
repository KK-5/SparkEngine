#pragma once

#include <EASTL/string.h>
#include <Math/Vector2.h>
#include <Math/Vector3.h>
#include <Math/Vector4.h>

namespace Editor
{
    enum class EnumElem
    {
        One,
        Two,
        Three
    };

    struct AllUIElement
    {
        eastl::string editString {"editString"};
        eastl::string readonlyString {"readonlyString"};
        float floatElement {10.f};
        float floatSlider {0.5f};
        int intElement {10};
        int intSlider {1};
        bool boolElement {false};
        Spark::Math::Vector2 vec2Element {0.f, 0.f};
        Spark::Math::Vector3 vec3Element {0.f, 0.f, 0.f};
        Spark::Math::Vector4 colorElement {1.f, 1.f, 1.f, 1.f};
        EnumElem enumElement {EnumElem::Three};
    };
    
}