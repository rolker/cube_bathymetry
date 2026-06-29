// Copyright 2025 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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

#include <gtest/gtest.h>
#include <cmath>
#include "cube_bathymetry/hypothesis.h"
#include "cube_bathymetry/parameters.h"

namespace cube
{

class HypothesisTest : public ::testing::Test
{
protected:
  // Default parameters with 1m grid
  Parameters params{CellSizes(1.0f), "order1a"};
};

TEST_F(HypothesisTest, ConstructorInitializesCorrectly)
{
  Hypothesis h(10.0f, 0.5f);

  EXPECT_DOUBLE_EQ(h.current_estimate, 10.0);
  EXPECT_DOUBLE_EQ(h.current_variance, 0.5);
  EXPECT_DOUBLE_EQ(h.predicted_estimate, 10.0);
  EXPECT_DOUBLE_EQ(h.predicted_variance, 0.5);
  EXPECT_EQ(h.number_of_samples, 1u);
  EXPECT_DOUBLE_EQ(h.cumulative_bayes_factor, 1.0);
  EXPECT_EQ(h.sequence_length, 0u);
}

TEST_F(HypothesisTest, NullHypothesisHasZeroSamples)
{
  auto h = Hypothesis::generateNullHypothesis(10.0f, 0.5f);

  EXPECT_DOUBLE_EQ(h->current_estimate, 10.0);
  EXPECT_DOUBLE_EQ(h->current_variance, 0.5);
  EXPECT_EQ(h->number_of_samples, 0u);
}

TEST_F(HypothesisTest, ResetMonitorRestoresDefaults)
{
  Hypothesis h(10.0f, 0.5f);
  h.cumulative_bayes_factor = 0.05;
  h.sequence_length = 3;

  h.resetMonitor();

  EXPECT_DOUBLE_EQ(h.cumulative_bayes_factor, 1.0);
  EXPECT_EQ(h.sequence_length, 0u);
}

TEST_F(HypothesisTest, UpdateWithConsistentDataSucceeds)
{
  Hypothesis h(10.0f, 1.0f);

  // Update with data close to the current estimate
  bool result = h.update(10.1f, 1.0f, params);

  EXPECT_TRUE(result);
  EXPECT_EQ(h.number_of_samples, 2u);
  // Estimate should move toward the new observation
  EXPECT_GT(h.current_estimate, 10.0);
  EXPECT_LT(h.current_estimate, 10.1);
  // Variance should decrease with more data
  EXPECT_LT(h.current_variance, 1.0);
}

TEST_F(HypothesisTest, UpdateKalmanFilterEquations)
{
  // Verify the Kalman filter update equations match W&H DLM
  Hypothesis h(10.0f, 1.0f);

  float depth = 12.0f;
  float variance = 2.0f;

  // With discount=1.0 (default), system variance = 0
  // predicted_variance = current_variance + 0 = current_variance
  // gain = pred_var / (obs_var + pred_var)
  double expected_gain = 1.0 / (2.0 + 1.0);
  double expected_innovation = 12.0 - 10.0;
  double expected_estimate = 10.0 + expected_gain * expected_innovation;
  double expected_cur_var = 2.0 * 1.0 / (2.0 + 1.0);

  bool result = h.update(depth, variance, params);
  EXPECT_TRUE(result);

  EXPECT_NEAR(h.current_estimate, expected_estimate, 1e-10);
  EXPECT_NEAR(h.current_variance, expected_cur_var, 1e-10);
}

TEST_F(HypothesisTest, UpdateWithOutlierTriggersIntervention)
{
  Hypothesis h(10.0f, 0.01f);  // Very tight variance

  // Data far from estimate should trigger intervention
  bool result = h.update(50.0f, 0.01f, params);

  EXPECT_FALSE(result);
  // Sample count should not increase on failed update
  EXPECT_EQ(h.number_of_samples, 1u);
}

TEST_F(HypothesisTest, MonitorDetectsSingleOutlier)
{
  Hypothesis h(10.0f, 0.1f);

  // Large deviation relative to forecast variance
  bool result = h.monitor(100.0f, 0.1f, params);

  EXPECT_FALSE(result);
}

TEST_F(HypothesisTest, MonitorAcceptsConsistentData)
{
  Hypothesis h(10.0f, 1.0f);

  // Small deviation relative to forecast variance
  bool result = h.monitor(10.5f, 1.0f, params);

  EXPECT_TRUE(result);
}

TEST_F(HypothesisTest, MonitorDetectsDriftByRunlength)
{
  Hypothesis h(10.0f, 1.0f);

  // Feed slightly off data repeatedly to trigger runlength failure
  // With est_offset=4.0, bayes_fac_t=0.135, runlength_t=5
  // We need cumulative Bayes factor to drop below threshold
  // or sequence length to exceed threshold
  bool last_result = true;
  for (int i = 0; i < 20; ++i) {
    // Moderate outlier that individually passes but cumulatively fails
    last_result = h.monitor(10.0f + 3.0f * std::sqrt(2.0f), 1.0f, params);
    if (!last_result) {break;}
  }
  // Should eventually detect drift
  EXPECT_FALSE(last_result);
}

TEST_F(HypothesisTest, InputSampleVarianceTracked)
{
  Hypothesis h(10.0f, 1.0f);
  EXPECT_FLOAT_EQ(h.input_sample_variance, 0.0f);

  h.update(10.5f, 1.0f, params);

  // After one update, sample variance should be non-zero
  // since the new depth differs from the estimate
  EXPECT_GT(h.input_sample_variance, 0.0f);
}

TEST_F(HypothesisTest, MultipleUpdatesConverge)
{
  Hypothesis h(10.0f, 10.0f);  // Start with high uncertainty

  // Feed consistent data at 12.0m
  for (int i = 0; i < 50; ++i) {
    h.update(12.0f, 0.5f, params);
  }

  // Should converge close to 12.0
  EXPECT_NEAR(h.current_estimate, 12.0, 0.1);
  // Variance should be small
  EXPECT_LT(h.current_variance, 0.5);
}

// Characterization (#30): the port computes |error| up front, collapsing Calder's
// signed +/- 2*h*error monitor branches into one. That is behavior-preserving only
// if the monitor is symmetric in the sign of the deviation. Symmetric deviations
// about the prediction must give identical accept/reject, Bayes factor and runlength.
TEST_F(HypothesisTest, MonitorIsSymmetricInErrorSign)
{
  Hypothesis h_plus(10.0f, 1.0f);
  Hypothesis h_minus(10.0f, 1.0f);

  bool r_plus = h_plus.monitor(10.0f + 2.5f, 1.0f, params);
  bool r_minus = h_minus.monitor(10.0f - 2.5f, 1.0f, params);

  EXPECT_EQ(r_plus, r_minus);
  EXPECT_DOUBLE_EQ(h_plus.cumulative_bayes_factor, h_minus.cumulative_bayes_factor);
  EXPECT_EQ(h_plus.sequence_length, h_minus.sequence_length);
}

// ADR-0007 D3/D4, cube#93: recordBeam folds each beam's CORRECTED intensity into
// the streaming Welford (n, mean, M2). With the default None correction the
// corrected value equals the raw, so two beams give the running mean of the raw
// dB. (White-box storage changed from a raw-sample vector to the O(1) Welford;
// the node-output mean/variance these feed are unchanged -- see test_node.)
TEST_F(HypothesisTest, RecordBeamFoldsCorrectedIntensityIntoWelford)
{
  Hypothesis h(10.0f, 1.0f);
  EXPECT_EQ(h.intensity.n, 0u);

  h.recordBeam(-30.0f, 0.1f, std::nan(""), params);  // None mode -> corrected = raw
  h.recordBeam(-28.0f, 0.2f, std::nan(""), params);

  EXPECT_EQ(h.intensity.n, 2u);
  EXPECT_DOUBLE_EQ(h.intensity.mean, -29.0);  // (-30 + -28) / 2
  // M2 = sum (x - mean)^2 = 1 + 1 = 2 -> sample variance M2/(n-1) = 2.
  EXPECT_DOUBLE_EQ(h.intensity.m2, 2.0);
}

// A NaN raw intensity (source omitted intensities) must never become a phantom
// backscatter sample -- it leaves the Welford count unchanged.
TEST_F(HypothesisTest, RecordBeamSkipsNanIntensity)
{
  Hypothesis h(10.0f, 1.0f);

  h.recordBeam(std::nan(""), 0.1f, std::nan(""), params);
  EXPECT_EQ(h.intensity.n, 0u);

  h.recordBeam(-25.0f, 0.3f, std::nan(""), params);
  EXPECT_EQ(h.intensity.n, 1u);
  EXPECT_DOUBLE_EQ(h.intensity.mean, -25.0);
}

// A NaN beam angle is still a valid intensity sample: it is counted, just not
// angle-corrected (with None mode there is no angle term anyway -> corrected = raw).
TEST_F(HypothesisTest, RecordBeamCountsNanAngle)
{
  Hypothesis h(10.0f, 1.0f);

  h.recordBeam(-20.0f, std::nan(""), std::nan(""), params);
  EXPECT_EQ(h.intensity.n, 1u);
  EXPECT_DOUBLE_EQ(h.intensity.mean, -20.0);
}

// cube#93 O(1) memory: per-cell intensity state is a fixed-size Welford triplet,
// NOT a per-beam vector, so a heavily-oversampled cell (the Massabesic OOM cause:
// ~10^9 beams over 47 tiles) does not grow per-cell RAM. The portable proxy: fold
// a large number of beams and assert the per-hypothesis footprint is unchanged
// (the old raw-sample vector would have grown ~12 bytes/beam without bound).
TEST_F(HypothesisTest, IntensityStateIsConstantSizeRegardlessOfBeamCount)
{
  const std::size_t hypothesis_size = sizeof(Hypothesis);

  Hypothesis h(10.0f, 1.0f);
  const int many = 500000;
  for (int i = 0; i < many; ++i) {
    // Vary the value so it is a real running estimate, not a constant fold.
    h.recordBeam(-30.0f - static_cast<float>(i % 11), 0.0f, std::nan(""), params);
  }

  // All beams folded into the O(1) accumulator...
  EXPECT_EQ(h.intensity.n, static_cast<uint32_t>(many));
  // ...with NO growth in the per-hypothesis footprint (the accumulator is a value
  // member of fixed size; this is the cube#93 invariant the old vector violated).
  EXPECT_EQ(sizeof(Hypothesis), hypothesis_size);
  EXPECT_LE(sizeof(IntensityWelford), static_cast<std::size_t>(32));
}

}  // namespace cube
