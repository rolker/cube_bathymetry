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
#include <vector>

#include "coverage_assessment/polygon_raster.h"

namespace coverage_assessment
{

namespace
{

const int64_t kBig = 1000000;

std::vector<Point2> rect(double x0, double y0, double x1, double y1)
{
  return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
}

}  // namespace

// A 10 m x 10 m square holds exactly 100 one-metre cells at any offset on the
// lattice, including offsets that put its edges on a centre line.
TEST(PolygonRaster, SquaresHoldExactAreaAtAnyOffset)
{
  for (double off : {0.0, 0.1, 0.25, 0.5, 0.75, 0.999, 3.37, -0.5, -7.3}) {
    const auto r = rasterisePolygon(rect(off, off + 2.0, off + 8.0, off + 12.0), 1.0, kBig);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.mask.count, 80u) << "offset " << off;  // 8 wide x 10 tall
  }
  for (double off : {0.0, 0.1, 0.5, 0.75, -3.3}) {
    const auto r = rasterisePolygon(rect(off, off, off + 10.0, off + 10.0), 1.0, kBig);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.mask.count, 100u) << "offset " << off;
  }
}

TEST(PolygonRaster, CellSizeScalesTheCount)
{
  const auto r = rasterisePolygon(rect(0.0, 0.0, 10.0, 6.0), 0.5, kBig);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.mask.count, 240u);  // 20 x 12 cells of 0.5 m
}

// Edges lying exactly on cell-centre lines: a centre on a lower/left edge is in,
// on an upper/right edge is out (documented half-open rule).
TEST(PolygonRaster, EdgeOnCentreLineIsHalfOpen)
{
  const auto r = rasterisePolygon(rect(0.5, 0.5, 10.5, 10.5), 1.0, kBig);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.mask.count, 100u);
  EXPECT_TRUE(r.mask.contains(0, 0));    // centre (0.5, 0.5) on the lower-left corner
  EXPECT_TRUE(r.mask.contains(9, 9));    // centre (9.5, 9.5)
  EXPECT_FALSE(r.mask.contains(10, 5));  // centre (10.5, 5.5) on the right edge
  EXPECT_FALSE(r.mask.contains(5, 10));  // centre (5.5, 10.5) on the top edge
}

// Two squares sharing an edge on a centre line partition the cells: no cell is
// counted twice and none is lost.
TEST(PolygonRaster, AdjacentPolygonsPartitionTheCells)
{
  const auto a = rasterisePolygon(rect(0.0, 0.0, 5.5, 4.0), 1.0, kBig);
  const auto b = rasterisePolygon(rect(5.5, 0.0, 10.0, 4.0), 1.0, kBig);
  const auto whole = rasterisePolygon(rect(0.0, 0.0, 10.0, 4.0), 1.0, kBig);
  ASSERT_TRUE(a.ok && b.ok && whole.ok);
  EXPECT_EQ(a.mask.count + b.mask.count, whole.mask.count);
  EXPECT_EQ(whole.mask.count, 40u);
  for (int64_t j = 0; j < 4; ++j) {
    for (int64_t i = 0; i < 10; ++i) {
      EXPECT_NE(a.mask.contains(i, j), b.mask.contains(i, j)) << i << "," << j;
    }
  }
}

// Right triangle (0,0) (20,0) (0,20): analytic area 200. Centres (i+.5, j+.5)
// with i + j + 1 < 20 are inside: sum over s = 0..18 of (s + 1) = 190, within
// one boundary row (20 cells) of the analytic area.
TEST(PolygonRaster, TriangleMatchesAnalyticAreaWithinOneBoundaryRow)
{
  const auto r = rasterisePolygon({{0.0, 0.0}, {20.0, 0.0}, {0.0, 20.0}}, 1.0, kBig);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.mask.count, 190u);
  EXPECT_LE(std::fabs(200.0 - static_cast<double>(r.mask.count)), 20.0);
  EXPECT_TRUE(r.mask.contains(0, 18));
  EXPECT_FALSE(r.mask.contains(0, 19));  // centre (0.5, 19.5): 20.0 is on the hypotenuse
  EXPECT_FALSE(r.mask.contains(10, 10));
}

// L shape: a 10x10 square minus its upper-right 5x5 quarter = 75 cells, and the
// notch really is empty.
TEST(PolygonRaster, LShapeLeavesTheNotchEmpty)
{
  const auto r = rasterisePolygon(
    {{0.0, 0.0}, {10.0, 0.0}, {10.0, 5.0}, {5.0, 5.0}, {5.0, 10.0}, {0.0, 10.0}}, 1.0, kBig);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.mask.count, 75u);
  EXPECT_TRUE(r.mask.contains(2, 8));
  EXPECT_TRUE(r.mask.contains(8, 2));
  EXPECT_FALSE(r.mask.contains(7, 7));
}

TEST(PolygonRaster, NegativeCoordinatesAndLatticeIndices)
{
  const auto r = rasterisePolygon(rect(-12.0, -6.0, -2.0, -1.0), 1.0, kBig);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.mask.count, 50u);
  EXPECT_EQ(r.mask.i0, -12);
  EXPECT_EQ(r.mask.j0, -6);
  EXPECT_EQ(r.mask.ni, 10);  // centres -11.5 .. -2.5: the box holds no empty column
  EXPECT_EQ(r.mask.nj, 5);
  EXPECT_TRUE(r.mask.contains(-12, -6));
  EXPECT_TRUE(r.mask.contains(-3, -2));
  EXPECT_FALSE(r.mask.contains(-2, -2));
  EXPECT_FALSE(r.mask.contains(-13, -3));
}

// Either vertex order is the same polygon.
TEST(PolygonRaster, WindingDirectionDoesNotMatter)
{
  auto cw = rect(0.0, 0.0, 7.0, 3.0);
  std::vector<Point2> ccw(cw.rbegin(), cw.rend());
  const auto a = rasterisePolygon(cw, 1.0, kBig);
  const auto b = rasterisePolygon(ccw, 1.0, kBig);
  ASSERT_TRUE(a.ok && b.ok);
  EXPECT_EQ(a.mask.count, 21u);
  EXPECT_EQ(b.mask.count, 21u);
  EXPECT_EQ(a.mask.inside, b.mask.inside);
}

TEST(PolygonRaster, RepeatedClosingVertexIsAccepted)
{
  auto closed = rect(0.0, 0.0, 4.0, 4.0);
  closed.push_back(closed.front());
  const auto r = rasterisePolygon(closed, 1.0, kBig);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.mask.count, 16u);
}

// A polygon smaller than a cell and holding no cell centre is valid, with zero
// cells (area_total can be 0).
TEST(PolygonRaster, PolygonSmallerThanACellIsEmptyButValid)
{
  const auto r = rasterisePolygon({{0.1, 0.1}, {0.4, 0.1}, {0.1, 0.4}}, 1.0, kBig);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.mask.count, 0u);
  EXPECT_FALSE(r.mask.contains(0, 0));
}

TEST(PolygonRaster, RejectsTooFewVertices)
{
  EXPECT_FALSE(rasterisePolygon({}, 1.0, kBig).ok);
  EXPECT_FALSE(rasterisePolygon({{0.0, 0.0}}, 1.0, kBig).ok);
  const auto two = rasterisePolygon({{0.0, 0.0}, {5.0, 5.0}}, 1.0, kBig);
  EXPECT_FALSE(two.ok);
  EXPECT_NE(two.error.find("3"), std::string::npos);
  // Three vertices of which one repeats the first is only two distinct.
  EXPECT_FALSE(rasterisePolygon({{0.0, 0.0}, {5.0, 5.0}, {0.0, 0.0}}, 1.0, kBig).ok);
}

TEST(PolygonRaster, RejectsZeroArea)
{
  const auto r = rasterisePolygon({{0.0, 0.0}, {5.0, 5.0}, {10.0, 10.0}}, 1.0, kBig);
  EXPECT_FALSE(r.ok);
  EXPECT_NE(r.error.find("zero area"), std::string::npos);
  EXPECT_FALSE(rasterisePolygon({{0.0, 0.0}, {5.0, 0.0}, {9.0, 0.0}}, 1.0, kBig).ok);
}

TEST(PolygonRaster, RejectsNonFiniteInput)
{
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  EXPECT_FALSE(rasterisePolygon({{0.0, 0.0}, {nan, 0.0}, {0.0, 5.0}}, 1.0, kBig).ok);
  EXPECT_FALSE(rasterisePolygon({{0.0, 0.0}, {5.0, inf}, {0.0, 5.0}}, 1.0, kBig).ok);
  EXPECT_FALSE(rasterisePolygon(rect(0.0, 0.0, 5.0, 5.0), nan, kBig).ok);
  EXPECT_FALSE(rasterisePolygon(rect(0.0, 0.0, 5.0, 5.0), 0.0, kBig).ok);
  EXPECT_FALSE(rasterisePolygon(rect(0.0, 0.0, 5.0, 5.0), -1.0, kBig).ok);
}

// Oversize is refused by the bounding box, before anything is allocated: a
// swapped lat/lon projects to a raster of the whole earth.
TEST(PolygonRaster, RejectsOversizeBoundingBox)
{
  const auto at_limit = rasterisePolygon(rect(0.0, 0.0, 100.0, 10.0), 1.0, 1000);
  ASSERT_TRUE(at_limit.ok) << at_limit.error;
  EXPECT_EQ(at_limit.mask.count, 1000u);

  const auto over = rasterisePolygon(rect(0.0, 0.0, 101.0, 10.0), 1.0, 1000);
  EXPECT_FALSE(over.ok);
  EXPECT_NE(over.error.find("1010"), std::string::npos) << over.error;

  const auto earth = rasterisePolygon(rect(-6.0e6, -6.0e6, 6.0e6, 6.0e6), 1.0, kBig);
  EXPECT_FALSE(earth.ok);
  const auto absurd = rasterisePolygon(rect(-1.0e300, -1.0e300, 1.0e300, 1.0e300), 1.0, kBig);
  EXPECT_FALSE(absurd.ok);
}

}  // namespace coverage_assessment
