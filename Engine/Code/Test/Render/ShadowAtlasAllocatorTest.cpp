#include <gtest/gtest.h>

#include <EASTL/vector.h>

#include <View/ShadowAtlasAllocator.h>

using namespace Spark;
using namespace Spark::Render;

namespace
{
    //! Tiles of the finest level that fit in the atlas — the unit the budget counts in.
    constexpr uint32_t kFinestTiles = kShadowBudgetUnits;

    //! Extent of a tile in units of the finest level, the only denominator two tiles of
    //! different levels can be compared in.
    struct Extent
    {
        uint32_t m_x = 0, m_y = 0, m_size = 0;
    };

    Extent ExtentOf(uint32_t tile)
    {
        const ShadowTileTree::Block block = ShadowTileTree::Decode(tile);
        const uint32_t              size  = 1u << (kShadowFinestLevel - block.m_level);
        return Extent{ block.m_x * size, block.m_y * size, size };
    }

    void ExpectAllDisjoint(const eastl::vector<uint32_t>& tiles)
    {
        for (size_t i = 0; i < tiles.size(); ++i)
        {
            for (size_t j = i + 1; j < tiles.size(); ++j)
            {
                const Extent a = ExtentOf(tiles[i]);
                const Extent b = ExtentOf(tiles[j]);
                const bool overlaps = a.m_x < b.m_x + b.m_size && b.m_x < a.m_x + a.m_size
                                   && a.m_y < b.m_y + b.m_size && b.m_y < a.m_y + a.m_size;
                EXPECT_FALSE(overlaps) << "tiles " << tiles[i] << " and " << tiles[j];
            }
        }
    }
}

TEST(ShadowAtlasAllocatorTest, TilesOfALevelCoverTheAtlasExactly)
{
    ShadowAtlasAllocator    atlas;
    eastl::vector<uint32_t> tiles(kFinestTiles);

    ASSERT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, kFinestTiles, tiles.data()));
    ExpectAllDisjoint(tiles);

    uint32_t oneMore = 0;
    EXPECT_FALSE(atlas.AllocateTilesAt(kShadowFinestLevel, 1, &oneMore));
}

//! A point light asks for its faces together, and a partial set would leave the cube leaking
//! through the faces that missed out.
TEST(ShadowAtlasAllocatorTest, APartialSetIsRolledBack)
{
    ShadowAtlasAllocator atlas;

    // Fill all but three of the finest tiles.
    eastl::vector<uint32_t> filler(kFinestTiles - 3);
    ASSERT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, kFinestTiles - 3, filler.data()));

    uint32_t six[kShadowCubeFaceCount] = {};
    EXPECT_FALSE(atlas.AllocateTilesAt(kShadowFinestLevel, 6, six));

    // The three it could have taken must still be there, or the failed attempt kept them.
    uint32_t three[3] = {};
    EXPECT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, 3, three));
}

TEST(ShadowAtlasAllocatorTest, SixFacesDoNotFitAtTheCoarsestLevel)
{
    ShadowAtlasAllocator atlas;
    uint32_t             tiles[kShadowCubeFaceCount] = {};

    // 6 * 16 units against a budget of 64: the reason a point light is never as sharp as a
    // spot in the same place.
    EXPECT_FALSE(atlas.AllocateTilesAt(kShadowCoarsestLevel, 6, tiles));

    const uint32_t granted = atlas.AllocateTilesOrFiner(kShadowCoarsestLevel, 6, tiles);
    EXPECT_GT(granted, kShadowCoarsestLevel);
    EXPECT_LE(granted, kShadowFinestLevel);
    for (uint32_t tile : tiles)
    {
        EXPECT_EQ(ShadowAtlasAllocator::LevelOfTile(tile), granted);
    }
}

//! Free area is not a free tile. A quarter of the atlas idle in scattered pieces still
//! cannot yield one coarse tile, and the caller is handed finer ones instead.
TEST(ShadowAtlasAllocatorTest, FragmentationDemotesRatherThanFails)
{
    ShadowAtlasAllocator    atlas;
    eastl::vector<uint32_t> tiles(kFinestTiles);
    ASSERT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, kFinestTiles, tiles.data()));

    // Return one finest tile out of every four, so no coarser tile can be cut anywhere.
    for (size_t i = 0; i < tiles.size(); i += 4)
    {
        atlas.ReleaseTile(tiles[i]);
    }

    uint32_t coarse[1] = {};
    EXPECT_FALSE(atlas.AllocateTilesAt(kShadowFinestLevel - 1, 1, coarse));
    EXPECT_EQ(atlas.AllocateTilesOrFiner(kShadowFinestLevel - 1, 1, coarse), kShadowFinestLevel);
}

TEST(ShadowAtlasAllocatorTest, ReleasedTilesMergeBackIntoCoarseOnes)
{
    ShadowAtlasAllocator    atlas;
    eastl::vector<uint32_t> tiles(kFinestTiles);
    ASSERT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, kFinestTiles, tiles.data()));

    for (uint32_t tile : tiles)
    {
        atlas.ReleaseTile(tile);
    }

    uint32_t coarse[1] = {};
    EXPECT_TRUE(atlas.AllocateTilesAt(kShadowCoarsestLevel, 1, coarse));
}

TEST(ShadowAtlasAllocatorTest, RowsComeOutConsecutive)
{
    ShadowAtlasAllocator atlas;

    const uint32_t first = atlas.AllocateRows(6);
    ASSERT_NE(first, kInvalidShadowSlot);

    const uint32_t second = atlas.AllocateRows(6);
    ASSERT_NE(second, kInvalidShadowSlot);
    EXPECT_GE(second, first + 6);
}

//! A run has to fit whole. Freeing scattered singles leaves plenty of rows and no run.
TEST(ShadowAtlasAllocatorTest, ARunIsNotSatisfiedByScatteredRows)
{
    ShadowAtlasAllocator    atlas;
    eastl::vector<uint32_t> singles;
    for (uint32_t i = 0; i < kShadowViewCapacity; ++i)
    {
        const uint32_t row = atlas.AllocateRows(1);
        ASSERT_NE(row, kInvalidShadowSlot);
        singles.push_back(row);
    }
    EXPECT_EQ(atlas.AllocateRows(1), kInvalidShadowSlot);

    for (size_t i = 0; i < singles.size(); i += 2)
    {
        atlas.ReleaseRows(singles[i], 1);
    }

    EXPECT_EQ(atlas.AllocateRows(2), kInvalidShadowSlot);
    EXPECT_NE(atlas.AllocateRows(1), kInvalidShadowSlot);
}

TEST(ShadowAtlasAllocatorTest, RowsAndTilesAreIndependent)
{
    ShadowAtlasAllocator atlas;

    const uint32_t rows = atlas.AllocateRows(6);
    ASSERT_NE(rows, kInvalidShadowSlot);

    uint32_t tiles[2] = {};
    ASSERT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, 2, tiles));

    // Retiling a light must leave its rows alone — that is what keeps the shadow index it
    // published to the shader stable across a change of level.
    atlas.ReleaseTile(tiles[0]);
    atlas.ReleaseTile(tiles[1]);
    EXPECT_EQ(atlas.AllocateRows(1), rows + 6);
}
