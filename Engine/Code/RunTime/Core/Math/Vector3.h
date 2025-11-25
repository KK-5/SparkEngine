#pragma once

#include <glm/vec3.hpp>

namespace Spark::Math
{
#ifdef MATH_BACKEND_GLM
    using Vector3 = glm::vec3;
#endif

    namespace Vector3Const
    {
        static const Vector3 ZERO(0.0f, 0.0f, 0.0f);
        static const Vector3 ONE(1.0f, 1.0f, 1.0f);
        static const Vector3 UNIT_X(1.0f, 0.0f, 0.0f);
        static const Vector3 UNIT_Y(0.0f, 1.0f, 0.0f);
        static const Vector3 UNIT_Z(0.0f, 0.0f, 1.0f);
        static const Vector3 UP(0.0f, 1.0f, 0.0f);
        static const Vector3 DOWN(0.0f, -1.0f, 0.0f);
        static const Vector3 LEFT(-1.0f, 0.0f, 0.0f);
        static const Vector3 RIGHT(1.0f, 0.0f, 0.0f);
        static const Vector3 FORWARD(0.0f, 0.0f, 1.0f);
        static const Vector3 BACKWARD(0.0f, 0.0f, -1.0f);
    }
}