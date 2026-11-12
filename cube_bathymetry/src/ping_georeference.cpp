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

#include "cube_bathymetry/ping_georeference.h"

#include <cmath>
#include <vector>

#include "kdl/frames.hpp"  // KDL::Frame/Vector for the per-ping transform hoist
#include "marine_autonomy/gz4d_geo.h"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"

namespace cube
{

/// Extracted verbatim from main()'s per-ping lambda so main() stays within the
/// cpplint function-size limit (cube#143); moved here from import_bag_main.cpp
/// by cube_bathymetry#173 with the arithmetic unchanged.
std::vector<GeoSounding> georeferencePing(
  const ProjectionResult & projection,
  const geometry_msgs::msg::TransformStamped & transform)
{
  const auto & origin = transform.transform.translation;
  gz4d::GeoPointLatLongDegrees ref_ll(
    gz4d::GeoPointECEF(origin.x, origin.y, origin.z));
  gz4d::LocalENU enu(ref_ll);

  const double ref_lat_deg = ref_ll.latitude();
  const double ref_lon_deg = ref_ll.longitude();
  const double ref_height = ref_ll.altitude();
  constexpr double kDeg2Rad = M_PI / 180.0;
  constexpr double kRad2Deg = 180.0 / M_PI;
  constexpr double kA = 6378137.0;             // WGS84 semi-major axis
  constexpr double kF = 1.0 / 298.257223563;   // WGS84 flattening
  constexpr double kE2 = kF * (2.0 - kF);       // first eccentricity^2
  const double lat0_rad = ref_lat_deg * kDeg2Rad;
  const double sin_lat0 = std::sin(lat0_rad);
  const double cos_lat0 = std::cos(lat0_rad);
  const double w = std::sqrt(1.0 - kE2 * sin_lat0 * sin_lat0);
  const double prime_vertical = kA / w;                     // N(lat0)
  const double meridional = kA * (1.0 - kE2) / (w * w * w);  // M(lat0)

  // Hoist the quaternion->matrix conversion out of the per-sounding loop:
  // tf2::doTransform(PointStamped, ...) rebuilds the KDL::Frame (quaternion
  // -> rotation matrix) from `transform` for EVERY sounding. Build it once
  // per ping and apply the frame as a matvec. Bit-identical to the
  // per-sounding doTransform -- the exact same KDL::Frame * KDL::Vector,
  // just hoisted (cube#107). This MUST stay on the KDL path
  // (tf2::gmTransformToKDL), NOT a tf2::Transform matvec: doTransform for a
  // point is KDL-based, and a different rotation build would perturb the
  // ECEF output in its low bits and shift boundary soundings between cells.
  const KDL::Frame ping_frame = tf2::gmTransformToKDL(transform);

  std::vector<GeoSounding> soundings;
  soundings.reserve(projection.soundings.size());
  for (const auto & s : projection.soundings) {
    const KDL::Vector sounding_ecef = ping_frame * KDL::Vector(
      s.sonar_relative_position.x,
      s.sonar_relative_position.y,
      s.sonar_relative_position.z);

    // ECEF -> local ENU (East, North, Up), then linearize ENU -> geodetic
    // delta about the per-ping reference latitude.
    const gz4d::Point<double> local = enu.toLocal(gz4d::GeoPointECEF(
      sounding_ecef.x(), sounding_ecef.y(), sounding_ecef.z()));
    const double lat_deg = ref_lat_deg + (local[1] / meridional) * kRad2Deg;
    const double lon_deg =
      ref_lon_deg + (local[0] / (prime_vertical * cos_lat0)) * kRad2Deg;
    const double height = ref_height + local[2];

    GeoSounding gs(gz4d::GeoPointLatLongDegrees(lat_deg, lon_deg, height));
    gs.sounding.vertical_error = s.vertical_error;
    gs.sounding.horizontal_error = s.horizontal_error;
    // Carry the {raw intensity, beam angle} sufficient-stats pair so the
    // CUBE node co-estimates backscatter (#54) on the winning depth
    // hypothesis -- without this every beam has NaN intensity and the
    // backscatter store (--bs-store, #80) accumulates nothing. node.cpp
    // emits the value UNCORRECTED; the angle correction is cube#81.
    gs.sounding.intensity = s.intensity;
    gs.sounding.beam_angle = s.beam_angle;
    // Per-beam slant range R = twtt*c/2 (set in the Sounding detections
    // ctor) for the tier-2 TL correction (cube#87).
    gs.sounding.slant_range = s.slant_range;
    gs.sounding.beam_index = s.beam_index;
    // The sonar-frame position, carried through so the depth-adaptive recon
    // can read the WATER DEPTH UNDER THE TRANSDUCER off `z` (cube#143): the
    // georeferenced `depth` above is a WGS84 ellipsoidal height (uma ADR-0002
    // D4) and the geoid offset would coarsen every tile by one to two levels.
    // Left unread by the estimator itself (batch_regen.cpp:55).
    gs.sounding.sonar_relative_position = s.sonar_relative_position;
    // The beam's arrival direction (cube_bathymetry#173): the chord from the
    // sensor origin to the touchdown, as a unit vector in the sensor's ENU
    // frame. `local` is exactly that offset, so no extra transform is needed.
    // Against the touchdown's own ENU frame the chord is off by about
    // range / R_earth (1e-5 rad at 100 m) and the projector traces no
    // refraction, so the chord is also the direction of arrival.
    const double chord = std::sqrt(local[0] * local[0] + local[1] * local[1] + local[2] * local[2]);
    if (chord > 0.0 && std::isfinite(chord)) {
      gs.sounding.beam_dir_enu = {local[0] / chord, local[1] / chord, local[2] / chord};
    }
    soundings.push_back(gs);
  }
  return soundings;
}

}  // namespace cube
