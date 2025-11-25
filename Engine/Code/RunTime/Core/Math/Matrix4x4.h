#pragma once

#include <glm/mat4x4.hpp>

namespace Spark::Math
{
#ifdef MATH_BACKEND_GLM
    using Matrix4X4 = glm::mat4x4;
#endif

    namespace Matrix4X4Const
    {
      static const Matrix4X4 IDENTITY(1.0f);
    }
}