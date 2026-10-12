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
#include <limits>
#include <string>

#include "coverage_assessment/source_accumulator.h"
#include "message_fixtures.h"

namespace coverage_assessment
{

namespace
{


// Lattice cells in [i0, i1) x [j0, j1) holding a finite elevation.
std::size_t countCovered(
  const SourceAccumulator & acc, int64_t i0, int64_t j0, int64_t i1, int64_t j1)
{
  std::size_t n = 0;
  for (int64_t j = j0; j < j1; ++j) {
    for (int64_t i = i0; i < i1; ++i) {
      if (std::isfinite(acc.get(i, j).elevation)) {
        ++n;
      }
    }
  }
  return n;
}

}  // namespace

TEST(SourceAccumulator, AlignedMessageLandsCellForCell)
{
  // Message cell values encode their own lattice index, so a lookup must
  // return exactly the value of the cell with the same centre.
  SourceAccumulator acc(1.0);
  const auto msg = makeMessage(
    -5.0, -3.0, 10, 6, 1.0, [](double cx, double cy, float * e, float * u) {
      *e = static_cast<float>(std::floor(cx) * 1000.0 + std::floor(cy));
      *u = 0.5f;
    });
  const AddResult r = acc.add(msg, Affine2());
  EXPECT_EQ(r.status, AddStatus::kAdded);
  EXPECT_TRUE(r.has_uncertainty_layer);
  EXPECT_EQ(r.cells_in_message, 60u);
  EXPECT_EQ(acc.elevationCellCount(), 60u);
  for (int64_t j = -3; j < 3; ++j) {
    for (int64_t i = -5; i < 5; ++i) {
      EXPECT_EQ(acc.get(i, j).elevation, static_cast<float>(i * 1000 + j)) << i << "," << j;
      EXPECT_EQ(acc.get(i, j).uncertainty, 0.5f);
    }
  }
  EXPECT_TRUE(std::isnan(acc.get(5, 0).elevation));
  EXPECT_TRUE(std::isnan(acc.get(0, 3).elevation));
  EXPECT_TRUE(std::isnan(acc.get(-6, 0).elevation));
  EXPECT_TRUE(std::isnan(acc.get(1000, -1000).elevation));
}

// A message offset by a fraction of a cell is pulled, not pushed: each lattice
// cell takes the message cell that contains its centre.
TEST(SourceAccumulator, PhaseShiftedMessageTakesTheCellContainingTheCentre)
{
  const double phase = 0.37;
  SourceAccumulator acc(1.0);
  const auto msg = makeMessage(
    phase, phase, 12, 12, 1.0, [ = ](double cx, double cy, float * e, float * u) {
      // Index of this message cell: its lower-left corner is (k + phase).
      const double kx = std::floor(cx - phase), ky = std::floor(cy - phase);
      *e = static_cast<float>(kx * 100.0 + ky);
      *u = 0.25f;
    });
  acc.add(msg, Affine2());
  // Lattice centre (i + 0.5) lies in message cell k = floor(i + 0.5 - phase).
  for (int64_t j = 1; j < 12; ++j) {
    for (int64_t i = 1; i < 12; ++i) {
      const double kx = std::floor(static_cast<double>(i) + 0.5 - phase);
      const double ky = std::floor(static_cast<double>(j) + 0.5 - phase);
      EXPECT_EQ(acc.get(i, j).elevation, static_cast<float>(kx * 100.0 + ky)) << i << "," << j;
    }
  }
  // Lattice cell 0 has its centre at 0.5, inside message cell 0 (0.37 to 1.37).
  EXPECT_EQ(acc.get(0, 0).elevation, 0.0f);
  // Past the far edge (12.37): centre 12.5 is outside; centre 11.5 is inside.
  EXPECT_TRUE(std::isnan(acc.get(12, 5).elevation));
  EXPECT_TRUE(std::isfinite(acc.get(11, 5).elevation));
}

// The same surface sampled at different phases gives the same coverage.
TEST(SourceAccumulator, PhaseShiftedMessagesOfOneSurfaceGiveIdenticalCoverage)
{
  // Surface: a 20 x 10 m rectangle [5,25) x [5,15), sampled at message cell
  // centres. Phases avoid an exact half cell, where a centre can sit on the edge.
  for (double phase : {0.0, 0.1, 0.37, 0.81}) {
    SourceAccumulator acc(1.0);
    const auto msg = makeMessage(
      phase - 3.0, phase - 3.0, 36, 26, 1.0, [](double cx, double cy, float * e, float * u) {
        if (cx >= 5.0 && cx <= 25.0 && cy >= 5.0 && cy <= 15.0) {
          *e = -30.0f;
          *u = 0.4f;
        }
      });
    acc.add(msg, Affine2());
    EXPECT_EQ(acc.elevationCellCount(), 200u) << "phase " << phase;
    EXPECT_EQ(countCovered(acc, 0, 0, 40, 30), 200u) << "phase " << phase;
    EXPECT_EQ(countCovered(acc, 5, 5, 25, 15), 200u) << "phase " << phase;
  }
}

// A message in a frame rotated by 90 degrees and translated is the same message
// as one in the raster frame once the affine is given. Values encode the
// message's own cell index, so the mapping is checked cell by cell.
TEST(SourceAccumulator, RotatedTranslatedFrameMapsEveryCell)
{
  // source = [0 -1; 1 0] * raster + (30.25, 7.5): raster (x, y) -> (30.25 - y, 7.5 + x).
  Affine2 t;
  t.a = 0.0;
  t.b = -1.0;
  t.c = 1.0;
  t.d = 0.0;
  t.tx = 30.25;
  t.ty = 7.5;
  // Message covers source x in [0.25, 40.25), y in [0.5, 30.5): raster x in
  // [-7, 23), y in [-10, 30).
  const auto msg = makeMessage(
    0.25, 0.5, 40, 30, 1.0, [](double cx, double cy, float * e, float * u) {
      *e = static_cast<float>(std::floor(cx - 0.25) * 100.0 + std::floor(cy - 0.5));
      *u = 0.3f;
    });
  SourceAccumulator acc(1.0);
  const AddResult r = acc.add(msg, t);
  EXPECT_EQ(r.status, AddStatus::kAdded);
  EXPECT_EQ(r.cells_in_message, 1200u);
  EXPECT_EQ(acc.elevationCellCount(), 1200u);
  for (int64_t j = -10; j < 30; ++j) {
    for (int64_t i = -7; i < 23; ++i) {
      // Lattice centre (i + .5, j + .5) -> source (30.25 - (j + .5), 7.5 + (i + .5)).
      const double sx = 30.25 - (static_cast<double>(j) + 0.5);
      const double sy = 7.5 + (static_cast<double>(i) + 0.5);
      const float want = static_cast<float>(std::floor(sx - 0.25) * 100.0 + std::floor(sy - 0.5));
      EXPECT_EQ(acc.get(i, j).elevation, want) << i << "," << j;
    }
  }
  EXPECT_TRUE(std::isnan(acc.get(-8, 0).elevation));
  EXPECT_TRUE(std::isnan(acc.get(23, 0).elevation));
  EXPECT_TRUE(std::isnan(acc.get(0, -11).elevation));
  EXPECT_TRUE(std::isnan(acc.get(0, 30).elevation));
}

// A message in a rotated frame gives the same coverage as the same message in
// the raster frame, for a rotation that is not a multiple of 90 degrees.
TEST(SourceAccumulator, RotatedFrameGivesTheSameCoverageInTheInterior)
{
  SourceAccumulator in_raster(1.0);
  in_raster.add(constantMessage(-30.0, -30.0, 60, 60, -20.0f, 0.5f), Affine2());

  // Raster -> source: rotate by 30 degrees about the origin, then translate. The
  // source message is a 90 m square centred on the image of the raster origin,
  // large enough to hold the whole interior region tested.
  const double th = 30.0 * M_PI / 180.0;
  Affine2 t;
  t.a = std::cos(th);
  t.b = -std::sin(th);
  t.c = std::sin(th);
  t.d = std::cos(th);
  t.tx = 500.0;
  t.ty = -200.0;
  SourceAccumulator in_rotated(1.0);
  const auto msg = constantMessage(455.0, -245.0, 90, 90, -20.0f, 0.5f);
  in_rotated.add(msg, t);

  // 40 x 40 interior of the raster-frame message: every cell in both.
  EXPECT_EQ(countCovered(in_raster, -20, -20, 20, 20), 1600u);
  EXPECT_EQ(countCovered(in_rotated, -20, -20, 20, 20), 1600u);
  for (int64_t j = -20; j < 20; ++j) {
    for (int64_t i = -20; i < 20; ++i) {
      EXPECT_EQ(in_rotated.get(i, j).elevation, -20.0f);
      EXPECT_EQ(in_rotated.get(i, j).uncertainty, 0.5f);
    }
  }
  // Far from the message nothing is held.
  EXPECT_EQ(countCovered(in_rotated, 100, 100, 140, 140), 0u);
  EXPECT_EQ(countCovered(in_rotated, -140, -140, -100, -100), 0u);
}

TEST(SourceAccumulator, NanNeverErasesEitherFieldAndFiniteOverwrites)
{
  SourceAccumulator acc(1.0);
  const auto at = [&acc]() {return acc.get(2, 3);};
  acc.add(constantMessage(0.0, 0.0, 5, 5, -30.0f, 0.5f), Affine2());
  EXPECT_EQ(at().elevation, -30.0f);
  EXPECT_EQ(at().uncertainty, 0.5f);

  // Finite elevation, NaN uncertainty: elevation moves, uncertainty is kept.
  acc.add(constantMessage(0.0, 0.0, 5, 5, -31.0f, kNan), Affine2());
  EXPECT_EQ(at().elevation, -31.0f);
  EXPECT_EQ(at().uncertainty, 0.5f);

  // NaN elevation, finite uncertainty: uncertainty moves, elevation is kept.
  acc.add(constantMessage(0.0, 0.0, 5, 5, kNan, 0.7f), Affine2());
  EXPECT_EQ(at().elevation, -31.0f);
  EXPECT_EQ(at().uncertainty, 0.7f);

  // Both NaN: nothing changes.
  acc.add(constantMessage(0.0, 0.0, 5, 5, kNan, kNan), Affine2());
  EXPECT_EQ(at().elevation, -31.0f);
  EXPECT_EQ(at().uncertainty, 0.7f);

  // Later finite values overwrite both.
  acc.add(constantMessage(0.0, 0.0, 5, 5, -35.0f, 0.2f), Affine2());
  EXPECT_EQ(at().elevation, -35.0f);
  EXPECT_EQ(at().uncertainty, 0.2f);
  EXPECT_EQ(acc.elevationCellCount(), 25u);
}

TEST(SourceAccumulator, ElevationWithoutUncertaintyIsHeld)
{
  SourceAccumulator acc(1.0);
  acc.add(constantMessage(0.0, 0.0, 4, 4, -12.5f, kNan), Affine2());
  EXPECT_EQ(acc.get(1, 1).elevation, -12.5f);
  EXPECT_TRUE(std::isnan(acc.get(1, 1).uncertainty));
  EXPECT_EQ(acc.elevationCellCount(), 16u);
}

TEST(SourceAccumulator, UncertaintyWithoutElevationDoesNotCountAsCovered)
{
  SourceAccumulator acc(1.0);
  acc.add(constantMessage(0.0, 0.0, 4, 4, kNan, 0.4f), Affine2());
  EXPECT_TRUE(std::isnan(acc.get(1, 1).elevation));
  EXPECT_EQ(acc.get(1, 1).uncertainty, 0.4f);
  EXPECT_EQ(acc.elevationCellCount(), 0u);
}

// Three windows sweep a 50 x 20 m strip like the live node's 200 m window does:
// coverage is the union and never decreases, and a later all-NaN window over
// the same ground erases nothing.
TEST(SourceAccumulator, MovingWindowKeepsTheUnionAndNeverDecreases)
{
  SourceAccumulator acc(1.0);
  std::size_t last = 0;
  acc.add(constantMessage(0.0, 0.0, 20, 20, -20.0f, 0.5f), Affine2());
  EXPECT_EQ(acc.elevationCellCount(), 400u);
  EXPECT_GE(acc.elevationCellCount(), last);
  last = acc.elevationCellCount();

  acc.add(constantMessage(15.0, 0.0, 20, 20, -21.0f, 0.6f), Affine2());  // x 15..35
  EXPECT_EQ(acc.elevationCellCount(), 700u);  // columns 0..34
  EXPECT_GE(acc.elevationCellCount(), last);
  last = acc.elevationCellCount();

  acc.add(constantMessage(30.4, 0.0, 20, 20, -22.0f, 0.7f), Affine2());  // x 30.4..50.4
  EXPECT_EQ(acc.elevationCellCount(), 1000u);  // columns 0..49
  EXPECT_GE(acc.elevationCellCount(), last);
  last = acc.elevationCellCount();

  // The first window's ground is still held with its own values where no later
  // window reached; the overlap holds the later window's.
  EXPECT_EQ(acc.get(5, 5).elevation, -20.0f);
  EXPECT_EQ(acc.get(25, 5).elevation, -21.0f);
  EXPECT_EQ(acc.get(45, 5).elevation, -22.0f);
  EXPECT_EQ(countCovered(acc, 0, 0, 50, 20), 1000u);

  // A window with nothing finite over the whole strip changes nothing.
  acc.add(constantMessage(0.0, 0.0, 50, 20, kNan, kNan), Affine2());
  EXPECT_EQ(acc.elevationCellCount(), 1000u);
  EXPECT_EQ(acc.get(5, 5).elevation, -20.0f);
  EXPECT_EQ(acc.get(45, 5).uncertainty, 0.7f);
}

TEST(SourceAccumulator, ClearEmptiesEverything)
{
  SourceAccumulator acc(1.0);
  acc.add(constantMessage(-100.0, -100.0, 200, 200, -9.0f, 0.1f), Affine2());
  EXPECT_EQ(acc.elevationCellCount(), 40000u);
  EXPECT_GT(acc.blockCount(), 0u);
  EXPECT_GT(acc.memoryBytes(), 0u);
  acc.clear();
  EXPECT_EQ(acc.elevationCellCount(), 0u);
  EXPECT_EQ(acc.blockCount(), 0u);
  EXPECT_EQ(acc.memoryBytes(), 0u);
  EXPECT_TRUE(std::isnan(acc.get(0, 0).elevation));
  EXPECT_TRUE(std::isnan(acc.get(0, 0).uncertainty));
  // It works again after a clear.
  acc.add(constantMessage(0.0, 0.0, 3, 3, -9.0f, 0.1f), Affine2());
  EXPECT_EQ(acc.elevationCellCount(), 9u);
}

// Negative lattice indices page into negative blocks: a message across the
// origin keeps both sides.
TEST(SourceAccumulator, BlocksAcrossTheOriginKeepBothSides)
{
  SourceAccumulator acc(1.0);
  acc.add(constantMessage(-70.0, -70.0, 140, 140, -9.0f, 0.1f), Affine2());
  EXPECT_EQ(acc.elevationCellCount(), 19600u);
  EXPECT_EQ(acc.get(-70, -70).elevation, -9.0f);
  EXPECT_EQ(acc.get(-1, -1).elevation, -9.0f);
  EXPECT_EQ(acc.get(0, 0).elevation, -9.0f);
  EXPECT_EQ(acc.get(69, 69).elevation, -9.0f);
  EXPECT_TRUE(std::isnan(acc.get(70, 0).elevation));
  EXPECT_TRUE(std::isnan(acc.get(-71, 0).elevation));
}

// Lattice cell size is the accumulator's, not the message's.
TEST(SourceAccumulator, FinerMessageResolutionIsResampledToTheLattice)
{
  SourceAccumulator acc(1.0);
  const auto msg = makeMessage(
    0.0, 0.0, 40, 40, 0.25, [](double, double, float * e, float * u) {
      *e = -8.0f;
      *u = 0.2f;
    });  // 10 x 10 m at 0.25 m
  const AddResult r = acc.add(msg, Affine2());
  EXPECT_EQ(r.cells_in_message, 100u);
  EXPECT_EQ(acc.elevationCellCount(), 100u);
}

TEST(SourceAccumulator, MissingElevationLayerStoresNothing)
{
  SourceAccumulator acc(1.0);
  grid_map::GridMap m({"uncertainty"});
  m.setGeometry(grid_map::Length(4.0, 4.0), 1.0, grid_map::Position(2.0, 2.0));
  m.add("uncertainty", 0.5f);
  const AddResult r = acc.add(m, Affine2());
  EXPECT_EQ(r.status, AddStatus::kMissingElevationLayer);
  EXPECT_EQ(acc.elevationCellCount(), 0u);
  EXPECT_EQ(acc.blockCount(), 0u);
}

TEST(SourceAccumulator, MissingUncertaintyLayerStillHoldsElevation)
{
  SourceAccumulator acc(1.0);
  grid_map::GridMap m({"elevation"});
  m.setGeometry(grid_map::Length(4.0, 4.0), 1.0, grid_map::Position(2.0, 2.0));
  m.add("elevation", -7.0f);
  const AddResult r = acc.add(m, Affine2());
  EXPECT_EQ(r.status, AddStatus::kAdded);
  EXPECT_FALSE(r.has_uncertainty_layer);
  EXPECT_EQ(acc.get(1, 1).elevation, -7.0f);
  EXPECT_TRUE(std::isnan(acc.get(1, 1).uncertainty));
}

TEST(SourceAccumulator, BadGeometryIsRefusedAndStoresNothing)
{
  SourceAccumulator acc(1.0);
  const auto msg = constantMessage(0.0, 0.0, 4, 4, -9.0f, 0.1f);

  Affine2 singular;
  singular.a = 1.0;
  singular.b = 2.0;
  singular.c = 2.0;
  singular.d = 4.0;
  EXPECT_EQ(acc.add(msg, singular).status, AddStatus::kBadGeometry);

  Affine2 nonfinite;
  nonfinite.tx = std::numeric_limits<double>::quiet_NaN();
  EXPECT_EQ(acc.add(msg, nonfinite).status, AddStatus::kBadGeometry);

  // A scale that blows the footprint past the guard.
  Affine2 tiny;
  tiny.a = 1.0e-9;
  tiny.d = 1.0e-9;
  EXPECT_EQ(acc.add(msg, tiny).status, AddStatus::kOutOfRange);

  grid_map::GridMap empty({"elevation", "uncertainty"});
  EXPECT_EQ(acc.add(empty, Affine2()).status, AddStatus::kBadGeometry);

  EXPECT_EQ(acc.elevationCellCount(), 0u);
  EXPECT_EQ(acc.blockCount(), 0u);
}

// Footprints beyond the stated bounds are refused with a reason before any
// double -> int64 conversion (a huge finite value used to be undefined behaviour).
TEST(SourceAccumulator, FootprintBeyondTheCoordinateBoundIsRefusedWithAReason)
{
  const auto msg = constantMessage(0.0, 0.0, 4, 4, -9.0f, 0.1f);
  SourceAccumulator acc(1.0);

  // Just inside the bound: a 4 m message whose far corner lands at 1e7 - 1.
  Affine2 inside;
  // raster_to_source maps raster -> source; source_to_raster is its inverse, so a
  // raster_to_source translation of -T places the message at +T in the raster.
  inside.tx = -(kMaxRasterCoordinate - 5.0);
  EXPECT_EQ(acc.add(msg, inside).status, AddStatus::kAdded);
  EXPECT_GT(acc.elevationCellCount(), 0u);

  // Just outside: the far corner at 1e7 + 1.
  SourceAccumulator out_acc(1.0);
  Affine2 outside;
  outside.tx = -(kMaxRasterCoordinate - 3.0);
  const AddResult r = out_acc.add(msg, outside);
  EXPECT_EQ(r.status, AddStatus::kOutOfRange);
  EXPECT_FALSE(r.reason.empty());
  EXPECT_EQ(out_acc.elevationCellCount(), 0u);
  EXPECT_EQ(out_acc.blockCount(), 0u);

  // Huge finite and overflowing values: no conversion UB, a refusal.
  for (const double huge : {1.0e18, 1.0e300, -1.0e300}) {
    SourceAccumulator a(1.0);
    Affine2 t;
    t.tx = huge;
    EXPECT_EQ(a.add(msg, t).status, AddStatus::kOutOfRange) << huge;
    EXPECT_EQ(a.blockCount(), 0u);
  }

  // A tiny cell size pushes the lattice index past its bound before the
  // coordinate bound.
  SourceAccumulator fine(1.0e-6);
  const AddResult f = fine.add(constantMessage(1.0e7 - 100.0, 0.0, 4, 4, -9.0f, 0.1f), Affine2());
  EXPECT_EQ(f.status, AddStatus::kOutOfRange);
}

TEST(SourceAccumulator, FootprintOverTheCellCapIsRefusedAtTheBoundary)
{
  // The footprint is the lattice box around the message (a message edge that
  // falls on a lattice line adds an index): a 2000 x 2000 cell message centred on
  // the origin spans 2001 x 2001 = 4,004,001 indices, exactly the cap, which is
  // CUBE's 200 m window at 0.1 m (2000 x 2000 cells) and must pass. One cell more
  // on one axis is over the cap.
  static_assert(SourceAccumulator::kMaxFootprintCells == 2001.0 * 2001.0,
    "the cap admits CUBE's 200 m window at 0.1 m");
  SourceAccumulator at_cap(1.0);
  const auto ok = constantMessage(0.0, 0.0, 2000, 2000, -9.0f, 0.1f);
  const AddResult r_ok = at_cap.add(ok, Affine2());
  EXPECT_EQ(r_ok.status, AddStatus::kAdded);
  EXPECT_EQ(r_ok.cells_in_message, 2000u * 2000u);

  SourceAccumulator over(1.0);
  const auto big = constantMessage(0.0, 0.0, 2000, 2001, -9.0f, 0.1f);
  const AddResult r = over.add(big, Affine2());
  EXPECT_EQ(r.status, AddStatus::kFootprintTooLarge);
  EXPECT_NE(r.reason.find("lattice cells"), std::string::npos) << r.reason;
  EXPECT_EQ(over.elevationCellCount(), 0u);
}

TEST(SourceAccumulator, NegativeUncertaintyNeverOverwritesAHeldValue)
{
  SourceAccumulator acc(1.0);
  acc.add(constantMessage(0.0, 0.0, 4, 4, -9.0f, 0.25f), Affine2());
  // Negative finite uncertainty (and -0 stays a valid zero, below) with a fresh
  // elevation: the elevation updates, the held uncertainty stays.
  acc.add(constantMessage(0.0, 0.0, 4, 4, -8.0f, -0.5f), Affine2());
  EXPECT_EQ(acc.get(1, 1).elevation, -8.0f);
  EXPECT_EQ(acc.get(1, 1).uncertainty, 0.25f);
  // On a cell with no held uncertainty a negative one is not stored either.
  acc.add(constantMessage(10.0, 10.0, 2, 2, -7.0f, -1.0f), Affine2());
  EXPECT_EQ(acc.get(10, 10).elevation, -7.0f);
  EXPECT_TRUE(std::isnan(acc.get(10, 10).uncertainty));
  // Zero is a legitimate uncertainty.
  acc.add(constantMessage(0.0, 0.0, 4, 4, -8.0f, 0.0f), Affine2());
  EXPECT_EQ(acc.get(1, 1).uncertainty, 0.0f);
}

// grid_map stores a circular buffer: after move() the start index is not zero and
// storage order differs from spatial order. Cells are addressed by position, so
// the accumulator must be unaffected.
TEST(SourceAccumulator, CircularBufferWithNonZeroStartIndexLandsCellForCell)
{
  grid_map::GridMap m({"elevation", "uncertainty"});
  m.setFrameId("src");
  m.setGeometry(grid_map::Length(10.0, 8.0), 1.0, grid_map::Position(5.0, 4.0));
  // Slide the window by (3, 2) cells: the buffer wraps and the start index moves.
  m.move(grid_map::Position(8.0, 6.0));
  ASSERT_NE(m.getStartIndex()(0), 0);
  ASSERT_NE(m.getStartIndex()(1), 0);
  for (grid_map::GridMapIterator it(m); !it.isPastEnd(); ++it) {
      grid_map::Position p;
      m.getPosition(*it, p);
      m.at("elevation", *it) = static_cast<float>(std::floor(p.x()) * 1000.0 + std::floor(p.y()));
      m.at("uncertainty", *it) = static_cast<float>(std::floor(p.x()) * 0.01);
  }
  SourceAccumulator acc(1.0);
  const AddResult r = acc.add(m, Affine2());
  ASSERT_EQ(r.status, AddStatus::kAdded);
  EXPECT_EQ(r.cells_in_message, 80u);
  // The window now spans x in [3, 13), y in [2, 10).
  for (int64_t j = 2; j < 10; ++j) {
    for (int64_t i = 3; i < 13; ++i) {
      EXPECT_EQ(acc.get(i, j).elevation, static_cast<float>(i * 1000 + j)) << i << "," << j;
      EXPECT_FLOAT_EQ(acc.get(i, j).uncertainty, static_cast<float>(i * 0.01)) << i << "," << j;
    }
  }
  EXPECT_TRUE(std::isnan(acc.get(2, 5).elevation));
  EXPECT_TRUE(std::isnan(acc.get(13, 5).elevation));
  EXPECT_TRUE(std::isnan(acc.get(5, 1).elevation));
  EXPECT_TRUE(std::isnan(acc.get(5, 10).elevation));
}

TEST(SourceAccumulator, RejectsBadCellSize)
{
  EXPECT_THROW(SourceAccumulator(0.0), std::invalid_argument);
  EXPECT_THROW(SourceAccumulator(-1.0), std::invalid_argument);
  EXPECT_THROW(SourceAccumulator(std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);
}

TEST(Affine2, InverseUndoesApply)
{
  Affine2 t;
  t.a = 0.6;
  t.b = -0.8;
  t.c = 0.8;
  t.d = 0.6;
  t.tx = 12.5;
  t.ty = -3.25;
  Affine2 inv;
  ASSERT_TRUE(t.inverse(&inv));
  const Point2 p = inv.apply(t.apply({7.0, -2.0}));
  EXPECT_NEAR(p.x, 7.0, 1e-12);
  EXPECT_NEAR(p.y, -2.0, 1e-12);
}

}  // namespace coverage_assessment
