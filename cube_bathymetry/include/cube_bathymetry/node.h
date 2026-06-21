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


#ifndef CUBE_BATHYMETRY__NODE_H_
#define CUBE_BATHYMETRY__NODE_H_

#include <cmath>
#include <cstdint>
#include <list>
#include <memory>
#include <vector>

#include "cube_bathymetry/common.h"
#include "cube_bathymetry/hypothesis.h"
#include "cube_bathymetry/parameters.h"
#include "cube_bathymetry/sounding.h"


namespace cube
{

/// Enriched per-node output of the CUBE pass (ADR-0007 D5): the winning
/// hypothesis's depth/uncertainty PLUS its co-estimated backscatter intensity,
/// intensity uncertainty and sample count, all from the same data association.
/// Emitted by Node::extractNodeRecord(); the existing
/// Node::extractDepthAndUncertainty() is kept unchanged for the depth-only
/// (bathy) consumers.
  struct NodeRecord
  {
  /// Winning-hypothesis depth (m); NaN when the node has no valid hypothesis.
    float depth = std::nan("");

  /// Depth uncertainty (confidence-scaled stddev), matching
  /// extractDepthAndUncertainty(); NaN when no valid hypothesis.
    float depth_var = std::nan("");

  /// Co-estimated backscatter intensity: the mean of the per-beam corrected
  /// intensities on the winning hypothesis. NaN when no intensity-bearing beams.
  /// (Phase B: correction is currently the identity -- emitted UNCORRECTED
  /// pending cube_bathymetry#15; see extractNodeRecord().)
    float intensity = std::nan("");

  /// Intensity ESTIMATE variance (variance of the mean, shrinks with n_samples;
  /// ADR-0007 D4), NOT the raw sample variance. NaN when fewer than 2 samples.
    float intensity_var = std::nan("");

  /// Number of intensity-bearing beams contributing to the intensity estimate.
    uint32_t n_samples = 0;
  };

/// Test-only accessor (defined in test/test_node.cpp) for the private
/// nominated_hypothesis_ slot, which has no production setter.
  struct NodeNominationTestAccess;

  class Node
  {
  // The nominated_hypothesis_ priority path of extractNodeRecord() /
  // extractDepthAndUncertainty() has no public setter (it is a user-nomination
  // placeholder, only ever reset() on insert). Befriend the unit-test fixture so
  // that path can be exercised directly, without adding production API whose
  // only purpose is testing. Forward-declared just above; the test TU defines it.
    friend class NodeNominationTestAccess;

public:
  ///  Add a specific depth hypothesis to the current list
  ///
  /// depth:  Depth to set for hypothesis (meter)
  /// variance    Variance to set for hypothesis  (meter^2)
  /// return true if the hypothesis was added, otherwise false
    bool addHypothesis(float depth, float variance);

  /// Update the CUBE equations for this node and input
  /// This runs the basic filter equations, using the KF formulation, and
  /// its innovations formulation.  Note that the updates have to be done
  /// in double precision to ensure that we don't have critical round-off
  /// problems. This algorithm now includes a discounted system noise
  /// variance model to set the evolution noise dynamically depending on
  /// the variance that was estimated at the previous stage (West &
  /// Harrison, 'Bayesian Forecasting and Dynamic Models', Springer, 2ed.,
  /// 1997, ch.2), and a monitoring scheme and feed-back interventions to
  /// allow the code to check that the estimates are staying in touch with
  /// the input data.  The monitoring scheme is also based on West &
  /// Harrison as above, Ch.11, Sec. 11.5.1, using cumulative Bayes
  /// factors and the unidirectional level shift alternate model.
  ///
  ///   depth: Depth estimate to incorporate
  ///   variance Estimate of depth estimate variance
  ///   parameters: Filtering parameters used to get at monitoring
  ///     algorithm parameters, and the discount factor for the
  ///     previous variance in order to compute the current
  ///     evolution noise (a.k.a. system noise variance).
  ///   intensity: per-beam raw backscatter for this sample (NaN when absent),
  ///     recorded on whichever hypothesis accepts/seeds from this beam so the
  ///     backscatter association tracks the depth association (ADR-0007 D2/D3).
  ///   beam_angle: per-beam receive/steering angle (radians, NaN when absent),
  ///     the angle half of the {raw intensity, angle} sufficient-stats pair.
    bool update(
      float depth, float variance, const Parameters & parameters,
      float intensity = std::nan(""), float beam_angle = std::nan(""));

  /// Find the closest matching hypothesis in the current linked list.
  /// This computes the normalised absolute error between one-step
  /// forecast for each hypothesis currently being tracked and the input
  /// sample; a pointer to the node with smallest error is returned, or
  /// NULL if there are no nodes.  If there is more than one node with
  /// the same error (unlikely in practice, but possible), then the last
  /// one in the list is chosen.  Since the list is maintained in
  /// push-down order this is the first hypothesis proposed (typically
  /// the `right' one unless the system burst fails at the start of
  /// sequence).
  ///   depth: Current input sample to be matched
  ///   variance: Current input variance to be matched
  /// Returns shared pointer to closest matching hypothesis of depth in the list
  /// provided, or NULL if there is no match (i.e., NULL root)
    std::shared_ptr < Hypothesis > bestHypothesis(float depth, float variance);


  /// Insert a single depth value into the node
  /// Inputs:
  ///    node_x, node_y: Position of the node in space
  ///    sounding: Sounding data to insert
  ///    parameters: Algorithm parameters
  /// Returns:
  ///    True if inserted OK, otherwise False
  /// This computes the variance scale factor for the new data, and then
  /// sends the data into the estimation queue, building it if required.
    bool insert(double distance, const Sounding & sounding, const Parameters & parameters);

  /// Insert points into the queue of estimates, and insert point into
  /// filter sequence if queue is filled
  /// Inputs:
  ///   depth: Depth estimate
  ///   var: Estimate of depth estimate variance
  ///   parameters: Algorithm parameters
  /// Outputs:  True if inserted, else False (no memory for queue)
  /// This inserts the depth given into the queue associated with the
  /// specified node, creating the queue if required.  After the queue
  /// has been primed (i.e., filled with estimates), on each call this
  /// routine extracts the median value from the queue (via a call to
  /// mapsheet_nodal_queue_insert()) and then inserts it into the CUBE
  /// input sequence (via a call to mapsheet_nodal_update_node()).
  /// Note that this algorithm means that the queue will always be
  /// full, and hence must be flushed before extracting any depth
  /// estimates (this can also be done to save memory).
  ///   intensity/beam_angle: the {raw intensity, angle} pair for this beam,
  ///     carried on the queue entry bound to its depth so the median sort never
  ///     mismatches a depth with a foreign intensity (ADR-0007 D3).
    bool queueEstimate(
      float depth, float variance, const Parameters & parameters,
      float intensity = std::nan(""), float beam_angle = std::nan(""));

  /* Routine: cube_node_extract_depth_unct
  * Purpose:  Extract depth and uncertainty of current best estimate
  * Inputs:  node  Node to work on
  *                     p    Parameters structure to use for data
  * Outputs:  *depth  Depth of best known hypothesis
  *      *unct  Uncertainty of best known hypothesis
  *      *ratio  Hypothesis strength ratio --- degree of belief in this
  *          hypothesis over the remainder
  *      True if a valid depth was extracted, otherwise False
  * Comment:  Note that there may be no hypotheses available in the node at the
  *      time this routine is called.  In this case, the system reports the
  *      default null depth and variance.  This routine
  *      can also be used to extract just depth, just variance or both by
  *      passing a NULL pointer rather than a valid address for the output
  *      variables.
  */
    DepthAndUncertainty extractDepthAndUncertainty(const Parameters & parameters);

  /// Extract the enriched node record (ADR-0007 D5): depth/uncertainty PLUS the
  /// co-estimated backscatter intensity/uncertainty/sample-count of the winning
  /// hypothesis. The depth fields match extractDepthAndUncertainty() exactly,
  /// including the nominated_hypothesis_ priority path, so the two outputs never
  /// disagree for the same node state. Intensity is computed from the winning
  /// hypothesis's per-beam {raw, angle} set: each beam is angle-corrected (D3),
  /// then the corrected values are combined into a mean + ESTIMATE variance
  /// (D4). The angle correction is currently the identity (no-op) pending
  /// cube_bathymetry#15 -- intensity is emitted UNCORRECTED, but the per-beam
  /// raw set is retained so it is re-derivable when #15 provides slope.
    NodeRecord extractNodeRecord(const Parameters & parameters);

  /* Routine:  cube_node_choose_hypothesis
  * Purpose:  Choose the current best hypothesis for the node in question
  * Inputs:  *list  Pointer to the list of hypotheses
  *      *best  Pointer to buffer for single best hypothesis
  * Outputs:  True if the sort took place, otherwise False (no buffer space).
  * Comment:  In this context, `best' means `hypothesis with most samples',
  *      rather than through any other metric.  This may not be the `best'
  *      until all of the data is in, but it should give an idea of what's
  *      going on in the data structure at any point (particularly if it
  *      changes dramatically from sample to sample).
  *        Note that this code does not check that there actually is a
  *      hypothesis list to sort ... expect Very Bad Things (tm) to happen
  *      if this isn't dealt with externally.
  */
    std::shared_ptr < Hypothesis > chooseHypothesis();

  /* Routine:  cube_node_truncate
 * Purpose:  Truncate a buffered sequence to reject outliers
 * Inputs:  node  CubeNode to work through
 *      *p    CubeParam structure to use for quotient limit
 * Outputs:  The node->queue[] structure is updated to remove any suspect
 *      outliers.
 * Comment:  The definition of 'suspect' depends on the value of the quotient
 *      limit set in the parameters structure.  In general, the higher the
 *      value, the more extreme must the departure be for the point to be
 *      considered an outlier.  In theory, the distribution of the quotient
 *      values computed should be approximately a Fisher F(1,N-2) where
 *      there are N points in the input sequence.  The values of the
 *      quotients are always positive, and monotonically increasing for
 *      worse outliers; therefore, one-sided critical values should be
 *      considered.
 */
    void truncate(const Parameters & parameters);

/* Routine:  cube_node_queue_flush_node
 * Purpose:  Flush a single node queue into the estimation input sequence
 * Inputs:  node  CubeNode to process
 *      *param  CUBE filter parameters structure
 * Outputs:  -
 * Comment:  This flushes the queue into the input sequence in order (i.e., take
 *      current median, resort, repeat).  Since the queue is always sorted,
 *      we can just walk the list in order, rather than having to re-sort
 *      or shift data, etc.  When we have an even number of points, we take
 *      the shallowest of the points first; this means that we walk the list
 *      alternately to the left and right, starting to the right if the
 *      initial number of points is even, and to the left if the number of
 *      points is odd.  To avoid shifting the data, we just increase the
 *      step after every extraction, until we step off the LHS of the array.
 */
    void queueFlush(const Parameters & parameters);

private:
  /// Queued points in pre-filter
    std::list < DepthAndUncertainty > queue_;

  /// Depth hypotheses currently being tracked
    std::vector < std::shared_ptr < Hypothesis >> depth_hypotheses_;

  /// A nominated hypothesis from the user
    std::shared_ptr < Hypothesis > nominated_hypothesis_;

  /// Predicted depth, or NaN for 'no update', or
  /// INVALID_DATA for 'no information available'
    float predicted_depth_ = INVALID_DATA;

  /// Variance of predicted depth, only valid if the
  /// predicted depth is (as above), meter^2
    float predicted_depth_variance_ = INVALID_DATA;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__NODE_H_
