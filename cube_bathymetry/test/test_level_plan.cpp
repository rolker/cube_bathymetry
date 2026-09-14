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
#include <map>
#include <set>
#include <string>

#include "cube_bathymetry/level_plan.h"

namespace cube
{

namespace
{
// Synthetic surveys are laid out inside one level-10 grid (~870 m) off
// Portsmouth NH. Soundings are added as dense patches: `perCell` soundings in
// every level-14 count cell of a block of level-14 grids, so the achieved level
// is under the test's control (6 per cell = n_req -> lambda 0 -> level 14 achievable).
class LevelPlanTest : public ::testing::Test
{
protected:
  static constexpr double kLat = 43.07;
  static constexpr double kLon = -70.76;

  LevelPlanPolicy policy;
  CountGrid counts{14};
  std::map<gggs::GridIndex, float> depths;

  gggs::GridIndex l14(double lat, double lon) const
  {
    return gggs::Level(14).gridIndex(lat, lon);
  }

  // Fill every fourth cell (a 4x4 lattice) of a level-14 grid with `per_cell`
  // soundings at `depth`: with per_cell >= n_req every occupied cell achieves
  // level 14 on its own, at a 16th of the cost of filling every cell.
  void fillGrid(const gggs::GridIndex & grid, int per_cell, float depth, double spread = 0.0)
  {
    for (uint16_t r = 0; r < CountGrid::kEdge; r += 4) {
      for (uint16_t c = 0; c < CountGrid::kEdge; c += 4) {
        for (int i = 0; i < per_cell; ++i) {
          counts.add(gggs::CellIndex(grid, r, c), spread);
        }
      }
    }
    depths[grid] = depth;
  }

  // Sparse: one sounding every `stride` cells.
  void sparseGrid(const gggs::GridIndex & grid, int stride, float depth)
  {
    for (uint16_t r = 0; r < CountGrid::kEdge; r += stride) {
      for (uint16_t c = 0; c < CountGrid::kEdge; c += stride) {
        counts.add(gggs::CellIndex(grid, r, c));
      }
    }
    depths[grid] = depth;
  }

  // All level-14 grids inside a coarser grid.
  std::vector<gggs::GridIndex> l14Inside(const gggs::GridIndex & coarse) const
  {
    std::vector<gggs::GridIndex> out{coarse};
    while (out.front().level() < 14) {
      std::vector<gggs::GridIndex> next;
      for (const auto & g : out) {
        for (const auto & c : gggs::children(g)) {next.push_back(c);}
      }
      out = next;
    }
    return out;
  }
};
}  // namespace

TEST(LevelPlanPolicyTest, ValidationNamesEachConstraint)
{
  LevelPlanPolicy ok;
  EXPECT_NO_THROW(ok.validate());
  EXPECT_EQ(ok.requiredObservations(), 6u);  // ceil(5 * 1.2)

  LevelPlanPolicy p = ok;
  p.depth.finest_level = 15;
  EXPECT_THROW(p.validate(), std::invalid_argument);  // finer than the survey index
  p = ok;
  p.depth.coarsest_level = 12;
  p.depth.finest_level = 11;
  EXPECT_THROW(p.validate(), std::invalid_argument);
  p = ok;
  p.count_level = 13;  // below finest 14
  EXPECT_THROW(p.validate(), std::invalid_argument);
  p.count_level = 21;
  EXPECT_THROW(p.validate(), std::invalid_argument);
  p = ok;
  p.depth.finest_level = 12;
  p.count_level = 13;  // count level may exceed finest
  EXPECT_NO_THROW(p.validate());
  p = ok;
  p.min_obs_per_node = 0;
  EXPECT_THROW(p.validate(), std::invalid_argument);
  p = ok;
  p.blunder_allowance = -0.1;
  EXPECT_THROW(p.validate(), std::invalid_argument);
  p = ok;
  p.achieved_percentile = 1.5;
  EXPECT_THROW(p.validate(), std::invalid_argument);
  p = ok;
  p.depth.capture_distance_scale = 0.0;
  EXPECT_THROW(p.validate(), std::invalid_argument);
}

TEST(LevelNoFinerThanTest, RoundsTowardTheCoarserLevel)
{
  // 0.33 m lies between level 11 (0.453 m) and level 12 (0.227 m):
  // fromCellSize says 12 (at-or-finer); the achieved level must be 11.
  EXPECT_EQ(gggs::Level::fromCellSize(0.33f).level(), 12);
  EXPECT_EQ(levelNoFinerThan(0.33, 8, 14), 11);
  // Exactly a level's cell: that level.
  EXPECT_EQ(levelNoFinerThan(gggs::Level(12).cellSize(), 8, 14), 12);
  // Finer than the finest cell: clamps to finest.
  EXPECT_EQ(levelNoFinerThan(0.01, 8, 14), 14);
  // Coarser than the coarsest cell: clamps to coarsest, never underflows.
  EXPECT_EQ(levelNoFinerThan(100.0, 8, 14), 8);
  EXPECT_EQ(levelNoFinerThan(1e6, 8, 14), 8);
  // Saturation (+inf) and nonsense read as coarsest.
  EXPECT_EQ(levelNoFinerThan(std::numeric_limits<double>::infinity(), 8, 14), 8);
  EXPECT_EQ(levelNoFinerThan(std::nan(""), 8, 14), 8);
  EXPECT_EQ(levelNoFinerThan(0.0, 8, 14), 8);
  EXPECT_THROW(levelNoFinerThan(1.0, 14, 8), std::invalid_argument);
}

TEST_F(LevelPlanTest, EmptySurveyEmitsNothing)
{
  const auto plan = levelPlanFor(counts, depths, policy);
  EXPECT_TRUE(plan.tiles().empty());
  EXPECT_TRUE(plan.levels().empty());
  EXPECT_NE(plan.report().find("no tiles"), std::string::npos);
}

TEST_F(LevelPlanTest, RejectsMismatchedCountLevel)
{
  CountGrid other(15);
  policy.count_level = 14;
  EXPECT_THROW(levelPlanFor(other, depths, policy), std::invalid_argument);
}

TEST_F(LevelPlanTest, DeepPlainEmitsAFullPrefixDownToTheRequiredLevel)
{
  // One densely sounded level-14 grid at 40 m depth: required level 9
  // (1.81 m cells; 36.24 <= 40 < 72.47), achieved level 14 (6/cell). Target
  // is the coarser, 9: the plan emits its level-8 ancestor and the level-9
  // tile, and stops.
  const auto g = l14(kLat, kLon);
  fillGrid(g, 6, -40.0f);
  const auto plan = levelPlanFor(counts, depths, policy);

  const std::set<uint8_t> expected{8, 9};
  EXPECT_EQ(plan.levels(), expected);
  EXPECT_EQ(plan.tilesAtLevel(8).size(), 1u);
  EXPECT_EQ(plan.tilesAtLevel(9).size(), 1u);
  // Every emitted tile's ancestors up to the coarsest level are emitted too.
  for (const auto & [grid, tile] : plan.tiles()) {
    gggs::GridIndex a = grid;
    while (a.level() > policy.depth.coarsest_level) {
      a = gggs::parent(a);
      EXPECT_TRUE(plan.isEmitted(a)) << "ancestor at level " << static_cast<int>(a.level());
    }
  }
  const auto & t9 = plan.tiles().at(plan.tilesAtLevel(9).front());
  EXPECT_EQ(t9.required_level, 9);
  EXPECT_EQ(t9.achieved_level, 14);
  EXPECT_FALSE(t9.refined);
  EXPECT_FALSE(t9.coverageDeficit());
  EXPECT_FLOAT_EQ(t9.decision_depth, -40.0f);
  const auto & t8 = plan.tiles().at(plan.tilesAtLevel(8).front());
  EXPECT_TRUE(t8.refined);
  EXPECT_TRUE(plan.coverageDeficit().empty());

  // Queries.
  const auto containing = plan.tilesContaining(kLat, kLon);
  EXPECT_EQ(containing.size(), 2u);
  gz4d::BoundsDegrees bounds(gz4d::PositionDegrees(kLat, kLon));
  EXPECT_EQ(plan.levelsIntersecting(bounds), expected);
  gz4d::BoundsDegrees far(gz4d::PositionDegrees(44.0, -69.0));
  EXPECT_TRUE(plan.levelsIntersecting(far).empty());
}

TEST_F(LevelPlanTest, ShoalRefinesOnlyTheTouchedChildren)
{
  // A level-11 block (4x4 = 16 level-14 grids... at level 11 one grid holds
  // 8x8 = 64 level-14 grids). Fill the whole level-12 block (16 level-14
  // grids) at 40 m densely, and make ONE level-14 grid shallow (3 m: required
  // level 13; 2.26 <= 3 < 4.53).
  const auto shoal = l14(kLat, kLon);
  const auto block12 = gggs::parent(gggs::parent(shoal));
  for (const auto & g : l14Inside(block12)) {
    fillGrid(g, 6, g == shoal ? -3.0f : -40.0f);
  }
  const auto plan = levelPlanFor(counts, depths, policy);

  // Levels 8..13 exist; 14 does not (required 13 is the finest anyone asks).
  const std::set<uint8_t> expected{8, 9, 10, 11, 12, 13};
  EXPECT_EQ(plan.levels(), expected);
  // Exactly one tile at each level below 9: the shoal's ancestor chain. A
  // refined parent descends into every touched child, but a child whose own
  // target is coarser than its level is NOT emitted -- the parent's estimate
  // serves that ground (parents-alive). So the deep-plain siblings of the
  // shoal chain stop at level 9 and never get level 10..13 tiles.
  for (int level = 10; level <= 13; ++level) {
    const auto at = plan.tilesAtLevel(static_cast<uint8_t>(level));
    ASSERT_EQ(at.size(), 1u) << "level " << level;
    gggs::GridIndex expected_tile = shoal;
    while (expected_tile.level() > level) {expected_tile = gggs::parent(expected_tile);}
    EXPECT_EQ(at.front(), expected_tile) << "level " << level;
    EXPECT_EQ(plan.tiles().at(at.front()).refined, level < 13);
  }
  // The plain: every touched level-9 tile under the level-8 root is emitted
  // (target 9 >= 9) and none is refined except the shoal's ancestor.
  std::size_t refined9 = 0;
  for (const auto & grid : plan.tilesAtLevel(9)) {
    if (plan.tiles().at(grid).refined) {++refined9;}
  }
  EXPECT_EQ(refined9, 1u);
  // The min-rollup made every ancestor of the shoal at least as shallow.
  for (const auto & grid : {gggs::parent(shoal), block12, gggs::parent(block12)}) {
    EXPECT_FLOAT_EQ(plan.tiles().at(grid).decision_depth, -3.0f)
      << "level " << static_cast<int>(grid.level());
  }
  // The whole block lies inside one level-9 tile, so every emitted tile is an
  // ancestor of the shoal and carries the rolled-up shallow depth; the deep
  // level-13 siblings inside the block are not emitted at all (their target,
  // 9, is coarser than 13 -- the level-12 parent serves them).
  for (const auto & [grid, tile] : plan.tiles()) {
    EXPECT_FLOAT_EQ(tile.decision_depth, -3.0f) << "level " << static_cast<int>(grid.level());
  }
  for (const auto & sibling : gggs::children(gggs::parent(shoal))) {
    EXPECT_EQ(plan.isEmitted(sibling), false) << "level-14 tiles are never asked for";
  }
  for (const auto & child13 : gggs::children(block12)) {
    if (child13 != gggs::parent(shoal)) {
      EXPECT_FALSE(plan.isEmitted(child13)) << "deep level-13 sibling must not be emitted";
      EXPECT_TRUE(plan.isTouched(child13));
    }
  }
}

TEST_F(LevelPlanTest, SparseShoalStaysCoarseAndIsACoverageDeficit)
{
  // Shallow (3 m -> required 13) but only one sounding every 20 count cells:
  // with n_req = 6 an interior cell needs the box of half-width 20 that holds
  // its eight lattice neighbours, so the achieved spacing is 41 * 0.057 m =
  // 2.3 m -- coarser than a level-9 cell (1.81 m), so achieved level 8. The
  // target is the coarser of 13 and 8: only the level-8 tile is emitted, and
  // it is a coverage deficit.
  const auto g = l14(kLat, kLon);
  sparseGrid(g, 20, -3.0f);
  const auto plan = levelPlanFor(counts, depths, policy);
  const std::set<uint8_t> expected{8};
  EXPECT_EQ(plan.levels(), expected);
  const auto & t8 = plan.tiles().at(plan.tilesAtLevel(8).front());
  EXPECT_EQ(t8.required_level, 13);
  EXPECT_EQ(t8.achieved_level, 8);
  EXPECT_FALSE(t8.refined);
  EXPECT_TRUE(t8.coverageDeficit());
  EXPECT_EQ(plan.coverageDeficit().size(), 1u);
  EXPECT_NE(plan.report().find("coverage deficit"), std::string::npos);
}

TEST_F(LevelPlanTest, TouchedSetGrowsWithTheLevelFloorAndSpreadTerm)
{
  // A single occupied cell at the very north-east corner of a level-14 grid,
  // with a 3 m spread term: at level 14 (cell 0.057 m) the disc reaches the
  // north, east and north-east neighbours; at level 8 the floor is the 3.6 m
  // cell, so the touched set at level 8 covers the same ground plus whatever
  // level-8 grids lie within 3.6 m of that corner.
  const auto g = l14(kLat, kLon);
  const uint16_t last = CountGrid::kEdge - 1;
  counts.add(gggs::CellIndex(g, last, last), 3.0);
  depths[g] = -40.0f;
  const auto plan = levelPlanFor(counts, depths, policy);

  EXPECT_TRUE(plan.isTouched(g));
  EXPECT_TRUE(plan.isTouched(CountGrid::neighbourGrid(g, 1, 0)));
  EXPECT_TRUE(plan.isTouched(CountGrid::neighbourGrid(g, 0, 1)));
  EXPECT_TRUE(plan.isTouched(CountGrid::neighbourGrid(g, 1, 1)));
  EXPECT_FALSE(plan.isTouched(CountGrid::neighbourGrid(g, -1, 0)));  // 54 m away
  // Coarser levels: the ancestor chain is touched at every level, and the
  // touched set at a coarser level is never narrower than the union of its
  // touched children's ancestors.
  for (int level = 8; level <= 14; ++level) {
    gggs::GridIndex a = g;
    while (a.level() > level) {a = gggs::parent(a);}
    EXPECT_TRUE(plan.isTouched(a)) << "level " << level;
  }
  for (int level = 9; level <= 14; ++level) {
    for (const auto & grid : plan.tilesAtLevel(static_cast<uint8_t>(level))) {
      EXPECT_TRUE(plan.isTouched(gggs::parent(grid)));
    }
  }
  // One sounding cannot achieve n_req = 6 anywhere: every occupied cell
  // saturates, the achieved level is the coarsest, and only the level-8 root
  // is emitted (target 8 < required 9 at 40 m).
  const std::set<uint8_t> only{8};
  EXPECT_EQ(plan.levels(), only);
  EXPECT_TRUE(plan.tiles().at(plan.tilesAtLevel(8).front()).coverageDeficit());
}

TEST_F(LevelPlanTest, SingleLevelPolicyEmitsExactlyTheTouchedSet)
{
  policy.depth.coarsest_level = 10;
  policy.depth.finest_level = 10;
  const auto g = l14(kLat, kLon);
  fillGrid(g, 6, -3.0f, 2.0);
  const auto plan = levelPlanFor(counts, depths, policy);
  const std::set<uint8_t> only{10};
  EXPECT_EQ(plan.levels(), only);
  // Emitted == touched at that level.
  std::set<gggs::GridIndex> emitted;
  for (const auto & grid : plan.tilesAtLevel(10)) {emitted.insert(grid);}
  gggs::GridIndex a = g;
  while (a.level() > 10) {a = gggs::parent(a);}
  EXPECT_TRUE(emitted.count(a));
  for (const auto & grid : emitted) {
    EXPECT_TRUE(plan.isTouched(grid));
  }
  // Nothing is refined when the policy has one level.
  for (const auto & [grid, tile] : plan.tiles()) {
    EXPECT_FALSE(tile.refined);
  }
}

TEST_F(LevelPlanTest, CanonicalJsonRoundTripsAndIsOrderIndependent)
{
  const auto shoal = l14(kLat, kLon);
  const auto block12 = gggs::parent(gggs::parent(shoal));
  for (const auto & g : l14Inside(block12)) {
    fillGrid(g, 6, g == shoal ? -3.0f : -40.0f, 1.5);
  }
  const auto plan = levelPlanFor(counts, depths, policy);
  const std::string json = plan.toJson();
  EXPECT_EQ(json.find(' '), std::string::npos) << "canonical JSON carries no whitespace";

  const auto back = LevelPlan::fromJson(json);
  EXPECT_EQ(back.toJson(), json);
  EXPECT_EQ(back.tiles().size(), plan.tiles().size());
  EXPECT_EQ(back.levels(), plan.levels());
  for (const auto & [grid, tile] : plan.tiles()) {
    ASSERT_TRUE(back.isEmitted(grid));
    const auto & b = back.tiles().at(grid);
    EXPECT_EQ(b.required_level, tile.required_level);
    EXPECT_EQ(b.achieved_level, tile.achieved_level);
    EXPECT_FLOAT_EQ(b.decision_depth, tile.decision_depth);
    EXPECT_EQ(b.refined, tile.refined);
    EXPECT_TRUE(back.isTouched(grid));
  }
  EXPECT_EQ(back.policy().count_level, policy.count_level);

  // Order independence: build the same survey with the grids filled in the
  // reverse order; the bytes are identical.
  CountGrid counts2(14);
  std::map<gggs::GridIndex, float> depths2;
  auto grids = l14Inside(block12);
  std::reverse(grids.begin(), grids.end());
  for (const auto & g : grids) {
    for (uint16_t r = 0; r < CountGrid::kEdge; r += 4) {
      for (uint16_t c = 0; c < CountGrid::kEdge; c += 4) {
        for (int i = 0; i < 6; ++i) {counts2.add(gggs::CellIndex(g, r, c), 1.5);}
      }
    }
    depths2[g] = (g == shoal) ? -3.0f : -40.0f;
  }
  EXPECT_EQ(levelPlanFor(counts2, depths2, policy).toJson(), json);

  EXPECT_THROW(LevelPlan::fromJson("not json"), std::runtime_error);
  EXPECT_THROW(LevelPlan::fromJson("{\"schema\":2}"), std::runtime_error);
}

TEST_F(LevelPlanTest, ReportStatesStorageDeficitAndMultiplier)
{
  const auto g = l14(kLat, kLon);
  fillGrid(g, 6, -40.0f);
  const auto plan = levelPlanFor(counts, depths, policy);
  const std::string r = plan.report(2.42e6);
  EXPECT_NE(r.find("level  cell(m)"), std::string::npos);
  EXPECT_NE(r.find("estimate-count multiplier"), std::string::npos);
  EXPECT_NE(r.find("coverage deficit"), std::string::npos);
  EXPECT_NE(r.find("coarser than level 10"), std::string::npos);
}

}  // namespace cube
