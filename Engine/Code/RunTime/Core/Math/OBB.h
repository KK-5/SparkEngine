#pragma once

#include "AABB.h"
#include "Matrix3X3.h"
#include "Matrix4X4.h"
#include "Vector4.h"

namespace Spark::Math
{
#ifdef MATH_BACKEND_GLM
    /// Oriented bounding box
    struct OBB
    {
        Vector3    center;
        Vector3    extents;
        Matrix4X4  orientation;

        static OBB FromAABB(const AABB& aabb, const Matrix4X4& transform)
        {
            OBB obb;
            obb.center      = transform * Vector4(aabb.Center(), 1.0f);
            obb.extents     = aabb.Extents();
            obb.orientation = Matrix4X4(Matrix3X3(transform));
            return obb;
        }
    };
#endif
}
