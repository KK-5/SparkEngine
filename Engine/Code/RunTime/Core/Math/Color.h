#pragma once

#include <glm/vec4.hpp>

namespace Spark::Math
{
#ifdef MATH_BACKEND_GLM
    using Color = glm::vec4;
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
        static const Color TRANSPARENT(0.0f, 0.0f, 0.0f, 0.0f);
    }
}
