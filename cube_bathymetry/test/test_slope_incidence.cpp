// Copyright 2026 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "cube_bathymetry/node.h"
#include "cube_bathymetry/parameters.h"
#include "cube_bathymetry/slope_incidence.h"
#include "marine_autonomy/gggs.h"
#include "marine_backscatter/bathy_dem.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/tile_io.hpp"

/// @file
/// @brief stampIncidence against closed-form planes (cube_bathymetry#173): the
///        angle, the fallbacks, the baseline, the aspect-independence of the
///        corrected level, and the cost of the sampler over a real-sized store.

namespace cube
{
namespace
{

namespace seabed = marine_backscatter::seabed;
constexpr double kRad = M_PI / 180.0;
constexpr double kLat0 = 43.0;
constexpr double kLon0 = -70.6;

/// A plane h = h0 + dh/de * east_m + dh/dn * north_m about (kLat0, kLon0), with
/// optional holes. Spans are fixed, so the module's floor is one `span_m`.
class PlaneSampler : public seabed::SeabedHeightSampler
{
public:
  PlaneSampler(double slope_deg, double aspect_deg, double span_m = 0.5)
  : span_m_(span_m),
    de_(std::tan(slope_deg * kRad) * std::sin(aspect_deg * kRad)),
    dn_(std::tan(slope_deg * kRad) * std::cos(aspect_deg * kRad))
  {
  }

  seabed::HeightSample heightAt(double lat, double lon) override
  {
    ++calls;
    points.emplace_back(lat, lon);
    seabed::HeightSample s;
    const auto mpd = seabed::metresPerDegree(kLat0);
    const double east = (lon - kLon0) * mpd.east;
    const double north = (lat - kLat0) * mpd.north;
    if (no_data && no_data(east, north)) {
      s.status = seabed::SampleStatus::kNoData;
      return s;
    }
    s.status = seabed::SampleStatus::kValid;
    s.height_m = -15.0 + de_ * east + dn_ * north;
    s.span_north_m = span_m_;
    s.span_east_m = span_m_;
    if (other_source && other_source(east, north)) {
      s.source.layer = 1;
    }
    return s;
  }

  std::function<bool(double, double)> no_data;       // (east_m, north_m) -> no data there
  std::function<bool(double, double)> other_source;  // -> a different layer there
  std::size_t calls = 0;
  std::vector<std::pair<double, double>> points;

  /// The unit surface normal of the plane.
  std::array<double, 3> normal() const
  {
    const double n = std::sqrt(de_ * de_ + dn_ * dn_ + 1.0);
    return {-de_ / n, -dn_ / n, 1.0 / n};
  }

private:
  double span_m_;
  double de_;
  double dn_;
};

/// One sounding at the plane origin, beam arriving from @p az_deg (the sensor
/// lies that way) at @p off_vertical_deg from the vertical, @p range_m long.
GeoSounding beamAt(
  double off_vertical_deg, double az_deg, double range_m, double east_m = 0.0,
  double north_m = 0.0)
{
  const auto mpd = seabed::metresPerDegree(kLat0);
  GeoSounding gs(gz4d::GeoPointLatLongDegrees(
      kLat0 + north_m / mpd.north, kLon0 + east_m / mpd.east, -15.0));
  const double ov = off_vertical_deg * kRad;
  const double az = az_deg * kRad;
  // Travel direction: down, and away from the sensor along azimuth az.
  gs.sounding.beam_dir_enu = {std::sin(ov) * std::sin(az), std::sin(ov) * std::cos(az),
    -std::cos(ov)};
  gs.sounding.slant_range = static_cast<float>(range_m);
  return gs;
}

double closedForm(const std::array<double, 3> & beam, const std::array<double, 3> & n)
{
  return std::acos(-(beam[0] * n[0] + beam[1] * n[1] + beam[2] * n[2]));
}

float correctedMean(const Parameters & p, const Sounding & s)
{
  Node n;
  n.insert(0.0, s, p);
  n.queueFlush(p);
  const auto record = n.extractNodeRecord(p);
  return record.n_samples == 1u ? record.intensity : std::nanf("");
}

}  // namespace

TEST(SlopeIncidence, ReproducesTheClosedFormOnPlanes)
{
  for (const double slope : {0.0, 5.0, 15.0}) {
    for (const double aspect : {0.0, 90.0, 180.0, 270.0}) {
      for (const double off : {0.0, 12.0, 35.0}) {
        for (const double az : {0.0, 70.0, 200.0}) {
          PlaneSampler sampler(slope, aspect);
          std::vector<GeoSounding> soundings = {beamAt(off, az, 25.0)};
          IncidenceCounters counters;
          stampIncidence(soundings, sampler, SlopeOptions{}, counters);
          const double expected = closedForm(soundings[0].sounding.beam_dir_enu, sampler.normal());
          EXPECT_NEAR(soundings[0].sounding.incidence_angle, expected, 1e-6)
            << "slope " << slope << " aspect " << aspect << " off " << off << " az " << az;
          EXPECT_EQ(counters.from_surface, 1u);
          EXPECT_EQ(counters.fallbackTotal(), 0u);
        }
      }
    }
  }
}

// Level seabed: the incidence is the earth-frame angle from vertical.
TEST(SlopeIncidence, ZeroSlopeIsTheAngleFromVertical)
{
  PlaneSampler sampler(0.0, 0.0);
  std::vector<GeoSounding> soundings = {beamAt(0.0, 0.0, 20.0), beamAt(30.0, 40.0, 20.0),
    beamAt(60.0, 250.0, 20.0)};
  IncidenceCounters counters;
  stampIncidence(soundings, sampler, SlopeOptions{}, counters);
  EXPECT_NEAR(soundings[0].sounding.incidence_angle, 0.0, 1e-6);
  EXPECT_NEAR(soundings[1].sounding.incidence_angle, 30.0 * kRad, 1e-6);
  EXPECT_NEAR(soundings[2].sounding.incidence_angle, 60.0 * kRad, 1e-6);
}

// Sign of the slope: a beam arriving from the west, onto a surface that rises
// toward the west (a face turned toward the sensor), sees a smaller incidence
// than on the flat; onto one falling toward the west, a larger one. This pins
// the up-positive height convention end to end.
TEST(SlopeIncidence, FacingTheSensorLowersTheIncidence)
{
  // Surface normal tilts toward -gradient: rising toward the west (aspect 270 =
  // the height gradient points west) tilts the normal east.
  const double beam_off = 30.0;
  PlaneSampler rises_west(10.0, 270.0);
  PlaneSampler falls_west(10.0, 90.0);
  // Sensor to the west of the touchdown: the beam travels east and down.
  std::vector<GeoSounding> a = {beamAt(beam_off, 90.0, 25.0)};
  std::vector<GeoSounding> b = {beamAt(beam_off, 90.0, 25.0)};
  IncidenceCounters counters;
  stampIncidence(a, rises_west, SlopeOptions{}, counters);
  stampIncidence(b, falls_west, SlopeOptions{}, counters);
  // Rises toward the west: the face looks east? No: normal = (-dh/de,..)
  // = pointing away from the rise, i.e. east, so the beam traveling east hits
  // the face at a LARGER angle to the normal.
  EXPECT_NEAR(a[0].sounding.incidence_angle, (beam_off + 10.0) * kRad, 1e-5);
  EXPECT_NEAR(b[0].sounding.incidence_angle, (beam_off - 10.0) * kRad, 1e-5);
}

// A beam with no vector is left unstamped and counted.
TEST(SlopeIncidence, ABeamWithoutAVectorIsCountedAndLeftNaN)
{
  PlaneSampler sampler(5.0, 0.0);
  GeoSounding bare(gz4d::GeoPointLatLongDegrees(kLat0, kLon0, -15.0));
  std::vector<GeoSounding> soundings = {bare};
  IncidenceCounters counters;
  stampIncidence(soundings, sampler, SlopeOptions{}, counters);
  EXPECT_TRUE(std::isnan(soundings[0].sounding.incidence_angle));
  EXPECT_EQ(counters.no_beam_vector, 1u);
  EXPECT_EQ(counters.beams, 1u);
  EXPECT_EQ(sampler.calls, 0u) << "no lookup for a beam that cannot use one";
}

// A vector off unit length is flagged invalid by the module: NaN, counted.
TEST(SlopeIncidence, ANonUnitVectorIsAnInvalidAngle)
{
  PlaneSampler sampler(5.0, 0.0);
  std::vector<GeoSounding> soundings = {beamAt(10.0, 0.0, 20.0)};
  soundings[0].sounding.beam_dir_enu = {0.0, 0.0, -1.2};
  IncidenceCounters counters;
  stampIncidence(soundings, sampler, SlopeOptions{}, counters);
  EXPECT_TRUE(std::isnan(soundings[0].sounding.incidence_angle));
  EXPECT_EQ(counters.invalid_angle, 1u);
}

// A steep reverse slope: the beam hits the back of the surface. Stamped raw
// (above 90 deg) and counted; the table's own "at or beyond 90" fallback decides.
TEST(SlopeIncidence, ABackFacingSlopeIsStampedRawAndCounted)
{
  PlaneSampler sampler(40.0, 270.0);  // rises to the west
  // Beam from the west, 60 deg from vertical, traveling east: 60 + 40 = 100 deg.
  std::vector<GeoSounding> soundings = {beamAt(60.0, 90.0, 25.0)};
  IncidenceCounters counters;
  stampIncidence(soundings, sampler, SlopeOptions{}, counters);
  EXPECT_NEAR(soundings[0].sounding.incidence_angle, 100.0 * kRad, 1e-5);
  EXPECT_EQ(counters.back_facing, 1u);
}

// Fallbacks: the vertical normal, so the angle from vertical, and a count per
// reason. A missing cell, a layer seam and a region with no survey at all
// (what a prior-only area looks like to the sampler).
TEST(SlopeIncidence, FallbacksStampTheAngleFromVerticalAndCountTheReason)
{
  PlaneSampler sampler(20.0, 0.0);
  // East of x = 5 m: nothing. A different layer for y > 8 m. Anything else is surface.
  sampler.no_data = [](double east, double) {return east > 5.0;};
  sampler.other_source = [](double, double north) {return north > 8.0;};
  std::vector<GeoSounding> soundings = {
    beamAt(25.0, 10.0, 25.0, 0.0, 0.0),     // surface
    beamAt(25.0, 10.0, 25.0, 20.0, 0.0),    // no data anywhere around
    beamAt(25.0, 10.0, 25.0, 0.0, 8.0),     // a probe lands in the other layer
    beamAt(25.0, 10.0, 25.0, 40.0, 40.0),   // no data
  };
  IncidenceCounters counters;
  stampIncidence(soundings, sampler, SlopeOptions{}, counters);
  EXPECT_EQ(counters.beams, 4u);
  EXPECT_EQ(counters.from_surface, 1u);
  using R = seabed::FallbackReason;
  EXPECT_EQ(counters.fallback[static_cast<std::size_t>(R::kMissingSample)], 2u);
  EXPECT_EQ(counters.fallback[static_cast<std::size_t>(R::kLayerSeam)], 1u);
  EXPECT_EQ(counters.fallbackTotal(), 3u);
  for (std::size_t i = 1; i < 4; ++i) {
    EXPECT_NEAR(soundings[i].sounding.incidence_angle, 25.0 * kRad, 1e-6)
      << "a fallback beam carries the angle from vertical";
  }
  // The surface beam saw the slope.
  EXPECT_GT(std::abs(soundings[0].sounding.incidence_angle - 25.0 * kRad), 0.05);
}

// Every beam says how its angle was obtained: the beam ledger records it.
TEST(SlopeIncidence, EachBeamIsStampedWithHowItsAngleWasObtained)
{
  PlaneSampler sampler(20.0, 0.0);
  sampler.no_data = [](double east, double) {return east > 5.0;};
  sampler.other_source = [](double, double north) {return north > 8.0;};
  GeoSounding bare(gz4d::GeoPointLatLongDegrees(kLat0, kLon0, -15.0));
  GeoSounding bad = beamAt(10.0, 0.0, 20.0);
  bad.sounding.beam_dir_enu = {0.0, 0.0, -1.2};
  std::vector<GeoSounding> soundings = {
    beamAt(25.0, 10.0, 25.0, 0.0, 0.0), beamAt(25.0, 10.0, 25.0, 20.0, 0.0),
    beamAt(25.0, 10.0, 25.0, 0.0, 8.0), bare, bad};
  EXPECT_EQ(soundings[0].sounding.incidence_source, Sounding::kIncidenceUnstamped);
  IncidenceCounters counters;
  stampIncidence(soundings, sampler, SlopeOptions{}, counters);
  using R = seabed::FallbackReason;
  EXPECT_EQ(soundings[0].sounding.incidence_source, 0);
  EXPECT_EQ(soundings[1].sounding.incidence_source, static_cast<uint8_t>(R::kMissingSample));
  EXPECT_EQ(soundings[2].sounding.incidence_source, static_cast<uint8_t>(R::kLayerSeam));
  EXPECT_EQ(soundings[3].sounding.incidence_source, 7);
  EXPECT_EQ(soundings[4].sounding.incidence_source, 8);
}

// The baseline follows range x angle: half of it either side, floored at one
// native cell, and the counters say how often the floor decided.
TEST(SlopeIncidence, TheBaselineScalesWithRangeAndIsFlooredAtOneCell)
{
  const double span = 0.5;
  const double baseline_deg = 4.0;
  PlaneSampler sampler(5.0, 0.0, span);
  std::vector<GeoSounding> soundings = {
    beamAt(0.0, 0.0, 40.0),   // half = 0.5 * 40 * 4 deg = 1.396 m, above the floor
    beamAt(0.0, 0.0, 10.0),   // 0.349 m: floored to 0.5 m
    beamAt(0.0, 0.0, 80.0),   // 2.793 m
  };
  IncidenceCounters counters;
  SlopeOptions options;
  options.baseline_deg = baseline_deg;
  stampIncidence(soundings, sampler, options, counters);
  EXPECT_EQ(counters.at_floor, 1u);
  EXPECT_EQ(counters.baselineCount(), 3u);
  EXPECT_NEAR(counters.max_half_baseline_m, 0.5 * 80.0 * baseline_deg * kRad, 1e-9);

  // The probes the sampler saw: centre, then +-step east and north per beam.
  ASSERT_EQ(sampler.points.size(), 15u);
  const auto mpd = seabed::metresPerDegree(kLat0);
  const std::array<double, 3> half = {0.5 * 40.0 * baseline_deg * kRad, span,
    0.5 * 80.0 * baseline_deg * kRad};
  for (std::size_t b = 0; b < 3; ++b) {
    // Probes are issued as east+, east-, north+, north- after the centre.
    std::vector<double> east_offsets;
    for (std::size_t k = 1; k < 5; ++k) {
      const auto & pt = sampler.points[5 * b + k];
      const auto & centre = sampler.points[5 * b];
      east_offsets.push_back(std::abs((pt.second - centre.second) * mpd.east));
      east_offsets.push_back(std::abs((pt.first - centre.first) * mpd.north));
    }
    EXPECT_NEAR(*std::max_element(east_offsets.begin(), east_offsets.end()), half[b], 1e-6)
      << "beam " << b;
  }
  // The distribution is of the FULL baseline.
  EXPECT_GE(counters.baselinePercentile(1.0), 2.0 * 0.5 * 80.0 * baseline_deg * kRad);
  EXPECT_LE(counters.baselinePercentile(0.0), 2.0 * span + IncidenceCounters::kBaselineBinM);
  EXPECT_NE(counters.report().find("at the one-cell floor: 1 of 3"), std::string::npos)
    << counters.report();
}

// The corrected level must not depend on which way the seabed faces. Beams of
// one fixed earth-frame direction land on planes of one slope and four aspects;
// the raw level carries a response f(incidence). Corrected on the incidence
// basis the four agree; on the beam basis (the same beams, corrected by the
// unchanging beam angle) they differ by the known amount. The negative control.
TEST(SlopeIncidence, TheCorrectedLevelDoesNotDependOnTheAspect)
{
  const double a = -30.0;
  const auto response = [](double incidence_rad) {return -0.2 * incidence_rad / kRad;};
  Parameters on_incidence{CellSizes(1.0f), "order1a"};
  on_incidence.backscatter_angle_correction = BackscatterAngleCorrection::Empirical;
  on_incidence.angular_response_curve = {{0.0f, 0.0f}, {89.0f, -17.8f}};
  on_incidence.backscatter_angle_basis = BackscatterAngleBasis::Incidence;
  Parameters on_beam = on_incidence;
  on_beam.backscatter_angle_basis = BackscatterAngleBasis::Beam;

  std::vector<double> incidence_levels;
  std::vector<double> beam_levels;
  std::vector<double> incidences;
  for (const double aspect : {0.0, 90.0, 180.0, 270.0}) {
    PlaneSampler sampler(15.0, aspect);
    std::vector<GeoSounding> soundings = {beamAt(25.0, 90.0, 30.0)};
    IncidenceCounters counters;
    stampIncidence(soundings, sampler, SlopeOptions{}, counters);
    Sounding s = soundings[0].sounding;
    ASSERT_TRUE(std::isfinite(s.incidence_angle));
    incidences.push_back(s.incidence_angle / kRad);
    s.beam_angle = static_cast<float>(25.0 * kRad);  // the sonar-frame angle never changes
    s.intensity = static_cast<float>(a + response(s.incidence_angle));
    s.depth = 15.0f;
    s.vertical_error = 0.01f;
    s.horizontal_error = 0.01f;
    incidence_levels.push_back(correctedMean(on_incidence, s));
    beam_levels.push_back(correctedMean(on_beam, s));
  }
  // The aspects really did change the angle the seabed sees.
  EXPECT_GT(*std::max_element(incidences.begin(), incidences.end()) -
    *std::min_element(incidences.begin(), incidences.end()), 20.0);
  for (std::size_t i = 0; i < 4; ++i) {
    EXPECT_NEAR(incidence_levels[i], a, 2e-3) << "aspect index " << i;
    // Beam basis: corrected by f(25 deg) instead of f(incidence).
    EXPECT_NEAR(beam_levels[i], a + response(incidences[i] * kRad) - response(25.0 * kRad), 2e-3)
      << "aspect index " << i;
  }
  EXPECT_GT(*std::max_element(beam_levels.begin(), beam_levels.end()) -
    *std::min_element(beam_levels.begin(), beam_levels.end()), 3.0)
    << "the negative control must show the aspect dependence";
}

// The counters' report names what a reader needs to judge the run.
TEST(SlopeIncidence, TheReportStatesTheDistributions)
{
  PlaneSampler sampler(12.0, 45.0);
  std::vector<GeoSounding> soundings;
  for (int i = 0; i < 20; ++i) {
    soundings.push_back(beamAt(20.0, 10.0 * i, 20.0 + 3.0 * i));
  }
  IncidenceCounters counters;
  stampIncidence(soundings, sampler, SlopeOptions{}, counters);
  const std::string report = counters.report();
  EXPECT_NE(report.find("20 beam(s); 20"), std::string::npos) << report;
  EXPECT_NE(report.find("differencing baseline"), std::string::npos) << report;
  EXPECT_NE(report.find("surface slope, beams per degree: 12=20"), std::string::npos) << report;
  EXPECT_NE(report.find("back-facing"), std::string::npos) << report;
}

// The sampler cost over a real-sized store. A plane written as 2 x 2 processed
// tiles of the GGGS grid (960 x 960 cells each, ~15 MB per tile), read through
// the real BathyDem with a cache above its default. The numbers are printed so
// the cost is known before an Appledore-sized run; the assertions are the
// closed form (so the store's heights and sign are right) and a loose ceiling
// that only catches a runaway.
TEST(SlopeIncidence, SamplerCostOverARealSizedStore)
{
  namespace mbs = marine_bathymetry_store;
  const gggs::Level level = gggs::Level::fromCellSize(1.0f);
  const auto lo = level.gridIndex(kLat0, kLon0);
  const double lat_span = lo.latitudinalSpan();
  const double lon_span = lo.longitudinalSpan();
  gggs::GridAreaIterator tiles(
    lo, level.gridIndex(kLat0 + 1.2 * lat_span, kLon0 + 1.2 * lon_span));

  const double slope = 8.0;
  const double aspect = 130.0;
  const double de = std::tan(slope * kRad) * std::sin(aspect * kRad);
  const double dn = std::tan(slope * kRad) * std::cos(aspect * kRad);
  const auto mpd = seabed::metresPerDegree(kLat0);

  const std::string root = (std::filesystem::temp_directory_path() /
    ("cube_slope_bench_" + std::to_string(::getpid()))).string();
  std::filesystem::remove_all(root);
  const std::string layer_dir = root + "/" + mbs::layerDirName(mbs::SourceLayer::Processed);
  std::filesystem::create_directories(layer_dir);
  std::size_t tile_count = 0;
  double west = 1e9, east = -1e9, south = 1e9, north = -1e9;
  for (; tiles.valid(); tiles.next()) {
    const gggs::GridIndex index = *tiles;
    mbs::BathymetryTile tile(index);
    const double dlat = index.latitudinalSpan() / mbs::BathymetryTile::edge;
    const double dlon = index.longitudinalSpan() / mbs::BathymetryTile::edge;
    for (uint16_t r = 0; r < mbs::BathymetryTile::edge; ++r) {
      for (uint16_t c = 0; c < mbs::BathymetryTile::edge; ++c) {
        const double lat = index.southLatitude() + (r + 0.5) * dlat;
        const double lon = index.westLongitude() + (c + 0.5) * dlon;
        mbs::BathyCell cell;
        cell.depth = -20.0 + de * (lon - kLon0) * mpd.east + dn * (lat - kLat0) * mpd.north;
        cell.uncertainty = 0.1;
        tile.set(r, c, cell);
      }
    }
    mbs::saveTile(tile, layer_dir + "/" + mbs::tileFilename(index));
    ++tile_count;
    west = std::min(west, index.westLongitude());
    east = std::max(east, index.eastLongitude());
    south = std::min(south, index.southLatitude());
    north = std::max(north, index.northLatitude());
  }
  ASSERT_GE(tile_count, 4u);

  marine_backscatter::BathyDem dem(root, {"processed"}, 16);
  // Soundings over the middle of the block, away from its edge by a margin.
  std::mt19937 rng(173);
  std::uniform_real_distribution<double> ulat(south + 0.15 * (north - south),
    north - 0.15 * (north - south));
  std::uniform_real_distribution<double> ulon(west + 0.15 * (east - west),
    east - 0.15 * (east - west));
  std::uniform_real_distribution<double> uoff(0.0, 55.0);
  std::uniform_real_distribution<double> uaz(0.0, 360.0);
  std::uniform_real_distribution<double> urange(8.0, 60.0);
  const std::size_t n = 100000;
  std::vector<GeoSounding> soundings;
  soundings.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    GeoSounding gs(gz4d::GeoPointLatLongDegrees(ulat(rng), ulon(rng), -20.0));
    const double ov = uoff(rng) * kRad;
    const double az = uaz(rng) * kRad;
    gs.sounding.beam_dir_enu = {std::sin(ov) * std::sin(az), std::sin(ov) * std::cos(az),
      -std::cos(ov)};
    gs.sounding.slant_range = static_cast<float>(urange(rng));
    soundings.push_back(gs);
  }
  IncidenceCounters counters;
  const auto t0 = std::chrono::steady_clock::now();
  stampIncidence(soundings, dem, SlopeOptions{}, counters);
  const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  const double us_per_beam = 1e6 * secs / static_cast<double>(n);
  std::cout << "[ cost     ] stampIncidence over " << tile_count << " processed tiles: "
            << us_per_beam << " us/beam (" << n << " beams in " << secs << " s)\n"
            << counters.report();
  RecordProperty("us_per_beam", std::to_string(us_per_beam));

  EXPECT_EQ(counters.from_surface, n) << counters.report();
  const std::array<double, 3> normal = {-de / std::sqrt(de * de + dn * dn + 1.0),
    -dn / std::sqrt(de * de + dn * dn + 1.0), 1.0 / std::sqrt(de * de + dn * dn + 1.0)};
  double worst = 0.0;
  for (const auto & gs : soundings) {
    worst = std::max(worst, std::abs(
        static_cast<double>(gs.sounding.incidence_angle) -
        closedForm(gs.sounding.beam_dir_enu, normal)));
  }
  EXPECT_LT(worst, 1e-4) << "BathyDem over a tilted store gives the closed form (sign included)";
  EXPECT_LT(us_per_beam, 500.0) << "a runaway cost";
  std::filesystem::remove_all(root);
}

}  // namespace cube
