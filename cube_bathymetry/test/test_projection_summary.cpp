// Copyright 2025 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
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
#include <sstream>
#include <string>

#include "cube_bathymetry/projection_summary.h"

namespace cube
{

namespace
{

// Run the reporter over a fixed set of totals and hand back both streams.
struct Report
{
  std::string out;
  std::string err;
};

Report run(const ProjectionRunTotals & totals)
{
  std::ostringstream out;
  std::ostringstream err;
  report_projection_summary(totals, out, err);
  return Report{out.str(), err.str()};
}

ProjectionRunTotals healthyRun()
{
  ProjectionRunTotals totals;
  totals.pings = 10;
  totals.georeferenced_pings = 10;
  totals.dropped_georef = 0;
  totals.soundings = 1000;
  totals.beams = 1000;
  return totals;
}

}  // namespace

// A clean run: the georeferencing clause is present, both beam counters read
// zero of the beam total, and nothing is warned about.
TEST(ProjectionSummary, HealthyRunReportsEverythingAndWarnsNothing)
{
  const auto report = run(healthyRun());

  EXPECT_NE(report.out.find("10 pings projected"), std::string::npos);
  EXPECT_NE(report.out.find("10 georeferenced into the grid"), std::string::npos);
  EXPECT_NE(report.out.find("0 dropped (no earth TF)"), std::string::npos);
  EXPECT_NE(report.out.find("1000 soundings"), std::string::npos);
  EXPECT_NE(report.out.find("0 of 1000 beams on the default beamwidth"), std::string::npos);
  EXPECT_NE(report.out.find("0 of 1000 beams with no receive angle"), std::string::npos);
  EXPECT_EQ(report.err, "");
}

// bag_to_geotiff does not georeference in this pass: the clause and its
// warning are both omitted rather than printed as zeroes.
TEST(ProjectionSummary, NonGeoreferencingToolOmitsTheGeorefClause)
{
  auto totals = healthyRun();
  totals.reports_georeferencing = false;
  totals.georeferenced_pings = 0;
  const auto report = run(totals);

  EXPECT_NE(report.out.find("10 pings,"), std::string::npos);
  EXPECT_EQ(report.out.find("georeferenced"), std::string::npos);
  EXPECT_EQ(report.err.find("earth transform"), std::string::npos);
  EXPECT_EQ(report.err, "");
}

// Every ping dropped for want of an earth transform: warned, but only for a
// tool that georeferences at all.
TEST(ProjectionSummary, AllPingsDroppedForGeorefIsWarned)
{
  auto totals = healthyRun();
  totals.georeferenced_pings = 0;
  totals.dropped_georef = 10;
  EXPECT_NE(run(totals).err.find("every ping was dropped"), std::string::npos);

  totals.reports_georeferencing = false;
  EXPECT_EQ(run(totals).err.find("every ping was dropped"), std::string::npos);
}

// The default-beamwidth warning fires only when the count is non-zero, and
// carries the beam denominator with it.
TEST(ProjectionSummary, DefaultBeamwidthWarningGatesOnTheCount)
{
  auto totals = healthyRun();
  totals.default_beamwidth_beams = 400;
  const auto report = run(totals);

  EXPECT_NE(report.out.find("400 of 1000 beams on the default beamwidth"), std::string::npos);
  EXPECT_NE(
    report.err.find("400 of 1000 beams took the generic device across-track beamwidth"),
    std::string::npos);
}

// #144: beams with no receive angle are reported as such, and are not left to
// masquerade as range-filtered soundings.
TEST(ProjectionSummary, MissingReceiveAngleIsWarnedSeparately)
{
  auto totals = healthyRun();
  totals.soundings = 600;
  totals.filtered_range = 400;
  totals.missing_rx_angle_beams = 400;
  const auto report = run(totals);

  EXPECT_NE(report.out.find("400 of 1000 beams with no receive angle"), std::string::npos);
  EXPECT_NE(report.err.find("400 of 1000 beams reported no usable receive angle"),
    std::string::npos);
}

// #144: the "0 soundings" warning must not point at the frame overrides when
// every beam was NaN before any frame was consulted.
TEST(ProjectionSummary, ZeroSoundingsBlamesReceiveAnglesWhenEveryBeamLacksOne)
{
  auto totals = healthyRun();
  totals.soundings = 0;
  totals.filtered_range = 1000;
  totals.missing_rx_angle_beams = 1000;
  const auto report = run(totals);

  EXPECT_NE(report.err.find("EVERY beam lacked a usable receive angle"), std::string::npos);
  EXPECT_EQ(report.err.find("--*-frame overrides"), std::string::npos);
}

// ...but with the receive angles intact, an empty run still points at the
// frames, which is the commonest cause.
TEST(ProjectionSummary, ZeroSoundingsStillBlamesFramesOtherwise)
{
  auto totals = healthyRun();
  totals.soundings = 0;
  totals.filtered_range = 1000;
  const auto report = run(totals);

  EXPECT_NE(report.err.find("--*-frame overrides"), std::string::npos);
  EXPECT_EQ(report.err.find("EVERY beam lacked"), std::string::npos);
}

}  // namespace cube
