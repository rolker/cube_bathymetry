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

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
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
  EXPECT_FALSE(r.over_max_cells);  // a shape error, not the cell cap
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
  EXPECT_TRUE(over.over_max_cells);
  EXPECT_FALSE(at_limit.over_max_cells);

  const auto earth = rasterisePolygon(rect(-6.0e6, -6.0e6, 6.0e6, 6.0e6), 1.0, kBig);
  EXPECT_FALSE(earth.ok);
  const auto absurd = rasterisePolygon(rect(-1.0e300, -1.0e300, 1.0e300, 1.0e300), 1.0, kBig);
  EXPECT_FALSE(absurd.ok);
}

namespace
{
// The fill as it was before the edge table: every edge tested on every row.
// The reference the edge-table fill must match cell for cell.
std::vector<uint8_t> referenceFill(
  const std::vector<Point2> & ring, const PolygonMask & m,
  double cs)
{
  auto centre_at_or_above = [cs](double x) {
      int64_t i = static_cast<int64_t>(std::ceil(x / cs - 0.5));
      while ((static_cast<double>(i) + 0.5) * cs < x) {
        ++i;
      }
      while ((static_cast<double>(i - 1) + 0.5) * cs >= x) {
        --i;
      }
      return i;
    };
  std::vector<uint8_t> inside(static_cast<std::size_t>(m.ni * m.nj), 0);
  std::vector<double> xs;
  for (int64_t j = m.j0; j < m.j0 + m.nj; ++j) {
    const double cy = (static_cast<double>(j) + 0.5) * cs;
    xs.clear();
    for (std::size_t k = 0; k < ring.size(); ++k) {
      const Point2 & a = ring[k];
      const Point2 & b = ring[(k + 1) % ring.size()];
      if ((a.y <= cy) != (b.y <= cy)) {
        xs.push_back(a.x + (cy - a.y) * (b.x - a.x) / (b.y - a.y));
      }
    }
    std::sort(xs.begin(), xs.end());
    for (std::size_t k = 0; k + 1 < xs.size(); k += 2) {
      const int64_t first = std::max(centre_at_or_above(xs[k]), m.i0);
      const int64_t end = std::min(centre_at_or_above(xs[k + 1]), m.i0 + m.ni);
      for (int64_t i = first; i < end; ++i) {
        inside[static_cast<std::size_t>((j - m.j0) * m.ni + (i - m.i0))] = 1;
      }
    }
  }
  return inside;
}
}  // namespace

// Random polygons (self-intersecting ones included, and vertices snapped onto
// cell-centre lines to exercise the half-open rule) fill exactly as the
// every-edge-on-every-row reference does.
TEST(PolygonRaster, EdgeTableFillMatchesTheEveryEdgeReference)
{
  std::mt19937 rng(164);
  std::uniform_real_distribution<double> coord(-40.0, 40.0);
  std::uniform_int_distribution<int> nverts(3, 30);
  std::uniform_int_distribution<int> snap(0, 3);
  int compared = 0;
  for (const double cs : {1.0, 0.5, 0.37}) {
    for (int trial = 0; trial < 300; ++trial) {
      std::vector<Point2> ring;
      const int n = nverts(rng);
      for (int k = 0; k < n; ++k) {
        Point2 p{coord(rng), coord(rng)};
        if (snap(rng) == 0) {  // onto a centre line
          p.y = (std::round(p.y / cs - 0.5) + 0.5) * cs;
        }
        if (snap(rng) == 0) {
          p.x = (std::round(p.x / cs - 0.5) + 0.5) * cs;
        }
        ring.push_back(p);
      }
      const auto r = rasterisePolygon(ring, cs, kBig);
      if (!r.ok) {
        continue;
      }
      const auto expected = referenceFill(ring, r.mask, cs);
      ASSERT_EQ(r.mask.inside, expected) << "cell size " << cs << ", trial " << trial;
      ++compared;
    }
  }
  EXPECT_GT(compared, 800);
}

// A comb whose teeth each span every row of a tall area has edges x rows
// crossings; over kMaxEdgeRowCrossings it is refused from the edges alone.
TEST(PolygonRaster, RejectsTooManyEdgeRowCrossings)
{
  const double height = 1.0e6;  // rows at 1 m
  std::vector<Point2> comb = {{0.0, 0.0}};
  for (int t = 0; t < 60; ++t) {  // 120 tall edges x 1e6 rows = 1.2e8 crossings
    comb.push_back({2.0 * t, height});
    comb.push_back({2.0 * t + 1.0, height});
    comb.push_back({2.0 * t + 1.0, 1.0});
    comb.push_back({2.0 * t + 2.0, 1.0});
  }
  comb.push_back({120.0, 0.0});
  const auto r = rasterisePolygon(comb, 1.0, 1000000000);
  EXPECT_FALSE(r.ok);
  EXPECT_FALSE(r.over_max_cells);
  EXPECT_NE(r.error.find("edges cross"), std::string::npos) << r.error;
}

// A tall thin area with thousands of vertices along its long sides has few
// crossings per row; the edge table fills it without testing every edge on
// every row (that was 2e5 rows x 1e4 edges, about 2e9 tests).
TEST(PolygonRaster, ManyVerticesOnATallThinAreaAreCheap)
{
  const double height = 200000.0;
  const int per_side = 5000;
  std::vector<Point2> ring;  // up the left side, down the right
  for (int k = 0; k <= per_side; ++k) {
    ring.push_back({0.0, height * k / per_side});
  }
  for (int k = per_side; k >= 0; --k) {
    ring.push_back({2.0, height * k / per_side});
  }
  const auto r = rasterisePolygon(ring, 1.0, kBig);
  ASSERT_TRUE(r.ok) << r.error;
  EXPECT_EQ(r.mask.count, static_cast<std::size_t>(2 * 200000));
}

// Huge finite coordinates used to overflow the double -> int64 conversion or spin
// firstCentreAtOrAbove above 2^53; they are refused with a reason first.
TEST(PolygonRaster, RejectsCoordinatesBeyondTheStatedBound)
{
  const double lim = kMaxRasterCoordinate;
  // At the bound: accepted, and exact.
  const auto at = rasterisePolygon(rect(lim - 10.0, lim - 10.0, lim, lim), 1.0, kBig);
  ASSERT_TRUE(at.ok) << at.error;
  EXPECT_EQ(at.mask.count, 100u);
  const auto neg = rasterisePolygon(rect(-lim, -lim, -lim + 10.0, -lim + 10.0), 1.0, kBig);
  ASSERT_TRUE(neg.ok) << neg.error;
  EXPECT_EQ(neg.mask.count, 100u);

  // Just beyond it, in x and in y: refused, with the bound named.
  const auto over_x = rasterisePolygon(rect(lim - 10.0, 0.0, lim + 1.0, 10.0), 1.0, kBig);
  EXPECT_FALSE(over_x.ok);
  EXPECT_NE(over_x.error.find("beyond the limit"), std::string::npos) << over_x.error;
  EXPECT_FALSE(rasterisePolygon(rect(0.0, -lim - 1.0, 10.0, 10.0), 1.0, kBig).ok);

  // Small polygons at huge coordinates (these used to loop or hit UB).
  for (const double huge : {1.0e16, 9.0e18, 1.0e300}) {
    const auto r = rasterisePolygon(rect(huge, huge, huge * 2.0, huge * 2.0), 1.0, kBig);
    EXPECT_FALSE(r.ok) << huge;
    EXPECT_FALSE(r.error.empty());
  }
  EXPECT_FALSE(rasterisePolygon(rect(1.0e16, 0.0, 1.0e16 + 4.0, 4.0), 1.0, kBig).ok);

  // A tiny cell size drives the lattice index past its own bound.
  const auto fine = rasterisePolygon(rect(0.0, 0.0, 1.0, 1.0), 1.0e-13, kBig);
  EXPECT_FALSE(fine.ok);
  EXPECT_NE(fine.error.find("lattice index"), std::string::npos) << fine.error;
}

}  // namespace coverage_assessment
