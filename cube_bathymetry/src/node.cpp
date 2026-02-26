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


#include "cube_bathymetry/node.h"
#include <cmath>

namespace cube
{

bool Node::addHypothesis(float depth, float variance)
{
  auto new_hypothesis = std::make_shared<Hypothesis>(depth, variance);
  new_hypothesis->hypothesis_number = depth_hypotheses_.size();
  depth_hypotheses_.push_back(new_hypothesis);
  return true;
}


bool Node::update(float depth, float variance, const Parameters& parameters)
{
  /* Find the best matching hypothesis for the current input sample given
   * those currently being tracked.
   */
  auto best = bestHypothesis(depth, variance);

  if(!best)
  {
    /* Didn't match a `best' hypothesis ... this only happens when there
     * are *no* hypotheses (i.e., intialisation mode).  So we add a new
     * hypothesis directly.
     */
    addHypothesis(depth, variance);
    return true;
  }
  else
  {
    /* Update the best hypothesis with the current data */
    if(!best->update(depth, variance, parameters))
    {
      /* Failed update --- indicates an intervention, so that we need to
			 * start a new hypothesis to capture the outlier/datum shift.
			 */
      best->resetMonitor();
      addHypothesis(depth, variance);
    }

  }
  return true;
}

std::shared_ptr<Hypothesis> Node::bestHypothesis(float depth, float variance)
{
  std::shared_ptr<Hypothesis> ret;
  double min_error = std::numeric_limits<float>::max();

  for(const auto& h: depth_hypotheses_)
  {
    double forecast_variance = h->predicted_variance + variance;
    double error = std::abs(depth - h->predicted_estimate)/std::sqrt(forecast_variance);
    if(error < min_error)
    {
      min_error = error;
      ret = h;
    }
  }
  return ret;
}

bool Node::insert(double distance, const Sounding& sounding, const Parameters& parameters)
{
  if(std::isnan(predicted_depth_))
    return true;

  /* Euclidean distance in projected space, i.e., distance sounding is being
   * propagated from touchdown boresight to node estimation point.
   */
  // distance

  float target_depth;
  if(predicted_depth_ != INVALID_DATA)
  {
    target_depth = predicted_depth_;

    double blunder_limit = std::min(target_depth - parameters.blunder_minimum, target_depth - parameters.blunder_percent*std::abs(target_depth));
    blunder_limit = std::min(blunder_limit, target_depth - parameters.blunder_scalar*sqrt(predicted_depth_variance_));
		if (sounding.depth < blunder_limit)
      return false;
  }
  else
  {
    target_depth = sounding.depth;
  }

  if(distance > std::max<double>(parameters.capture_distance_scale*std::abs(target_depth), 0.5))
    return false;
  
  distance += CONF_95PC * std::sqrt(sounding.horizontal_error);

  float offset = 0.0;
  double variance = sounding.vertical_error*(1.0 + parameters.variance_scale*pow(distance, parameters.distance_exponent));


  // if (sounding.range != 0.0 && predicted_depth_  != parameters.no_data_value)
  // {
  //   offset = predicted_depth_ - sounding.range;
  //   /* Refered variance is boresight variance multiplied by dilution factor,
  //    * a function of distance and a user scale parameter.
  //    */
  // }

  /* Adding data removes any nomination in effect */
  nominated_hypothesis_.reset();

  return queueEstimate(sounding.depth+offset, variance, parameters);

}

bool Node::queueEstimate(float depth, float variance, const Parameters & parameters)
{
  if(queue_.size() >= parameters.median_length)
  {
    auto mi = queue_.begin();
    advance(mi, parameters.median_length/2);
    update(mi->depth, mi->uncertainty, parameters);
    queue_.erase(mi);
  }

  auto i = queue_.begin();
  while(i != queue_.end() && i->depth > depth)
    i++;
  queue_.insert(i, DepthAndUncertainty(depth, variance));

  if(queue_.size() >= parameters.median_length)
  {

    /* Compute the likely 99% confidence bound below the shallowest point, and
    * above the deepest point in the buffer, and check that they do actually
    * overlap somewhere in the middle.  Otherwise, with less than 1% chance of
    * error, we are suspicious that there are outliers in the buffer somewhere,
    * and we should attempt a round of outlier rejection.  Assuming that the
    * errors are approximately normal, 0.5% in either tail is achieved at
    * 2.5758 std. dev. from the mean.
    */
    auto lo_water = queue_.front().depth - CONF_99PC * sqrt(queue_.front().uncertainty);
    auto hi_water = queue_.back().depth + CONF_99PC * sqrt(queue_.back().uncertainty);

    if(lo_water >= hi_water)
      truncate(parameters);
  }

  return true;
}

DepthAndUncertainty Node::extractDepthAndUncertainty(const Parameters & parameters)
{
  if(nominated_hypothesis_)
    return {nominated_hypothesis_->current_estimate, parameters.stddev_to_confidence_interval_scale*std::sqrt(nominated_hypothesis_->input_sample_variance)};

  auto h = chooseHypothesis();

  if(h)
  {
    if(h->number_of_samples > 0)
    {
      float depth = h->current_estimate;
      float uncertainty = parameters.stddev_to_confidence_interval_scale*std::sqrt(h->input_sample_variance);
      return {depth, uncertainty};
    }
  }
  
  return {};
}

std::shared_ptr<Hypothesis> Node::chooseHypothesis()
{
  std::shared_ptr<Hypothesis> ret;
  int max_sample_count = 0;
  for(auto h: depth_hypotheses_)
    if(h->number_of_samples > max_sample_count)
    {
      ret = h;
      max_sample_count = h->number_of_samples;
    }
  return ret;
}

void Node::truncate(const Parameters & parameters)
{
  if(queue_.size() < 3)
    return;

  float mean = 0.0;
  float ssd = 0.0;
  auto n = queue_.size()-1;

  /* First compute mean and overall sum of squared differences (SSD) */
  for(auto &p: queue_)
  {
    mean += p.depth;
    ssd += p.depth*p.depth;
  }
  ssd -= mean*mean/(n+1);
  mean /= (n+1);
  float ssd_k = n*ssd/(n*n-1);

  /* Run the list computing quotients; outliers are removed from the queue.
   */
  for(auto i = queue_.begin(); i != queue_.end();)
  {
    auto diff_sq = (i->depth - mean)*(i->depth - mean);
    auto q = diff_sq/(ssd_k - diff_sq/(n-1));
    if(q > parameters.quotient_limit)
      i = queue_.erase(i);
    else
      ++i;
  }

}


void Node::queueFlush(const Parameters & parameters)
{
  if(queue_.empty())
    return;

  truncate(parameters);

  // Copy to vector for indexed access (original uses array indices)
  std::vector<DepthAndUncertainty> q(queue_.begin(), queue_.end());
  queue_.clear();

  int n = q.size();
  int ex_pt, direction, scale = 1;

  if ((n % 2) == 0) {
    // Even: start just left of center, go right first
    ex_pt = n / 2 - 1;
    direction = +1;
  } else {
    // Odd: start at center, go left first
    ex_pt = n / 2;
    direction = -1;
  }

  while (ex_pt >= 0) {
    update(q[ex_pt].depth, q[ex_pt].uncertainty, parameters);
    ex_pt += direction * scale;
    direction = -direction;
    scale++;
  }

}


} // namespace cube
