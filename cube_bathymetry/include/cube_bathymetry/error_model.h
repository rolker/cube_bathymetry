#ifndef CUBE_BATHYMETRY_ERROR_MODEL_H
#define CUBE_BATHYMETRY_ERROR_MODEL_H


namespace cube
{

struct Device
{
  /// degrees
  double across_track_beamwidth = 2.0;

  /// degrees
  double along_track_beamwidth = 2.0;
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
	double	static_roll = 0.0;			/* Static head roll (mounting angle), deg. */


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
  double along_track_beamwdith_coefficient;

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
  double total_roll_variance;

  /// Dynamic per ping heave error variance, m2
  double total_heave_variance;

  /// Dynamic per ping latency error, m^2.
  double horizontal_latency_variance;

	/// Dynamic per ping GPS offset error, m^2
  double horizontal_tx_relative_variance;

	/// Trig. values from platform orient.
  double cos_roll, sin_roll, cos_pitch, sin_pitch;
};

class ErrorModel
{
  ErrorModel(const Vessel& vessel, const Device& device);

private:

  StaticErrorSources static_error_sources_;
};


} // namespace cube

#endif
