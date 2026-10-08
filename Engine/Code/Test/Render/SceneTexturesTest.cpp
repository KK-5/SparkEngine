#include <gtest/gtest.h>

#include <Feature/SceneTextures/SceneTextures.h>

using namespace Spark;
using namespace Spark::Render::SceneTextures;

TEST(SceneTexturesHZBTest, Mip0IsHalfTheRenderSizeRoundedUpToAPowerOfTwo)
{
    EXPECT_EQ(HZB::MipSize(Math::Vector2Int(1920, 1080), 0), Math::Vector2Int(1024, 1024));
    EXPECT_EQ(HZB::MipSize(Math::Vector2Int(2560, 1080), 0), Math::Vector2Int(2048, 1024));
    EXPECT_EQ(HZB::MipSize(Math::Vector2Int(1024, 512), 0), Math::Vector2Int(512, 256));
}

TEST(SceneTexturesHZBTest, AnOddSideKeepsItsLastPixel)
{
    // 1025 pixels need 513 texels of two pixels each, which a 512-wide mip 0 would not hold.
    EXPECT_EQ(HZB::MipSize(Math::Vector2Int(1025, 3), 0), Math::Vector2Int(1024, 2));
}

TEST(SceneTexturesHZBTest, TheChainHalvesDownToOneTexel)
{
    const Math::Vector2Int renderSize(2560, 1080);
    const uint32_t mipCount = HZB::MipCount(renderSize);

    EXPECT_EQ(mipCount, 12u);
    EXPECT_EQ(HZB::MipSize(renderSize, 1), Math::Vector2Int(1024, 512));
    EXPECT_EQ(HZB::MipSize(renderSize, mipCount - 2), Math::Vector2Int(2, 1));
    EXPECT_EQ(HZB::MipSize(renderSize, mipCount - 1), Math::Vector2Int(1, 1));
}

TEST(SceneTexturesHZBTest, ASinglePixelIsOneMip)
{
    EXPECT_EQ(HZB::MipSize(Math::Vector2Int(1, 1), 0), Math::Vector2Int(1, 1));
    EXPECT_EQ(HZB::MipCount(Math::Vector2Int(1, 1)), 1u);
}

TEST(SceneTexturesHZBTest, UvFactorIsTheScreensShareOfMip0)
{
    const Math::Vector2 factor = HZB::UvFactor(Math::Vector2Int(1920, 1080));
    EXPECT_FLOAT_EQ(factor.x, 1920.0f / 2048.0f);
    EXPECT_FLOAT_EQ(factor.y, 1080.0f / 2048.0f);

    // A power-of-two render size fills the chain.
    const Math::Vector2 full = HZB::UvFactor(Math::Vector2Int(1024, 512));
    EXPECT_FLOAT_EQ(full.x, 1.0f);
    EXPECT_FLOAT_EQ(full.y, 1.0f);
}
