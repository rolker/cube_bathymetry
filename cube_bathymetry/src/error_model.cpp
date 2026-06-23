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


#include "cube_bathymetry/error_model.h"
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace cube
{

ErrorModel::ErrorModel(const Vessel & vessel, const Device & device)
:vessel_(vessel), device_(device)
{
  static_error_sources_.vertical_reduction =
    vessel.draft_sdev * vessel.draft_sdev +
    vessel.ddraft_sdev * vessel.ddraft_sdev +
    vessel.loading_sdev * vessel.loading_sdev;

  // Tide terms (Calder Eqn. 3.63): only included when the grid is reduced to a
  // tidal datum. The grid here is ellipsoid-referenced by default (#47), so the
  // tide_*_sdev terms are intentionally omitted and swath_vertical() is
  // unchanged. Setting ellipsoidal_referenced = false adds them back here, so
  // the rest of the model needs no awareness of the mode. See the divergences doc.
  if(!vessel.ellipsoidal_referenced) {
    static_error_sources_.vertical_reduction +=
      vessel.tide_measured_sdev * vessel.tide_measured_sdev +
      vessel.tide_predicted_sdev * vessel.tide_predicted_sdev;
  }

  static_error_sources_.heave_fixed = vessel.heave_fixed_sdev * vessel.heave_fixed_sdev;
  static_error_sources_.base_roll_variance = vessel.roll_sdev * vessel.roll_sdev +
    vessel.imu_rp_align_sdev * vessel.imu_rp_align_sdev;
  static_error_sources_.base_roll_variance *= (M_PI / 180.0) * (M_PI / 180.0);

  static_error_sources_.m_imu_offset_variance = vessel.imu_off_sdev * vessel.imu_off_sdev;
  static_error_sources_.m_gps_offset_variance = vessel.gps_off_sdev * vessel.gps_off_sdev;

  static_error_sources_.sound_speed_profile_variance = vessel.svp_sdev * vessel.svp_sdev;

  static_error_sources_.along_track_beamwidth_coefficient = (1.0 -
    cos((device.along_track_beamwidth * M_PI / 180.0) / 2.0)) *
    (1.0 - cos((device.along_track_beamwidth * M_PI / 180.0) / 2.0));

  static_error_sources_.total_pitch_variance = vessel.pitch_sdev * vessel.pitch_sdev +
    vessel.imu_rp_align_sdev * vessel.imu_rp_align_sdev +
    vessel.pitch_stab_sdev * vessel.pitch_stab_sdev;
  static_error_sources_.total_pitch_variance *= (M_PI / 180.0) * (M_PI / 180.0);

  static_error_sources_.total_gyro_variance = vessel.gyro_sdev * vessel.gyro_sdev +
    vessel.imu_g_align_sdev * vessel.imu_g_align_sdev;
  static_error_sources_.total_gyro_variance *= (M_PI / 180.0) * (M_PI / 180.0);

  static_error_sources_.surface_sound_speed_variance = vessel.surf_ss_sdev * vessel.surf_ss_sdev;

  // assuming flat transducer
  static_error_sources_.min_angle = static_error_sources_.max_angle = -vessel.static_roll;

  static_error_sources_.total_latency_variance = vessel.gps_latency_sdev * vessel.gps_latency_sdev +
    vessel.imu_latency_sdev * vessel.imu_latency_sdev +
    vessel.tx_latency_sdev * vessel.tx_latency_sdev;

  static_error_sources_.total_gps_variance = vessel.gps_drms * vessel.gps_drms;
}

double ErrorModel::swath_heave(
  const Platform & platform,
  const PerPingErrorSources & per_ping_sources) const
{
  double measured_heave = vessel_.heave_var_percent * 2.0 * platform.heave *
    vessel_.heave_var_percent * 2.0 * platform.heave;

  double pitch_comp = vessel_.imu_x * per_ping_sources.cos_pitch -
    vessel_.imu_y * per_ping_sources.sin_roll * per_ping_sources.sin_pitch -
    vessel_.imu_z * per_ping_sources.cos_roll * per_ping_sources.sin_pitch;
  double roll_comp = vessel_.imu_y * per_ping_sources.cos_roll * per_ping_sources.cos_pitch -
    vessel_.imu_z * per_ping_sources.sin_roll * per_ping_sources.cos_pitch;
  double offset_comp = per_ping_sources.sin_pitch * per_ping_sources.sin_pitch +
    per_ping_sources.sin_roll * per_ping_sources.sin_roll * per_ping_sources.cos_pitch *
    per_ping_sources.cos_pitch +
    (1 - per_ping_sources.cos_roll * per_ping_sources.cos_pitch) *
    (1 - per_ping_sources.cos_roll * per_ping_sources.cos_pitch);
  double induced_heave = static_error_sources_.total_pitch_variance *
    pitch_comp * pitch_comp + static_error_sources_.base_roll_variance *
    roll_comp * roll_comp + 2.0 * static_error_sources_.m_imu_offset_variance * offset_comp;

  if(measured_heave < static_error_sources_.heave_fixed) {
    measured_heave = static_error_sources_.heave_fixed;
  }

  return induced_heave + measured_heave;
}

double ErrorModel::horizontal_positioning_error(
  const Platform & platform,
  const PerPingErrorSources & per_ping_sources) const
{
  double offset_err = per_ping_sources.cos_pitch * per_ping_sources.cos_pitch +
    per_ping_sources.cos_roll * per_ping_sources.cos_roll +
    per_ping_sources.sin_roll * per_ping_sources.sin_roll * per_ping_sources.sin_pitch *
    per_ping_sources.sin_pitch +
    per_ping_sources.sin_roll * per_ping_sources.sin_roll +
    per_ping_sources.cos_roll * per_ping_sources.cos_roll * per_ping_sources.sin_pitch *
    per_ping_sources.sin_pitch;

  offset_err *= vessel_.gps_off_sdev * vessel_.gps_off_sdev +
    vessel_.imu_off_sdev * vessel_.imu_off_sdev;

  double heading_err = vessel_.gps_x * vessel_.gps_x * per_ping_sources.cos_pitch *
    per_ping_sources.cos_pitch +
    vessel_.gps_y * vessel_.gps_y * per_ping_sources.cos_roll * per_ping_sources.cos_roll +
    vessel_.gps_y * vessel_.gps_y * per_ping_sources.sin_roll * per_ping_sources.sin_roll *
    per_ping_sources.sin_pitch * per_ping_sources.sin_pitch +
    vessel_.gps_z * vessel_.gps_z * per_ping_sources.sin_pitch * per_ping_sources.sin_pitch *
    per_ping_sources.cos_roll * per_ping_sources.cos_roll +
    vessel_.gps_z * vessel_.gps_z * per_ping_sources.sin_roll * per_ping_sources.sin_roll -
    vessel_.gps_x * vessel_.gps_y * per_ping_sources.cos_pitch * per_ping_sources.sin_pitch *
    per_ping_sources.sin_roll -
    vessel_.gps_x * vessel_.gps_z * per_ping_sources.cos_pitch * per_ping_sources.sin_pitch *
    per_ping_sources.cos_roll -
    vessel_.gps_y * vessel_.gps_z * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch *
    per_ping_sources.sin_roll * per_ping_sources.cos_roll;

  heading_err *= (M_PI / 180.0) * (M_PI / 180.0) * vessel_.gyro_sdev * vessel_.gyro_sdev;

  double roll_err = vessel_.gps_y * vessel_.gps_y *
    (per_ping_sources.sin_roll * per_ping_sources.sin_roll + per_ping_sources.cos_roll *
    per_ping_sources.cos_roll * per_ping_sources.sin_pitch * per_ping_sources.sin_pitch) +
    vessel_.gps_z * vessel_.gps_z *
    (per_ping_sources.cos_roll * per_ping_sources.cos_roll + per_ping_sources.sin_roll *
    per_ping_sources.sin_roll * per_ping_sources.sin_pitch * per_ping_sources.sin_pitch) +
    vessel_.gps_y * vessel_.gps_z * per_ping_sources.cos_roll * per_ping_sources.sin_roll *
    per_ping_sources.cos_pitch * per_ping_sources.cos_pitch;
  roll_err *= (M_PI / 180.0) * (M_PI / 180.0) * vessel_.roll_sdev * vessel_.roll_sdev;

  double pitch_err = vessel_.gps_x * per_ping_sources.sin_pitch +
    vessel_.gps_y * per_ping_sources.sin_roll * per_ping_sources.cos_pitch +
    vessel_.gps_z * per_ping_sources.cos_roll * per_ping_sources.cos_pitch;
  pitch_err *= pitch_err * (M_PI / 180.0) * (M_PI / 180.0) * vessel_.pitch_sdev *
    vessel_.pitch_sdev;

  return offset_err + heading_err + roll_err + pitch_err;
}

double ErrorModel::horizontal_latency(
  const Platform & platform,
  const PerPingErrorSources & per_ping_sources) const
{
  // full horizontal latency
  // Eqn. 3.96
  double sog_error = vessel_.gps_latency * vessel_.gps_latency * vessel_.sog_sdev *
    vessel_.sog_sdev * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch;

  // Floor a non-finite speed (offline replay supplies NaN; a corrupt odom twist
  // could yield +/-inf) to 0 so the speed-dependent terms below stay finite. A
  // non-finite value here propagates through horizontal TPU and, via the
  // Node::insert capture-distance term (sqrt(horizontal_error)), poisons the
  // depth variance and zeroes the whole epoch (cube_bathymetry#63); Node::insert
  // mirrors this isfinite defense on the same quantity downstream. A finite
  // speed's sign is left untouched: it enters every latency term squared
  // (jitter/head/pitch), so the sign is irrelevant and Calder's errmod_iho
  // applies no abs/clamp here either (#16).
  const double vessel_speed =
    std::isfinite(platform.vessel_speed) ? platform.vessel_speed : 0.0;

  double jitter_error = vessel_speed * vessel_speed *
  // Eqn. 3.97
    static_error_sources_.total_latency_variance * per_ping_sources.cos_pitch *
    per_ping_sources.cos_pitch;
  double head_error = vessel_speed * vessel_speed *
    vessel_.gps_latency * vessel_.gps_latency * (M_PI / 180.0) * (M_PI / 180.0) *
  // Eqn. 3.98
    vessel_.gyro_sdev * vessel_.gyro_sdev * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch;

  double pitch_error = vessel_speed * vessel_speed *
    vessel_.gps_latency * vessel_.gps_latency * static_error_sources_.total_pitch_variance *
    per_ping_sources.sin_pitch * per_ping_sources.sin_pitch;

  return sog_error + jitter_error + head_error + pitch_error;
}

double ErrorModel::beam_angle(
  const marine_acoustic_msgs::msg::SonarDetections & detections,
  size_t i) const
{
  return -detections.rx_angles[i] + (M_PI / 180.0) * vessel_.static_roll;
}

double ErrorModel::swath_angle_error(
  const Platform & platform,
  const PerPingErrorSources & per_ping_sources,
  const marine_acoustic_msgs::msg::SonarDetections & detections, size_t i
) const
{
  double meas_angle = beam_angle(detections, i);

  double offset = 0.0;
  double ang_surf_speed = 0.0;
  if(static_error_sources_.surface_sound_speed_variance > 0.0 &&
    (meas_angle < static_error_sources_.min_angle ||
    meas_angle > static_error_sources_.max_angle))
  {
    if(meas_angle < static_error_sources_.min_angle) {
      offset = meas_angle - static_error_sources_.min_angle;
    } else if(meas_angle > static_error_sources_.max_angle) {
      offset = meas_angle - static_error_sources_.max_angle;
    }
    ang_surf_speed = tan(offset) * tan(offset) *
      static_error_sources_.surface_sound_speed_variance /
      (platform.surf_sspeed * platform.surf_sspeed);
  }
  double ang_svp = tan(meas_angle) * tan(meas_angle) *
    static_error_sources_.sound_speed_profile_variance /
    (4.0 * platform.mean_speed * platform.mean_speed);

  double ang_meas = device_.across_track_beamwidth / 12.0;
  if(i < detections.ping_info.rx_beamwidths.size()) {
    ang_meas = detections.ping_info.rx_beamwidths[i] * (M_PI / 180.0) / 12.0;
  }
  ang_meas *= ang_meas;

  return ang_meas + ang_svp + ang_surf_speed + static_error_sources_.base_roll_variance;
}

double ErrorModel::swath_vertical(
  const Platform & platform,
  const PerPingErrorSources & per_ping_sources,
  const marine_acoustic_msgs::msg::SonarDetections & detections, size_t i
) const
{
  return static_error_sources_.vertical_reduction +
         per_ping_sources.total_heave_variance +
         swath_depth(platform, per_ping_sources, detections, i).second;
}

double ErrorModel::range_error(double depth) const
{
  // Range error as the larger of a depth-proportional term and an absolute
  // floor (device-specific; see Device::range_error_percent / _floor_m and the
  // divergences doc). depth is negative by convention, so take the magnitude;
  // the floor prevents a zero error near the surface.
  auto error = std::max(std::abs(depth) * device_.range_error_percent,
      device_.range_error_floor_m);
  return error * error;
}

std::pair<double, double> ErrorModel::swath_depth(
  const Platform & platform,
  const PerPingErrorSources & per_ping_sources,
  const marine_acoustic_msgs::msg::SonarDetections & detections, size_t i
) const
{
  double meas_angle = beam_angle(detections, i);
  double cosT = cos((platform.roll * M_PI / 180.0) + meas_angle);
  double sinT = sin((platform.roll * M_PI / 180.0) + meas_angle);

  double range = detections.two_way_travel_times[i] * detections.ping_info.sound_speed / 2.0;

  double depth = -range * cosT;

  double range_err = range_error(depth) +
    (range * range * static_error_sources_.sound_speed_profile_variance) /
    (platform.mean_speed * platform.mean_speed);

  range_err *= per_ping_sources.cos_pitch * cosT * per_ping_sources.cos_pitch * cosT;

  double total_roll_variance = swath_angle_error(
    platform, per_ping_sources, detections, i
  );

  double angle_err = total_roll_variance * range * range *
    sinT * sinT * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch;

  double pitch_err = range * range * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch *
    per_ping_sources.sin_pitch * per_ping_sources.sin_pitch *
    static_error_sources_.total_pitch_variance;

  double beamwidth_err = static_error_sources_.along_track_beamwidth_coefficient * depth * depth;

  return std::make_pair(depth, range_err + angle_err + pitch_err + beamwidth_err);
}


double ErrorModel::swath_horizontal(
  const Platform & platform,
  const PerPingErrorSources & per_ping_sources,
  const marine_acoustic_msgs::msg::SonarDetections & detections,
  size_t i,
  const Sounding & sounding
) const
{
  return horizontal_positioning_error(
    platform, per_ping_sources, detections, i, sounding
  ) + per_ping_sources.horizontal_tx_relative_variance +
         per_ping_sources.horizontal_latency_variance +
         static_error_sources_.total_gps_variance;
}

double ErrorModel::horizontal_positioning_error(
  const Platform & platform,
  const PerPingErrorSources & per_ping_sources,
  const marine_acoustic_msgs::msg::SonarDetections & detections,
  size_t i,
  const Sounding & sounding
) const
{
  double meas_angle = beam_angle(detections, i);

  // Calder forms this term from the SVP standard deviation (svp_sdev) and then
  // squares once (errmod_full.c:663-664). sound_speed_profile_variance is already
  // svp_sdev^2, so using it here and squaring the whole expression would carry
  // svp_sdev^4. Use svp_sdev directly. See #46.
  double profile_err = vessel_.svp_sdev * sounding.depth /
    (platform.mean_speed * cos(meas_angle));
  profile_err *= profile_err;

  double rmeas_err = range_error(sounding.depth);
  double range_err = (profile_err + rmeas_err) *
    (1.0 - per_ping_sources.cos_pitch * per_ping_sources.cos_pitch * cos(meas_angle) *
    cos(meas_angle));
  double heading_err = static_error_sources_.total_gyro_variance * sounding.depth * sounding.depth *
    (per_ping_sources.sin_pitch * per_ping_sources.sin_pitch + tan(meas_angle) * tan(meas_angle));

  double range = detections.two_way_travel_times[i] * detections.ping_info.sound_speed / 2.0;

  double angle_err = swath_angle_error(
    platform, per_ping_sources, detections, i
    ) * range * range *
    (1.0 - per_ping_sources.cos_pitch * per_ping_sources.cos_pitch * sin(meas_angle) *
    sin(meas_angle));

  double pitch_err = sounding.depth * sounding.depth * per_ping_sources.cos_pitch *
    per_ping_sources.cos_pitch * static_error_sources_.total_pitch_variance;

  return range_err + heading_err + angle_err + pitch_err;
}

// std::vector<Sounding> ErrorModel::compute(
//   marine_acoustic_msgs::msg::SonarDetections& detections,
//   Platform& platform)
std::vector<Sounding> ErrorModel::compute(
  const marine_acoustic_msgs::msg::SonarDetections & detections, const Platform & platform) const
{
  PerPingErrorSources per_ping_sources;
  per_ping_sources.cos_pitch = cos(platform.pitch * M_PI / 180.0);
  per_ping_sources.sin_pitch = sin(platform.pitch * M_PI / 180.0);
  per_ping_sources.cos_roll = cos(platform.roll * M_PI / 180.0);
  per_ping_sources.sin_roll = sin(platform.roll * M_PI / 180.0);

  per_ping_sources.total_heave_variance = swath_heave(platform, per_ping_sources);

  per_ping_sources.horizontal_tx_relative_variance =
    horizontal_positioning_error(platform, per_ping_sources);

  // full horizontal latency

  per_ping_sources.horizontal_latency_variance =
    horizontal_latency(platform, per_ping_sources);


  std::vector<Sounding> soundings;

  for(size_t i = 0; i < detections.two_way_travel_times.size(); ++i) {
    Sounding sounding(detections, i, swath_depth(platform, per_ping_sources, detections, i).first);

    sounding.vertical_error = swath_vertical(
      platform, per_ping_sources, detections, i);


    sounding.horizontal_error = swath_horizontal(
      platform, per_ping_sources, detections, i, sounding);


    soundings.push_back(sounding);
  }
  return soundings;
}

}  // namespace cube
