#ifndef CUBE_BATHYMETRY_NODE_H
#define CUBE_BATHYMETRY_NODE_H

#include "hypothesis.h"
#include <vector>
#include <memory>
#include "common.h"
#include "parameters.h"
#include "sounding.h"
#include <list>

namespace cube
{

class Node
{
public:

  ///	Add a specific depth hypothesis to the current list
  ///
  /// depth:	Depth to set for hypothesis (meter)
  /// variance		Variance to set for hypothesis	(meter^2)
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
  bool update(float depth, float variance, const Parameters& parameters);

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
  std::shared_ptr<Hypothesis> bestHypothesis(float depth, float variance);


  /// Insert a single depth value into the node
  /// Inputs:
  ///    node_x, node_y: Position of the node in space
  ///    sounding: Sounding data to insert
  ///    parameters: Algorithm parameters
  /// Returns:
  ///    True if inserted OK, otherwise False
  /// This computes the variance scale factor for the new data, and then
  /// sends the data into the estimation queue, building it if required.
  bool insert(double node_x, double node_y, double distance_squared, const Sounding &sounding, const Parameters & parameters);


  /// Insert points into the queue of estimates, and insert point into
  /// filter sequence if queue is filled
  /// Inputs:
  ///   depth: Depth estimate
  ///   var: Estimate of depth estimate variance
  ///   parameters: Algorithm parameters
  /// Outputs:	True if inserted, else False (no memory for queue)
  /// This inserts the depth given into the queue associated with the
  /// specified node, creating the queue if required.  After the queue
  /// has been primed (i.e., filled with estimates), on each call this
  /// routine extracts the median value from the queue (via a call to
  /// mapsheet_nodal_queue_insert()) and then inserts it into the CUBE
  /// input sequence (via a call to mapsheet_nodal_update_node()).
  /// Note that this algorithm means that the queue will always be
  /// full, and hence must be flushed before extracting any depth
  /// estimates (this can also be done to save memory).
  bool queueEstimate(float depth, float variance, const Parameters & parameters);

  /* Routine: cube_node_extract_depth_unct
  * Purpose:	Extract depth and uncertainty of current best estimate
  * Inputs:	node	Node to work on
  * 			p		Parameters structure to use for data
  * Outputs:	*depth	Depth of best known hypothesis
  *			*unct	Uncertainty of best known hypothesis
  *			*ratio	Hypothesis strength ratio --- degree of belief in this
  *					hypothesis over the remainder
  *			True if a valid depth was extracted, otherwise False
  * Comment:	Note that there may be no hypotheses available in the node at the
  *			time this routine is called.  In this case, the system reports the
  *			default null depth and variance.  This routine
  *			can also be used to extract just depth, just variance or both by
  *			passing a NULL pointer rather than a valid address for the output
  *			variables.
  */
  DepthAndUncertainty extractDepthAndUncertainty(const Parameters & parameters);

  /* Routine:	cube_node_choose_hypothesis
  * Purpose:	Choose the current best hypothesis for the node in question
  * Inputs:	*list	Pointer to the list of hypotheses
  *			*best	Pointer to buffer for single best hypothesis
  * Outputs:	True if the sort took place, otherwise False (no buffer space).
  * Comment:	In this context, `best' means `hypothesis with most samples',
  *			rather than through any other metric.  This may not be the `best'
  *			until all of the data is in, but it should give an idea of what's
  *			going on in the data structure at any point (particularly if it
  *			changes dramatically from sample to sample).
  *				Note that this code does not check that there actually is a
  *			hypothesis list to sort ... expect Very Bad Things (tm) to happen
  *			if this isn't dealt with externally.
  */
  std::shared_ptr<Hypothesis> chooseHypothesis();

  /* Routine:	cube_node_truncate
 * Purpose:	Truncate a buffered sequence to reject outliers
 * Inputs:	node	CubeNode to work through
 *			*p		CubeParam structure to use for quotient limit
 * Outputs:	The node->queue[] structure is updated to remove any suspect
 *			outliers.
 * Comment:	The definition of 'suspect' depends on the value of the quotient
 *			limit set in the parameters structure.  In general, the higher the
 *			value, the more extreme must the departure be for the point to be
 *			considered an outlier.  In theory, the distribution of the quotient
 *			values computed should be approximately a Fisher F(1,N-2) where
 *			there are N points in the input sequence.  The values of the
 *			quotients are always positive, and monotonically increasing for
 *			worse outliers; therefore, one-sided critical values should be
 *			considered.
 */
  void truncate(const Parameters & parameters);

/* Routine:	cube_node_queue_flush_node
 * Purpose:	Flush a single node queue into the estimation input sequence
 * Inputs:	node	CubeNode to process
 *			*param	CUBE filter parameters structure
 * Outputs:	-
 * Comment:	This flushes the queue into the input sequence in order (i.e., take
 *			current median, resort, repeat).  Since the queue is always sorted,
 *			we can just walk the list in order, rather than having to re-sort
 *			or shift data, etc.  When we have an even number of points, we take
 *			the shallowest of the points first; this means that we walk the list
 *			alternately to the left and right, starting to the right if the
 *			initial number of points is even, and to the left if the number of
 *			points is odd.  To avoid shifting the data, we just increase the
 *			step after every extraction, until we step off the LHS of the array.
 */
  void queueFlush(const Parameters & parameters);

private:
  /// Queued points in pre-filter
  std::list<DepthAndUncertainty> queue_;

  /// Depth hypotheses currently being tracked
  std::vector<std::shared_ptr<Hypothesis> > depth_hypotheses_;

  /// A nominated hypothesis from the user
  std::shared_ptr<Hypothesis> nominated_hypothesis_;

  /// Predicted depth, or NaN for 'no update', or
  /// INVALID_DATA for 'no information available'
  float predicted_depth_ = INVALID_DATA;

  /// Variance of predicted depth, only valid if the
  /// predicted depth is (as above), meter^2
  float predicted_depth_variance_ = INVALID_DATA;
};

}  // namespace cube

#endif
