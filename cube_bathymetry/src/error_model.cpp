#include "cube_bathymetry/error_model.h"
#include <cmath>

namespace cube
{

ErrorModel::ErrorModel(const Vessel& vessel, const Device& device)
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

  static_error_sources_.along_track_beamwdith_coefficient = (1.0 - cos((device.along_track_beamwidth*M_PI/180.0)/2.0))*(1.0 - cos((device.along_track_beamwidth*M_PI/180.0)/2.0));

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



} // namespace cube
