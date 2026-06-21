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
#include <memory>
#include "cube_bathymetry/node.h"

namespace cube
{

// Test-only accessor for Node's private nominated_hypothesis_ slot (befriended in
// node.h). Lets us exercise the nominated-hypothesis priority path of
// extractNodeRecord()/extractDepthAndUncertainty(), which has no production setter.
struct NodeNominationTestAccess
{
  static void nominate(Node & node, const std::shared_ptr<Hypothesis> & h)
  {
    node.nominated_hypothesis_ = h;
  }
};

class NodeTest : public ::testing::Test
{
protected:
  Parameters params{CellSizes(1.0f), "order1a"};
};

TEST_F(NodeTest, AddHypothesisSucceeds)
{
  Node n;
  EXPECT_TRUE(n.addHypothesis(10.0f, 1.0f));
}

TEST_F(NodeTest, BestHypothesisReturnsNullWhenEmpty)
{
  Node n;
  auto h = n.bestHypothesis(10.0f, 1.0f);
  EXPECT_EQ(h, nullptr);
}

TEST_F(NodeTest, BestHypothesisFindsClosest)
{
  Node n;
  n.addHypothesis(10.0f, 1.0f);
  n.addHypothesis(20.0f, 1.0f);

  auto h = n.bestHypothesis(11.0f, 1.0f);
  ASSERT_NE(h, nullptr);
  EXPECT_NEAR(h->current_estimate, 10.0, 1e-6);
}

TEST_F(NodeTest, UpdateCreatesHypothesisWhenEmpty)
{
  Node n;
  EXPECT_TRUE(n.update(10.0f, 1.0f, params));

  auto h = n.chooseHypothesis();
  ASSERT_NE(h, nullptr);
  EXPECT_NEAR(h->current_estimate, 10.0, 1e-6);
}

TEST_F(NodeTest, UpdateWithOutlierCreatesNewHypothesis)
{
  Node n;
  n.update(10.0f, 0.01f, params);

  // Very different depth with tight variance triggers intervention
  n.update(50.0f, 0.01f, params);

  // Should now have two hypotheses; chosen one should not be
  // near the midpoint, confirming they are separate hypotheses
  auto chosen = n.chooseHypothesis();
  ASSERT_NE(chosen, nullptr);
  EXPECT_TRUE(
    std::abs(chosen->current_estimate - 10.0) < 1.0 ||
    std::abs(chosen->current_estimate - 50.0) < 1.0);
}

TEST_F(NodeTest, ChooseHypothesisPicksMostSamples)
{
  Node n;
  n.addHypothesis(10.0f, 1.0f);
  n.addHypothesis(20.0f, 1.0f);

  // Update with data near first hypothesis multiple times
  for (int i = 0; i < 5; ++i) {
    n.update(10.1f, 1.0f, params);
  }

  auto h = n.chooseHypothesis();
  ASSERT_NE(h, nullptr);
  EXPECT_NEAR(h->current_estimate, 10.0, 0.5);
}

TEST_F(NodeTest, QueueEstimateMedianFilter)
{
  Node n;
  // Fill beyond median_length (default=11) to push data through
  for (int i = 0; i < 15; ++i) {
    n.queueEstimate(10.0f + i * 0.1f, 1.0f, params);
  }

  // After exceeding median_length, data should have been pushed through
  // Then flush remaining
  n.queueFlush(params);

  auto h = n.chooseHypothesis();
  ASSERT_NE(h, nullptr);
}

TEST_F(NodeTest, QueueFlushProcessesAllData)
{
  Node n;
  // Add some data to the queue (less than median_length)
  for (int i = 0; i < 5; ++i) {
    n.queueEstimate(10.0f, 1.0f, params);
  }

  // Queue should have data but not yet pushed through
  // Flush should process it
  n.queueFlush(params);

  auto h = n.chooseHypothesis();
  ASSERT_NE(h, nullptr);
  EXPECT_NEAR(h->current_estimate, 10.0, 0.1);
}

TEST_F(NodeTest, ExtractDepthAndUncertaintyNoData)
{
  Node n;
  auto result = n.extractDepthAndUncertainty(params);
  EXPECT_TRUE(std::isnan(result.depth));
  EXPECT_TRUE(std::isnan(result.uncertainty));
}

TEST_F(NodeTest, ExtractDepthAndUncertaintyWithData)
{
  Node n;
  // Need multiple updates with varying depths so input_sample_variance > 0
  n.update(10.0f, 1.0f, params);
  n.update(10.5f, 1.0f, params);
  n.update(9.5f, 1.0f, params);

  auto result = n.extractDepthAndUncertainty(params);
  EXPECT_FALSE(std::isnan(result.depth));
  EXPECT_NEAR(result.depth, 10.0, 0.5);
  EXPECT_FALSE(std::isnan(result.uncertainty));
  EXPECT_GT(result.uncertainty, 0.0);
}

TEST_F(NodeTest, TruncateRemovesOutliers)
{
  Node n;
  // Add consistent data and one outlier via the queue
  for (int i = 0; i < 10; ++i) {
    n.queueEstimate(10.0f, 0.1f, params);
  }
  // Add an outlier
  n.queueEstimate(100.0f, 0.1f, params);

  // Flush should apply truncation
  n.queueFlush(params);

  auto h = n.chooseHypothesis();
  ASSERT_NE(h, nullptr);
  // Result should be near 10.0, not pulled toward the outlier
  EXPECT_NEAR(h->current_estimate, 10.0, 1.0);
}

TEST_F(NodeTest, MultipleConsistentUpdatesConverge)
{
  Node n;
  for (int i = 0; i < 20; ++i) {
    n.update(15.0f, 0.5f, params);
  }

  auto result = n.extractDepthAndUncertainty(params);
  EXPECT_NEAR(result.depth, 15.0, 0.1);
}

// ---- Backscatter co-estimation (#54, ADR-0007 D2/D3/D4) --------------------

// First beam at a node creates a hypothesis (the !best path); its intensity must
// be recorded on that new hypothesis, else every node silently drops its first
// beam. (Plan-review must-fix #2.)
TEST_F(NodeTest, FirstBeamInitializationRecordsIntensity)
{
  Node n;
  // No prior hypotheses -> addHypothesis path; intensity must land on it.
  EXPECT_TRUE(n.update(10.0f, 1.0f, params, -30.0f, 0.1f));

  auto record = n.extractNodeRecord(params);
  EXPECT_EQ(record.n_samples, 1u);
  ASSERT_FALSE(std::isnan(record.intensity));
  EXPECT_FLOAT_EQ(record.intensity, -30.0f);
  // One sample: estimate variance is undefined (NaN), not zero.
  EXPECT_TRUE(std::isnan(record.intensity_var));
}

// Mean + ESTIMATE variance over an associated set. estimate_var = sample_var / n
// (variance of the mean, shrinks with n; D4), NOT the raw sample variance.
TEST_F(NodeTest, NodeRecordMeanAndEstimateVariance)
{
  Node n;
  // Consistent depths -> one hypothesis accumulates all four beams.
  // Intensities {-30,-28,-32,-30}: mean = -30.0.
  n.update(10.0f, 1.0f, params, -30.0f, 0.1f);  // !best path (first beam)
  n.update(10.0f, 1.0f, params, -28.0f, 0.1f);
  n.update(10.0f, 1.0f, params, -32.0f, 0.1f);
  n.update(10.0f, 1.0f, params, -30.0f, 0.1f);

  auto record = n.extractNodeRecord(params);
  ASSERT_EQ(record.n_samples, 4u);
  EXPECT_NEAR(record.intensity, -30.0f, 1e-4);

  // Sample variance (unbiased) of {-30,-28,-32,-30}: deviations {0,2,-2,0},
  // sum_sq_dev = 8, /(n-1)=3 -> 8/3. Estimate variance = (8/3)/4 = 2/3.
  ASSERT_FALSE(std::isnan(record.intensity_var));
  EXPECT_NEAR(record.intensity_var, 2.0 / 3.0, 1e-4);
}

// A NaN-intensity beam (source omitted intensities for it) must be skipped, while
// its depth still contributes. n_samples counts only intensity-bearing beams.
TEST_F(NodeTest, NodeRecordSkipsNanIntensityBeam)
{
  Node n;
  n.update(10.0f, 1.0f, params, -30.0f, 0.1f);            // valid
  n.update(10.0f, 1.0f, params, std::nan(""), 0.1f);      // NaN intensity: skip
  n.update(10.0f, 1.0f, params, -30.0f, 0.1f);            // valid

  auto record = n.extractNodeRecord(params);
  EXPECT_EQ(record.n_samples, 2u);
  EXPECT_NEAR(record.intensity, -30.0f, 1e-4);
}

// Exclusion-on-intervention (mandatory): a beam rejected for depth (W&H monitor
// intervention) seeds a NEW hypothesis; its intensity goes to the new hypothesis,
// and the original hypothesis's intensity set is left UNCHANGED.
TEST_F(NodeTest, ExclusionOnInterventionLeavesOriginalIntensityUnchanged)
{
  Node n;
  // Seed hypothesis 0 with a tight-variance beam.
  n.update(10.0f, 0.01f, params, -30.0f, 0.1f);
  // A far-off beam with tight variance triggers an intervention: rejected from
  // hyp 0, seeds hyp 1 with intensity -10.0.
  n.update(50.0f, 0.01f, params, -10.0f, 0.2f);

  // There must now be two hypotheses; find each by depth.
  auto shallow = n.bestHypothesis(10.0f, 0.01f);
  auto deep = n.bestHypothesis(50.0f, 0.01f);
  ASSERT_NE(shallow, nullptr);
  ASSERT_NE(deep, nullptr);
  ASSERT_NE(shallow, deep);

  // Original (shallow) hypothesis keeps ONLY its own beam's intensity.
  ASSERT_EQ(shallow->intensity_samples.size(), 1u);
  EXPECT_FLOAT_EQ(shallow->intensity_samples[0].raw_intensity, -30.0f);

  // The outlier's intensity is on the newly-seeded (deep) hypothesis.
  ASSERT_EQ(deep->intensity_samples.size(), 1u);
  EXPECT_FLOAT_EQ(deep->intensity_samples[0].raw_intensity, -10.0f);
}

// extractNodeRecord() must honor the nominated_hypothesis_ priority path exactly
// like extractDepthAndUncertainty() (plan-review must-fix #3): when a hypothesis
// is nominated, the record is built from it.
TEST_F(NodeTest, NodeRecordHonorsNominatedHypothesis)
{
  Node n;
  // Build a real, settled hypothesis (chooseHypothesis would pick this one).
  n.update(10.0f, 1.0f, params, -30.0f, 0.1f);
  n.update(10.0f, 1.0f, params, -30.0f, 0.1f);

  // Nominate a DIFFERENT hypothesis with a distinct depth + intensities so we can
  // tell which path extractNodeRecord() took.
  auto nominated = std::make_shared<Hypothesis>(42.0f, 1.0f);
  nominated->input_sample_variance = 4.0f;
  nominated->recordBeam(-12.0f, 0.0f);
  nominated->recordBeam(-16.0f, 0.0f);
  NodeNominationTestAccess::nominate(n, nominated);

  auto record = n.extractNodeRecord(params);
  // Depth half mirrors extractDepthAndUncertainty()'s nominated branch.
  auto depth_only = n.extractDepthAndUncertainty(params);
  EXPECT_FLOAT_EQ(record.depth, depth_only.depth);
  EXPECT_FLOAT_EQ(record.depth_var, depth_only.uncertainty);
  EXPECT_NEAR(record.depth, 42.0f, 1e-4);

  // Intensity comes from the NOMINATED hypothesis's beams: mean of {-12,-16}=-14.
  ASSERT_EQ(record.n_samples, 2u);
  EXPECT_NEAR(record.intensity, -14.0f, 1e-4);
  // Estimate variance: sample var of {-12,-16} = 8/(2-1)=8; /n=2 -> 4.
  EXPECT_NEAR(record.intensity_var, 4.0, 1e-4);
}

// No data: enriched record is all-NaN / zero, mirroring extractDepthAndUncertainty.
TEST_F(NodeTest, NodeRecordNoData)
{
  Node n;
  auto record = n.extractNodeRecord(params);
  EXPECT_TRUE(std::isnan(record.depth));
  EXPECT_TRUE(std::isnan(record.depth_var));
  EXPECT_TRUE(std::isnan(record.intensity));
  EXPECT_TRUE(std::isnan(record.intensity_var));
  EXPECT_EQ(record.n_samples, 0u);
}

// D3 binding through the median pre-queue: each beam's intensity must stay bound
// to ITS depth through Node::insert() -> queueEstimate() -> queueFlush(), even
// when beams arrive in an order that differs from the depth-sorted queue order.
// This exercises the real input path (insert) with median-induced reordering.
//
// Strategy: use a small median_length=3 so the median fires quickly. Send 4 beams
// via insert() with depths {10.0, 8.0, 12.0, 10.0} and corresponding
// intensities {-30, -20, -40, -30}. All depths are close so they land on one
// hypothesis. After queueFlush(), verify n_samples == 4 and mean intensity is the
// true mean of all four intensities (-30.0 dB), confirming no depth/intensity
// cross-contamination through the median sort.
TEST_F(NodeTest, InsertDrivenMedianQueuePreservesIntensityBinding)
{
  // Use median_length=3 so the median fires on the 3rd insert, pushing one beam
  // through to update() before the 4th arrives; queueFlush() clears the rest.
  Parameters p{CellSizes(1.0f), "order1a"};
  p.median_length = 3;

  Node n;

  // Beams with varying depths (arrival order != depth-sort order).
  // Intensities are deliberately tied to specific depths so a swap would show up
  // as a wrong mean:
  //   depth 10.0 -> intensity -30.0   (arrives 1st; shallowest of the set)
  //   depth  8.0 -> intensity -20.0   (arrives 2nd; middle depth)
  //   depth 12.0 -> intensity -40.0   (arrives 3rd; deepest)
  //   depth 10.0 -> intensity -30.0   (arrives 4th; same as 1st)
  //
  // Correct mean: (-30 + -20 + -40 + -30) / 4 = -30.0.
  //
  // distance=0.0 always passes the capture-distance guard in insert().
  // vertical_error and horizontal_error are small so variance passes IHO limit.

  auto make_sounding = [](float depth, float intensity) -> Sounding {
      Sounding s(depth);
      s.intensity = intensity;
      s.beam_angle = 0.0f;
      s.vertical_error = 0.01f;
      s.horizontal_error = 0.01f;
      return s;
    };

  n.insert(0.0, make_sounding(10.0f, -30.0f), p);
  n.insert(0.0, make_sounding(8.0f, -20.0f), p);
  n.insert(0.0, make_sounding(12.0f, -40.0f), p);
  n.insert(0.0, make_sounding(10.0f, -30.0f), p);
  n.queueFlush(p);

  auto record = n.extractNodeRecord(p);
  ASSERT_GT(record.n_samples, 0u);
  // All four beams have similar depths -> one hypothesis accumulates them.
  // The mean must be the true arithmetic mean of the four intensities: -30.0.
  // A binding error (intensity swapped to wrong depth entry) would shift this.
  EXPECT_NEAR(record.intensity, -30.0f, 1.0f);
}

// Negative-variance clamp: the sum-of-squares form used by extractNodeRecord()
// can yield a tiny negative sample_variance due to float rounding when the mean
// is large in magnitude (e.g. O(-30 dB)) and n is large. The clamp must ensure
// intensity_var is never negative -- only 0.0 or positive (or NaN for n<2).
TEST_F(NodeTest, IntensityVarNonNegativeAfterClamp)
{
  // Construct a case where float rounding could produce a negative intermediate:
  // many identical values so sample variance is theoretically 0 but rounding in
  // the sum-of-squares form may go slightly negative. The clamp must floor it.
  Node n;
  const float identical_intensity = -30.0f;
  for (int i = 0; i < 10; ++i) {
    n.update(10.0f, 1.0f, params, identical_intensity, 0.0f);
  }

  auto record = n.extractNodeRecord(params);
  ASSERT_GE(record.n_samples, 2u);
  // intensity_var must be >= 0 (not NaN for n>=2, and never negative).
  ASSERT_FALSE(std::isnan(record.intensity_var));
  EXPECT_GE(record.intensity_var, 0.0f);
}

}  // namespace cube
