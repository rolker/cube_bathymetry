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


#ifndef CUBE_BATHYMETRY__ERROR_MODEL_H_
#define CUBE_BATHYMETRY__ERROR_MODEL_H_

#include <cstdint>
#include <vector>
#include <utility>
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

  /// Range error as a fraction of depth (0.005 == 0.5%). Sonar-device specific.
  /// Replaces Calder's hardcoded 5% placeholder. See docs/divergences_from_calder.md.
    double range_error_percent = 0.005;

  /// Absolute floor on range error, m. Prevents a zero error near the surface
  /// where the percentage term vanishes.
    double range_error_floor_m = 0.05;
  };

  struct Platform
  {
    double timestamp;  /* seconds (with ms accuracy) since 00:00 01/01/1970 */
    /* No latitude/longitude/heading: the error model never uses them. They were
     * inherited from Calder's Platform (where they fed georeferencing); in this
     * ROS port georeferencing is done by TF, so they were dead fields. */
    /* RADIANS, not degrees (#147). Platform is filled by our own
     * DetectionsProjector straight from tf2::getEulerYPR, which returns
     * radians -- there is no human-typed, datasheet-facing configuration
     * surface here of the kind that keeps Device/Vessel angles in degrees, so
     * the units are normalized at the type instead of at the use site. Before
     * #147 these were documented as degrees, converted as degrees by
     * ErrorModel, and filled with radians by the one and only producer, which
     * understated attitude by 57.3x and effectively switched it off.
     *
     * Sign conventions are CALDER'S, unchanged (#144). Every equation ported
     * into ErrorModel was derived under them, so they are held here and the
     * producer converts, exactly as it does for units.
     *
     * - roll: +ve port side up. This coincides with REP-103: a right-handed
     *   rotation about +x (forward) lifts +y (port). No conversion needed --
     *   verified twice.
     * - pitch: +ve BOW UP. This is the OPPOSITE of what tf2::getEulerYPR
     *   returns for an FLU rotation (pitch = -asin(R[2][0]), a right-handed
     *   rotation about +y/port, i.e. bow-down positive), so DetectionsProjector
     *   negates it at the boundary. An earlier revision of this comment
     *   asserted the two senses agreed; that was only half checked -- the roll
     *   half was right, the pitch half was not.
     * - heave: +ve DOWN, likewise opposite to the REP-103 +up TF translation
     *   the projector reads, and likewise negated there. Inert numerically
     *   (heave enters only squared) but kept consistent on purpose.
     *
     * Three ported terms are ODD in sin(pitch) and so are sensitive to the
     * pitch sign: swath_heave's IMU lever-arm term and the heading and pitch
     * cross-terms of the static horizontal_positioning_error. All three vanish
     * when the IMU/GPS offsets are zero, which is why a wrong sign stayed
     * latent at the defaults. */
    float roll;  /* Roll in radians, +ve is port side up */
    float pitch;  /* Pitch in radians, +ve is bow up (negated by the producer) */
    float heave;  /* Heave in meters, +ve down (negated by the producer) */
    float surf_sspeed;  /* Surface sound speed, m/s */
    float mean_speed;  /* Geometric mean equivalent sound speed, m/s */
    float vessel_speed;  /* Vessel's speed-over-ground, m/s */
  };


  struct Vessel
  {
  /* Direct offsets, usu. measured wrt transducer head */

  /// GPS offsets, m
    double gps_x = 0.0;
    double gps_y = 0.0;
    double gps_z = 0.0;

    double  gps_off_sdev = 0.005;  /* SDev of GPS offset measurements, m */
    double gps_latency_sdev = 0.03;  /* SDev of GPS latency error, s. */
    double  gps_drms = 2.0;  /* Circularly symmetric GPS position error, m. */
    double  gps_latency = 0.0;  /* Latency between GPS and logger, s. */

  /// IMU offsets, m
    double imu_x = 0.0;
    double imu_y = 0.0;
    double imu_z = 0.0;

    double  imu_off_sdev = 0.005;  /* SDev of IMU offset measurements, m */

  /* IMU alignment, deg. */
    double imu_r = 0.0;
    double imu_p = 0.0;
    double imu_g = 0.0;

    double  imu_rp_align_sdev = 0.05;  /* SDev of IMU roll/pitch alignment err., deg */
    double  imu_g_align_sdev = 0.10;  /* SDev of IMU heading alignment err, deg. */
    double  imu_latency_sdev = 0.005;  /* SDev of IMU latency error, s. */

    double draft = 0.0;  /* Draft of tx head below water line, m */
    double  tx_latency_sdev = 0.005;  /* SDev of tx head latency, s. */
    double  static_roll = 0.0;  /* Static head roll (mounting angle), deg. +ve is port side up*/


        /* Measurement accuracies */
    double  roll_sdev = 0.05;  /* SDev of roll measurement, deg. */
    double  pitch_sdev = 0.05;  /* SDev of pitch measurement, deg. */
    double  pitch_stab_sdev = 0.0;  /* SDev of pitch stabilisation, deg. */
    double  gyro_sdev = 0.50;  /* SDev of gyro measurement, deg. */
    double  svp_sdev = 0.52;  /* SDev of SVP cast measurements, m/s */
    double  surf_ss_sdev = 0.50;  /* SDev of sound speed surface meas., m/s */


    double  heave_fixed_sdev = 0.05;  /* SDev of heave measurements, m */
    double  heave_var_percent = 0.05;  /* Percentage of measurement variable error */
    double        draft_sdev = 0.02;  /* SDev of water level measurements, m */
    double  ddraft_sdev = 0.02;  /* SDev of dynamic draft measurements, m */
    double  loading_sdev = 0.02;  /* SDev of platform loading meas., m */
    double  sog_sdev = 0.02;  /* SDev of speed-over-ground meas., m/s */
    double  tide_measured_sdev = 0.02;  /* SDev of tide guage readings, m */
    double  tide_predicted_sdev = 0.02;  /* SDev of tide prediction error, m */

    /* When true (default), the grid is ellipsoid-referenced and no tidal datum
     * reduction is applied, so the tide_*_sdev terms are intentionally omitted
     * from the vertical error budget. Set false to enable tidal-datum mode (the
     * tide variances are then summed into vertical_reduction). See
     * docs/divergences_from_calder.md and #47. */
    bool ellipsoidal_referenced = true;
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
  // double total_roll_variance;

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
    const std::vector < float > * tx_beamwidths = nullptr;
  /// Sonar reported -3db RECEIVE beamwidths in radians. (This said "transmit"
  /// until #144 -- a copy-paste from the line above, on the very field whose
  /// units that issue was about.) May be empty when the driver reports none;
  /// values are validated before use, see ErrorModel::per_beam_beamwidth_usable.
    const std::vector < float > * rx_beamwidths = nullptr;
  /// Detection flags. 0 means good.
    const std::vector < uint8_t > * detection_flags = nullptr;
  /// travel times in seconds
    const std::vector < float > * two_way_travel_times = nullptr;
  /// Transmit steering angles in radians, positive is forwards
    const std::vector < float > * tx_angles = nullptr;
  /// Receive steering angles in radians, positive is starboard
    const std::vector < float > * rx_angles = nullptr;
  };

  class ErrorModel
  {
public:
    ErrorModel(const Vessel & vessel, const Device & device);

    std::vector < Sounding > compute(const marine_acoustic_msgs::msg::SonarDetections & detections,
      const Platform & platform) const;

  /// Largest per-beam receive beamwidth the model will accept, in radians.
  /// This is a HARD PHYSICAL bound, not a plausibility clamp: a beam cannot
  /// subtend half a turn or more, so anything at or above pi rad is nonsense
  /// (a unit mix-up, a sentinel, or a corrupt field) rather than a wide beam.
  ///
  /// It is deliberately not tighter. `garmin_sidescan` reports 55 degrees
  /// (0.96 rad) across-track for SideVu and 46 for ClearVu, and those are
  /// CORRECT -- a sidescan does no across-track beamforming, so its receive
  /// fan genuinely is that wide. Any clamp tight enough to be a "plausibility"
  /// check would throw that legitimate data away.
  ///
  /// The corollary is that this ceiling does NOT catch a misplaced transmit
  /// fan: an R2Sonic driver stamps the transmit horizontal fan (~2.27 rad,
  /// 130 degrees) into `rx_beamwidths`, which is a real, correctly-scaled
  /// measurement of the wrong quantity, and it passes this bound. That is a
  /// driver fault and is fixed there, not laundered here. See the divergences
  /// doc for the current tracking reference.
    static constexpr float kMaxPerBeamBeamwidthRad = 3.14159265358979323846f;

  /// True when a sonar-reported per-beam receive beamwidth (radians) is usable
  /// as a measurement: finite, strictly positive, and below the physical
  /// ceiling above. Public so a caller can count and report rejections without
  /// duplicating the predicate -- `DetectionsProjector` fills
  /// `ProjectionDiagnostics::default_beamwidth_beams` with it.
    static bool per_beam_beamwidth_usable(float beamwidth_rad);

private:
  /// Compute induced and measured heave components.
  /// Returns variance of total heave component of vertical error.
  /// This implements eqn. 3.57, 3.58, and 3.59.
    double swath_heave(
      const Platform & platform,
      const PerPingErrorSources & per_ping_sources) const;

  /// Compute variance of horizontal positioning error caused by
  /// GPS antennae not being at the transducer head.
  /// Returns a variance in m^2 at one sigma: no confidence-interval scaling is
  /// applied here (see #144). Calder's own header claims a doubling to reach a
  /// 95% interval, but neither his implementation nor this one applies it --
  /// the confidence scaling lives at reporting time (CONF_95PC), not in the
  /// error budget.
  /// This computes eqn. 3.90, summarizing the component of horizontal
  /// error due to misalignment of the GPS antennae and the tx head.
    double horizontal_positioning_error(
      const Platform & platform,
      const PerPingErrorSources & per_ping_sources) const;

  /// Compute the horizontal error component due to latency errors.
  /// Returns a variance in m^2 at one sigma: no confidence-interval scaling is
  /// applied here (see #144). This header used to claim an "approximate 95%
  /// error bound" -- the third copy of a claim the implementation has never
  /// matched, alongside the two on horizontal_positioning_error. The 95%
  /// scaling lives at reporting time (CONF_95PC), not in the error budget.
  /// We assume that the coefficients for eqn 3.100 have been pre-computed
  /// and stored in the workspace, and that the trig. functions for the
  /// current swath orientation have been computed.
    double horizontal_latency(
      const Platform & platform,
      const PerPingErrorSources & per_ping_sources) const;

    double beam_angle(
      const marine_acoustic_msgs::msg::SonarDetections & detections,
      size_t i) const;

  /// Compute vertical roll/pointing angle error
  /// This computes Eqns. 3.43 and its parents.
    double swath_angle_error(
      const Platform & platform,
      const PerPingErrorSources & per_ping_sources,
      const marine_acoustic_msgs::msg::SonarDetections & detections, size_t i
    ) const;

  /// Compute vertical swath error budget
  /// Returns variance estimate of total reduced depth error
  /// Comment: This implements eqn. 3.65, 3.63, and 3.61, and uses sub-routines
  /// to compute the other components (heave and measured depth). Result
  /// is the total variance associated with the reduced depth. Note that
  /// for speed, eqn. 3.63 and 3.61 (water level reduction and dynamic
  /// draft variance) are pre-computed and stored in the workspace.
    double swath_vertical(
      const Platform & platform,
      const PerPingErrorSources & per_ping_sources,
      const marine_acoustic_msgs::msg::SonarDetections & detections, size_t i
    ) const;

    double range_error(double depth) const;

  /// Compute measured depth error component
  /// Returns variance of total measured depth component of vert. error.
  /// This implements eqn. 3.52, and the various components which
  /// are required for it.
  /// Returns depth and error.
    std::pair < double, double > swath_depth(
    const Platform & platform,
    const PerPingErrorSources & per_ping_sources,
    const marine_acoustic_msgs::msg::SonarDetections & detections, size_t i
    ) const;

  /// Compute horizontal swath error budget.
  /// Returns variance estimate for total horizontal error budget.
  /// This implements equations 3.100, 3.90, 3.82 and 3.69 for the
  /// components of the error budget, combining them with 3.70.
    double swath_horizontal(
      const Platform & platform,
      const PerPingErrorSources & per_ping_sources,
      const marine_acoustic_msgs::msg::SonarDetections & detections,
      size_t i,
      const Sounding & sounding
    ) const;

  /// Compute component of horizontal positioning error associated with
  /// ship attitude and offsets.
  /// Returns a variance in m^2 at one sigma: no confidence-interval scaling is
  /// applied here (see #144). The stale claim that the estimate is multiplied
  /// by 2.0 to reach a 95% interval never matched the implementation, here or
  /// in Calder's.
  /// We are computing eqns. 3.77-3.82. Note that the spreadsheet
  /// does not include any component for the along-track beam angle,
  /// unlike the report, and we ignore it here also.
    double horizontal_positioning_error(
      const Platform & platform,
      const PerPingErrorSources & per_ping_sources,
      const marine_acoustic_msgs::msg::SonarDetections & detections, size_t i,
      const Sounding & sounding
    ) const;

    Vessel vessel_;
    StaticErrorSources static_error_sources_;
    Device device_;

  /// `Device::across_track_beamwidth` converted from degrees to radians once,
  /// in the constructor. `Device` itself stays degrees-valued (sonar datasheets
  /// and every other angular field on `Device` and `Vessel` are in degrees);
  /// this is the single boundary conversion, so use sites consume radians
  /// without converting again. `Platform` is the deliberate exception -- it
  /// holds radians outright (#147), being a measurement rather than
  /// human-entered configuration. See #144 and docs/divergences_from_calder.md.
    double device_across_track_beamwidth_rad_ = 0.0;
  };


}  // namespace cube

#endif  // CUBE_BATHYMETRY__ERROR_MODEL_H_
