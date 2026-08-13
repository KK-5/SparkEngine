#include <gtest/gtest.h>

#include <Math/Frustum.h>
#include <Math/MathUtils.h>

using namespace Spark;

namespace
{
    constexpr float kNear = 1.0f;
    constexpr float kFar  = 100.0f;

    //! Eye at the origin looking down +Z (left-handed), 90 degree vertical fov, square.
    //! At 90 degrees the side planes are z = |x| and z = |y|, so the frustum's half extent
    //! at any depth equals that depth — every expectation below can be read off by hand.
    Math::Frustum MakeTestFrustum()
    {
        const Math::Matrix4X4 view = Math::LookAt(
            Math::Vector3(0.0f, 0.0f, 0.0f),
            Math::Vector3(0.0f, 0.0f, 1.0f),
            Math::Vector3(0.0f, 1.0f, 0.0f));
        const Math::Matrix4X4 proj = Math::PerspectiveFov(Math::Radians(90.0f), 1.0f, kNear, kFar);

        return Math::Frustum::FromViewProjection(proj * view);
    }

    bool ContainsPoint(const Math::Frustum& f, const Math::Vector3& p)
    {
        return f.IntersectsSphere(p, 0.0f);
    }
}

TEST(FrustumTest, PlanesAreNormalized)
{
    const Math::Frustum f = MakeTestFrustum();
    for (const Math::Vector4& p : f.planes)
    {
        EXPECT_NEAR(Math::Length(Math::Vector3(p)), 1.0f, 1e-5f);
    }
}

TEST(FrustumTest, PointOnAxisInsideDepthRange)
{
    const Math::Frustum f = MakeTestFrustum();

    EXPECT_TRUE(ContainsPoint(f, Math::Vector3(0.0f, 0.0f, 50.0f)));
    EXPECT_FALSE(ContainsPoint(f, Math::Vector3(0.0f, 0.0f, 0.5f)));    // in front of near
    EXPECT_FALSE(ContainsPoint(f, Math::Vector3(0.0f, 0.0f, 200.0f)));  // past far
    EXPECT_FALSE(ContainsPoint(f, Math::Vector3(0.0f, 0.0f, -50.0f)));  // behind the eye
}

//! The [-1,1] near-plane form (row3 + row2) would put the near plane at 0.50 instead of
//! 1.0 for this projection, so this point is the one that separates the two conventions.
TEST(FrustumTest, NearPlaneUsesZeroToOneConvention)
{
    const Math::Frustum f = MakeTestFrustum();

    EXPECT_FALSE(ContainsPoint(f, Math::Vector3(0.0f, 0.0f, 0.75f)));
    EXPECT_TRUE(ContainsPoint(f, Math::Vector3(0.0f, 0.0f, 1.25f)));
}

TEST(FrustumTest, SidePlanesFollowTheCone)
{
    const Math::Frustum f = MakeTestFrustum();

    EXPECT_TRUE(ContainsPoint(f, Math::Vector3(49.0f, 0.0f, 50.0f)));
    EXPECT_FALSE(ContainsPoint(f, Math::Vector3(51.0f, 0.0f, 50.0f)));
    EXPECT_TRUE(ContainsPoint(f, Math::Vector3(0.0f, -49.0f, 50.0f)));
    EXPECT_FALSE(ContainsPoint(f, Math::Vector3(0.0f, -51.0f, 50.0f)));
}

TEST(FrustumTest, SphereStraddlingAPlaneIntersects)
{
    const Math::Frustum f = MakeTestFrustum();

    // Centre sits 0.5 in front of the near plane, radius reaches past it.
    EXPECT_TRUE(f.IntersectsSphere(Math::Vector3(0.0f, 0.0f, 0.5f), 1.0f));
    EXPECT_FALSE(f.IntersectsSphere(Math::Vector3(0.0f, 0.0f, 0.5f), 0.25f));

    // Same on a side plane: at depth 50 the boundary is x = 50.
    EXPECT_TRUE(f.IntersectsSphere(Math::Vector3(52.0f, 0.0f, 50.0f), 3.0f));
    EXPECT_FALSE(f.IntersectsSphere(Math::Vector3(52.0f, 0.0f, 50.0f), 0.5f));
}

TEST(FrustumTest, OrthographicProjection)
{
    const Math::Matrix4X4 view = Math::LookAt(
        Math::Vector3(0.0f, 0.0f, 0.0f),
        Math::Vector3(0.0f, 0.0f, 1.0f),
        Math::Vector3(0.0f, 1.0f, 0.0f));
    const Math::Matrix4X4 proj = Math::OrthographicProjection(-10.0f, 10.0f, -10.0f, 10.0f, 0.0f, 50.0f);
    const Math::Frustum   f    = Math::Frustum::FromViewProjection(proj * view);

    EXPECT_TRUE(ContainsPoint(f, Math::Vector3(9.0f, 9.0f, 25.0f)));
    EXPECT_FALSE(ContainsPoint(f, Math::Vector3(11.0f, 0.0f, 25.0f)));
    EXPECT_FALSE(ContainsPoint(f, Math::Vector3(0.0f, 0.0f, 60.0f)));
    EXPECT_FALSE(ContainsPoint(f, Math::Vector3(0.0f, 0.0f, -1.0f)));
}
