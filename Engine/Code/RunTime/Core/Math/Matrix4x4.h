#pragma once

#include <glm/mat4x4.hpp>

#include "Vector4.h"

namespace Spark::Math
{
#ifdef MATH_BACKEND_GLM
    using Matrix4X4 = glm::mat4x4;

    //! Row i in the mathematical sense. The only place that knows the backend's storage
    //! order — GLM is column-major, so m[c][r] and a row is a walk across columns.
    inline Vector4 Row(const Matrix4X4& m, int i) { return Vector4(m[0][i], m[1][i], m[2][i], m[3][i]); }
#endif

    namespace Matrix4X4Const
    {
        static const Matrix4X4 IDENTITY(1.0f);
    }
}