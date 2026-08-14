#pragma once

#include "MathUtils.h"
#include "Vector3.h"

namespace Spark::Math
{
    /// Bounding sphere.
    struct Sphere
    {
        Vector3 center{0.0f};
        float   radius = 0.0f;

        //! Smallest sphere enclosing a right circular cone. axis must be unit length.
        //! The branch is the 45 degree case: a base wider than the cone is tall has a
        //! circumsphere that already reaches the apex.
        static Sphere FromCone(const Vector3& apex, const Vector3& axis, float height, float halfAngle)
        {
            if (height <= 0.0f)
            {
                return Sphere{ apex, 0.0f };
            }

            const float baseRadius = height * Tan(halfAngle);
            if (baseRadius >= height)
            {
                return Sphere{ apex + axis * height, baseRadius };
            }

            const float d = (height * height + baseRadius * baseRadius) / (2.0f * height);
            return Sphere{ apex + axis * d, d };
        }
    };
}
