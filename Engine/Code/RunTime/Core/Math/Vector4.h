#pragma once

#include <glm/vec4.hpp>

namespace Spark::Math
{
#ifdef MATH_BACKEND_GLM
    using Vector4 = glm::vec4;
#endif

    namespace Vector4Const
    {
        static const Vector4 ZERO(0.0f, 0.0f, 0.0f, 0.0f);
        static const Vector4 ONE(1.0f, 1.0f, 1.0f, 1.0f);
        static const Vector4 UNIT_X(1.0f, 0.0f, 0.0f, 0.0f);
        static const Vector4 UNIT_Y(0.0f, 1.0f, 0.0f, 0.0f);
        static const Vector4 UNIT_Z(0.0f, 0.0f, 1.0f, 0.0f);
        static const Vector4 UNIT_W(0.0f, 0.0f, 0.0f, 1.0f);
    }
}