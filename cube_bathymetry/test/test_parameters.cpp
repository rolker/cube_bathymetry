// Copyright 2025 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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
#include <stdexcept>
#include "cube_bathymetry/parameters.h"

namespace cube
{

TEST(ParametersTest, Order1aDefaults)
{
  Parameters p(CellSizes(1.0f), "order1a");

  // IHO Order 1a: fixed=0.5 (squared), percent=0.013 (squared)
  EXPECT_DOUBLE_EQ(p.iho_fixed, 0.5 * 0.5);
  EXPECT_DOUBLE_EQ(p.iho_percent, 0.013 * 0.013);
  EXPECT_EQ(p.iho_order, "order1a");
}

TEST(ParametersTest, ExclusiveOrder)
{
  Parameters p(CellSizes(1.0f), "exclusive");

  EXPECT_DOUBLE_EQ(p.iho_fixed, 0.15 * 0.15);
  EXPECT_DOUBLE_EQ(p.iho_percent, 0.0075 * 0.0075);
}

TEST(ParametersTest, SpecialOrder)
{
  Parameters p(CellSizes(1.0f), "special");

  EXPECT_DOUBLE_EQ(p.iho_fixed, 0.25 * 0.25);
  EXPECT_DOUBLE_EQ(p.iho_percent, 0.0075 * 0.0075);
}

TEST(ParametersTest, Order1b)
{
  Parameters p(CellSizes(1.0f), "order1b");

  // Same as order1a
  EXPECT_DOUBLE_EQ(p.iho_fixed, 0.5 * 0.5);
  EXPECT_DOUBLE_EQ(p.iho_percent, 0.013 * 0.013);
}

TEST(ParametersTest, Order2)
{
  Parameters p(CellSizes(1.0f), "order2");

  EXPECT_DOUBLE_EQ(p.iho_fixed, 1.0 * 1.0);
  EXPECT_DOUBLE_EQ(p.iho_percent, 0.023 * 0.023);
}

TEST(ParametersTest, UnknownOrderThrows)
{
  EXPECT_THROW(Parameters(CellSizes(1.0f), "invalid"), std::invalid_argument);
}

TEST(ParametersTest, GridResolutionDistanceScale)
{
  // distance_scale should equal the cell size (square cells)
  Parameters p(CellSizes(2.0f));
  EXPECT_DOUBLE_EQ(p.distance_scale, 2.0);

  Parameters p2(CellSizes(5.0f));
  EXPECT_DOUBLE_EQ(p2.distance_scale, 5.0);
}

TEST(ParametersTest, GridResolutionVarianceScale)
{
  // variance_scale = distance_scale^(-distance_exponent)
  // With 1m grid: distance_scale=1.0, distance_exponent=2.0
  // variance_scale = 1.0^(-2.0) = 1.0
  Parameters p(CellSizes(1.0f));
  EXPECT_DOUBLE_EQ(p.variance_scale, 1.0);

  // With 2m grid: distance_scale=2.0
  // variance_scale = 2.0^(-2.0) = 0.25
  Parameters p2(CellSizes(2.0f));
  EXPECT_DOUBLE_EQ(p2.variance_scale, 0.25);
}

TEST(ParametersTest, ContextSearchRange)
{
  // min_context = 5.0 / distance_scale
  // max_context = 10.0 / distance_scale
  Parameters p(CellSizes(2.0f));
  EXPECT_DOUBLE_EQ(p.minimum_context_search_range, 5.0 / 2.0);
  EXPECT_DOUBLE_EQ(p.maximum_context_search_range, 10.0 / 2.0);
}

TEST(ParametersTest, DefaultParameterValues)
{
  Parameters p(CellSizes(1.0f));

  EXPECT_EQ(p.median_length, 11u);
  EXPECT_FLOAT_EQ(p.quotient_limit, 30.0f);
  EXPECT_FLOAT_EQ(p.discount, 1.0f);
  EXPECT_FLOAT_EQ(p.estimate_offset, 4.0f);
  EXPECT_FLOAT_EQ(p.bayes_factor_threshold, 0.135f);
  EXPECT_EQ(p.runlength_threshold, 5u);
  EXPECT_FLOAT_EQ(p.stddev_to_confidence_interval_scale, 1.96f);
  EXPECT_FLOAT_EQ(p.blunder_minimum, 10.0f);
  EXPECT_FLOAT_EQ(p.blunder_percent, 0.25f);
  EXPECT_FLOAT_EQ(p.blunder_scalar, 3.0f);
  EXPECT_FLOAT_EQ(p.capture_distance_scale, 0.05f);
  EXPECT_EQ(p.extractor, CUBE_LHOOD);
}

// Regression for #46 bug 2: the whole IHO numerator (iho_fixed + iho_percent*z^2)
// must be divided by CONF_95PC^2 -- not just the depth-dependent term. The two grid
// call sites previously relied on operator precedence, which left iho_fixed outside
// the division. This pins the corrected value via the shared helper.
TEST(ParametersTest, MaxVarianceAllowedDividesWholeNumerator)
{
  Parameters p(CellSizes(1.0f), "order1a");  // iho_fixed=0.25, iho_percent=1.69e-4
  const double conf2 = 1.96 * 1.96;          // CONF_95PC^2 = 3.8416

  // depth=0 isolates the fixed term: correct = iho_fixed/conf2 = 0.0651,
  // whereas the precedence bug would return iho_fixed = 0.25 undivided.
  EXPECT_NEAR(p.maxVarianceAllowed(0.0), 0.25 / conf2, 1e-9);
  EXPECT_GT(std::abs(p.maxVarianceAllowed(0.0) - 0.25), 0.18);  // far from the bug value

  // depth=100: (0.25 + 1.69) / 3.8416 = 0.50500, not the bug value
  // 0.25 + 1.69/3.8416 = 0.68992.
  EXPECT_NEAR(p.maxVarianceAllowed(100.0), 0.50500, 1e-4);
  const double bug_value = p.iho_fixed + p.iho_percent * 100.0 * 100.0 / conf2;
  EXPECT_NEAR(bug_value, 0.68992, 1e-4);  // documents what the precedence bug produced
  EXPECT_GT(std::abs(p.maxVarianceAllowed(100.0) - bug_value), 0.18);

  // Numerator scales with depth^2 inside the single division.
  EXPECT_NEAR(
    p.maxVarianceAllowed(50.0),
    (p.iho_fixed + p.iho_percent * 50.0 * 50.0) / conf2, 1e-12);
}

}  // namespace cube
