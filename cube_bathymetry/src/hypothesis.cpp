#include "cube_bathymetry/hypothesis.h"

#include <cmath>

namespace cube
{

Hypothesis::Hypothesis(float  initial_mean, float initial_variance)
  :current_estimate(initial_mean), current_variance(initial_variance),
  predicted_estimate(initial_mean), predicted_variance(initial_variance),
  number_of_samples(1)
{

}

std::shared_ptr<Hypothesis> Hypothesis::generateNullHypothesis(float depth, float variance)
{
  auto h = std::make_shared<Hypothesis>(depth, variance);
  h->number_of_samples = 0;
  return h;
}

void Hypothesis::resetMonitor()
{
  cumulative_bayes_factor = 1.0;
  sequence_length = 0;
}

bool Hypothesis::monitor(float depth, float variance, const Parameters& parameters)
{
  double forecast_variance = predicted_variance + variance;
  double error = std::abs(depth - predicted_estimate)/std::sqrt(forecast_variance);
  double bayes_factor;
  if (error >= 0.0)
    bayes_factor = exp(0.5*(parameters.estimate_offset*parameters.estimate_offset - 2.0*parameters.estimate_offset*error));
  else
    bayes_factor = exp(0.5*(parameters.estimate_offset*parameters.estimate_offset + 2.0*parameters.estimate_offset*error));
  
  /* Check for single component failure */
  if (bayes_factor < parameters.bayes_factor_threshold)
  {
    /* This is a potential outlier.  Indicate potential failure. */
    return false;
  }

  /* Update monitors */
  if (cumulative_bayes_factor < 1.0)
    ++sequence_length;
  else
    sequence_length = 1;
  cumulative_bayes_factor = bayes_factor * std::min(1.0, cumulative_bayes_factor);

  /* Check for consequtive failure errors */
  if (cumulative_bayes_factor < parameters.bayes_factor_threshold || sequence_length > parameters.runlength_threshold)
  {
    /* Indicate intervention required */
    return false;
  }

  /* Otherwise, the current model is still supported ... */
  return true;
}

bool Hypothesis::update(float depth, float variance, const Parameters& parameters)
{
  /* Check current estimate with monitoring */
  if (!monitor(depth, variance, parameters))
    return false;

  /* Otherwise, update current hypothesis with new information */

  input_sample_variance = ((number_of_samples + 1 - 2)*input_sample_variance/(number_of_samples + 1 - 1) + (depth - current_estimate)*(depth - current_estimate)/double(number_of_samples));

  double system_variance = current_variance * (1.0-parameters.discount)/parameters.discount;

  double gain = predicted_variance/(variance + predicted_variance);
  double innovation = depth - predicted_estimate;
  predicted_estimate += gain*innovation;
  current_estimate = predicted_estimate;
  current_variance = variance*predicted_variance/(variance+predicted_variance);
  predicted_variance = current_variance + system_variance;

  ++number_of_samples;

  return true;
}


} // namespace cube
