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


#ifndef CUBE_BATHYMETRY__HYPOTHESIS_H_
#define CUBE_BATHYMETRY__HYPOTHESIS_H_

#include <cstdint>
#include <memory>
#include <vector>
#include "cube_bathymetry/parameters.h"

namespace cube
{

/// Per-beam backscatter sufficient statistics retained on a hypothesis
/// (ADR-0007 D3). Each contributing beam contributes its RAW (uncorrected)
/// intensity and the per-beam receive/steering (beam/incidence) angle, so the
/// radiometric (empirical-ARA / GeoCoder) correction can be applied later -- at
/// node-output, once depth and local slope have settled -- rather than baking
/// an angle-corrupted, non-re-correctable average into the hypothesis.
  struct BeamIntensitySample
  {
  /// Raw, uncorrected per-beam intensity (e.g. M3 reflectivity in dB).
    float raw_intensity;

  /// Per-beam receive/steering (beam/incidence) angle from nadir, in radians
  /// (NOT a true grazing angle of 90 deg - theta; nadir = 0). May be NaN when
  /// the source did not report an angle for the beam; the output correction
  /// treats a NaN angle as "no angle correction available" and emits that beam
  /// uncorrected.
    float beam_angle;
  };

/// Depth hypothesis structure used to maintain a current track on the depth
/// at the node in question.  This contains current estimates of depth and
/// variance, one-step predictions and the monitoring variables.  The number
/// of elements incorporated in the node is also recorded so that a pseudo-MAP
/// estimate of best model (or at least most frequently visited model) can be
/// computed.
  struct Hypothesis
  {
    Hypothesis(float initial_mean, float initial_variance);

    static std::shared_ptr < Hypothesis > generateNullHypothesis(float depth, float variance);

  /// Reset monitoring structure to defaults
    void resetMonitor();

  /// Compute West & Harrison's monitoring statistics for the node
  /// This code depends on the parameters:
  ///   est_offset:   Offset considered to be significant
  ///   bayes_fac_t:  Bayes Factor threshold before intervention
  ///   runlength_t:  Number of bad factors to indicate sequence
  ///     failure
  /// which must be set up by the user externally of the code.  The
  /// est_offset is W&H's `h' parameter (i.e., expected normalised
  /// difference between the current forecast and the observation which
  /// just indicates an outlier), bayes_fac_t is W&H's `tau' (i.e.,
  /// the minimum Bayes factor which is acceptable as evidence for the
  /// current model), and runlength_t is W&H's limit on l_t (i.e.,
  /// the number of consequtively bad Bayes factors which indicate that
  /// there has been a gradual shift away from the predictor).
  /// Inputs:
  ///   depth:    New input sample which is about the be incorporated
  ///   variance: Observation noise variance
  /// Outputs:
  ///    true if an intervention is indicated, otherwise false
    bool monitor(float depth, float variance, const Parameters & parameters);

  /// Update a particular hypothesis being tracked at a node
  /// This implements the standard univariate dynamic linear model update
  /// equations (West & Harrison, 'Bayesian Forecasting and Dynamic
  /// Models', Springer, 2ed, 1997, Ch. 2), along with the Bayes factor
  /// monitoring code (W&H, Ch. 11).  The only failure mode possible with
  /// this code is if the input data would cause an intervention to be
  /// requested on the current track.  In this case, it is the caller's
  /// responsibility to utilise the data point, since it will not be
  /// incorporated into the hypothesis --- typically this would mean
  /// adding a new hypothesis and pushing it onto the stack.
  ///
  /// Inputs:
  ///   hypothesis: Hypothesis to be updated
  ///   depth: Estimate of beam depth
  ///   variance: Estimate of beam depth variance
  ///   *p      Parameter structure for algorithm
  /// Outputs:
  ///   Returns False if the estimate does not really match the track that
  ///   the hypothesis represents (i.e., an intervention is required).
    bool update(float depth, float variance, const Parameters & parameters);

  /// Record one beam's backscatter sufficient statistics on this hypothesis
  /// (ADR-0007 D3). Called by Node::update() only for a beam whose depth was
  /// accepted by (or used to seed) THIS hypothesis, so the backscatter
  /// association mirrors the depth association exactly -- depth-geometry
  /// outliers the W&H monitor rejects never enter this set.
  ///
  /// A NaN raw_intensity is skipped (a source that omits intensities must never
  /// inject a phantom sample); a NaN beam_angle is retained (the beam is
  /// still a valid intensity sample, merely uncorrectable for angle).
    void recordBeam(float raw_intensity, float beam_angle);

  /// Current depth mean estimate
    double current_estimate;

  /// Current depth variance estimate
    double current_variance;

  /// Current depth next-state mean prediction
    double predicted_estimate;

  /// Current depth next-state variance pred.
    double predicted_variance;

  /// Cumulative Bayes factor for node monitoring
    double cumulative_bayes_factor = 1.0;

  /// Worst-case sequence length for monitoring
    uint16_t sequence_length = 0;

  /// Index term for debugging
    uint16_t hypothesis_number = 0;

  /// Number of samples incorporated into this node
    uint32_t number_of_samples = 0;

  /// In normal operation, the algorithm does not pay any attention to the input
  /// sample variance, reporting only the post. est. var. for the chosen depth
  /// hypothesis.  Under some conditions this doesn't make sense, and we might
  /// want to report the sample variance of the samples used to make the
  /// hypothesis. This tracks the input sample variance.
    float input_sample_variance = 0.0;

  /// In normal operation, the algorithm does not pay any attention to the input
  /// sample variance, reporting only the post. est. var. for the chosen depth
  /// hypothesis.  Under some conditions this doesn't make sense, and we might
  /// want to report the maximum of
  /// the input sample variance and the predicted post. est. var.
  /// This tracks the maximum of the two estimates.
    float maximum_of_input_and_predicted_variance = 0.0;

  /// Per-beam backscatter sufficient statistics for the beams associated with
  /// this hypothesis (ADR-0007 D3). Bounded by hypothesis membership -- one
  /// entry per contributing beam with a non-NaN intensity, a few bytes each.
  /// The radiometric correction (D3 GeoCoder) is applied per element at
  /// node-output (Node::extractNodeRecord); these raw pairs are retained so the
  /// node value stays re-derivable when slope (cube_bathymetry#15) lands.
  ///
  /// Memory budget: sizeof(BeamIntensitySample) == 8 bytes (two floats). Growth
  /// class is identical to number_of_samples (one entry per accepted beam, for
  /// the survey lifetime of the hypothesis). Worst-case is ~8 bytes/beam/node.
  /// Once cube_bathymetry#15's correction model settles this can be reduced to
  /// pure sufficient statistics (mean, M2, count) if the per-beam retention is
  /// no longer needed for re-derivation.
    std::vector < BeamIntensitySample > intensity_samples;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__HYPOTHESIS_H_
