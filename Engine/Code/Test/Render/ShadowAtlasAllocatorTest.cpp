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

    void ExpectAllDisjoint(const ShadowTileList& tiles)
    {
        for (size_t i = 0; i < tiles.size(); ++i)
        {
            for (size_t j = i + 1; j < tiles.size(); ++j)
            {
                const Extent a = ExtentOf(tiles[i].Get());
                const Extent b = ExtentOf(tiles[j].Get());
                const bool overlaps = a.m_x < b.m_x + b.m_size && b.m_x < a.m_x + a.m_size
                                   && a.m_y < b.m_y + b.m_size && b.m_y < a.m_y + a.m_size;
                EXPECT_FALSE(overlaps) << "tiles " << tiles[i].Get() << " and " << tiles[j].Get();
            }
        }
    }
}

TEST(ShadowAtlasAllocatorTest, TilesOfALevelCoverTheAtlasExactly)
{
    ShadowAtlasAllocator atlas;
    ShadowTileList       tiles;

    ASSERT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, kFinestTiles, tiles));
    ExpectAllDisjoint(tiles);

    ShadowTileList oneMore;
    EXPECT_FALSE(atlas.AllocateTilesAt(kShadowFinestLevel, 1, oneMore));
}

//! A point light asks for its faces together, and a partial set would leave the cube leaking
//! through the faces that missed out.
TEST(ShadowAtlasAllocatorTest, APartialSetIsRolledBack)
{
    ShadowAtlasAllocator atlas;

    // Fill all but three of the finest tiles.
    ShadowTileList filler;
    ASSERT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, kFinestTiles - 3, filler));

    ShadowTileList six;
    EXPECT_FALSE(atlas.AllocateTilesAt(kShadowFinestLevel, 6, six));

    // The three it could have taken must still be there, or the failed attempt kept them.
    ShadowTileList three;
    EXPECT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, 3, three));
}

TEST(ShadowAtlasAllocatorTest, SixFacesDoNotFitAtTheCoarsestLevel)
{
    ShadowAtlasAllocator atlas;
    ShadowTileList       tiles;

    // 6 * 16 units against a budget of 64: the reason a point light is never as sharp as a
    // spot in the same place.
    EXPECT_FALSE(atlas.AllocateTilesAt(kShadowCoarsestLevel, 6, tiles));

    const uint32_t granted = atlas.AllocateTilesOrFiner(kShadowCoarsestLevel, 6, tiles);
    EXPECT_GT(granted, kShadowCoarsestLevel);
    EXPECT_LE(granted, kShadowFinestLevel);
    for (const ShadowAtlasTile& tile : tiles)
    {
        EXPECT_EQ(ShadowAtlasAllocator::LevelOfTile(tile.Get()), granted);
    }
}

//! Free area is not a free tile. A quarter of the atlas idle in scattered pieces still
//! cannot yield one coarse tile, and the caller is handed finer ones instead.
TEST(ShadowAtlasAllocatorTest, FragmentationDemotesRatherThanFails)
{
    ShadowAtlasAllocator atlas;
    ShadowTileList       tiles;
    ASSERT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, kFinestTiles, tiles));

    // Return one finest tile out of every four, so no coarser tile can be cut anywhere.
    for (size_t i = 0; i < tiles.size(); i += 4)
    {
        tiles[i].Reset();
    }

    ShadowTileList coarse;
    EXPECT_FALSE(atlas.AllocateTilesAt(kShadowFinestLevel - 1, 1, coarse));
    EXPECT_EQ(atlas.AllocateTilesOrFiner(kShadowFinestLevel - 1, 1, coarse), kShadowFinestLevel);
}

TEST(ShadowAtlasAllocatorTest, ReleasedTilesMergeBackIntoCoarseOnes)
{
    ShadowAtlasAllocator atlas;
    ShadowTileList       tiles;
    ASSERT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, kFinestTiles, tiles));

    tiles.clear();

    ShadowTileList coarse;
    EXPECT_TRUE(atlas.AllocateTilesAt(kShadowCoarsestLevel, 1, coarse));
}

TEST(ShadowAtlasAllocatorTest, RowsComeOutConsecutive)
{
    ShadowAtlasAllocator atlas;

    const ShadowViewRows first = atlas.AllocateRows(6);
    ASSERT_TRUE(first.IsValid());

    const ShadowViewRows second = atlas.AllocateRows(6);
    ASSERT_TRUE(second.IsValid());
    EXPECT_GE(second.Get(), first.Get() + 6);
}

//! A run has to fit whole. Freeing scattered singles leaves plenty of rows and no run.
TEST(ShadowAtlasAllocatorTest, ARunIsNotSatisfiedByScatteredRows)
{
    ShadowAtlasAllocator         atlas;
    eastl::vector<ShadowViewRows> singles;
    for (uint32_t i = 0; i < kShadowViewCapacity; ++i)
    {
        singles.push_back(atlas.AllocateRows(1));
        ASSERT_TRUE(singles.back().IsValid());
    }
    EXPECT_FALSE(atlas.AllocateRows(1).IsValid());

    for (size_t i = 0; i < singles.size(); i += 2)
    {
        singles[i].Reset();
    }

    EXPECT_FALSE(atlas.AllocateRows(2).IsValid());
    EXPECT_TRUE(atlas.AllocateRows(1).IsValid());
}

TEST(ShadowAtlasAllocatorTest, RowsAndTilesAreIndependent)
{
    ShadowAtlasAllocator atlas;

    const ShadowViewRows rows = atlas.AllocateRows(6);
    ASSERT_TRUE(rows.IsValid());

    ShadowTileList tiles;
    ASSERT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, 2, tiles));

    // Retiling a light must leave its rows alone — that is what keeps the shadow index it
    // published to the shader stable across a change of level.
    tiles.clear();
    const ShadowViewRows next = atlas.AllocateRows(1);
    ASSERT_TRUE(next.IsValid());
    EXPECT_EQ(next.Get(), rows.Get() + 6);
}

//! The path that leaked before: the tile rides on a view entity, so destroying that entity
//! is what hands it back — no system has to see it happen.
TEST(ShadowAtlasAllocatorTest, ATileComesBackWhenItsViewEntityIsDestroyed)
{
    ShadowAtlasAllocator atlas;
    ShadowTileList       filler;
    ASSERT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, kFinestTiles - 1, filler));

    RHI::RHIContext rhiCtx;
    {
        ShadowTileList one;
        ASSERT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, 1, one));

        const RHI::RHIHandle view = rhiCtx.CreateEntity();
        rhiCtx.Add<ShadowAtlasTile>(view, eastl::move(one[0]));

        ShadowTileList none;
        ASSERT_FALSE(atlas.AllocateTilesAt(kShadowFinestLevel, 1, none));

        rhiCtx.DestoryEntity(view);
    }

    ShadowTileList again;
    EXPECT_TRUE(atlas.AllocateTilesAt(kShadowFinestLevel, 1, again));
}
