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
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>
#include "cube_bathymetry/geo_map_sheet.h"

namespace cube
{

class GeoMapSheetTest : public ::testing::Test
{
protected:
  float cell_size{1.0f};
};

TEST_F(GeoMapSheetTest, AddSoundingsUpdatesTimestamp)
{
  GeoMapSheet ms(cell_size);

  auto before = std::chrono::steady_clock::now();

  std::vector<GeoSounding> soundings;
  gz4d::GeoPointLatLongDegrees point(43.0, -70.0, -10.0);
  GeoSounding s(point);
  s.sounding.vertical_error = 0.5f;
  s.sounding.horizontal_error = 0.1f;
  soundings.push_back(s);

  ms.addSoundings(soundings);

  EXPECT_GE(ms.lastUpdateTime(), before);
}

TEST_F(GeoMapSheetTest, AddEmptySoundingsNoTimestampUpdate)
{
  GeoMapSheet ms(cell_size);

  auto initial_time = ms.lastUpdateTime();

  std::vector<GeoSounding> soundings;
  ms.addSoundings(soundings);

  EXPECT_EQ(ms.lastUpdateTime(), initial_time);
}

TEST_F(GeoMapSheetTest, AddSoundingsCreatesGrids)
{
  GeoMapSheet ms(cell_size);

  std::vector<GeoSounding> soundings;
  gz4d::GeoPointLatLongDegrees point(43.0, -70.0, -10.0);
  GeoSounding s(point);
  s.sounding.vertical_error = 0.5f;
  s.sounding.horizontal_error = 0.1f;
  soundings.push_back(s);

  ms.addSoundings(soundings);
  EXPECT_FALSE(ms.grids().empty());
}

TEST_F(GeoMapSheetTest, TimestampNotUpdatedWhenNoInsert)
{
  GeoMapSheet ms(cell_size);

  // Add soundings to establish grids and a baseline timestamp
  std::vector<GeoSounding> soundings1;
  gz4d::GeoPointLatLongDegrees point1(43.0, -70.0, -10.0);
  GeoSounding s1(point1);
  s1.sounding.vertical_error = 0.5f;
  s1.sounding.horizontal_error = 0.1f;
  soundings1.push_back(s1);

  auto time1 = std::chrono::steady_clock::now();
  ms.addSoundings(soundings1, time1);

  auto baseline_time = ms.lastUpdateTime();
  EXPECT_EQ(baseline_time, time1);

  // Verify the first insertion produced non-NaN grid data
  bool has_real_data = false;
  for (const auto & g  :  ms.grids()) {
    auto vals = g->values();
    has_real_data = std::any_of(vals.begin(), vals.end(),
        [](const auto & v){return !std::isnan(v.depth);});
    if(has_real_data) {
      break;
    }
  }
  EXPECT_TRUE(has_real_data) << "First insertion should produce non-NaN grid data";

  // Add an empty soundings vector at a later time — no data inserted,
  // so the timestamp must not advance
  std::vector<GeoSounding> empty_soundings;
  auto time2 = time1 + std::chrono::seconds(10);
  ms.addSoundings(empty_soundings, time2);

  EXPECT_EQ(ms.lastUpdateTime(), baseline_time);
}

}  // namespace cube
