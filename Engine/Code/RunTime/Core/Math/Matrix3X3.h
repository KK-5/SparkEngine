#pragma once

#include <glm/mat3x3.hpp>

namespace Spark::Math
{
#ifdef MATH_BACKEND_GLM
    using Matrix3X3 = glm::mat3x3;
#endif

    namespace Matrix3X3Const
    {
        static const Matrix3X3 IDENTITY(1.0f);
    }
}