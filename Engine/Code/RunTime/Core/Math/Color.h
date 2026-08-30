#pragma once

#include <glm/vec4.hpp>

namespace Spark::Math
{
#ifdef MATH_BACKEND_GLM
    //! A colour, and a type of its own rather than an alias for Vector4. Reflection is
    //! keyed by C++ type, so an alias could only ever carry Vector4's field names: a
    //! colour would spell itself x/y/z/w in every file it reaches. Deriving keeps all of
    //! glm's arithmetic and converts to vec4 implicitly, so the two remain interchangeable
    //! everywhere except where a name is written down.
    struct Color : glm::vec4
    {
        using glm::vec4::vec4;

        //! glm's converting constructors cannot be told apart when handed a plain vec4, so
        //! the exact-match one is spelled out. Declaring any constructor costs the implicit
        //! default one, hence the defaulted sibling.
        constexpr Color() = default;
        constexpr Color(const glm::vec4& value) : glm::vec4(value) {}
    };
#endif

    namespace ColorConst
    {
        static const Color WHITE(1.0f, 1.0f, 1.0f, 1.0f);
        static const Color BLACK(0.0f, 0.0f, 0.0f, 1.0f);
        static const Color RED(1.0f, 0.0f, 0.0f, 1.0f);
        static const Color GREEN(0.0f, 1.0f, 0.0f, 1.0f);
        static const Color BLUE(0.0f, 0.0f, 1.0f, 1.0f);
        static const Color YELLOW(1.0f, 1.0f, 0.0f, 1.0f);
        static const Color CYAN(0.0f, 1.0f, 1.0f, 1.0f);
        static const Color MAGENTA(1.0f, 0.0f, 1.0f, 1.0f);

        //! Not TRANSPARENT: wingdi.h defines that as a macro, and this header now reaches
        //! translation units that include Windows.h.
        static const Color TRANSPARENT_BLACK(0.0f, 0.0f, 0.0f, 0.0f);
    }
}
