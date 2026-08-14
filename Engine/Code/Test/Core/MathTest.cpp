#include <gtest/gtest.h>

#include <Math/Frustum.h>
#include <Math/MathUtils.h>
#include <Math/Sphere.h>

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

//! z = 0.75 is where the two conventions disagree: the [-1,1] form puts this projection's
//! near plane at 0.50 rather than 1.0.
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

namespace
{
    Math::AABB MakeBox(const Math::Vector3& min, const Math::Vector3& max)
    {
        Math::AABB box;
        box.min = min;
        box.max = max;
        return box;
    }
}

TEST(FrustumTest, AABBInsideAndOutside)
{
    const Math::Frustum f = MakeTestFrustum();

    EXPECT_TRUE(f.IntersectsAABB(MakeBox({-1.0f, -1.0f, 10.0f}, {1.0f, 1.0f, 20.0f})));
    EXPECT_FALSE(f.IntersectsAABB(MakeBox({-1.0f, -1.0f, 150.0f}, {1.0f, 1.0f, 160.0f})));
    EXPECT_FALSE(f.IntersectsAABB(MakeBox({60.0f, -1.0f, 49.0f}, {70.0f, 1.0f, 51.0f})));
}

TEST(FrustumTest, AABBStraddlingAPlane)
{
    const Math::Frustum f = MakeTestFrustum();

    EXPECT_TRUE(f.IntersectsAABB(MakeBox({-1.0f, -1.0f, 0.5f}, {1.0f, 1.0f, 1.5f})));
}

//! Every corner is outside, yet the box swallows the frustum whole.
TEST(FrustumTest, AABBEnclosingTheFrustum)
{
    const Math::Frustum f = MakeTestFrustum();

    EXPECT_TRUE(f.IntersectsAABB(MakeBox({-1000.0f, -1000.0f, -1000.0f}, {1000.0f, 1000.0f, 1000.0f})));
}

namespace
{
    constexpr float kConeHeight = 10.0f;
    const Math::Vector3 kApex(0.0f, 0.0f, 0.0f);
    const Math::Vector3 kAxis(0.0f, 0.0f, 1.0f);

    void ExpectEnclosesCone(const Math::Sphere& s, float halfAngleDeg)
    {
        const float baseRadius = kConeHeight * Math::Tan(Math::Radians(halfAngleDeg));
        const Math::Vector3 samples[] = {
            kApex,
            Math::Vector3( baseRadius, 0.0f, kConeHeight),
            Math::Vector3(-baseRadius, 0.0f, kConeHeight),
            Math::Vector3(0.0f,  baseRadius, kConeHeight),
            Math::Vector3(baseRadius * 0.5f, 0.0f, kConeHeight * 0.5f),
        };
        for (const Math::Vector3& p : samples)
        {
            EXPECT_LE(Math::Distance(p, s.center), s.radius + 1e-4f);
        }
    }
}

TEST(SphereTest, ConeNarrowerThan45Degrees)
{
    const Math::Sphere s = Math::Sphere::FromCone(kApex, kAxis, kConeHeight, Math::Radians(30.0f));

    // baseRadius = 5.7735, so d = (100 + 33.333) / 20.
    EXPECT_NEAR(s.radius, 6.6667f, 1e-3f);
    EXPECT_NEAR(s.center.z, 6.6667f, 1e-3f);
    ExpectEnclosesCone(s, 30.0f);
}

TEST(SphereTest, ConeWiderThan45DegreesSitsOnTheBase)
{
    const Math::Sphere s = Math::Sphere::FromCone(kApex, kAxis, kConeHeight, Math::Radians(60.0f));

    EXPECT_NEAR(s.radius, kConeHeight * Math::Tan(Math::Radians(60.0f)), 1e-3f);
    EXPECT_NEAR(s.center.z, kConeHeight, 1e-3f);
    ExpectEnclosesCone(s, 60.0f);
}

TEST(SphereTest, ConeBranchesAgreeAt45Degrees)
{
    const Math::Sphere s = Math::Sphere::FromCone(kApex, kAxis, kConeHeight, Math::Radians(45.0f));

    EXPECT_NEAR(s.radius, kConeHeight, 1e-3f);
    EXPECT_NEAR(s.center.z, kConeHeight, 1e-3f);
}

TEST(SphereTest, ConeIsTighterThanTheApexSphere)
{
    const Math::Sphere s = Math::Sphere::FromCone(kApex, kAxis, kConeHeight, Math::Radians(15.0f));

    EXPECT_LT(s.radius, kConeHeight);
    ExpectEnclosesCone(s, 15.0f);
}

TEST(SphereTest, ConeWithNoHeightIsDegenerate)
{
    const Math::Sphere s = Math::Sphere::FromCone(kApex, kAxis, 0.0f, Math::Radians(30.0f));

    EXPECT_FLOAT_EQ(s.radius, 0.0f);
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
