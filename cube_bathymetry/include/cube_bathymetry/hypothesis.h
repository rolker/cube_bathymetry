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
#include <limits>
#include <memory>
#include <vector>
#include "cube_bathymetry/parameters.h"

namespace cube
{

/// @brief Streaming Welford accumulator of the CORRECTED per-beam backscatter on
///        a hypothesis (ADR-0007 D3/D4, cube_bathymetry#93).
///
/// Replaces the unbounded `std::vector<BeamIntensitySample>` raw-sample retention:
/// the angular-response + tier-2 TL correction is now applied at RECORD time
/// (`correctBeamIntensity`), and only the running `(n, mean, M2)` of the corrected
/// dB value is kept -- O(1) per cell regardless of beam count, the fix for the
/// Massabesic OOM (per-cell intensity memory used to grow with total beams).
///
/// `mean` and `m2` (sum of squared deviations from the mean) are `double` for
/// numerical stability over the ~10^9 beams a multi-day survey accumulates.
/// Node-output reads `intensity = mean` and the estimate variance
/// `intensity_var = (m2/(n-1)) / n` (variance of the mean, ADR-0007 D4) -- the
/// SAME values the retain-and-correct-at-extract path produced.
  struct IntensityWelford
  {
  /// Number of corrected intensity samples folded in (intensity-bearing beams).
    uint32_t n = 0;
  /// Running mean of the corrected intensity (dB).
    double mean = 0.0;
  /// Running sum of squared deviations from the mean (Welford M2); m2/(n-1) is
  /// the unbiased sample variance.
    double m2 = 0.0;
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

  /// Record one beam's backscatter on this hypothesis (ADR-0007 D3/D4,
  /// cube_bathymetry#93). Called by Node::update() only for a beam whose depth was
  /// accepted by (or used to seed) THIS hypothesis, so the backscatter association
  /// mirrors the depth association exactly -- depth-geometry outliers the W&H
  /// monitor rejects never enter the accumulator.
  ///
  /// Applies the angular-response + tier-2 TL correction (`correctBeamIntensity`,
  /// using @p parameters) at RECORD time, then folds the corrected value into the
  /// streaming Welford `intensity` (no raw-sample retention -- O(1) per cell).
  ///
  /// A NaN @p raw_intensity is skipped (a source that omits intensities must never
  /// inject a phantom sample); a NaN @p beam_angle is still counted (a valid
  /// intensity sample, merely uncorrected for angle); a NaN / non-positive
  /// @p range still counted (the tier-2 TL term is skipped for it, cube#87).
    void recordBeam(
      float raw_intensity, float beam_angle, float range,
      const Parameters & parameters);

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

  /// Streaming Welford of the CORRECTED backscatter for the beams associated with
  /// this hypothesis (ADR-0007 D3/D4, cube_bathymetry#93). O(1) -- 16 bytes,
  /// independent of beam count (the cube#93 OOM fix). The correction is applied at
  /// record (`recordBeam` -> `correctBeamIntensity`), so this is already the
  /// node-output value; `Node::extractNodeRecord` reads it without re-correcting.
  /// The tile-eviction spill/reload persists this triplet (a perfect sufficient
  /// statistic, so reload + continue is bit-identical to never-evicting, #92).
    IntensityWelford intensity;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__HYPOTHESIS_H_
