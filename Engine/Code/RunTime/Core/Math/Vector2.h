#pragma once

#include <glm/vec2.hpp>

namespace Spark::Math
{
#ifdef MATH_BACKEND_GLM
    using Vector2 = glm::vec2;
#endif

    namespace Vector2Const
    {
        static const Vector2 ZERO(0.0f, 0.0f);
        static const Vector2 ONE(1.0f, 1.0f);
        static const Vector2 UNIT_X(1.0f, 0.0f);
        static const Vector2 UNIT_Y(0.0f, 1.0f);
    }
}