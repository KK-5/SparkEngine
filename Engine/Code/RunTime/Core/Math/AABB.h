#pragma once

#include "MathUtils.h"

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

    //! World-space AABB enclosing an affine transform of the local box.
    inline AABB TransformBounds(const AABB& bounds, const Matrix4X4& transform)
    {
        const Vector3 center(transform * Vector4(bounds.Center(), 1.0f));
        const Vector3 extents = bounds.Extents();
        const Vector3 worldExtents = Abs(Vector3(transform[0])) * extents.x
                                   + Abs(Vector3(transform[1])) * extents.y
                                   + Abs(Vector3(transform[2])) * extents.z;
        return {center - worldExtents, center + worldExtents};
    }
#endif
}
