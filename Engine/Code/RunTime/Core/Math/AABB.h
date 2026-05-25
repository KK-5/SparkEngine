#pragma once

#include "Vector3.h"

namespace Spark::Math
{
#ifdef MATH_BACKEND_GLM
    /// Axis-aligned bounding box
    struct AABB
    {
        Vector3 min{0.0f};
        Vector3 max{0.0f};

        void Expand(const Vector3& p)
        {
            min = Min(min, p);
            max = Max(max, p);
        }

        Vector3 Center() const  { return (min + max) * 0.5f; }
        Vector3 Extents() const { return (max - min) * 0.5f; }
    };
#endif
}
