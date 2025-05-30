#include "cube_bathymetry/error_model.h"
#include <cmath>

namespace cube
{

ErrorModel::ErrorModel(const Vessel& vessel, const Device& device):
  vessel_(vessel)
{
  static_error_sources_.vertical_reduction = 
    vessel.draft_sdev*vessel.draft_sdev +
    vessel.ddraft_sdev*vessel.ddraft_sdev +
    vessel.loading_sdev*vessel.loading_sdev;

  static_error_sources_.heave_fixed = vessel.heave_fixed_sdev*vessel.heave_fixed_sdev;
  static_error_sources_.base_roll_variance = vessel.roll_sdev*vessel.roll_sdev + vessel.imu_rp_align_sdev*vessel.imu_rp_align_sdev;
  static_error_sources_.base_roll_variance *= (M_PI/180.0)*(M_PI/180.0);

  static_error_sources_.m_imu_offset_variance = vessel.imu_off_sdev*vessel.imu_off_sdev;
  static_error_sources_.m_gps_offset_variance = vessel.gps_off_sdev*vessel.gps_off_sdev;

  static_error_sources_.sound_speed_profile_variance = vessel.svp_sdev*vessel.svp_sdev;

  static_error_sources_.along_track_beamwidth_coefficient = (1.0 - cos((device.along_track_beamwidth*M_PI/180.0)/2.0))*(1.0 - cos((device.along_track_beamwidth*M_PI/180.0)/2.0));

  static_error_sources_.total_pitch_variance = vessel.pitch_sdev * vessel.pitch_sdev +
    vessel.imu_rp_align_sdev * vessel.imu_rp_align_sdev +
    vessel.pitch_stab_sdev * vessel.pitch_stab_sdev;
  static_error_sources_.total_pitch_variance *= (M_PI/180.0)*(M_PI/180.0);

  static_error_sources_.total_gyro_variance = vessel.gyro_sdev * vessel.gyro_sdev +
    vessel.imu_g_align_sdev * vessel.imu_g_align_sdev;
  static_error_sources_.total_gyro_variance *= (M_PI/180.0)*(M_PI/180.0);

  static_error_sources_.surface_sound_speed_variance = vessel.surf_ss_sdev * vessel.surf_ss_sdev;

  // assuming flat transducer
  static_error_sources_.min_angle = static_error_sources_.max_angle = -vessel.static_roll;

  static_error_sources_.total_latency_variance = vessel.gps_latency_sdev*vessel.gps_latency_sdev
    + vessel.imu_latency_sdev*vessel.imu_latency_sdev
    + vessel.tx_latency_sdev*vessel.tx_latency_sdev;
  
  static_error_sources_.total_gps_variance = vessel.gps_drms*vessel.gps_drms;
}

double ErrorModel::swath_heave(Platform& platform, PerPingErrorSources& per_ping_sources)
{
  double measured_heave = vessel_.heave_var_percent * 2.0 * platform.heave *
    vessel_.heave_var_percent*2.0*platform.heave;

  double pitch_comp = vessel_.imu_x*per_ping_sources.cos_pitch
    - vessel_.imu_y*per_ping_sources.sin_roll*per_ping_sources.sin_pitch
    - vessel_.imu_z*per_ping_sources.cos_roll*per_ping_sources.sin_pitch;
  double roll_comp = vessel_.imu_y*per_ping_sources.cos_roll*per_ping_sources.cos_pitch
    - vessel_.imu_z*per_ping_sources.sin_roll*per_ping_sources.cos_pitch;
  double offset_comp = per_ping_sources.sin_pitch * per_ping_sources.sin_pitch +
    per_ping_sources.sin_roll * per_ping_sources.sin_roll * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch +
    (1 - per_ping_sources.cos_roll * per_ping_sources.cos_pitch) * (1 - per_ping_sources.cos_roll * per_ping_sources.cos_pitch);
  double induced_heave = static_error_sources_.total_pitch_variance
    * pitch_comp * pitch_comp + static_error_sources_.base_roll_variance
    * roll_comp * roll_comp + 2.0 * static_error_sources_.m_imu_offset_variance * offset_comp;
  
  if(measured_heave < static_error_sources_.heave_fixed)
  {
    measured_heave = static_error_sources_.heave_fixed; // Eqn. 3.57
  }

  return induced_heave + measured_heave;
}

double ErrorModel::horizontal_positioning_error(Platform& platform, PerPingErrorSources& per_ping_sources)
{
  double offset_err = per_ping_sources.cos_pitch * per_ping_sources.cos_pitch +
    per_ping_sources.cos_roll * per_ping_sources.cos_roll +
    per_ping_sources.sin_roll * per_ping_sources.sin_roll * per_ping_sources.sin_pitch * per_ping_sources.sin_pitch +
    per_ping_sources.sin_roll * per_ping_sources.sin_roll +
    per_ping_sources.cos_roll * per_ping_sources.cos_roll * per_ping_sources.sin_pitch * per_ping_sources.sin_pitch;
  
  offset_err *= vessel_.gps_off_sdev* vessel_.gps_off_sdev
      + vessel_.imu_off_sdev * vessel_.imu_off_sdev;

  double heading_err = vessel_.gps_x * vessel_.gps_x * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch +
    vessel_.gps_y * vessel_.gps_y * per_ping_sources.cos_roll * per_ping_sources.cos_roll +
    vessel_.gps_y * vessel_.gps_y * per_ping_sources.sin_roll * per_ping_sources.sin_roll * per_ping_sources.sin_pitch * per_ping_sources.sin_pitch +
    vessel_.gps_z * vessel_.gps_z * per_ping_sources.sin_pitch * per_ping_sources.sin_pitch * per_ping_sources.cos_roll * per_ping_sources.cos_roll +
    vessel_.gps_z * vessel_.gps_z * per_ping_sources.sin_roll * per_ping_sources.sin_roll -
    vessel_.gps_x * vessel_.gps_y * per_ping_sources.cos_pitch * per_ping_sources.sin_pitch * per_ping_sources.sin_roll -
    vessel_.gps_x * vessel_.gps_z * per_ping_sources.cos_pitch * per_ping_sources.sin_pitch * per_ping_sources.cos_roll -
    vessel_.gps_y * vessel_.gps_z * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch * per_ping_sources.sin_roll * per_ping_sources.cos_roll;

  heading_err *= (M_PI / 180.0) * (M_PI / 180.0) * vessel_.gyro_sdev * vessel_.gyro_sdev;

  double roll_err = vessel_.gps_y * vessel_.gps_y * (per_ping_sources.sin_roll * per_ping_sources.sin_roll + per_ping_sources.cos_roll * per_ping_sources.cos_roll * per_ping_sources.sin_pitch * per_ping_sources.sin_pitch) +
    vessel_.gps_z * vessel_.gps_z * (per_ping_sources.cos_roll * per_ping_sources.cos_roll + per_ping_sources.sin_roll * per_ping_sources.sin_roll * per_ping_sources.sin_pitch * per_ping_sources.sin_pitch) +
    vessel_.gps_y * vessel_.gps_z * per_ping_sources.cos_roll * per_ping_sources.sin_roll * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch;
  roll_err *= (M_PI / 180.0) * (M_PI / 180.0) * vessel_.roll_sdev * vessel_.roll_sdev;

  double pitch_err = vessel_.gps_x * per_ping_sources.sin_pitch +
    vessel_.gps_y * per_ping_sources.sin_roll * per_ping_sources.cos_pitch +
    vessel_.gps_z * per_ping_sources.cos_roll * per_ping_sources.cos_pitch;
  pitch_err *= pitch_err * (M_PI / 180.0) * (M_PI / 180.0) * vessel_.pitch_sdev * vessel_.pitch_sdev;

  return offset_err + heading_err + roll_err + pitch_err;
}

double ErrorModel::horizontal_latency(Platform& platform, PerPingErrorSources& per_ping_sources)
{
  // full horizontal latency
  double sog_error = vessel_.gps_latency * vessel_.gps_latency * vessel_.sog_sdev * vessel_.sog_sdev * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch; // Eqn. 3.96

  double jitter_error = platform.vessel_speed * platform.vessel_speed *
    static_error_sources_.total_latency_variance * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch; // Eqn. 3.97

  double head_error = platform.vessel_speed * platform.vessel_speed *
    vessel_.gps_latency * vessel_.gps_latency * (M_PI / 180.0) * (M_PI / 180.0) *
    vessel_.gyro_sdev * vessel_.gyro_sdev * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch; // Eqn. 3.98

  double pitch_error = platform.vessel_speed * platform.vessel_speed *
    vessel_.gps_latency * vessel_.gps_latency * static_error_sources_.total_pitch_variance *
    per_ping_sources.sin_pitch * per_ping_sources.sin_pitch; // Eqn. 3.99

  return sog_error + jitter_error + head_error + pitch_error; // Eqn. 3.100
}

double ErrorModel::beam_angle(const Ping& ping, size_t i)
{
  return -ping.rx_beamwidths->at(i)+(M_PI / 180.0) * vessel_.static_roll;
}

double ErrorModel::swath_angle_error(
  Platform& platform,
  PerPingErrorSources& per_ping_sources,
  const Ping& ping, size_t i
)
{
  double meas_angle = beam_angle(ping, i);

  double offset = 0.0;
  double ang_surf_speed = 0.0;
  if(static_error_sources_.surface_sound_speed_variance > 0.0
    && (meas_angle < static_error_sources_.min_angle
    || meas_angle > static_error_sources_.max_angle))
  {
    if(meas_angle < static_error_sources_.min_angle)
    {
      offset = meas_angle - static_error_sources_.min_angle;
    }
    else if(meas_angle > static_error_sources_.max_angle)
    {
      offset = meas_angle - static_error_sources_.max_angle;
    }
    ang_surf_speed = tan(offset) * tan(offset) *
      static_error_sources_.surface_sound_speed_variance / (platform.surf_sspeed * platform.surf_sspeed);
  }
  double ang_svp = tan(meas_angle) * tan(meas_angle) * static_error_sources_.sound_speed_profile_variance /
    (4.0 * platform.mean_speed * platform.mean_speed); // Eqn. 3.34
  
  double ang_meas = ping.rx_beamwidths->at(i)/12.0;
  ang_meas *= ang_meas;

  return ang_meas + ang_svp + ang_surf_speed + static_error_sources_.base_roll_variance; // Eqn. 3.43

}

double ErrorModel::swath_vertical(
  Platform& platform,
  PerPingErrorSources& per_ping_sources,
  const Ping& ping, size_t i
)
{
  return static_error_sources_.vertical_reduction
    + per_ping_sources.total_heave_variance
    + swath_depth(platform, per_ping_sources, ping, i).second;
}

double ErrorModel::range_error(double depth)
{
  // todo: replace with device specific info
  return depth*0.003;
}

std::pair<double, double> ErrorModel::swath_depth(
    Platform& platform,
    PerPingErrorSources& per_ping_sources,
    const Ping& ping, size_t i
)
{
  double meas_angle = beam_angle(ping, i);
  double cosT = cos((platform.roll * M_PI / 180.0) + meas_angle);
  double sinT = sin((platform.roll * M_PI / 180.0) + meas_angle);

  double range = (*ping.two_way_travel_times)[i] * ping.sound_speed / 2.0;

  double depth = range * cosT;

  double range_err = range_error(depth)
    + (range*range*static_error_sources_.sound_speed_profile_variance)
    / (platform.mean_speed * platform.mean_speed);

  range_err *= per_ping_sources.cos_pitch * cosT * per_ping_sources.cos_pitch * cosT; // Eqn. 3.47

  double total_roll_variance = swath_angle_error(
    platform, per_ping_sources, ping, i
  );

  double angle_err = total_roll_variance * range * range *
    sinT * sinT * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch; // Eqn. 3.48

  double pitch_err = range * range * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch * per_ping_sources.sin_pitch * per_ping_sources.sin_pitch *
    static_error_sources_.total_pitch_variance; // Eqn. 3.49

  double beamwidth_err = static_error_sources_.along_track_beamwidth_coefficient * depth * depth; // Eqn. 3.51

  return std::make_pair(depth, range_err + angle_err + pitch_err + beamwidth_err); // Eqn. 3.52

}


double ErrorModel::swath_horizontal(
  Platform& platform,
  PerPingErrorSources& per_ping_sources,
  const Ping& ping,
  size_t i,
  Sounding& sounding
)
{
  return horizontal_positioning_error(
    platform, per_ping_sources, ping, i, sounding
  ) + per_ping_sources.horizontal_tx_relative_variance
    + per_ping_sources.horizontal_latency_variance
    + static_error_sources_.total_gps_variance;
}

double ErrorModel::horizontal_positioning_error(
  Platform& platform,
  PerPingErrorSources& per_ping_sources,
  const Ping& ping, size_t i, Sounding& sounding
)
{
  double meas_angle = beam_angle(ping, i);

  double profile_err = static_error_sources_.sound_speed_profile_variance * sounding.depth / (platform.mean_speed * cos(meas_angle));
  profile_err *= profile_err;

  double rmeas_err = range_error(sounding.depth);
  double range_err = (profile_err + rmeas_err)
    * (1.0 - per_ping_sources.cos_pitch * per_ping_sources.cos_pitch * cos(meas_angle) * cos(meas_angle));
  double heading_err = static_error_sources_.total_gyro_variance * sounding.depth * sounding.depth *
    (per_ping_sources.sin_pitch * per_ping_sources.sin_pitch + tan(meas_angle) * tan(meas_angle)); // Eqn. 3.78

  double range = (*ping.two_way_travel_times)[i] * ping.sound_speed / 2.0;

  double angle_err = swath_angle_error(
    platform, per_ping_sources, ping, i
  ) * range * range * (1.0 - per_ping_sources.cos_pitch * per_ping_sources.cos_pitch * sin(meas_angle) * sin(meas_angle));

  double pitch_err = sounding.depth * sounding.depth * per_ping_sources.cos_pitch * per_ping_sources.cos_pitch * static_error_sources_.total_pitch_variance; // Eqn. 3.80

  return range_err + heading_err + angle_err + pitch_err;
}

std::vector<Sounding> ErrorModel::compute(Ping& ping, Platform& platform)
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

  for(size_t i = 0; i < ping.two_way_travel_times->size(); ++i)
  {
    Sounding sounding(swath_depth(platform, per_ping_sources, ping, i).first);

    sounding.vertical_error = swath_vertical(
      platform, per_ping_sources, ping, i);

    
    sounding.horizontal_error = swath_horizontal(
      platform, per_ping_sources, ping, i, sounding);


    soundings.push_back(sounding);
  }
  return soundings;
}

} // namespace cube
