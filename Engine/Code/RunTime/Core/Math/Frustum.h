#pragma once

#include "AABB.h"
#include "MathUtils.h"
#include "Matrix4x4.h"
#include "Vector3.h"
#include "Vector4.h"

namespace Spark::Math
{
    /// View frustum as six inward-facing planes: (a,b,c,d) with ax+by+cz+d >= 0 inside.
    struct Frustum
    {
        Vector4 planes[6];   // left, right, bottom, top, near, far

        //! LH_ZO ONLY. Clip z runs 0..w rather than -w..w, so the near plane is row 2 alone
        //! and not row3 + row2.
        static Frustum FromViewProjection(const Matrix4X4& viewProj)
        {
            const Vector4 r0 = Row(viewProj, 0);
            const Vector4 r1 = Row(viewProj, 1);
            const Vector4 r2 = Row(viewProj, 2);
            const Vector4 r3 = Row(viewProj, 3);

            Frustum f;
            f.planes[0] = r3 + r0;
            f.planes[1] = r3 - r0;
            f.planes[2] = r3 + r1;
            f.planes[3] = r3 - r1;
            f.planes[4] = r2;
            f.planes[5] = r3 - r2;

            // Makes d a metric distance, which the volume tests compare against a radius.
            for (Vector4& p : f.planes)
            {
                const float len = Length(Vector3(p));
                if (len > 0.0f)
                {
                    p /= len;
                }
            }
            return f;
        }

        bool IntersectsSphere(const Vector3& center, float radius) const
        {
            for (const Vector4& plane : planes)
            {
                if (Dot(Vector3(plane), center) + plane.w < -radius)
                {
                    return false;
                }
            }
            return true;
        }

        //! Dot(Abs(n), extents) is the box's support radius along n — the role radius plays
        //! in the sphere test. Testing the eight corners instead would answer containment,
        //! not intersection.
        bool IntersectsAABB(const AABB& box) const
        {
            const Vector3 center  = box.Center();
            const Vector3 extents = box.Extents();

            for (const Vector4& plane : planes)
            {
                const Vector3 n = Vector3(plane);
                if (Dot(n, center) + Dot(Abs(n), extents) + plane.w < 0.0f)
                {
                    return false;
                }
            }
            return true;
        }
    };
}
