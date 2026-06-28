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


#include "cube_bathymetry/node.h"
#include <cassert>
#include <cmath>
#include <utility>
#include <vector>

namespace cube
{

namespace
{

/// Empirical angular-response lookup: the curve's db_relative_to_nadir at
/// @p abs_angle_deg, linearly interpolated between adjacent bin centres. The
/// curve is ascending {abs_angle_deg, db_relative_to_nadir} pairs (loaded by
/// loadAngularResponseCurve). Returns 0 (identity) when the curve is empty or
/// @p abs_angle_deg lies beyond the curve's max angle; clamps to the first bin
/// below the curve's min angle (nadir bin ~0 -> ~identity near nadir).
double curveRelativeDb(
  const std::vector<std::pair<float, float>> & curve, double abs_angle_deg)
{
  if (curve.empty()) {
    return 0.0;
  }
  // Below the first bin centre: clamp to it (the nadir bin's value, ~0).
  if (abs_angle_deg <= curve.front().first) {
    return curve.front().second;
  }
  // Beyond the last bin centre: clamp to the outermost bin's correction rather
  // than jump to identity. The empirical curve is bounded (unlike a cos/log model
  // that diverges near grazing), so continuing the edge value keeps the correction
  // continuous and avoids a swath-edge discontinuity / bright ring (#81 review).
  if (abs_angle_deg > curve.back().first) {
    return curve.back().second;
  }
  // Find the bracketing pair [lo, hi] and linearly interpolate.
  for (std::size_t i = 1; i < curve.size(); ++i) {
    if (abs_angle_deg <= curve[i].first) {
      const double a0 = curve[i - 1].first;
      const double d0 = curve[i - 1].second;
      const double a1 = curve[i].first;
      const double d1 = curve[i].second;
      const double span = a1 - a0;
      if (span <= 0.0) {
        return d1;  // duplicate angle: take the upper bin's value
      }
      const double t = (abs_angle_deg - a0) / span;
      return d0 + t * (d1 - d0);
    }
  }
  return 0.0;  // unreachable (guarded by the back() check above)
}

}  // namespace

bool Node::addHypothesis(float depth, float variance)
{
  auto new_hypothesis = std::make_shared<Hypothesis>(depth, variance);
  new_hypothesis->hypothesis_number = depth_hypotheses_.size();
  depth_hypotheses_.push_back(new_hypothesis);
  return true;
}

void Node::seedSettledDepth(
  float depth, float uncertainty, const Parameters & parameters)
{
  // Recover the DLM variance from the stored 1-sigma confidence interval:
  // extractDepthAndUncertainty() reports scale * sqrt(input_sample_variance),
  // so input_sample_variance = (uncertainty / scale)^2 makes the reload
  // round-trip exactly (ADR-0001). A single-sample CUBE cell can persist a
  // zero / non-finite uncertainty; floor the variance at a small positive
  // epsilon (1 cm 1-sigma) so the West-Harrison update stays well-defined.
  constexpr float kSeedVarianceFloor = 1e-4F;  // m^2 (== 1 cm 1-sigma)
  const float scale = parameters.stddev_to_confidence_interval_scale;
  float variance = kSeedVarianceFloor;
  if (std::isfinite(uncertainty) && uncertainty > 0.0F && scale > 0.0F) {
    const float sigma = uncertainty / scale;
    const float v = sigma * sigma;
    if (v > variance) {
      variance = v;
    }
  }

  // The Hypothesis ctor sets current/predicted_estimate = depth,
  // current/predicted_variance = variance, number_of_samples = 1. It does NOT
  // set input_sample_variance (which drives the extracted uncertainty), so set
  // it here to the same recovered variance for an exact uncertainty round-trip.
  auto hypothesis = std::make_shared<Hypothesis>(depth, variance);
  hypothesis->hypothesis_number = depth_hypotheses_.size();
  hypothesis->input_sample_variance = variance;
  depth_hypotheses_.push_back(hypothesis);
}


bool Node::update(
  float depth, float variance, const Parameters & parameters,
  float intensity, float beam_angle, float range)
{
  /* Find the best matching hypothesis for the current input sample given
   * those currently being tracked.
   */
  auto best = bestHypothesis(depth, variance);

  if(!best) {
    /* Didn't match a `best' hypothesis ... this only happens when there
     * are *no* hypotheses (i.e., intialisation mode).  So we add a new
     * hypothesis directly.
     */
    addHypothesis(depth, variance);
    // This beam seeded (and so is a member of) the new hypothesis: record its
    // backscatter on it. Without this the first beam at every node is dropped.
    depth_hypotheses_.back()->recordBeam(intensity, beam_angle, range);
    return true;
  } else {
    /* Update the best hypothesis with the current data */
    if(best->update(depth, variance, parameters)) {
      // Accepted into the winning hypothesis -- accumulate its backscatter on
      // the SAME hypothesis (ADR-0007 D2: intensity rides the winning depth).
      best->recordBeam(intensity, beam_angle, range);
    } else {
      /* Failed update --- indicates an intervention, so that we need to
                         * start a new hypothesis to capture the outlier/datum shift.
                         */
      best->resetMonitor();
      addHypothesis(depth, variance);
      // The beam was REJECTED from `best` (depth-geometry outlier) and used to
      // seed the new hypothesis, so its backscatter belongs to the new
      // hypothesis, NOT to `best` (exclusion-on-intervention invariant).
      depth_hypotheses_.back()->recordBeam(intensity, beam_angle, range);
    }
  }
  return true;
}

std::shared_ptr<Hypothesis> Node::bestHypothesis(float depth, float variance)
{
  std::shared_ptr<Hypothesis> ret;
  double min_error = std::numeric_limits<float>::max();

  for (const auto & h  :  depth_hypotheses_) {
    double forecast_variance = h->predicted_variance + variance;
    double error = std::abs(depth - h->predicted_estimate) / std::sqrt(forecast_variance);
    if(error < min_error) {
      min_error = error;
      ret = h;
    }
  }
  return ret;
}

bool Node::insert(double distance, const Sounding & sounding, const Parameters & parameters)
{
  if(std::isnan(predicted_depth_)) {
    return true;
  }

  /* Euclidean distance in projected space, i.e., distance sounding is being
   * propagated from touchdown boresight to node estimation point.
   */
  // distance

  float target_depth;
  if(predicted_depth_ != INVALID_DATA) {
    target_depth = predicted_depth_;

    double blunder_limit = std::min(target_depth - parameters.blunder_minimum,
        target_depth - parameters.blunder_percent * std::abs(target_depth));
    blunder_limit = std::min(blunder_limit,
        target_depth - parameters.blunder_scalar * sqrt(predicted_depth_variance_));
    if (sounding.depth < blunder_limit) {
      return false;
    }
  } else {
    target_depth = sounding.depth;
  }

  if(distance > std::max<double>(parameters.capture_distance_scale * std::abs(target_depth), 0.5)) {
    return false;
  }

  // Defensive: a non-finite horizontal_error (e.g. an upstream TPU gap) must not
  // poison `distance` -> the depth `variance` below -> the whole estimate -> an
  // empty epoch (cube_bathymetry#63). Skip the horizontal term if it isn't finite
  // rather than NaN-propagating into the depth solution.
  if (std::isfinite(sounding.horizontal_error)) {
    distance += CONF_95PC * std::sqrt(sounding.horizontal_error);
  }

  float offset = 0.0;
  double variance = sounding.vertical_error * (1.0 + parameters.variance_scale * pow(distance,
      parameters.distance_exponent));


  /* Slope correction. Projects a sounding that touched down off this node onto
   * the node along the predicted seabed surface (not along the acoustic beam):
   *
   *   offset = predicted_depth_(node) - predicted_depth_at_touchdown
   *   queued = sounding.depth + offset
   *
   * Both terms are negative-down predicted-surface depths in the same
   * convention, so the offset is purely the predicted slope between the node
   * and the sounding's touchdown point. This is the original CUBE
   * `offset = node->pred_depth - snd->range` (`cube_node.c:1846`) once you
   * account for `snd->range` being OVERWRITTEN at `mapsheet_cube.c:2434` with
   * `cube_grid_interpolate(... de, dn ...)` (the bilinear blend of the four
   * surrounding nodes' pred_depth at the touchdown). There is no 1/cos^2
   * obliquity factor: the error-model slant range `depth/cos(angle)`
   * (`sounding.c:1268`) is a different quantity that is overwritten before the
   * offset runs and is irrelevant here.
   *
   * Disabled at port time because the port had no `range`/predicted-surface
   * field to drive it. Re-enabled here using the new
   * `Sounding::predicted_depth_at_touchdown` field; the guard reproduces the
   * original's `range != 0.0 && pred_depth != no_data_value` exactly, with both
   * sentinels expressed as `INVALID_DATA` (the live blunder check above uses
   * `INVALID_DATA`; the original commented-out guard referenced
   * `parameters.no_data_value`, which is `quiet_NaN()` here — doubly wrong, so
   * it is deliberately corrected). With no producer wired yet (the
   * external-prior load + touchdown-interpolation subsystem is a deferred
   * follow-on), `predicted_depth_at_touchdown` stays at its `INVALID_DATA`
   * sentinel => offset 0 => the defined, safe, correct-but-uncorrected behaviour
   * the grids have today.
   */
  if (sounding.predicted_depth_at_touchdown != INVALID_DATA &&
    predicted_depth_ != INVALID_DATA)
  {
    offset = predicted_depth_ - sounding.predicted_depth_at_touchdown;
  }

  /* Adding data removes any nomination in effect */
  nominated_hypothesis_.reset();

  return queueEstimate(sounding.depth + offset, variance, parameters,
      sounding.intensity, sounding.beam_angle, sounding.slant_range);
}

bool Node::queueEstimate(
  float depth, float variance, const Parameters & parameters,
  float intensity, float beam_angle, float range)
{
  if(queue_.size() >= parameters.median_length) {
    auto mi = queue_.begin();
    advance(mi, parameters.median_length / 2);
    update(mi->depth, mi->uncertainty, parameters, mi->intensity, mi->beam_angle,
      mi->range);
    queue_.erase(mi);
  }

  auto i = queue_.begin();
  while(i != queue_.end() && i->depth > depth) {
    i++;
  }
  queue_.insert(i, DepthAndUncertainty(depth, variance, intensity, beam_angle, range));

  if(queue_.size() >= parameters.median_length) {
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

    if(lo_water >= hi_water) {
      truncate(parameters);
    }
  }

  return true;
}

DepthAndUncertainty Node::extractDepthAndUncertainty(const Parameters & parameters)
{
  if(nominated_hypothesis_) {
    return {nominated_hypothesis_->current_estimate,
      parameters.stddev_to_confidence_interval_scale *
      std::sqrt(nominated_hypothesis_->input_sample_variance)};
  }

  auto h = chooseHypothesis();

  if(h) {
    if(h->number_of_samples > 0) {
      float depth = h->current_estimate;
      float uncertainty = parameters.stddev_to_confidence_interval_scale *
        std::sqrt(h->input_sample_variance);
      return {depth, uncertainty};
    }
  }

  return {};
}

NodeRecord Node::extractNodeRecord(const Parameters & parameters)
{
  // Depth half mirrors extractDepthAndUncertainty() EXACTLY, including the
  // nominated_hypothesis_ priority path, so the enriched record never disagrees
  // with the depth-only output for the same node state (ADR-0007 D5).
  std::shared_ptr<Hypothesis> chosen;
  NodeRecord record;

  if(nominated_hypothesis_) {
    chosen = nominated_hypothesis_;
    record.depth = nominated_hypothesis_->current_estimate;
    record.depth_var = parameters.stddev_to_confidence_interval_scale *
      std::sqrt(nominated_hypothesis_->input_sample_variance);
  } else {
    auto h = chooseHypothesis();
    if(h && h->number_of_samples > 0) {
      chosen = h;
      record.depth = h->current_estimate;
      record.depth_var = parameters.stddev_to_confidence_interval_scale *
        std::sqrt(h->input_sample_variance);
    }
  }

  if(!chosen) {
    // No valid hypothesis: depth/intensity stay NaN, n_samples stays 0.
    return record;
  }

  // Backscatter half (ADR-0007 D2/D3/D4). Apply the per-beam angular-response
  // correction to each retained {raw_intensity, beam_angle} sample, THEN combine
  // the corrected values into a mean and an ESTIMATE variance.
  //
  // Empirical ARA (cube_bathymetry#81): corrected_dB = raw_dB - curveRel(|beam_angle|),
  // where curveRel is the per-sonar angular-response curve's db_relative_to_nadir
  // column, linearly interpolated between bin centres by |beam_angle| in DEGREES.
  // Nadir -> curveRel ~0 -> identity. Beyond the curve's max angle, a NaN
  // beam_angle, mode None, or an empty curve all fall back to identity (raw).
  //
  // rx_angles sign/zero gate (verified against kongsberg_em_bridge): the producer
  // negates the Kongsberg pointing angle (+PORT -> +STARBOARD; nadir = 0; radians;
  // intensity in dB). The curve is keyed on |beam_angle|, so port/starboard beams
  // of equal magnitude get the same correction.
  //
  // Tier-2 (cube_bathymetry#87): when the loaded curve is a TL-REMOVED residual
  // (backscatter_tl_removed), first remove each beam's 2-way transmission loss
  //   TL(R) = 40*log10(R) + 2*alpha*R      (R = per-beam slant range, m)
  // so the residual curve is depth/range transferable:
  //   corrected = raw - TL(R) - residualCurve(|beam_angle|).
  // alpha is read verbatim from the curve header (backscatter_absorption_db_per_m);
  // the estimator NEVER recomputes the (Francois-Garrison) absorption -- the
  // Python derive tool is the single source of truth, guaranteeing consistency.
  // A NaN / non-positive R skips the TL term (no log of a non-positive range);
  // tier-1 (backscatter_tl_removed == false) skips it entirely (unchanged).
  //
  // Deferred (NOT done here): the full radiometric GeoCoder chain -- insonified
  // area, beam-pattern, TVG residual, and the depth/slope incidence term
  // (ADR-0007 D3, cube_bathymetry#15/#59). The empirical curve absorbs the
  // aggregate angular falloff for a flat bottom; per-beam {raw_intensity,
  // beam_angle, range} stay retained so the node value is re-derivable when #15
  // lands.
  const bool apply_ara =
    parameters.backscatter_angle_correction ==
    BackscatterAngleCorrection::Empirical &&
    !parameters.angular_response_curve.empty();
  const bool apply_tl = apply_ara && parameters.backscatter_tl_removed;
  const double alpha = parameters.backscatter_absorption_db_per_m;
  double sum = 0.0;
  double sum_sq = 0.0;
  uint32_t n = 0;
  for(const auto & sample : chosen->intensity_samples) {
    // recordBeam() already excluded NaN-intensity beams, so raw_intensity is real.
    double corrected = sample.raw_intensity;
    if(apply_tl) {
      const double range = static_cast<double>(sample.range);
      // Skip the TL term for a missing / non-positive range (log10 undefined);
      // the beam is still corrected by the residual angular-response curve below.
      if(std::isfinite(range) && range > 0.0) {
        corrected -= 40.0 * std::log10(range) + 2.0 * alpha * range;
      }
    }
    if(apply_ara && !std::isnan(sample.beam_angle)) {
      const double abs_angle_deg =
        std::abs(static_cast<double>(sample.beam_angle)) * 180.0 / M_PI;
      corrected -= curveRelativeDb(parameters.angular_response_curve, abs_angle_deg);
    }
    sum += corrected;
    sum_sq += corrected * corrected;
    ++n;
  }

  record.n_samples = n;
  if(n > 0) {
    const double mean = sum / n;
    record.intensity = static_cast<float>(mean);
    if(n >= 2) {
      // Sample variance (unbiased), then divide by n to get the variance of the
      // MEAN -- the estimate variance that shrinks with n (ADR-0007 D4), NOT the
      // raw sample variance. Mirrors the bathy store's depth uncertainty.
      // Clamp to 0: float rounding on the sum-of-squares form can produce a
      // tiny negative sample_variance at low-dB means over many beams.
      const double sample_variance = std::max(
        0.0, (sum_sq - sum * sum / n) / (n - 1));
      record.intensity_var = static_cast<float>(sample_variance / n);
    }
    // n == 1: intensity set, intensity_var stays NaN (no spread from one beam).
  }

  return record;
}

std::shared_ptr<Hypothesis> Node::chooseHypothesis()
{
  std::shared_ptr<Hypothesis> ret;
  int max_sample_count = 0;
  for (auto h  :  depth_hypotheses_) {
    if(h->number_of_samples > max_sample_count) {
      ret = h;
      max_sample_count = h->number_of_samples;
    }
  }
  return ret;
}

void Node::truncate(const Parameters & parameters)
{
  if(queue_.size() < 3) {
    return;
  }

  float mean = 0.0;
  float ssd = 0.0;
  auto n = queue_.size() - 1;

  /* First compute mean and overall sum of squared differences (SSD) */
  for (auto & p  :  queue_) {
    mean += p.depth;
    ssd += p.depth * p.depth;
  }
  ssd -= mean * mean / (n + 1);
  mean /= (n + 1);
  float ssd_k = n * ssd / (n * n - 1);

  /* Run the list computing quotients; outliers are removed from the queue.
   */
  for(auto i = queue_.begin(); i != queue_.end(); ) {
    auto diff_sq = (i->depth - mean) * (i->depth - mean);
    auto q = diff_sq / (ssd_k - diff_sq / (n - 1));
    if(q > parameters.quotient_limit) {
      i = queue_.erase(i);
    } else {
      ++i;
    }
  }
}


void Node::setPredictedDepth(float depth, float variance)
{
  // 1:1 port of cube_node_set_preddepth (cube_node.c:1084): a trivial setter.
  // pred_depth == NaN     => do not incorporate any data into the node
  // pred_depth == INVALID => no prediction available (no slope correction)
  //
  // Precondition: a *real* predicted depth must carry a finite, positive,
  // non-sentinel variance. insert()'s blunder limit uses sqrt(predicted_depth_-
  // variance_), so pairing a valid depth with an INVALID_DATA (= float max)
  // variance would make sqrt(.) ~1e19 and silently neutralize blunder rejection.
  // No producer wires this yet; the assert guards the future external-prior path.
  assert(
    (depth == INVALID_DATA || std::isnan(depth) ||
    (std::isfinite(variance) && variance > 0.0F && variance != INVALID_DATA)) &&
    "setPredictedDepth: a real predicted depth requires a finite positive variance");
  predicted_depth_ = depth;
  predicted_depth_variance_ = variance;
}


void Node::queueFlush(const Parameters & parameters)
{
  if(queue_.empty()) {
    return;
  }

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
    update(q[ex_pt].depth, q[ex_pt].uncertainty, parameters,
      q[ex_pt].intensity, q[ex_pt].beam_angle, q[ex_pt].range);
    ex_pt += direction * scale;
    direction = -direction;
    scale++;
  }
}


}  // namespace cube
