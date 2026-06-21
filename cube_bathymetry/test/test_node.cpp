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
#include "cube_bathymetry/node.h"

namespace cube
{

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

}  // namespace cube
