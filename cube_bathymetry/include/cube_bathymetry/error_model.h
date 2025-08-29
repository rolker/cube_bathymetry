// Copyright 2025 Center for Coastal and Ocean Mapping and NOAA-UNH Joint Hydrographic Center, University of New Hampshire
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


#ifndef CUBE_BATHYMETRY_ERROR_MODEL_H
#define CUBE_BATHYMETRY_ERROR_MODEL_H

#include <cstdint>
#include <vector>
#include "cube_bathymetry/sounding.h"
#include "marine_acoustic_msgs/msg/sonar_detections.hpp"

namespace cube
{

struct Device
{
  /// degrees
  double across_track_beamwidth = 2.0;

  /// degrees
  double along_track_beamwidth = 2.0;
};

struct Platform
{
  double timestamp;		/* seconds (with ms accuracy) since 00:00 01/01/1970 */
  double latitude;		/* Latitude in degrees */
  double longitude;		/* Longitude in degrees */
  float roll;			/* Roll in degrees, +ve is port side up */
  float pitch;			/* Pitch in degrees, +ve is bow up */
  float heading;		/* Heading in degrees, +ve CW from N */
  float heave;			/* Heave in meters, +ve down */
  float surf_sspeed;	/* Surface sound speed, m/s */
  float mean_speed;		/* Geometric mean equivalent sound speed, m/s */
  float vessel_speed;	/* Vessel's speed-over-ground, m/s */
};


struct Vessel
{
  /* Direct offsets, usu. measured wrt transducer head */
  
  /// GPS offsets, m
	double gps_x = 0.0;
  double gps_y = 0.0;
  double gps_z = 0.0;

	double	gps_off_sdev = 0.005;			/* SDev of GPS offset measurements, m */
	double gps_latency_sdev = 0.03;		/* SDev of GPS latency error, s. */
	double	gps_drms = 2.0;				/* Circularly symmetric GPS position error, m. */
	double	gps_latency = 0.0;			/* Latency between GPS and logger, s. */

  /// IMU offsets, m
	double imu_x = 0.0;
  double imu_y = 0.0;
  double imu_z = 0.0;

	double	imu_off_sdev = 0.005;			/* SDev of IMU offset measurements, m */

  /* IMU alignment, deg. */
	double imu_r = 0.0;
  double imu_p = 0.0;
  double imu_g = 0.0;	

	double	imu_rp_align_sdev = 0.05;		/* SDev of IMU roll/pitch alignment err., deg */
	double	imu_g_align_sdev = 0.10;		/* SDev of IMU heading alignment err, deg. */
	double	imu_latency_sdev = 0.005;		/* SDev of IMU latency error, s. */

	double draft = 0.0;					/* Draft of tx head below water line, m */
	double	tx_latency_sdev = 0.005;		/* SDev of tx head latency, s. */
	double	static_roll = 0.0;			/* Static head roll (mounting angle), deg. +ve is port side up*/


	/* Measurement accuracies */
	double	roll_sdev = 0.05;				/* SDev of roll measurement, deg. */
	double	pitch_sdev = 0.05;				/* SDev of pitch measurement, deg. */
	double	pitch_stab_sdev = 0.0;		/* SDev of pitch stabilisation, deg. */
	double	gyro_sdev =  0.50;				/* SDev of gyro measurement, deg. */
	double	svp_sdev = 0.52;				/* SDev of SVP cast measurements, m/s */
	double	surf_ss_sdev = 0.50;			/* SDev of sound speed surface meas., m/s */


	double	heave_fixed_sdev = 0.05;		/* SDev of heave measurements, m */
	double	heave_var_percent = 0.05;		/* Percentage of measurement variable error */
  double	draft_sdev = 0.02;				/* SDev of water level measurements, m */
	double	ddraft_sdev = 0.02;			/* SDev of dynamic draft measurements, m */
	double	loading_sdev = 0.02;			/* SDev of platform loading meas., m */
	double	sog_sdev = 0.02;				/* SDev of speed-over-ground meas., m/s */
	double  tide_measured_sdev = 0.02;		/* SDev of tide guage readings, m */
	double	tide_predicted_sdev = 0.02;	/* SDev of tide prediction error, m */

};

struct StaticErrorSources
{
  // Vertical error sources and components

  /// draft + water level reduction var in m.
  double vertical_reduction;

  /// Fixed component of heave error
  double heave_fixed;

  /// Combination of roll meas. and align. err
  double base_roll_variance;

  /// Profile SVP measurement variance
  double sound_speed_profile_variance;

  /// Coefficient of depth for along track beamwidth component (eqn. 3.51)
  double along_track_beamwidth_coefficient;

  /// Static pitch error variance
  double total_pitch_variance;

  /// Transition angle (rad)
  double steering_angle;

  /// Angles (rad) at which beams start to be
  /// steered.  This allows asymmetry as occurs
  /// where tdrs are offset by static roll.
  double min_angle, max_angle;

  /// Variance in measurement of speed of sound
  /// at the transducer, if beams are steered
  double surface_sound_speed_variance;


  // Horizontal error sources and components

  /// IMU offset determination variance, rad^2
  double m_imu_offset_variance;

  /// GPS offset determination variance, rad^2
  double m_gps_offset_variance;

  /// Variance of latency errors, s^2.
  double total_latency_variance;

  /// Static gyro error variance, rad^2.
  double total_gyro_variance;

  /// Nominal GPS accuracy, m^2.
  double total_gps_variance;
};


///  Dynamic variables initialised on a per-ping basis
struct PerPingErrorSources
{
  /// Dynamically component total roll error
  //double total_roll_variance;

  /// Dynamic per ping heave error variance, m2
  double total_heave_variance;

  /// Dynamic per ping latency error, m^2.
  double horizontal_latency_variance;

	/// Dynamic per ping GPS offset error, m^2
  double horizontal_tx_relative_variance;

	/// Trig. values from platform orient.
  double cos_roll, sin_roll, cos_pitch, sin_pitch;
};

/// Adapts arrays of data from a ping for use with the error model
/// Designed to work with ROS marine_acoustic_msgs::msg::SonarDetections, but adaptable
/// to other similar data structures.
struct Ping
{
  /// Frequency in Hz
  float frequency = 0.0;
  /// Sound speed in m/s
  float sound_speed = 0.0;
  /// Sonar reported -3db transmit beamwidths in radians
  const std::vector<float>* tx_beamwidths = nullptr;
  /// Sonar reported -3db transmit beamwidths in radians
  const std::vector<float>* rx_beamwidths = nullptr;
  /// Detection flags. 0 means good.
  const std::vector<uint8_t>* detection_flags = nullptr;
  /// travel times in seconds
  const std::vector<float>* two_way_travel_times = nullptr;
  /// Transmit steering angles in radians, positive is forwards
  const std::vector<float>* tx_angles = nullptr;
  /// Receive steering angles in radians, positive is starboard
  const std::vector<float>* rx_angles = nullptr;
};

class ErrorModel
{
public:
  ErrorModel(const Vessel& vessel, const Device& device);

  std::vector<Sounding> compute(const marine_acoustic_msgs::msg::SonarDetections& detections, const Platform& platform) const;

private:
  /// Compute induced and measured heave components.
  /// Returns variance of total heave component of vertical error.
  /// This implements eqn. 3.57, 3.58, and 3.59.
  double swath_heave(const Platform& platform, const PerPingErrorSources& per_ping_sources) const;

  /// Compute variance of horizontal positioning error caused by
  /// GPS antennae not being at the transducer head
  /// Returns approximate 95% confidence interval for error
  /// This computes eqn. 3.90, summarizing the component of horizontal
  /// error due to misalignment of the GPS antennae and the tx head.
  /// Note that in keeping with the report and spreadsheet, we return
  /// twice the nominal variance in order to approximate the 95% conf.
  /// interval assuming a Gaussian distribution.
  double horizontal_positioning_error(const Platform& platform, const PerPingErrorSources& per_ping_sources) const;

  /// Compute approximate 95% error bound due to latency errors
  /// We assume that the coefficients for eqn 3.100 have been pre-computed
  /// and stored in the workspace, and that the trig. functions for the
  /// current swath orientation have been computed.
  double horizontal_latency(const Platform& platform, const PerPingErrorSources& per_ping_sources) const;

  double beam_angle(const marine_acoustic_msgs::msg::SonarDetections& detections, size_t i) const;

  /// Compute vertical roll/pointing angle error
  /// This computes Eqns. 3.43 and its parents.
  double swath_angle_error(
    const Platform& platform,
    const PerPingErrorSources& per_ping_sources,
    const marine_acoustic_msgs::msg::SonarDetections& detections, size_t i
  ) const;

  /// Compute vertical swath error budget
  /// Returns variance estimate of total reduced depth error
  /// Comment: This implements eqn. 3.65, 3.63, and 3.61, and uses sub-routines
  /// to compute the other components (heave and measured depth). Result
  /// is the total variance associated with the reduced depth. Note that
  /// for speed, eqn. 3.63 and 3.61 (water level reduction and dynamic
  /// draft variance) are pre-computed and stored in the workspace.
  double swath_vertical(
    const Platform& platform,
    const PerPingErrorSources& per_ping_sources,
    const marine_acoustic_msgs::msg::SonarDetections& detections, size_t i
  ) const;

  double range_error(double depth) const;

  /// Compute measured depth error component
  /// Returns variance of total measured depth component of vert. error.
  /// This implements eqn. 3.52, and the various components which
  /// are required for it.
  /// Returns depth and error.
  std::pair<double, double> swath_depth(
    const Platform& platform,
    const PerPingErrorSources& per_ping_sources,
    const marine_acoustic_msgs::msg::SonarDetections& detections, size_t i
  ) const;

  /// Compute horizontal swath error budget.
  /// Returns variance estimate for total horizontal error budget.
  /// This implements equations 3.100, 3.90, 3.82 and 3.69 for the
  /// components of the error budget, combining them with 3.70.
  double swath_horizontal(
    const Platform& platform,
    const PerPingErrorSources& per_ping_sources,
    const marine_acoustic_msgs::msg::SonarDetections& detections,
    size_t i,
    const Sounding& sounding
  ) const;

  /// Compute component of horizontal positioning error associated with
  /// ship attitude and offsets.
  /// Returns approximate 95% confidence interval.
  /// This assumes, per the spreadsheet and report, that we have to work
  /// at the 95% confidence level due to the drms approximation. We
  /// multiply the standard deviation estimate by 2.0 to approximate this.
  /// We are computing eqns. 3.77-3.82. Note that the spreadsheet
  /// does not include any component for the along-track beam angle,
  /// unlike the report, and we ignore it here also.
  double horizontal_positioning_error(
    const Platform& platform,
    const PerPingErrorSources& per_ping_sources,
    const marine_acoustic_msgs::msg::SonarDetections& detections, size_t i,
    const Sounding& sounding
  ) const;

  Vessel vessel_;
  StaticErrorSources static_error_sources_;
  Device device_;
};


} // namespace cube

#endif
