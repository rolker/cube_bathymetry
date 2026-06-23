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

#include "../src/chart_import.hpp"

namespace
{

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kMassabesicDatum = 48.88;   // full-pool lake surface, ellipsoidal

using cube_bathymetry::chartCellFromContour;

// depth below surface -5.0 m at datum 48.88 -> ellipsoidal 43.88 m.
TEST(ChartImport, DepthBelowSurfaceToEllipsoidal)
{
  const auto cell = chartCellFromContour(-5.0, 3.0, 0.0, kMassabesicDatum);
  ASSERT_TRUE(cell.hasData());
  EXPECT_DOUBLE_EQ(cell.depth, 43.88);
}

// A zero-depth cell sits exactly at the lake surface (= the datum height).
TEST(ChartImport, ZeroDepthSitsAtSurface)
{
  const auto cell = chartCellFromContour(0.0, 3.0, 0.0, kMassabesicDatum);
  ASSERT_TRUE(cell.hasData());
  EXPECT_DOUBLE_EQ(cell.depth, kMassabesicDatum);
}

// Deeper water -> lower ellipsoidal height; sign convention is negative-down.
TEST(ChartImport, DeeperIsLower)
{
  const auto shallow = chartCellFromContour(-2.0, 3.0, 0.0, kMassabesicDatum);
  const auto deep = chartCellFromContour(-20.0, 3.0, 0.0, kMassabesicDatum);
  EXPECT_DOUBLE_EQ(shallow.depth, 46.88);
  EXPECT_DOUBLE_EQ(deep.depth, 28.88);
  EXPECT_LT(deep.depth, shallow.depth);
}

// Band-1 NaN is the no-data sentinel: the cell stays no-data regardless of datum.
TEST(ChartImport, NanDepthIsNoData)
{
  const auto cell = chartCellFromContour(kNaN, 3.0, 0.0, kMassabesicDatum);
  EXPECT_FALSE(cell.hasData());
  EXPECT_TRUE(std::isnan(cell.depth));
  // No-data cells carry no timestamp/source either.
  EXPECT_EQ(cell.timestamp, 0);
  EXPECT_EQ(cell.source_index, 0);
}

// Uncertainty (band 2) is carried through verbatim.
TEST(ChartImport, UncertaintyCarriedThrough)
{
  const auto cell = chartCellFromContour(-5.0, 2.5, 0.0, kMassabesicDatum);
  EXPECT_DOUBLE_EQ(cell.uncertainty, 2.5);
}

// An unknown uncertainty (NaN) is preserved as NaN on a valid-depth cell.
TEST(ChartImport, NanUncertaintyPreservedOnValidDepth)
{
  const auto cell = chartCellFromContour(-5.0, kNaN, 0.0, kMassabesicDatum);
  ASSERT_TRUE(cell.hasData());
  EXPECT_TRUE(std::isnan(cell.uncertainty));
}

// Legacy seconds timestamp is converted to int64 nanoseconds.
TEST(ChartImport, TimestampSecondsToNanoseconds)
{
  // 1_700_000_000.5 s -> 1_700_000_000_500_000_000 ns.
  const auto cell = chartCellFromContour(-5.0, 3.0, 1700000000.5, kMassabesicDatum);
  EXPECT_EQ(cell.timestamp, static_cast<int64_t>(1700000000500000000LL));
}

// A 0 timestamp means "unset" -> stays 0 (we never fabricate a time).
TEST(ChartImport, ZeroTimestampStaysUnset)
{
  const auto cell = chartCellFromContour(-5.0, 3.0, 0.0, kMassabesicDatum);
  EXPECT_EQ(cell.timestamp, 0);
}

// A NaN timestamp is treated as unset, not converted.
TEST(ChartImport, NanTimestampStaysUnset)
{
  const auto cell = chartCellFromContour(-5.0, 3.0, kNaN, kMassabesicDatum);
  ASSERT_TRUE(cell.hasData());
  EXPECT_EQ(cell.timestamp, 0);
}

// The Chart prior is a single read-only source: source_index is always 0.
TEST(ChartImport, SourceIndexIsZero)
{
  const auto cell = chartCellFromContour(-5.0, 3.0, 1700000000.0, kMassabesicDatum);
  EXPECT_EQ(cell.source_index, 0);
}

}  // namespace
