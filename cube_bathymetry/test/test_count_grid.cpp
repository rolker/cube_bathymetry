// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
// Hydrographic Center, University of New Hampshire
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>

#include "cube_bathymetry/count_grid.h"

namespace cube
{

namespace
{
// A level-14 count grid (0.057 m cells) somewhere off Portsmouth NH, with a
// reference grid and a helper to address cells by row/column inside it.
class CountGridTest : public ::testing::Test
{
protected:
  static constexpr uint8_t kLevel = 14;
  CountGrid grid{kLevel};
  gggs::GridIndex home = gggs::Level(kLevel).gridIndex(43.07, -70.76);

  gggs::CellIndex cell(uint16_t row, uint16_t col) const
  {
    return gggs::CellIndex(home, row, col);
  }

  // Brute-force box sum against which the summed-area path is checked.
  uint64_t bruteBox(const gggs::CellIndex & c, int lambda) const
  {
    uint64_t sum = 0;
    for (int dr = -lambda; dr <= lambda; ++dr) {
      for (int dc = -lambda; dc <= lambda; ++dc) {
        const int r = static_cast<int>(c.row()) + dr;
        const int col = static_cast<int>(c.column()) + dc;
        int gr = 0, gc = 0;
        int rr = r, cc = col;
        while (rr < 0) {rr += CountGrid::kEdge; --gr;}
        while (rr >= CountGrid::kEdge) {rr -= CountGrid::kEdge; ++gr;}
        while (cc < 0) {cc += CountGrid::kEdge; --gc;}
        while (cc >= CountGrid::kEdge) {cc -= CountGrid::kEdge; ++gc;}
        const gggs::GridIndex g = CountGrid::neighbourGrid(c.grid(), gr, gc);
        if (!grid.tiles().count(g)) {
          continue;
        }
        sum += grid.tiles().at(g).get(static_cast<uint16_t>(rr), static_cast<uint16_t>(cc), 0);
      }
    }
    return sum;
  }

  std::string tempDir(const std::string & tag) const
  {
    const auto dir = std::filesystem::temp_directory_path() /
      ("cube_count_grid_" + tag + "_" + std::to_string(::getpid()));
    std::filesystem::remove_all(dir);
    return dir.string();
  }
};
}  // namespace

TEST_F(CountGridTest, CountsAccumulatePerCellAndSaturate)
{
  EXPECT_EQ(grid.countAt(cell(10, 10)), 0u);
  grid.add(cell(10, 10));
  grid.add(cell(10, 10));
  grid.add(cell(10, 11));
  EXPECT_EQ(grid.countAt(cell(10, 10)), 2u);
  EXPECT_EQ(grid.countAt(cell(10, 11)), 1u);
  EXPECT_EQ(grid.total(), 3u);
  EXPECT_EQ(grid.tileCount(), 1u);

  for (int i = 0; i < 70000; ++i) {
    grid.add(cell(5, 5));
  }
  EXPECT_EQ(grid.countAt(cell(5, 5)), std::numeric_limits<CountGrid::Count>::max());
  EXPECT_EQ(grid.total(), 70003u);  // total counts every sounding, saturation or not
}

TEST_F(CountGridTest, GeographicAddLandsInTheExpectedCell)
{
  const auto c = gggs::Level(kLevel).cellIndex(gggs::geoPoint(43.07, -70.76));
  grid.add(43.07, -70.76);
  EXPECT_EQ(grid.countAt(c), 1u);
  EXPECT_EQ(c.grid(), home);
}

TEST_F(CountGridTest, RejectsCellsAtAnotherLevel)
{
  const auto other = gggs::Level(13).cellIndex(gggs::geoPoint(43.07, -70.76));
  EXPECT_THROW(grid.add(other), std::invalid_argument);
  EXPECT_THROW(grid.countAt(other), std::invalid_argument);
  EXPECT_THROW(grid.countInBox(other, 0), std::invalid_argument);
}

TEST_F(CountGridTest, BoxSumsMatchBruteForceInsideOneTile)
{
  // A scattered pattern well inside the tile.
  for (uint16_t r = 100; r < 140; r += 3) {
    for (uint16_t c = 200; c < 260; c += 5) {
      grid.add(cell(r, c));
      if ((r + c) % 2) {grid.add(cell(r, c));}
    }
  }
  for (int lambda : {0, 1, 3, 7, 20, 60}) {
    EXPECT_EQ(grid.countInBox(cell(120, 230), lambda), bruteBox(cell(120, 230), lambda))
      << "lambda " << lambda;
    EXPECT_EQ(grid.countInBox(cell(101, 201), lambda), bruteBox(cell(101, 201), lambda))
      << "lambda " << lambda;
  }
}

TEST_F(CountGridTest, BoxesStitchAcrossTileEdges)
{
  // Soundings on both sides of the home tile's north and east edges, plus the
  // diagonal neighbour, so a box at the corner reads all four tiles.
  const gggs::GridIndex north = CountGrid::neighbourGrid(home, 1, 0);
  const gggs::GridIndex east = CountGrid::neighbourGrid(home, 0, 1);
  const gggs::GridIndex north_east = CountGrid::neighbourGrid(home, 1, 1);
  ASSERT_EQ(north.row(), home.row() + 1);
  ASSERT_EQ(east.column(), home.column() + 1);
  ASSERT_EQ(north_east.row(), home.row() + 1);
  ASSERT_EQ(north_east.column(), home.column() + 1);
  const uint16_t last = CountGrid::kEdge - 1;
  grid.add(cell(last, last));
  grid.add(cell(last - 1, last - 2));
  grid.add(gggs::CellIndex(north, 0, last));
  grid.add(gggs::CellIndex(north, 2, last - 1));
  grid.add(gggs::CellIndex(east, last, 0));
  grid.add(gggs::CellIndex(east, last - 3, 1));
  grid.add(gggs::CellIndex(north_east, 0, 0));
  grid.add(gggs::CellIndex(north_east, 1, 1));

  // lambda 0: only the corner cell itself.
  EXPECT_EQ(grid.countInBox(cell(last, last), 0), 1u);
  // lambda 1: the 3x3 box straddles all four tiles.
  EXPECT_EQ(grid.countInBox(cell(last, last), 1), bruteBox(cell(last, last), 1));
  EXPECT_EQ(grid.countInBox(cell(last, last), 1), 4u);  // corner, north(0,last), east(last,0), ne(0,0)
  // lambda 3: everything above is within 3 cells of the corner.
  EXPECT_EQ(grid.countInBox(cell(last, last), 3), bruteBox(cell(last, last), 3));
  EXPECT_EQ(grid.countInBox(cell(last, last), 3), 8u);
  // From the neighbour's corner the box reads the home tile's counts too; the
  // east tile's (last-3, 1) sounding is 4 rows below and falls outside.
  EXPECT_EQ(grid.countInBox(gggs::CellIndex(north_east, 0, 0), 3), 7u);
  // A full-tile-edge box from the corner still stays within the 3x3 stitch.
  EXPECT_EQ(grid.countInBox(cell(last, last), CountGrid::kMaxLambda), 8u);
  EXPECT_THROW(grid.countInBox(cell(last, last), CountGrid::kMaxLambda + 1), std::invalid_argument);
}

TEST_F(CountGridTest, LevelOfAggregationIsTheSmallestSatisfyingBox)
{
  // Five soundings in one cell: lambda 0 already reaches n_req = 5.
  for (int i = 0; i < 5; ++i) {grid.add(cell(300, 300));}
  auto loa = grid.levelOfAggregation(cell(300, 300), 5);
  EXPECT_FALSE(loa.saturated);
  EXPECT_EQ(loa.lambda, 0u);
  EXPECT_DOUBLE_EQ(grid.achievedSpacing(loa), grid.cellSizeMeters());

  // n_req = 6 needs the neighbours: one more sounding 4 cells away -> lambda 4.
  grid.add(cell(304, 300));
  loa = grid.levelOfAggregation(cell(300, 300), 6);
  EXPECT_FALSE(loa.saturated);
  EXPECT_EQ(loa.lambda, 4u);
  EXPECT_DOUBLE_EQ(grid.achievedSpacing(loa), 9.0 * grid.cellSizeMeters());

  // An empty cell far away: lambda grows until the box reaches the data.
  loa = grid.levelOfAggregation(cell(300, 400), 5);
  EXPECT_FALSE(loa.saturated);
  EXPECT_EQ(loa.lambda, 100u);

  // More than the whole neighbourhood holds: saturated, spacing +inf.
  loa = grid.levelOfAggregation(cell(300, 300), 7);
  EXPECT_TRUE(loa.saturated);
  EXPECT_EQ(loa.lambda, CountGrid::kMaxLambda);
  EXPECT_TRUE(std::isinf(grid.achievedSpacing(loa)));

  EXPECT_THROW(grid.levelOfAggregation(cell(300, 300), 0), std::invalid_argument);
}

TEST_F(CountGridTest, OneFlierMovesOneCountByOne)
{
  // The count-based decision is immune to a flier's *depth*: a blunder is one
  // sounding like any other, so it changes the box count by exactly one and
  // cannot by itself refine a cell that the other soundings do not support.
  for (int i = 0; i < 4; ++i) {grid.add(cell(50, 50));}
  auto before = grid.levelOfAggregation(cell(50, 50), 5);
  grid.add(cell(50, 50));  // "the flier" -- indistinguishable in a count grid
  auto after = grid.levelOfAggregation(cell(50, 50), 5);
  EXPECT_EQ(before.lambda, CountGrid::kMaxLambda);  // 4 < 5: saturated
  EXPECT_EQ(after.lambda, 0u);                       // the 5th sounding satisfies
  EXPECT_EQ(grid.countInBox(cell(50, 50), 0), 5u);
}

TEST_F(CountGridTest, PercentileOverOccupiedCellsInsideACoarseGrid)
{
  // Dense patch: 25 soundings per cell over a 4x4 block -> lambda 0 everywhere.
  for (uint16_t r = 400; r < 404; ++r) {
    for (uint16_t c = 400; c < 404; ++c) {
      for (int i = 0; i < 25; ++i) {grid.add(cell(r, c));}
    }
  }
  // Sparse patch far away: five lone soundings 10 cells apart along one row.
  // For n_req = 5 each needs a box reaching all four others: the middle one
  // at lambda 20, its neighbours at 30, the ends at 40.
  for (uint16_t c = 600; c <= 640; c += 10) {
    grid.add(cell(700, c));
  }
  const auto coarse = gggs::parent(gggs::parent(home));  // level 12, holds the whole tile
  ASSERT_TRUE(CountGrid::tileInside(home, coarse));

  // 21 votes sorted: 16 x R, 41R, 61R, 61R, 81R, 81R. Nearest-rank p50 is the
  // 11th (R); p95 is the 20th (81R).
  const auto p50 = grid.achievedSpacingPercentile(coarse, 0.5, 5);
  ASSERT_TRUE(p50.has_value());
  EXPECT_DOUBLE_EQ(*p50, grid.cellSizeMeters());
  const auto p95 = grid.achievedSpacingPercentile(coarse, 0.95, 5);
  ASSERT_TRUE(p95.has_value());
  EXPECT_DOUBLE_EQ(*p95, 81.0 * grid.cellSizeMeters());
  const auto p0 = grid.achievedSpacingPercentile(coarse, 0.0, 5);
  ASSERT_TRUE(p0.has_value());
  EXPECT_DOUBLE_EQ(*p0, grid.cellSizeMeters());

  // A coarse grid holding no occupied cell has no vote.
  const gggs::GridIndex elsewhere = gggs::Level(12).gridIndex(43.5, -70.2);
  EXPECT_FALSE(grid.achievedSpacingPercentile(elsewhere, 0.95, 5).has_value());

  // Argument validation.
  EXPECT_THROW(grid.achievedSpacingPercentile(coarse, 1.5, 5), std::invalid_argument);
  EXPECT_THROW(grid.achievedSpacingPercentile(coarse, 0.5, 0), std::invalid_argument);
  const auto finer = gggs::Level(15).gridIndex(43.07, -70.76);
  EXPECT_THROW(grid.achievedSpacingPercentile(finer, 0.5, 5), std::invalid_argument);
}

TEST_F(CountGridTest, LevelHistogramPercentileMatchesTheSpacingPercentile)
{
  // Same layout as the spacing test: 16 dense cells (level 14) and five sparse
  // ones. "No finer than" rounds toward the coarser cell: 41R = 2.3 m and
  // 61R = 3.5 m -> level 8 (3.62 m cells); 81R = 4.6 m -> level 7 (7.25 m).
  for (uint16_t r = 400; r < 404; ++r) {
    for (uint16_t c = 400; c < 404; ++c) {
      for (int i = 0; i < 25; ++i) {grid.add(cell(r, c));}
    }
  }
  for (uint16_t c = 600; c <= 640; c += 10) {
    grid.add(cell(700, c));
  }
  const auto & h = grid.achievedLevelHistogram(home, 5);
  EXPECT_EQ(h[14], 16u);
  EXPECT_EQ(h[8], 3u);   // 41R, 61R, 61R
  EXPECT_EQ(h[7], 2u);   // 81R, 81R
  EXPECT_EQ(h[CountGrid::kSaturatedBin], 0u);

  const auto coarse = gggs::parent(gggs::parent(home));
  auto p50 = grid.achievedLevelPercentile(coarse, 0.5, 5);
  ASSERT_TRUE(p50.has_value());
  EXPECT_FALSE(p50->saturated);
  EXPECT_EQ(p50->level, 14);
  auto p95 = grid.achievedLevelPercentile(coarse, 0.95, 5);
  ASSERT_TRUE(p95.has_value());
  EXPECT_EQ(p95->level, 7);
  auto p80 = grid.achievedLevelPercentile(coarse, 0.80, 5);  // rank 17 -> first level-8 vote
  ASSERT_TRUE(p80.has_value());
  EXPECT_EQ(p80->level, 8);
  EXPECT_FALSE(grid.achievedLevelPercentile(gggs::Level(12).gridIndex(43.5, -70.2), 0.95, 5).has_value());

  // The cache follows the data: a new sounding changes the histogram.
  const CountGrid::LevelHistogram before = grid.achievedLevelHistogram(home, 5);
  for (int i = 0; i < 4; ++i) {grid.add(cell(700, 600));}  // 600 now holds 5: lambda 0 there
  const CountGrid::LevelHistogram after = grid.achievedLevelHistogram(home, 5);
  EXPECT_NE(before, after) << "histogram must be recomputed after an add";
  EXPECT_EQ(after[14], 17u);
  EXPECT_THROW(grid.achievedLevelHistogram(CountGrid::neighbourGrid(home, 5, 5), 5), std::invalid_argument);
  EXPECT_THROW(grid.achievedLevelHistogram(home, 0), std::invalid_argument);
}

TEST_F(CountGridTest, SaturatedCellsVoteInfinity)
{
  // Two lone soundings: with n_req = 5 every occupied cell saturates.
  grid.add(cell(10, 10));
  grid.add(cell(900, 900));
  const auto p = grid.achievedSpacingPercentile(home, 0.95, 5);
  ASSERT_TRUE(p.has_value());
  EXPECT_TRUE(std::isinf(*p));
  const auto l = grid.achievedLevelPercentile(home, 0.95, 5);
  ASSERT_TRUE(l.has_value());
  EXPECT_TRUE(l->saturated);
}

TEST_F(CountGridTest, TileInsideFollowsTheQuadtree)
{
  EXPECT_TRUE(CountGrid::tileInside(home, home));
  EXPECT_TRUE(CountGrid::tileInside(home, gggs::parent(home)));
  gggs::GridIndex g = home;
  for (int i = 0; i < 6; ++i) {g = gggs::parent(g);}
  EXPECT_EQ(g.level(), 8);
  EXPECT_TRUE(CountGrid::tileInside(home, g));
  EXPECT_FALSE(CountGrid::tileInside(gggs::parent(home), home));  // coarser is never inside finer
  const gggs::GridIndex sibling = CountGrid::neighbourGrid(home, 0, 1);
  EXPECT_FALSE(CountGrid::tileInside(sibling, home));
  EXPECT_FALSE(CountGrid::tileInside(gggs::GridIndex(), home));
}

TEST_F(CountGridTest, SpreadTermKeepsThePerTileMaximum)
{
  EXPECT_DOUBLE_EQ(grid.maxSpreadTerm(home), 0.0);
  grid.add(cell(1, 1), 0.4);
  grid.add(cell(1, 2), 1.7);
  grid.add(cell(1, 3), 0.9);
  grid.add(cell(1, 4), std::numeric_limits<double>::quiet_NaN());  // ignored
  EXPECT_DOUBLE_EQ(grid.maxSpreadTerm(home), 1.7);
}

TEST_F(CountGridTest, MergeIsAdditiveAndSaturating)
{
  CountGrid other(kLevel);
  grid.add(cell(7, 7), 0.5);
  other.add(cell(7, 7), 2.0);
  other.add(cell(8, 8));
  grid.merge(other);
  EXPECT_EQ(grid.countAt(cell(7, 7)), 2u);
  EXPECT_EQ(grid.countAt(cell(8, 8)), 1u);
  EXPECT_EQ(grid.total(), 3u);
  EXPECT_DOUBLE_EQ(grid.maxSpreadTerm(home), 2.0);

  CountGrid wrong_level(13);
  EXPECT_THROW(grid.merge(wrong_level), std::invalid_argument);

  // Saturation survives a merge.
  CountGrid a(kLevel), b(kLevel);
  for (int i = 0; i < 40000; ++i) {a.add(cell(2, 2)); b.add(cell(2, 2));}
  a.merge(b);
  EXPECT_EQ(a.countAt(cell(2, 2)), std::numeric_limits<CountGrid::Count>::max());
}

TEST_F(CountGridTest, SaveAndMergeFromRoundTripsAdditively)
{
  const auto dir = tempDir("roundtrip");
  grid.add(cell(30, 31), 1.25);
  grid.add(cell(30, 31));
  grid.add(gggs::CellIndex(CountGrid::neighbourGrid(home, 1, 0), 0, 0), 0.3);
  EXPECT_EQ(grid.saveTo(dir), 2u);
  EXPECT_EQ(CountGrid::levelOf(dir), kLevel);

  CountGrid loaded(kLevel);
  EXPECT_EQ(loaded.mergeFrom(dir), 2u);
  EXPECT_EQ(loaded.countAt(cell(30, 31)), 2u);
  EXPECT_EQ(loaded.total(), 3u);
  EXPECT_EQ(loaded.tileCount(), 2u);
  EXPECT_DOUBLE_EQ(loaded.maxSpreadTerm(home), 1.25);

  // Merging the same directory again doubles the counts (additive by design:
  // a live-collected count grid folds into the offline one).
  EXPECT_EQ(loaded.mergeFrom(dir), 2u);
  EXPECT_EQ(loaded.countAt(cell(30, 31)), 4u);

  CountGrid wrong_level(13);
  EXPECT_THROW(wrong_level.mergeFrom(dir), std::invalid_argument);
  EXPECT_THROW(CountGrid::levelOf(dir + "_missing"), std::runtime_error);
  std::filesystem::remove_all(dir);
}

}  // namespace cube
