#ifndef CUBE_BATHYMETRY_HYPOTHESIS_H
#define CUBE_BATHYMETRY_HYPOTHESIS_H

#include <cstdint>
#include <memory>
#include "parameters.h"

namespace cube
{

/// Depth hypothesis structure used to maintain a current track on the depth
/// at the node in question.  This contains current estimates of depth and
/// variance, one-step predictions and the monitoring variables.  The number
/// of elements incorporated in the node is also recorded so that a pseudo-MAP
/// estimate of best model (or at least most frequently visited model) can be
/// computed.
struct Hypothesis
{
  Hypothesis(float initial_mean, float initial_variance);

  static std::shared_ptr<Hypothesis> generateNullHypothesis(float depth, float variance);

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
  bool monitor(float depth, float variance, const Parameters& parameters);

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
  ///   *p			Parameter structure for algorithm
  /// Outputs:
  ///   Returns False if the estimate does not really match the track that
  ///   the hypothesis represents (i.e., an intervention is required).
  bool update(float depth, float variance, const Parameters& parameters);

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
  float input_sample_variance = 0.0;;

  /// In normal operation, the algorithm does not pay any attention to the input
  /// sample variance, reporting only the post. est. var. for the chosen depth
  /// hypothesis.  Under some conditions this doesn't make sense, and we might
  /// want to report the maximum of
  /// the input sample variance and the predicted post. est. var.
  /// This tracks the maximum of the two estimates.
  float maximum_of_input_and_predicted_variance = 0.0;
};

} // namespace cube

#endif
