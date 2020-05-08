/*
 * $Id: cube_node.h 17 2004-02-24 00:02:30Z brc $
 * $Log$
 * Revision 1.2  2004/02/24 00:02:30  brc
 * Fixed a bug with computation of the pseduo-posterior prob. in
 * cube_node_extract_post_depth_uncrt().  The code was using the
 * variance of the hypotheses it was testing to do the computation,
 * rather than the variance of the guide depth.  This leads to the
 * hypotheses being compared to a shifting baseline, and the values
 * don't make a lot of sense.  The new code allows the user to pass
 * in a variance from the outside (presumably from the guide node),
 * and computes the simplified log posterior using it (actually making
 * the computation significantly simpler by elimination of a log()
 * evaluation in the inner loop).  Modifications to the cube_node.h
 * declaration, cube_node.c definition and code, and the call in
 * cube_grid.c (no other calls found).
 *
 * Revision 1.1.1.1  2003/02/03 20:18:42  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2.4.3  2003/01/31 21:51:15  dneville
 * (BRC)  Added code for nomination, reset of nomination, extraction and
 * removal of hypotheses for HyperCUBE surfaces.
 *
 * Revision 1.2.4.2  2003/01/29 13:08:32  dneville
 * (BRC) Added prototypes for nominated hypothesis code.
 *
 * Revision 1.2.4.1  2003/01/28 14:29:44  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.5.2.1  2002/12/15 01:28:55  brc
 * Cosmetic changes to comments, and added the prototype for
 * cube_node_add_null_hypothesis().
 *
 * Revision 1.5  2002/05/10 20:10:34  brc
 * Multiple modifications brought on by the Snow Passage verification.
 * 1. Improved memory estimation code so that the module correctly reports
 * the data sizes required for a single node.
 * 2. Added the facility to set predicted depth and uncertainty for each node
 * (used for slope corrections).
 * 3. Improved node monitoring so that the decisions of the intervention engine
 * can be followed as data is being assimilated.
 * 4. Added code to compute hypothesis strength ratios based on the number of
 * points assimilated into the hypothesis being reported as 'best' (under
 * whatever scheme is being used in the disam. engine), and the 'next best'.
 * For everything except PRIOR disam., this means the number of samples in
 * all of the remaining hypotheses.  The value reported is not the ratio itself
 * but max(0, L-R) where L is an upper limit, and R is the ratio.  This is
 * based on the idea that N_{best}/N_{rest} is approximately odds on the best
 * hypothesis against the rest; anything better than L:1 odds isn't worth
 * reporting, and we want an index where bigger is worse (like uncertainty and
 * number of hypotheses).  Typically, L=5, so odds of 5:1 or more are considered
 * to be a 'pretty strong' certainty that CUBE is making the right decision,
 * even though there are alternative hypotheses.  This makes more sense in
 * trying to determine whether there are problems with any particular location
 * since multiple hypotheses aren't necessarily a problem, as long as CUBE
 * picks the 'right' answer :-)
 * 5.  Modified the data insertion code to do the checking on whether data
 * should be inserted into the node directly.  This actually makes more sense
 * in terms of the definition of the node, and the idea that it represents
 * the depth *here*.  Currently, capture radius is determined as a fixed
 * percentage of the predicted depth at the node, or the depth of the data
 * point itself if no predicted depth is available (which should be the same
 * in the case of good data, and of limited difficulty in bad data).  Also
 * modified code to do slope corrections based on the predicted depth that
 * is recorded in the Sounding structure (stashed by mapsheet_cube.c).  Note
 * that the error due to using the predicted surface depth for slope correction
 * is added here, although the interpolation error is not (it's done when the
 * predicted depth for the sounding is computed in mapsheet_cube.c).
 *
 * Revision 1.4  2001/12/07 20:46:43  brc
 * Changed locations from f32 to f64 so that we have enough dynamic range to
 * do small spacings in UTM coordinates.  Added facility to enumerate all of
 * the hypotheses at a node as an output/debug option.
 *
 * Revision 1.3  2001/09/20 18:02:21  brc
 * Fixed serialisation problems (didn't correctly terminate Hypothesis linked
 * list on deserialisation, didn't write 0 for # hypotheses if there were none
 * [i.e., read-back couldn't work this out]).  Changed to buffered FILE stream
 * for speed.
 *
 * Revision 1.2  2001/08/28 15:59:01  brc
 * Added debug calls to allow the nodes to start and stop writing the output
 * sequence information in a better controlled manner.
 *
 * Revision 1.1  2001/08/21 02:00:08  brc
 * Added headers for CUBE module.
 *
 *
 * File:	cube_node.h
 * Purpose:	Implement CUBE algorithm for depth estimation at a node
 * Date:	27 July 2001
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2001.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __CUBE_NODE_H__
#define __CUBE_NODE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "sounding.h"
#include "cube.h"

typedef struct _cube_node *CubeNode;	/* Estimation parameter set per node */

/* Routine:	cube_node_estimate_mem
 * Purpose:	Estimate number of bytes required for variable memory in a node
 * Inputs:	exp_hyp	Expected number of hypotheses per node
 *			p		CUBE parameters in effect
 * Outputs:	Returns number of bytes for variable portion of node memory
 * Comment:	Note that this is *not* the total memory requirement for the node;
 *			this is only the portion that is allocated when data arrives at a
 *			node.
 */

extern u32 cube_node_estimate_mem(f32 exp_hyp, Cube p);

/* Routine:	cube_node_new
 * Purpose:	Allocate memory for a new node
 * Inputs:	-
 * Outputs:	Pointer to a new CubeNode, or NULL if failed
 * Comment:	New node is initialise per cube_node_init()
 */

extern CubeNode cube_node_new(void);

/* Routine:	cube_node_init
 * Purpose:	Initialise a CubeNode structure
 * Inputs:	node	Node to initialise
 * Outputs:	-
 * Comment:	This initialises the node to a zero length queue, and no depth
 *			hypotheses.  Both are added by the rest of the insertion code only
 *			as required.
 */

extern void cube_node_init(CubeNode node);

/* Routine:	cube_node_reinit
 * Purpose:	Remove memory from a node, and re-initialise
 * Inputs:	node	CubeNode to work on
 * Ouputs:	-
 * Comment:	This releases all dynamically allocated memory for the node, and
 *			then calls cube_node_init().
 */

extern void cube_node_reinit(CubeNode node);

/* Routine:	cube_node_start_debug
 * Purpose:	Turn on debugging for a particular node
 * Inputs:	*node	Node to work with
 * Outputs:	-
 * Comment:	This sets the node's ->write flag, so that it will be included in
 *			output sequence code.
 */

extern void cube_node_start_debug(CubeNode node);

/* Routine:	cube_node_stop_debug
 * Purpose:	Turn off debugging for a particular node
 * Inputs:	node	Node to work with
 * Outputs:	-
 * Comment:	This just resets the node's ->write flag.
 */

extern void cube_node_stop_debug(CubeNode node);

/* Routine:	cube_node_serialise
 * Purpose:	Write a binary description of a node to file
 * Inputs:	node	Pointer to the CubeNode structure to use
 *			p		Cube parameter structure to work from
 *			fd		File descriptor on which to write
 * Outputs:	True if write worked, else False
 * Comment:	This routine does *not* flush the input pre-filter queue into the
 *			estimator before the node is written, *and does not write the
 *			queue into the file*.  The reason for not flushing the queue is that
 *			by the time the data is being flushed to disc, the remainder of the
 *			points in the queue are probably pretty bad, and flushing them
 *			would cause more problems than they would fix.  This, of course,
 *			means that the estimation process isn't the same if you do it in
 *			two parts, and particularly, you may experience some problems if
 *			you have very limited data quantities and flush the surface to disc
 *			(in which case all data will be lost).  People who are interested in
 *			this and don't mind the noise can call cube_node_flush() before
 *			calling the serialisation code.
 */

extern Bool cube_node_serialise(CubeNode node, Cube p, FILE *fd);

/* Routine: cube_node_deserialise
 * Purpose:	Read a node description from file
 * Inputs:	fd		File descriptor to read from
 *			node	Node description to read into
 *			p		Cube parameter structure
 * Outputs:	False if read failed, otherwise True
 * Comment:	This routine does not check whether the node passed in has a queue
 *			or a hypothesis sequence before it overwrites them.
 */

extern Bool cube_node_deserialise(FILE *fd, CubeNode node, Cube p);

/* Routine:	cube_node_add_hypothesis
 * Purpose:	Add a specific depth hypothesis to the current list
 * Inputs:	node	CubeNode to work on
 *			depth,
 *			var		Initialisation variables to use
 * Outputs:	True if the hypothesis was added, otherwise False
 * Comment:	Call-through to cube_node_new_hypothesis() for external users.
 */

extern Bool cube_node_add_hypothesis(CubeNode node, f32 depth, f32 var);

/* Routine:	cube_node_add_null_hypothesis
 * Purpose:	Add a specific depth hypothesis to the current list, with
 *			number of samples set to zero.
 * Inputs:	node	CubeNode to work on
 *			depth,
 *			var		Initialisation variables to use
 * Outputs:	True if the hypothesis was initialised, otherwise False
 * Comment:	Call-through to cube_node_new_hypothesis() for external users,
 *			with n_j = 0 rather than n_j = 1.  This is typically used for
 *			initialisation surface hypotheses, so that they can be
 *			easily differentiated from hypotheses based on data.  In the
 *			read-back code, hypotheses with n_j = 0 are not allowed to be
 *			chosen.
 */

extern Bool cube_node_add_null_hypothesis(CubeNode node, f32 depth, f32 var);

/* Routine:	cube_node_remove_hypothesis
 * Purpose:	Remove a particular hypothesis from the node
 * Inputs:	node	CubeNode to operate on
 *			depth	The depth of the hypothesis to remove
 * Outputs:	True if successful, False if the hypo couldn't be found
 * Comment:	This removes a hypothesis from a CubeNode permanently.  The hypothesis
 *			to remove is determined by the depth provided.  The algorithm allows
 *			up to HYP_SELECT_TOLERANCE difference between this depth and the
 *			depth in the hypothesis, but will only remove the hypothesis if there
 *			is a unique match to the depth.  Tolerance is nominally a metric
 *			whisker (slightly smaller than the imperial), or 0.01m.
 */

extern Bool cube_node_remove_hypothesis(CubeNode node, f32 depth);

/* Routine: cube_node_nominate_hypothesis
 * Purpose:	Nominate an extant hypothesis as 'the whole truth' to be preserved
 *			inviolate until more data is added to the node.
 * Inputs:	node	CubeNode to operate on
 *			depth	Depth of the hypothesis to preserve
 * Outputs:	True if set, False if the hypothesis couldn't be found (see comment)
 * Comment:	This searches the list of hypotheses for one with depth within
 *			a whisker of the specified value --- in this case, a metric
 *			whisker, which is the same as 0.01m.  The hypothesis that matches,
 *			or the one that minimises the distance if there is more than one,
 *			is marked as 'nominated', and is reconstructed every time without
 *			running the disam. engine until the user explicitly resets the
 *			over-ride (with cube_node_reset_nomination) or more data is added to
 *			the node.
 */

extern Bool cube_node_nominate_hypothesis(CubeNode node, f32 depth);

/* Routine: cube_node_reset_nomination
 * Purpose:	Remove the nomination lock on a node
 * Inputs:	node CubeNode to work on
 * Outputs:	-
 * Comment:	This removes any nomination currently in effect, forcing the
 *			disam. engine to run for this node again.
 */

extern void cube_node_reset_nomination(CubeNode node);

/* Routine:	cube_node_is_nominated
 * Purpose:	Determine whether the result from a node was due to nomination or not
 * Inputs:	node	CubeNode to work on
 * Outputs:	True if the node has a nominated hypothesis marked, otherwise False
 * Comment:	-
 */

extern Bool cube_node_is_nominated(CubeNode node);

/* Routine:	cube_node_set_preddepth
 * Purpose:	Set the node's notion of a 'predicted depth'
 * Inputs:	node	CubeNode to operate on
 *			depth	Depth to set (meters)
 *			var		Variance of depth to set (meter^2)
 * Outputs:	-
 * Comment:	This is simply a matter to making a predicted depth setter available
 *			to the outside world on the basis that the cube_node...() code
 *			should be doing this, rather than some other part of the cube...()
 *			module.
 *				Note that the following conventions are applied in the
 *			integration code:
 *				pred_depth == NaN => Do not incorporate any data into node
 *				pred_depth == INVALID => No prediction of depth available (so
 *										 node has to guess, and doesn't make
 *										 slope corrections).
 */

extern void cube_node_set_preddepth(CubeNode node, f32 depth, f32 var);

/* Routine: cube_node_insert
 * Purpose:	Insert a single depth value into the node
 * Inputs:	*node	Pointer to the node to operate on
 *			node_x,
 *			node_y	Position of the node in space
 *			*snd	Sounding data to insert
 *			p		Pointer to (opaque) data structure for the algorithm params
 * Outputs:	True if inserted OK, otherwise False
 * Comment:	This computes the variance scale factor for the new data, and then
 *			sends the data into the estimation queue, building it if required.
 */

extern Bool cube_node_insert(CubeNode node, f64 node_x, f64 node_y, f64 dist_sq,
							 Sounding *snd, Cube p);

/* Routine: cube_node_extract_depth_unct
 * Purpose:	Extract depth and uncertainty of current best estimate
 * Inputs:	node	Node to work on
 * 			p		Parameters structure to use for data
 * Outputs:	*depth	Depth of best known hypothesis
 *			*unct	Uncertainty of best known hypothesis
 *			True if a valid depth was extracted, otherwise False
 * Comment:	Note that there may be no hypotheses available in the node at the
 *			time this routine is called.  In this case, the system reports the
 *			default null depth and variance, and returns False.  This routine
 *			can also be used to extract just depth, just variance or both by
 *			passing a NULL pointer rather than a valid address for the output
 *			variables.
 */

extern Bool cube_node_extract_depth_unct(CubeNode node, Cube p,
										 f32 *depth, f32 *unct, f32 *ratio);

/* Routine:	cube_node_extract_depth
 * Purpose:	Extract best depth from current hypotheses
 * Inputs:	node	CubeNode to work on
 *			p		Cube parameters structure to work with
 * Outputs:	*depth	Value of current best depth estimate
 *			True if depth estimate is valid, else False (and *depth is set to
 *			the current default null depth).
 * Comment:	This computes the 'best' hypothesis by the 'longest held' algorithm,
 *			that is: the hypothesis which has absorbed the most data is most
 *			likely the correct one.  This is a pseudo-MAP algorithm, and is the
 *			only one which can be implemented without spatial context from other
 *			surrouning nodes.
 */

extern Bool cube_node_extract_depth(CubeNode node, Cube p, f32 *depth,
									f32 *ratio);

/* Routine:	cube_node_extract_unct
 * Purpose:	Extract best uncertainty from current hypotheses
 * Inputs:	node	CubeNode to work on
 *			p		Cube parameters structure to work with
 * Outputs:	*unct	Value of current best depth estimate
 *			True if depth estimate is valid, else False (and *depth is set to
 *			the current default null depth).
 * Comment:	This computes the 'best' hypothesis by the 'longest held' algorithm,
 *			that is: the hypothesis which has absorbed the most data is most
 *			likely the correct one.  This is a pseudo-MAP algorithm, and is the
 *			only one which can be implemented without spatial context from other
 *			surrouning nodes.
 */

extern Bool cube_node_extract_unct(CubeNode node, Cube p, f32 *unct,
								   f32 *ratio);

/* Routine:	cube_node_extract_closest_depth_unct
 * Purpose:	Extract the depth estimate which is closest to the supplied guide
 *			values in minimum error sense
 * Inputs:	node	CubeNode to work on
 *			p		Cube parameter structure
 *			depth
 *			var		Guide data points
 * Outputs:	*z, *v	Closest depth hypothesis at this node
 * Comment:	If there are no depth hypotheses at this node, then the default
 *			values are returned instead.
 */

extern Bool cube_node_extract_closest_depth_unct(CubeNode node, Cube p,
												 f32 depth, f32 var,
												 f32p z, f32p v, f32p ratio);

/* Routine:	cube_node_extract_post_depth_unct
 * Purpose:	Extract a posterior weighted best depth hypothesis using a guide
 *			node depth supplied
 * Inputs:	node	CubeNode to work on
 *			p		Cube parameter structure
 *			depth	Guide depth
 *			var	Guide variance
 * Outputs:	*z, *v set to best known depth hypothesis
 *			True if extraction worked, otherwise False
 * Comment:	If there are no hypotheses, or only one, then this call acts the
 *			same as a call to cube_node_extract_depth_unct().
 */

extern Bool cube_node_extract_post_depth_unct(CubeNode node, Cube p, f32 depth, f32 var,
									   		  f32p z, f32p v, f32p ratio);

/* Routine:	cube_node_queue_flush_node
 * Purpose:	Flush a single node queue into the estimato input sequence
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
 *				If that doesn't make any sense to you, you shouldn't be calling
 *			this routine.  Seriously.  Calling this routine without a subtle
 *			(and quite possibly cunning) understanding of what is going on in
 *			the bowels (and I use the word advisedly) of the CUBE algorithm is
 *			very likely to result in some extraordinarily strange results.  You
 *			should consider this fair warning and proceed with extreme caution.
 */

extern void cube_node_queue_flush_node(CubeNode node, Cube param);

/* Routine:	cube_node_extract_nhyp
 * Purpose:	Extract the number of hypotheses currently in the node
 * Inputs:	node	CubeNode to work on
 *			p		Cube parameter structure to work with
 * Output:	Number of hypotheses currently being tracked
 * Comment:	The actual number of hypotheses may be zero if no data has been
 *			presented to the node.  This is also true even if a base hypothesis
 *			has been added through an initialisation call, since such hypotheses
 *			(with n_j = 0 [i.e., no samples]) are not counted by this method.
 */

extern f32 cube_node_extract_nhyp(CubeNode node, Cube p);

/* Routine:	cube_node_dump_hypotheses
 * Purpose:	Dump a list of hypotheses to output for use in debugging.
 * Inputs:	node	CubeNode to dump
 * Outputs:	-
 * Comment:	-
 */

extern void cube_node_dump_hypotheses(CubeNode node);

/* Routine:	cube_node_enumerate_hypotheses
 * Purpose:	Generate a set of Soundings for the hypotheses in a node
 * Inputs:	node	CubeNode to enumerate
 * Outputs:	Returns pointer to Sounding array with hypotheses, or NULL if
 *			there is a problem, or no hypotheses.
 *			*nhyp	Set to number of hypotheses returned.  Set to -1 on error.
 * Comment:	This fills in the Sounding structure as well as possible.  The
 *			following substitutions are made:
 *				snd->file_id		Set to number of points in hypothesis
 *				snd->beam_number	Set to hypothesis number in sequence
 *				snd->backscatter	Not set.
 *				snd->east			Not set.
 *				snd->north			Not set.
 *				snd->depth			Set to hypothesis depth
 *				snd->range			Set to hypothesis depth
 *				snd->dz				Set to hypothesis variance
 *				snd->flags			Not set.
 *			Note that a pure node does not have any idea about georeferencing,
 *			hence the east/north not being set.  All values are initialised to
 *			zero when the memory is allocated.
 */

extern Sounding *cube_node_enumerate_hypotheses(CubeNode node, s32 *nhyp);

#ifdef __cplusplus
}
#endif

#endif
