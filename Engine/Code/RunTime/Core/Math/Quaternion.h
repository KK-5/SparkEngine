#pragma once

#include <glm/ext/quaternion_float.hpp>

namespace Spark::Math
{
#ifdef MATH_BACKEND_GLM
    using Quaternion = glm::quat;
#endif

    namespace QuaternionConst
    {
      static const Quaternion IDENTITY(1.0f, 0.0f, 0.0f, 0.0f);
    }
}