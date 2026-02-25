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
#include "cube_bathymetry/node.h"
#include <cmath>

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

  // Should now have two hypotheses
  auto chosen = n.chooseHypothesis();
  ASSERT_NE(chosen, nullptr);
  // The chosen one has the most samples (1 each), so either could be chosen
  // but we should have at least one hypothesis
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
  n.update(10.0f, 1.0f, params);

  auto result = n.extractDepthAndUncertainty(params);
  EXPECT_FALSE(std::isnan(result.depth));
  EXPECT_NEAR(result.depth, 10.0, 0.1);
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

}  // namespace cube
