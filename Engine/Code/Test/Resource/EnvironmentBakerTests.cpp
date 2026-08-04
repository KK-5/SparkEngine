#include <gtest/gtest.h>

#include <Resource/Image/EnvironmentBaker.h>

using namespace Spark;
using namespace Spark::Resource;

// The prefiltered cube's roughness ladder is defined twice, in two languages:
// EnvironmentBaker::LodToRoughness (bake) and RoughnessToLod in
// Engine/Asset/Shaders/SceneBindings.hlsl (render). Nothing fails if they drift apart --
// reflections just come out uniformly too sharp or too blurred, which is not a symptom
// that points anywhere. EnvironmentBaker::RoughnessToLod mirrors the HLSL one purely so
// the inverse relationship can be pinned here.
//
// This guards the C++ pair only. Keeping the HLSL copy in step is on whoever edits it;
// the two are deliberately given the same name and shape so a diff reads side by side.

namespace
{
    constexpr uint32_t kMips = EnvironmentBaker::kPrefilterMips;
}

TEST(EnvironmentBakerRoughnessLadder, RoundTripsForEveryMip)
{
    for (uint32_t mip = 0; mip < kMips; ++mip)
    {
        const float roughness = EnvironmentBaker::LodToRoughness(mip, kMips);
        const float lod       = EnvironmentBaker::RoughnessToLod(roughness, kMips);
        EXPECT_NEAR(lod, static_cast<float>(mip), 1e-4f)
            << "mip " << mip << " did not survive the round trip";
    }
}

TEST(EnvironmentBakerRoughnessLadder, EndpointsAreMirrorAndFullyRough)
{
    // Mip 0 must stay an exact mirror: PrefilterBake's mirror-mip path keys off
    // roughness == 0, and the runtime's specular would otherwise never reach mip 0.
    EXPECT_FLOAT_EQ(EnvironmentBaker::LodToRoughness(0, kMips), 0.0f);
    EXPECT_FLOAT_EQ(EnvironmentBaker::LodToRoughness(kMips - 1, kMips), 1.0f);

    EXPECT_FLOAT_EQ(EnvironmentBaker::RoughnessToLod(0.0f, kMips), 0.0f);
    EXPECT_FLOAT_EQ(EnvironmentBaker::RoughnessToLod(1.0f, kMips),
                    static_cast<float>(kMips - 1));
}

TEST(EnvironmentBakerRoughnessLadder, IsStrictlyIncreasing)
{
    // Monotonicity is what makes the mapping invertible at all; a non-monotonic ladder
    // would make two roughness values fetch the same mip.
    float previous = -1.0f;
    for (uint32_t mip = 0; mip < kMips; ++mip)
    {
        const float roughness = EnvironmentBaker::LodToRoughness(mip, kMips);
        EXPECT_GT(roughness, previous) << "mip " << mip << " is not above its predecessor";
        previous = roughness;
    }
}

TEST(EnvironmentBakerRoughnessLadder, ConcentratesLevelsAtLowRoughness)
{
    // The whole point of the non-uniform ladder: the first step must be shorter than a
    // uniform one would be. If someone reverts to mip/(mipCount-1) this fails rather than
    // silently changing every reflection.
    const float uniformStep = 1.0f / static_cast<float>(kMips - 1);
    EXPECT_LT(EnvironmentBaker::LodToRoughness(1, kMips), uniformStep);
}

TEST(EnvironmentBakerRoughnessLadder, SingleMipDegeneratesToMirror)
{
    // Guards the division by (mipCount - 1).
    EXPECT_FLOAT_EQ(EnvironmentBaker::LodToRoughness(0, 1), 0.0f);
    EXPECT_FLOAT_EQ(EnvironmentBaker::RoughnessToLod(0.5f, 1), 0.0f);
}

TEST(EnvironmentBakerPrefilterFaceSize, IsCappedBySkyCube)
{
    // A prefiltered cube wider than the sky cube would only hold a magnified copy, and
    // PrefilterBake's mirror mip log2(src/dst) would go negative.
    EXPECT_EQ(EnvironmentBaker::PrefilterFaceSize(128u), 128u);
    EXPECT_EQ(EnvironmentBaker::PrefilterFaceSize(EnvironmentBaker::kPrefilterSizeMax),
              EnvironmentBaker::kPrefilterSizeMax);
    EXPECT_EQ(EnvironmentBaker::PrefilterFaceSize(2048u), EnvironmentBaker::kPrefilterSizeMax);
}
