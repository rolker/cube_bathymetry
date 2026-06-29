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
#include <vector>
#include "cube_bathymetry/angular_response_curve.h"
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

// Lossless reload (ADR-0001): a seeded settled value must round-trip EXACTLY
// through extractDepthAndUncertainty -- this is the property that lets an evicted
// (or primed) tile survive the next whole-tile save instead of being wiped.
TEST_F(NodeTest, SeedSettledDepthRoundTripsThroughExtract)
{
  Node n;
  const float depth = -12.5f;
  const float uncertainty = 0.42f;  // stored 1.96-sigma confidence interval
  n.seedSettledDepth(depth, uncertainty, params);

  const DepthAndUncertainty out = n.extractDepthAndUncertainty(params);
  EXPECT_NEAR(out.depth, depth, 1e-4) << "seeded depth must re-emit unchanged";
  EXPECT_NEAR(out.uncertainty, uncertainty, 1e-4)
    << "seeded uncertainty must re-emit unchanged (variance round-trip)";
}

// The seeded value is a Bayesian prior: a consistent new sounding refines the
// SAME hypothesis (it does not spawn a competitor), so the cell stays valid and
// the estimate stays near the agreed depth.
TEST_F(NodeTest, SeedSettledDepthActsAsPriorForNewData)
{
  Node n;
  n.seedSettledDepth(-12.5f, 0.42f, params);

  // A new sounding consistent with the prior updates the seeded hypothesis.
  EXPECT_TRUE(n.update(-12.4f, 0.25f, params));
  auto h = n.chooseHypothesis();
  ASSERT_NE(h, nullptr);
  EXPECT_NEAR(h->current_estimate, -12.5, 0.5)
    << "estimate stays near the seeded prior after one consistent sample";
  EXPECT_GT(h->number_of_samples, 1u)
    << "the new sample accreted onto the seeded hypothesis";
}

// A non-finite / non-positive stored uncertainty (a single-sample CUBE cell can
// persist one) must not poison the reload: variance floors to a small positive
// epsilon so the depth still round-trips and the DLM stays defined.
TEST_F(NodeTest, SeedSettledDepthFloorsDegenerateUncertainty)
{
  Node n;
  n.seedSettledDepth(-8.0f, 0.0f, params);
  const DepthAndUncertainty out = n.extractDepthAndUncertainty(params);
  EXPECT_NEAR(out.depth, -8.0, 1e-4);
  EXPECT_TRUE(std::isfinite(out.uncertainty));
  EXPECT_GE(out.uncertainty, 0.0f);
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

// --- Slope correction (issue #15) ------------------------------------------
//
// The re-enabled offset in Node::insert is the predicted-SURFACE slope delta
//   offset = predicted_depth_(node) - sounding.predicted_depth_at_touchdown
// (both negative-down predicted-surface depths), queued as
//   sounding.depth + offset.
// The discriminator against the two prior wrong formulas (.z vertical component
// and depth/cos(angle)) is the TOUCHDOWN-POSITION delta along a known synthetic
// prior surface; the corrected offset is angle-independent. Each test below
// reconstructs the expected value INDEPENDENTLY from pred_node - pred_touchdown,
// it does not echo the implementation. Soundings are inserted repeatedly and
// flushed so the median/CUBE estimate converges to the queued corrected depth,
// which is what extractDepthAndUncertainty() returns.

// Helper: a sounding sited off-node whose touchdown predicted-surface depth is
// supplied directly (the per-sounding analog of the original's overwritten
// snd->range), with the variances the insert path needs.
static Sounding makeSlopeSounding(float depth, float predicted_depth_at_touchdown)
{
  Sounding s(depth);
  s.predicted_depth_at_touchdown = predicted_depth_at_touchdown;
  s.vertical_error = 0.01f;
  s.horizontal_error = 0.0f;
  return s;
}

TEST_F(NodeTest, SlopeCorrectionSurfaceSlopeDelta)
{
  // Known synthetic prior surface: node sits at -10.0 m, the sounding touched
  // down at a point where the prior surface is -10.6 m (0.6 m deeper, i.e. a
  // genuine slope between node and touchdown). The raw sounding depth is -10.5.
  const float pred_node = -10.0f;
  const float pred_touchdown = -10.6f;
  const float depth = -10.5f;

  // Independent reconstruction of the expected corrected depth:
  //   queued = depth + (pred_node - pred_touchdown)
  // Deliberately NOT pred_node (the v1 .z trap) and NOT pred - depth/cos (v2).
  const float expected = depth + (pred_node - pred_touchdown);  // = -9.9

  Node n;
  n.setPredictedDepth(pred_node, 0.01f);

  // Insert the same off-node sounding many times so the estimate converges to
  // the queued corrected value, then flush the pre-filter queue.
  for (int i = 0; i < 20; ++i) {
    EXPECT_TRUE(n.insert(0.0, makeSlopeSounding(depth, pred_touchdown), params));
  }
  n.queueFlush(params);

  auto result = n.extractDepthAndUncertainty(params);
  ASSERT_FALSE(std::isnan(result.depth));
  EXPECT_NEAR(result.depth, expected, 0.05);
  // Sanity: it must NOT collapse to pred_node (v1) or to the raw depth (no corr).
  EXPECT_GT(std::abs(result.depth - pred_node), 0.05);
  EXPECT_GT(std::abs(result.depth - depth), 0.05);
}

TEST_F(NodeTest, SlopeCorrectionZeroOnFlatSurface)
{
  // Touchdown predicted depth equals the node predicted depth => offset 0 =>
  // queued depth is the raw sounding depth, uncorrected.
  const float pred_node = -10.0f;
  const float depth = -10.5f;

  Node n;
  n.setPredictedDepth(pred_node, 0.01f);
  for (int i = 0; i < 20; ++i) {
    EXPECT_TRUE(n.insert(0.0, makeSlopeSounding(depth, pred_node), params));
  }
  n.queueFlush(params);

  auto result = n.extractDepthAndUncertainty(params);
  ASSERT_FALSE(std::isnan(result.depth));
  EXPECT_NEAR(result.depth, depth, 0.05);
}

TEST_F(NodeTest, NoSlopeCorrectionWhenTouchdownSentinel)
{
  // predicted_depth_ is set on the node, but the sounding carries the
  // no-correction sentinel (INVALID_DATA) for its touchdown depth => offset 0.
  const float pred_node = -10.0f;
  const float depth = -10.5f;

  Node n;
  n.setPredictedDepth(pred_node, 0.01f);
  for (int i = 0; i < 20; ++i) {
    // Default Sounding leaves predicted_depth_at_touchdown == INVALID_DATA.
    Sounding s(depth);
    s.vertical_error = 0.01f;
    s.horizontal_error = 0.0f;
    EXPECT_TRUE(n.insert(0.0, s, params));
  }
  n.queueFlush(params);

  auto result = n.extractDepthAndUncertainty(params);
  ASSERT_FALSE(std::isnan(result.depth));
  EXPECT_NEAR(result.depth, depth, 0.05);  // offset 0: queued == raw depth
}

TEST_F(NodeTest, NoSlopeCorrectionWhenPredictedDepthInvalid)
{
  // The node has no prediction (predicted_depth_ == INVALID_DATA by default),
  // even though the sounding supplies a touchdown depth => offset 0. This is
  // the unwired production path: with no prior surface loaded, behaviour is
  // unchanged correct-but-uncorrected.
  const float depth = -10.5f;
  const float pred_touchdown = -10.6f;

  Node n;  // predicted_depth_ defaults to INVALID_DATA, nothing sets it.
  EXPECT_EQ(n.predictedDepth(), INVALID_DATA);
  for (int i = 0; i < 20; ++i) {
    EXPECT_TRUE(n.insert(0.0, makeSlopeSounding(depth, pred_touchdown), params));
  }
  n.queueFlush(params);

  auto result = n.extractDepthAndUncertainty(params);
  ASSERT_FALSE(std::isnan(result.depth));
  EXPECT_NEAR(result.depth, depth, 0.05);  // offset 0: queued == raw depth
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

  // Original (shallow) hypothesis keeps ONLY its own beam's intensity (Welford
  // of one corrected sample == the raw, default None correction).
  EXPECT_EQ(shallow->intensity.n, 1u);
  EXPECT_DOUBLE_EQ(shallow->intensity.mean, -30.0);

  // The outlier's intensity is on the newly-seeded (deep) hypothesis.
  EXPECT_EQ(deep->intensity.n, 1u);
  EXPECT_DOUBLE_EQ(deep->intensity.mean, -10.0);
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
  nominated->recordBeam(-12.0f, 0.0f, std::nan(""), params);  // None -> corrected = raw
  nominated->recordBeam(-16.0f, 0.0f, std::nan(""), params);
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

// ---- Empirical angular-response (ARA) correction (#81) ---------------------

namespace
{
// Degrees -> radians, matching how rx_angles are stored on a sample (radians).
float deg2rad(float deg) {return deg * static_cast<float>(M_PI) / 180.0f;}

// A Parameters with the Empirical ARA mode + a known 2-point curve:
// nadir bin (0 deg -> 0 dB) and edge bin (60 deg -> -12 dB). At 30 deg the
// linearly-interpolated db_relative_to_nadir is -6 dB.
Parameters empiricalParams()
{
  Parameters p{CellSizes(1.0f), "order1a"};
  p.backscatter_angle_correction = BackscatterAngleCorrection::Empirical;
  p.angular_response_curve = {{0.0f, 0.0f}, {60.0f, -12.0f}};
  return p;
}

// Build a single-beam node via a nominated hypothesis so the surfaced intensity
// equals the corrected value of exactly that beam (no averaging). The correction
// is applied AT RECORD (cube#93), so the curve params are passed to recordBeam.
std::shared_ptr<Hypothesis> singleBeamNominated(
  Node & n, float raw_intensity, float beam_angle_rad, const Parameters & record_params)
{
  auto h = std::make_shared<Hypothesis>(10.0f, 1.0f);
  h->input_sample_variance = 1.0f;
  h->recordBeam(raw_intensity, beam_angle_rad, std::nan(""), record_params);
  NodeNominationTestAccess::nominate(n, h);
  return h;
}
}  // namespace

// Mid-angle beam: corrected = raw - curveRel(30 deg) = -30 - (-6) = -24.
TEST_F(NodeTest, ARAInterpolation)
{
  Parameters ara = empiricalParams();
  Node n;
  singleBeamNominated(n, -30.0f, deg2rad(30.0f), ara);

  auto record = n.extractNodeRecord(ara);
  ASSERT_EQ(record.n_samples, 1u);
  EXPECT_NEAR(record.intensity, -24.0f, 1e-3);
}

// Nadir beam: curveRel(0) = 0 -> corrected == raw.
TEST_F(NodeTest, ARANadirIdentity)
{
  Parameters ara = empiricalParams();
  Node n;
  singleBeamNominated(n, -30.0f, 0.0f, ara);

  auto record = n.extractNodeRecord(ara);
  ASSERT_EQ(record.n_samples, 1u);
  EXPECT_NEAR(record.intensity, -30.0f, 1e-4);
}

// Beyond the curve's max angle (60 deg): identity (no extrapolation).
TEST_F(NodeTest, ARABeyondMaxAngleClampsToEdge)
{
  // Beyond the curve's last bin (60 deg) the correction clamps to the outermost
  // bin value (-12 dB), NOT identity -- continuous, no swath-edge jump (#81 review).
  // corrected = raw - curveRel = -30 - (-12) = -18.
  Parameters ara = empiricalParams();
  Node n;
  singleBeamNominated(n, -30.0f, deg2rad(70.0f), ara);

  auto record = n.extractNodeRecord(ara);
  ASSERT_EQ(record.n_samples, 1u);
  EXPECT_NEAR(record.intensity, -18.0f, 1e-3);
}

// NaN beam angle: no correction applicable -> identity.
TEST_F(NodeTest, ARANaNAngleIdentity)
{
  Parameters ara = empiricalParams();
  Node n;
  singleBeamNominated(n, -30.0f, std::nan(""), ara);

  auto record = n.extractNodeRecord(ara);
  ASSERT_EQ(record.n_samples, 1u);
  EXPECT_NEAR(record.intensity, -30.0f, 1e-4);
}

// Mode None on a non-nadir beam: the correction never runs -> corrected == raw.
TEST_F(NodeTest, ARAOffIsIdentity)
{
  Node n;
  singleBeamNominated(n, -30.0f, deg2rad(30.0f), params);  // default None at record

  auto record = n.extractNodeRecord(params);  // default None
  ASSERT_EQ(record.n_samples, 1u);
  EXPECT_NEAR(record.intensity, -30.0f, 1e-4);
}

// Port/starboard symmetry: +30 deg and -30 deg get the same correction because
// the curve is keyed on |beam_angle|. Two equal-magnitude beams -> equal
// corrected values -> mean -24, zero spread.
TEST_F(NodeTest, ARAPortStarboardSymmetry)
{
  Parameters ara = empiricalParams();
  Node n;
  auto h = std::make_shared<Hypothesis>(10.0f, 1.0f);
  h->input_sample_variance = 1.0f;
  h->recordBeam(-30.0f, deg2rad(30.0f), std::nan(""), ara);   // starboard
  h->recordBeam(-30.0f, deg2rad(-30.0f), std::nan(""), ara);  // port
  NodeNominationTestAccess::nominate(n, h);

  auto record = n.extractNodeRecord(ara);
  ASSERT_EQ(record.n_samples, 2u);
  EXPECT_NEAR(record.intensity, -24.0f, 1e-3);
  // Equal corrected values -> zero estimate variance (clamped).
  ASSERT_FALSE(std::isnan(record.intensity_var));
  EXPECT_NEAR(record.intensity_var, 0.0f, 1e-4);
}

// ---- Tier-2: TL-removed angular response (#87) -----------------------------

namespace
{
// empiricalParams() + the tier-2 TL provenance: the loaded curve is a TL-removed
// residual, so the estimator also compensates (ADDS BACK) 40*log10(R) + 2*alpha*R
// per beam.
Parameters tier2Params(float alpha)
{
  Parameters p = empiricalParams();  // Empirical, curve {{0,0},{60,-12}}
  p.backscatter_tl_removed = true;
  p.backscatter_absorption_db_per_m = alpha;
  return p;
}

// Single-beam nominated hypothesis carrying a per-beam slant range, so the
// surfaced intensity equals the tier-2 corrected value of exactly that beam. The
// correction is applied AT RECORD (cube#93), so the tier-2 params are passed in.
std::shared_ptr<Hypothesis> singleBeamNominatedRange(
  Node & n, float raw_intensity, float beam_angle_rad, float range,
  const Parameters & record_params)
{
  auto h = std::make_shared<Hypothesis>(10.0f, 1.0f);
  h->input_sample_variance = 1.0f;
  h->recordBeam(raw_intensity, beam_angle_rad, range, record_params);
  NodeNominationTestAccess::nominate(n, h);
  return h;
}

// Reference tier-2 correction: corrected = raw + (40log10R + 2*alpha*R) - residual.
// TL is ADDED BACK (compensated): a distant return lost more energy, so it is
// boosted to recover range-independent backscatter (TVG-style, #87 sign fix).
double tier2Expected(double raw, double range, double alpha, double residual_db)
{
  const double tl = 40.0 * std::log10(range) + 2.0 * alpha * range;
  return raw + tl - residual_db;
}
}  // namespace

// Tier-2 mid-angle beam with a known range: the TL term is compensated and the
// residual curve removed -- corrected = raw + (40log10R + 2*alpha*R) - residual(30deg).
TEST_F(NodeTest, Tier2RemovesTLAndResidual)
{
  const float alpha = 0.05f;
  const float range = 50.0f;
  Parameters t2 = tier2Params(alpha);
  Node n;
  singleBeamNominatedRange(n, -30.0f, deg2rad(30.0f), range, t2);

  auto record = n.extractNodeRecord(t2);
  ASSERT_EQ(record.n_samples, 1u);
  // residual(30deg) = -6 dB (interpolated {0,0}..{60,-12}).
  const double expected = tier2Expected(-30.0, range, alpha, -6.0);
  EXPECT_NEAR(record.intensity, static_cast<float>(expected), 1e-2);
}

// Tier-2 nadir beam: residual(0) = 0, only the TL term is removed.
TEST_F(NodeTest, Tier2NadirRemovesTLOnly)
{
  const float alpha = 0.04897f;
  const float range = 35.0f;
  Parameters t2 = tier2Params(alpha);
  Node n;
  singleBeamNominatedRange(n, -40.0f, 0.0f, range, t2);

  auto record = n.extractNodeRecord(t2);
  ASSERT_EQ(record.n_samples, 1u);
  const double expected = tier2Expected(-40.0, range, alpha, 0.0);
  EXPECT_NEAR(record.intensity, static_cast<float>(expected), 1e-2);
}

// Tier-2 with a NaN range: the TL term is skipped (no log of a non-positive R),
// so the beam falls back to tier-1 (residual-only) correction.
TEST_F(NodeTest, Tier2NaNRangeSkipsTL)
{
  Parameters t2 = tier2Params(0.05f);
  Node n;
  singleBeamNominatedRange(n, -30.0f, deg2rad(30.0f), std::nan(""), t2);

  auto record = n.extractNodeRecord(t2);
  ASSERT_EQ(record.n_samples, 1u);
  // No TL -> corrected = raw - residual(30deg) = -30 - (-6) = -24.
  EXPECT_NEAR(record.intensity, -24.0f, 1e-3);
}

// Tier-2 with a non-positive range: TL skipped likewise (identity TL term).
TEST_F(NodeTest, Tier2NonPositiveRangeSkipsTL)
{
  Parameters t2 = tier2Params(0.05f);
  Node n;
  singleBeamNominatedRange(n, -30.0f, deg2rad(30.0f), -5.0f, t2);

  auto record = n.extractNodeRecord(t2);
  ASSERT_EQ(record.n_samples, 1u);
  EXPECT_NEAR(record.intensity, -24.0f, 1e-3);
}

// Regression: tl_removed == false (tier-1) ignores the range entirely, even when
// a finite range is present -- the TL term must NOT be applied.
TEST_F(NodeTest, Tier1IgnoresRange)
{
  Parameters ara = empiricalParams();  // tl_removed defaults false
  Node n;
  singleBeamNominatedRange(n, -30.0f, deg2rad(30.0f), 50.0f, ara);

  auto record = n.extractNodeRecord(ara);
  ASSERT_EQ(record.n_samples, 1u);
  // Tier-1: corrected = raw - residual(30deg) = -24 (no TL despite range=50).
  EXPECT_NEAR(record.intensity, -24.0f, 1e-3);
}

// Threading: a per-beam range survives Node::update() onto the seeding/winning
// hypothesis, so the tier-2 TL correction sees it at extractNodeRecord().
TEST_F(NodeTest, Tier2RangeThreadsThroughUpdate)
{
  const float alpha = 0.05f;
  const float range = 50.0f;
  Parameters t2 = tier2Params(alpha);
  Node n;
  // !best path (first beam seeds a new hypothesis) carries the range.
  ASSERT_TRUE(n.update(10.0f, 1.0f, t2, -30.0f, deg2rad(30.0f), range));

  auto record = n.extractNodeRecord(t2);
  ASSERT_EQ(record.n_samples, 1u);
  const double expected = tier2Expected(-30.0, range, alpha, -6.0);
  EXPECT_NEAR(record.intensity, static_cast<float>(expected), 1e-2);
}

// THE load-bearing cube#93 proof: correct-at-record (streaming Welford of the
// corrected intensity) reproduces correct-at-extract (retain raw + correct each at
// extract + naive mean/variance) for a fixed curve. Feed N varied beams through
// recordBeam (which corrects + folds), extract the node-output mean/variance, and
// compare to a reference computed the OLD way (correctBeamIntensity per beam, then
// naive sum / sum-of-squares -- the exact formula extractNodeRecord used before).
TEST_F(NodeTest, CorrectAtRecordMatchesCorrectAtExtract)
{
  Parameters ara = empiricalParams();  // Empirical, curve {{0,0},{60,-12}}

  // A spread of beams across the swath (mix of angles + dB), all on one nominated
  // hypothesis so the record surfaces exactly this set.
  const std::vector<std::pair<float, float>> beams = {  // {raw_dB, angle_rad}
    {-30.0f, deg2rad(0.0f)}, {-28.5f, deg2rad(12.0f)}, {-33.0f, deg2rad(25.0f)},
    {-26.0f, deg2rad(40.0f)}, {-31.5f, deg2rad(55.0f)}, {-29.0f, deg2rad(70.0f)},
    {-35.0f, deg2rad(33.0f)}, {-27.0f, deg2rad(8.0f)}};

  Node n;
  auto h = std::make_shared<Hypothesis>(10.0f, 1.0f);
  h->input_sample_variance = 1.0f;
  for (const auto & b : beams) {
    h->recordBeam(b.first, b.second, std::nan(""), ara);  // correct-at-record
  }
  NodeNominationTestAccess::nominate(n, h);
  const auto record = n.extractNodeRecord(ara);

  // Reference: the OLD retain-and-correct-at-extract computation.
  double sum = 0.0;
  double sum_sq = 0.0;
  const auto nN = static_cast<double>(beams.size());
  for (const auto & b : beams) {
    const double c = correctBeamIntensity(b.first, b.second, std::nan(""), ara);
    sum += c;
    sum_sq += c * c;
  }
  const double ref_mean = sum / nN;
  const double ref_sample_var = (sum_sq - sum * sum / nN) / (nN - 1.0);
  const double ref_var_of_mean = ref_sample_var / nN;

  ASSERT_EQ(record.n_samples, beams.size());
  // Welford vs naive agree to ~13+ digits; assert tight equivalence.
  EXPECT_NEAR(record.intensity, static_cast<float>(ref_mean), 1e-5);
  EXPECT_NEAR(record.intensity_var, static_cast<float>(ref_var_of_mean), 1e-5);
}

}  // namespace cube
