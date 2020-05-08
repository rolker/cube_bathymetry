/*
 * $Id: cube_node.c 26 2006-01-20 16:44:43Z brc $
 * $Log$
 * Revision 1.6  2006/01/20 16:44:43  brc
 * Added the (compile time) facility to track the input sample variance of the
 * soundings being incorporated into a hypothesis, and to report this instead of
 * CUBE's internal (posterior variance) estimate of uncertainty.  This is controlled
 * by the TRACK_IP_SAMP_VAR and TRACK_MAX_VAR macros.  When
 * either is defined, an extra variable is included in the Hypothesis structure to
 * handle the estimates, and extra code is added to the estimator to update it
 * (in cube_node_update_hypothesis()).  When the user requests uncertainty
 * estimates, extra code is also included to select either the input sample
 * estimate of variance (TRACK_IP_SAMP_VAR) or the maximum of this
 * value and CUBE's internal estimate (TRACK_MAX_VAR).  In either case, the
 * value being reported is converted to a standard deviation first, and then
 * rescaled to the appropriate CI using the normal configuration parameters for
 * this module.  The default behaviour is to define TRACK_IP_SAMP_VAR,
 * but this is context specific, and may not be what you want.  The behaviour
 * of the file as it is before this change can be had by undefining both macros.
 * Note that this does not affect the core of the CUBE estimator algorithm in
 * any way (i.e., how soundings are treated, hypotheses are constructed and
 * depths are estimated) --- only the value that is reported when the user asks
 * for the estimated uncertainty at any hypothesis.
 *
 * Revision 1.5  2004/09/22 15:52:39  brc
 * Testing CUBE commit log interface after server upgrade.
 *
 * Revision 1.4  2004/09/22 15:34:52  brc
 * Fixed non-terminating loop in cube_node_nominate_hypothesis() (found and
 * contributed by B. Lamey, CARIS).
 *
 * Revision 1.3  2004/02/24 00:02:30  brc
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
 * Revision 1.2  2003/07/23 00:34:56  brc
 * Merged modifications from development stream.
 *
 * Revision 1.8.2.5  2003/04/16 18:17:18  brc
 * Added facility to control the node capture radius computation from
 * the outside.  This is the radius about a node from which the
 * algorithm will accept data, specified as a scalar multiple of the
 * target or predicted depth (whichever is available).
 *
 * Revision 1.8.2.4  2003/03/05 20:35:10  brc
 * Added minimum spreading radius so that shoal points are not
 * truncated arbitrarily.  Ensured that negative 'depths' are correctly
 * handled.
 *
 * Revision 1.1.1.1  2003/02/03 20:18:42  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2.4.5  2003/01/31 22:56:18  dneville
 * (BRC) Added check for nominated hypothesis reset to the hypothesis
 * removal routine --- if the nominated hypothesis is removed, so is the
 * indication of nomination.
 *
 * Revision 1.2.4.4  2003/01/31 22:52:21  dneville
 * (BRC) Added check for nominated hypothesis reset to the hypothesis
 * removal routine --- if the nominated hypothesis is removed, so is the
 * indication of nomination.
 *
 * Revision 1.2.4.3  2003/01/31 21:51:15  dneville
 * (BRC)  Added code for nomination, reset of nomination, extraction and
 * removal of hypotheses for HyperCUBE surfaces.
 *
 * Revision 1.2.4.2  2003/01/29 13:11:00  dneville
 * (BRC) Added code for nominated hypotheses, and modified serialisation to
 * put the reference number for the nominated hypothesis (or zero for none)
 * after all of the hypotheses are written to file.  Note that this means that we
 * now have to re-write the hypothesis reference numbers when the node is
 * serialised (which doesn't really matter, since they shouldn't be relied on
 * anyway).  However, this resets the sequencing so that the debuging code
 * now reports the right number and order of hypotheses...
 *
 * Revision 1.2.4.1  2003/01/28 14:29:44  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.8.2.3  2002/12/15 01:28:15  brc
 * Added facility to generate base (null) hypotheses separate from the usual calls
 * to add_hypothesis().  This allows them to be specified with an n_samples count
 * of zero, so that they can be identified later.  Rest of the code was also modified
 * to ensure that such hypotheses are never chosen for reporting as reconstructed
 * values (otherwise things can get a little confusing).  Added blunder filters to cull
 * deep points more than a fixed depth, percentage of predicted depth, or
 * fixed scale of predicted uncertainty away from the predicted depth.  Removed
 * the interpolation error addition, since it appeared to be making all of the errors
 * way too big --- and it shouldn't feed through like this.  This is really waiting on a
 * better solution, since it should be fed in somewhere in theory.
 *
 * Revision 1.8.2.2  2002/08/15 18:08:19  brc
 * Updates to make the windows port compile and operate on Unix, and to make
 * sure that blendsurfaces is added to the project.
 *
 * Revision 1.8.2.1  2002/07/14 02:20:46  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.8  2002/06/16 02:27:25  brc
 * Added initialisation for the predicted surface depth and variance as part of
 * the normal CubeNode initialisation --- this makes sure that if there is not
 * a predicted surface, the interpolation module in cube_grid.c can spot it
 * and not use the data, and hence not do bogus corrections.
 *
 * Revision 1.7  2002/05/10 20:10:34  brc
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
 * Revision 1.6  2001/12/07 20:46:43  brc
 * Changed locations from f32 to f64 so that we have enough dynamic range to
 * do small spacings in UTM coordinates.  Added facility to enumerate all of
 * the hypotheses at a node as an output/debug option.
 *
 * Revision 1.5  2001/10/15 23:05:38  brc
 * Fixed a couple of slepping mistooks, fixed precedence problem with modulus
 * operation, and commented out a raise(SIG_QUIT) used for debugging (desperate
 * straits division).
 *
 * Revision 1.4  2001/09/20 18:02:21  brc
 * Fixed serialisation problems (didn't correctly terminate Hypothesis linked
 * list on deserialisation, didn't write 0 for # hypotheses if there were none
 * [i.e., read-back couldn't work this out]).  Changed to buffered FILE stream
 * for speed.
 *
 * Revision 1.3  2001/08/28 15:59:01  brc
 * Added debug calls to allow the nodes to start and stop writing the output
 * sequence information in a better controlled manner.
 *
 * Revision 1.2  2001/08/21 02:25:19  brc
 * Removed extraneous variables which are not being used to avoid compiler
 * warning messages.
 *
 * Revision 1.1  2001/08/20 22:40:19  brc
 * Added CUBE algorithm as a callable entity, rather than being tied into
 * the mapsheet implementation.  This means that it is significantly easier
 * to use from other systems, and to implement in different ways (e.g., node-
 * wise or grid-wise) as might be required.
 *
 *
 * File:	cube_node.c
 * Purpose:	Implement CUBE algorithm for surface estimation at a node
 * Date:	30 July 2001
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2001.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 *
 * ----------------------------------------------------------------------------
 * Log from mapsheet_nodal.c follows:
 *
 * Revision 1.7  2001/05/16 21:08:01  brc
 * Added float.h for FLT_MAX, DBL_MAX, etc. under Linux
 *
 * Revision 1.6  2001/05/14 21:54:24  brc
 * Major modifications to support multi-hypothesis estimation and other things:
 * 1.  Added parameter executive code to make module 'params'-aware, and hence
 *     making it possible to modify the algorithm control parameters from the
 *     outside without recompiling the code (huzzah!)  This also means that the
 *     static parameters have become variables, and the variables are now used
 *     in the code.  The KalParam structure has a few new variables as well.
 *     The parameters file is read and parsed into the default_param file
 *     static variable, which is then cloned every time a new estimation
 *     sheet is constructed.
 * 2.  Restructured extraction code to allow depth and uncertainty to be
 *     generated together (with retrofit to the base call sequence for
 *     compatibility).  This can make a very significant difference when the
 *     multi-hypothesis code has to disambiguate a set of depth hypotheses,
 *     which can take a while.
 * 3.  Made the mapping from standard error to confidence interval a full
 *     parameter (i.e., an arbitrary scale factor).  The parameter reading
 *     code understands 68% (1se), 90%, 95% and 99%.
 * 4.  Modified the depth integration code so that the IHO survey order
 *     specified when the mapsheet is constructed is used to determine, per
 *     depth sounding, the maximum allowable vertical error.  This is used
 *     to compute the allowable radius of influence for the data point, making
 *     the integration more reasonable.
 * 5.  The multi-hypothesis estimation itself:
 *     a.  A hypothesis is a separate depth track, held in a linked list of
 *         structures at each DepthNode structure in the estimation grid.
 *     b.  DepthNodes now only contain minimal information and the pre-queue,
 *         with hypotheses being spawned and added as required.
 *     c.  When new data arrives, it is matched to the current list of
 *         hypotheses using a minimum standardised prediction error (like the
 *         monitoring code).  The data point is then compared with the closest
 *         current hypothesis using the standard monitoring code.  If the
 *         data looks good, it is integrated; if it would cause an exception,
 *         a new hypothesis is initialised with the current data point and
 *         then added to the linked list already in existence.
 *     d.  Since we can have multiple hypotheses, we need a way to generate
 *         a single depth/uncertainty surface (i.e., choosing the `best' or
 *         `most probable' hypothesis).  Three methods have been developed:
 *         pseudo-MAP hypothesis probability, pseudo-likelihood based on the
 *         closest single-hypothesis estimation node, and pseudo-posterior
 *         based on the product of the previous two.  The method to use is
 *         stored in the KalParam structure, and the extraction code auto-
 *         matically chooses the correct routines.  In most cases, the
 *         simple prior will work; in very heavy noise, the likelihood method
 *         seems to work well.  The posterior method seems to make little
 *         improvement over the likelihood method in the case of noisy data
 *         where it might be expected to be more appropriate.
 *
 * Revision 1.5  2001/04/10 23:14:08  brc
 * Multiple modifications to look at robustness in the estimation sequence.
 * 1.  Added facility to estimate the standard deviation of the input sample
 * sequence in the node in order to give a backward error measure of the
 * expected noise.  Added facility to have this read back from the mapsheet.c
 * interface (only works with this estimation sub-module).
 * 2.  Turned on the continual Eeg-style culling, although the utility of this
 * is still in doubt.  The values which are computed depend strongly on the
 * particular dataset, and hence the culling threshold required is not very
 * clearly defined.  Jorgen's suggestion of q_max = 30 would result in no data
 * being culled in Pinnacles EM1002 or Portsmouth SB8101, for example, where
 * the values are much closer to 1.0.  Added lots of debug code for this,
 * controlled by the __EEG_DEBUG__ macro (very noisy when on).
 * 3.  Added a sheet reinitialisation feature where arbitrary data can be used
 * to initialise the mapsheet, using a particular uncertainty across the entire
 * sheet.  Added a mask element so that a second binary data array can be used
 * to select which parts of the sheet get initialised.  This can be used to
 * fake a previous dataset so that the ideas of recursive updating can be tested.
 * 4.  Added a discounting structure (West & Harrison, 'Bayesian Forecasting
 * and Dynamic Models', Ch. 2ff.) rather than a fixed system variance.  This
 * allows the model to adapt to the quality of the data, allowing faster adaption
 * when the estimates are poor, and better smoothing when the model is clean.
 * W&H suggest that discounting rates of about 0.8-1.0 are reasonable; the
 * default value of 0.9 seems to be good, at least for Portsmouth, where this
 * has been tested most.
 * 5.  Added West & Harrison's model monitoring code (as above, Ch.11).  This
 * is designed to determine when a model stops looking like the input data,
 * and hence when some intervention is required.  The model is based on
 * sequentially computed Bayes factors, with an additional monitoring of how
 * long the current sequence of `bad' factors have been going on, so that it
 * can also detect slow drifts away from the current model.  The alternate
 * model here (Bayes factors compare two models) is one based on detecting a
 * level shift (i.e., d.c. shift in depth, e.g., burst mode sounder failure), and
 * the parameters are the expected shift, h, the Bayes factor threshold, tau, and
 * the expected total length of a bad drift sequence, M.  The defaults are based
 * on the W&H analysis, which should be good everywhere since it based on a
 * unit standard normal.  The defaults are h=4.0, tau=0.135 and M=5.  This
 * model remediates indicated intervention points by resetting the current track
 * to the input value which caused the intervention.  This means that the chain
 * moves about a great deal more quickly, but also that the last estimate may
 * not be the `best' one.  However, the number of interventions gives a good
 * indication that something is going wrong.  In Portsmouth, about 99.5% of the
 * time there are none; the remainder of the interventions are clustered around
 * regions known to be bad, which is encouraging.  This is also the basis for
 * a model which maintains multiple hypotheses.
 *
 * Revision 1.4  2001/02/10 18:12:24  brc
 * Modifications to support the new error modeling structure, where errors in
 * depth and location are computed externally as the soundings are imported,
 * rather than trying to do this in the mapsheet code.
 *
 * Revision 1.3  2000/12/03 20:27:43  brc
 * Multiple modifications to improve robustness of the algorithm in the face
 * of outliers early in the KF input sequence.  Added a pre-filtering queue to
 * each node, or fixed by user specified length.  Data are added to the queue
 * in depth sorted order, shallowest at the top until the queue is filled.  Then,
 * as a new point becomes available, the median depth is removed from the queue
 * and inserted into the KF, with the new value being inserted into the queue
 * again to maintain the sort order.  Linear search is used to keep the queue in
 * order since (a) it's usually quite short, and (b) is always in sorted order,
 * so the algorithm is almost linear.  Next, added Jorgen Eeg's outlier detection
 * mechanism to deal with the case where the input sequence for the KF is quite
 * short, but still has wild fliers.  This works by computing sums of squared
 * differences on a leave-one-out basis, ranking the points according to a
 * quotient which is approximately F distributed.  This is currently only applied
 * when the pre-filter queue is flushed into the KF before the surface is read
 * back, and hence only has an effect with very limited data.  However, it could
 * also be used on a rolling basis to keep the queue free of duff points, rather
 * than having them accumulate in the outer reaches of the pre-filter queue.  This
 * would need to have a way to record which points are being culled, however,
 * to avoid any hydrographic concerns.  To support these two modifications, also
 * added a finalisation concept through mapsheet_nodal_finalise().  This causes
 * all of the queues to be processed and flushed into the KF, freeing up the
 * memory associated with them (queues are only added when there is extant data).
 * Note that this means that there is a potential problem with consistency of the
 * algorithm in that a sequence applied all at once, and a sequence applied in
 * two parts with a mapsheet_nodal_finalise() between will not necessarily give
 * the same answer.  The user can avoid this if they are willing to accept that
 * the depth/uncert. surface they retrieve may not be the current best estimate,
 * simply by not calling the finalisation code.  However, finalisation code is
 * always called when the mapsheet is saved, so the sheet would have to remain
 * in memory at all times.  This is being designed with a view to a display prog.
 * to show current estimate on screen as data is being incorporated.
 *     The error model has also been modified to ensure that in <30m of water,
 * the error returned is not artificially small.  Otherwise, rogue points which are
 * reported as being very shallow can significantly bias the estimation if they
 * occur early in the input sequence (i.e., depth 1m +- 0.01m when the true depth
 * is 18m +- 0.18m).  This makes the system closer to IHO V3 anyway for touchdown
 * variance limits, which is quite useful.  Typical EM1000 accuracy in 25m is about
 * 0.25m at nadir (90% conf.), so setting this to the IHO 0.30m is appropriate.
 * The code also now reads back 90% confidence limits for the uncertainty surface
 * rather than straight std. err.  This assumes that the errors are Gaussian so
 * that it can do a simple conversion of 90% = 1.64*err.
 *     Finally, improved comments on the function headers and added a debug structure
 * to report the node's input sequence, the KF's input sequence (essentially a
 * peturbation of the node's inputs) and the buffer flush sequence.  Controlled
 * by a .write in the node's data structure, set in mapsheet_nodal_init_grid().
 *
 * Revision 1.2  2000/08/24 15:11:35  brc
 * Modified numerous files to allow the code to compile cleanly under Linux.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:25  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	mapsheet_nodal.c
 * Purpose:	Depth update algorithm based on nodal estimate of depth and
 *			prediction uncertainty
 * Date:	8 July 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include <signal.h>
#include "stdtypes.h"
#include "error.h"
#include "cube.h"			/* Parameter structure */
#include "cube_node.h"		/* Prototypes */
#include "cube_private.h"	/* Pulls in data structures */
#include "sounding.h"		/* Interface to soundings structures */
#include "mapsheet.h"

#ifdef linux
#define FSQRT (float)sqrt
#else
#define FSQRT fsqrt
#endif

/* WIN32 has _isnan() rather than isnan().  Redefine to keep the code
 * simpler in the body.
 */
#ifdef WIN32
#define isnan _isnan
#endif

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

#define CONF_99PC				2.56	/* Scale for 99% CI on Unit Normal */
#define CONF_95PC				1.96	/* Scale for 95% CI on Unit Normal */

#define MAX_HYPOTHESIS_RATIO	5.0	/* Ceiling to place on hypothesis
										 * strength ratios.
										 */
#define HYP_SELECTION_TOLERANCE	0.01	/* A metric whisker (see
										 * cube_node_nominate_hypothesis) */


/* ---- DEBUG ---- DEBUG ---- DEBUG ---- DEBUG ---- DEBUG ---- DEBUG ----
 * The following flags, along with the generic __DEBUG__ turn on component
 * parts of the debuging structure for the mapsheet integration algorithm.  All
 * of the code is selected by the grid.write element for the nodes required;
 * if this is set to True, then the node debug information is reported.  Note
 * that many of the routines do not indicate which node is being debuged, and
 * hence it can get a little confusing when you have more than one set ....
 *     Note also that some of the debuging routines add files into the local
 * filespace with default names, so that if you have something in the way, it's
 * going to get blitzed on each run.  Set write filter options at the grid
 * initialisation stage in mapsheet_nodal_init_grid().
 */

/* Generic debuging of the source.  This actually turns on a great deal of code
 * in the section that deals with reporting data coming in, all going to the
 * standard error reporting stream (typically stderr).  This can make the code
 * quite noisy.
 */

#undef __DEBUG__

/* This turns on debuging of the hypothesis selection process which takes place
 * when the mapsheet is queried for a depth estimate at each node.  This reports
 * the number of hypothesis, and the decisions made to select one (if more than
 * one exists).
 */
#define __HYP_DEBUG__

/* This is a little badly named, since it really debugs the input sequence
 * pre-filter structure.  That is, when this is defined, the code spits out
 * to "./debug-eeg.txt" a list of pre-filter structure samples before and
 * after each fill() and insert() call so that the contents of the filter
 * buffer can be tracked.  The code is activated by the node->write element
 * of the required node being set True.  Of course, this generates a great
 * deal of output, so it can be turned on and off independently of the rest
 * of the debug output sequences.  Expect several hundred kbyte for even
 * moderately short input sequences (1000-1500 points).
 */
#undef __EEG_DEBUG__

#ifdef __EEG_DEBUG__
static FILE *debug_eeg = NULL;
#endif

/* This turns on tracing of the input sequence to the algorithm, and the values
 * of the hypotheses as they are tracked.  When enabled, files with default
 * names of 'debug-ip.txt', 'debug-kfs.txt', and 'debug-flush.txt' are
 * generated in the current directory.  As well as being useful for debugging
 * purposes, these are also good to use for testing new modifications to the
 * algorithm without having to run on a whole area of data.
 */

#define __EST_TRACE__

static char *modname = "cube_node";
static char *modrev = "$Revision: 26 $";

/* Queue element using in median filtered pre-filter queue structure. */

typedef struct _queue {
	f32	depth;
	f32	var;
} Queue;

/*
 * Depth hypothesis structure used to maintain a current track on the depth
 * at the node in question.  This contains current estimates of depth and
 * variance, one-step predictions and the monitoring variables.  The number
 * of elements incorporated in the node is also recorded so that a pseudo-MAP
 * estimate of best model (or at least most frequently visited model) can be
 * computed.
 *
 */
 
/* The following two defines can be used to tweak the algorithm slightly.  In
 * normal operation, the algorithm does not pay any attention to the input
 * sample variance, reporting only the post. est. var. for the chosen depth
 * hypothesis.  Under some conditions this doesn't make sense, and we might
 * want to report the sample variance of the samples used to make the
 * hypothesis, and under some conditions, we'ld want to report the maximum of
 * the input sample variance and the predicted post. est. var.  Defining
 * TRACK_IP_SAMP_VAR allows you to track input sample variance; defining
 * TRACK_MAX_VAR allows you to track the maximum of the two estimates.  These
 * are mutually exclusive and only one should be set at a time; if both are
 * set, TRACK_MAX_VAR takes precedence.
 */

#define TRACK_IP_SAMP_VAR
#undef TRACK_MAX_VAR

typedef struct _depth_hypothesis {
	f64		cur_estimate;	/* Current depth mean estimate */
	f64		cur_variance;	/* Current depth variance estimate */
	f64		pred_estimate;	/* Current depth next-state mean prediction */
	f64		pred_variance;	/* Current depth next-state variance pred. */
	f64		cum_bayes_fac;	/* Cumulative Bayes factor for node monitoring */
	u16		seq_length;		/* Worst-case sequence length for monitoring */
	u16		hypothesis_num;	/* Index term for debuging */
	u32		num_samples;	/* Number of samples incorporated into this node */
#if defined(TRACK_IP_SAMP_VAR) || defined(TRACK_MAX_VAR)
	f32		var_estimate;	/* Running estimate of variance of inputs (m^2) */
#endif
	struct _depth_hypothesis *next;	/* Singly Linked List */
} Hypothesis;

/* Routine:	cube_node_estimate_mem
 * Purpose:	Estimate number of bytes required for variable memory in a node
 * Inputs:	exp_hyp	Expected number of hypotheses per node
 *			p		CUBE parameters in effect
 * Outputs:	Returns number of bytes for variable portion of node memory
 * Comment:	Note that this is *not* the total memory requirement for the node;
 *			this is only the portion that is allocated when data arrives at a
 *			node.
 */

u32 cube_node_estimate_mem(f32 exp_hyp, Cube p)
{
	u32		size;
	
	size = (u32)floor(sizeof(Queue)*p->median_length
							+ sizeof(Hypothesis)*exp_hyp);
	return(size);
}

/* Routine:	cube_node_init
 * Purpose:	Initialise a CubeNode structure
 * Inputs:	node	Node to initialise
 * Outputs:	-
 * Comment:	This initialises the node to a zero length queue, and no depth
 *			hypotheses.  Both are added by the rest of the insertion code only
 *			as required.
 */

void cube_node_init(CubeNode node)
{
	f32 invalid_data;
	
	mapsheet_get_invalid(MAP_DATA_F32, &invalid_data);
	node->queue = NULL;
	node->n_queued = 0;
	node->depth = node->nominated = NULL;
	node->write = False;
	node->pred_depth = invalid_data;
	node->pred_var = invalid_data;
}

/* Routine:	cube_node_start_debug
 * Purpose:	Turn on debugging for a particular node
 * Inputs:	*node	Node to work with
 * Outputs:	-
 * Comment:	This sets the node's ->write flag, so that it will be included in
 *			output sequence code.
 */

void cube_node_start_debug(CubeNode node)
{
	node->write = True;
}

/* Routine:	cube_node_stop_debug
 * Purpose:	Turn off debugging for a particular node
 * Inputs:	node	Node to work with
 * Outputs:	-
 * Comment:	This just resets the node's ->write flag.
 */

void cube_node_stop_debug(CubeNode node)
{
	node->write = False;
}

/* Routine:	cube_node_new
 * Purpose:	Allocate memory for a new node
 * Inputs:	-
 * Outputs:	Pointer to a new CubeNode, or NULL if failed
 * Comment:	Node is initialised per cube_node_init().
 */

CubeNode cube_node_new(void)
{
	CubeNode	rtn;
	
	if ((rtn = (CubeNode)malloc(sizeof(CNode))) == NULL)
		return(NULL);
	cube_node_init(rtn);
	return(rtn);
}

/* Routine:	cube_node_release_hypotheses
 * Purpose:	Releases memory associated with a linked list of hypotheses
 * Inputs:	*root	Pointer to start of the linked list
 * Outputs:	-
 * Comment:	-
 */

static void cube_node_release_hypotheses(Hypothesis *root)
{
	Hypothesis	*next;
	
	while (root != NULL) {
		next = root->next;
		free(root);
		root = next;
	}
}

/* Routine:	cube_node_reinit
 * Purpose:	Remove memory from a node, and re-initialise
 * Inputs:	node	CubeNode to work on
 * Ouputs:	-
 * Comment:	This releases all dynamically allocated memory for the node, and
 *			then calls cube_node_init().
 */

void cube_node_reinit(CubeNode node)
{	
	if (node->queue != NULL) free(node->queue);
	cube_node_release_hypotheses(node->depth);
	cube_node_init(node);
}

/* Routine:	cube_node_serialise
 * Purpose:	Write a binary description of a node to file
 * Inputs:	node	Pointer to the CubeNode structure to use
 *			p		Cube parameter structure to work from
 *			fd		File descriptor on which to write
 * Outputs:	True if write worked, else False
 * Comment:	This routine does *not* flush the input pre-filter queue into the
 *			estimator before the node is written, but does write the queue into
 *			the file.  The reason for not flushing the queue is that
 *			by the time the data is being flushed to disc, the remainder of the
 *			points in the queue are probably pretty bad, and flushing them
 *			would cause more problems than they would fix.  This would also
 *			cause a non-reversible modification if the estimation was done in
 *			two parts.  This method preserves state, but makes bigger files than
 *			previous versions which didn't retain the queue. Note that the
 *			reference numbers on the hypotheses are also reset so that the first
 *			one in the file is numbered 1 (and hence reconstructs the sequence
 *			built on first generation when they are re-read from disc later, i.e.,
 *			with #1 at the tail of the list).  This also allows us to attach a
 *			reference number to the 'nominated' hypothesis which is consistent
 *			within the file, and hence maintains state across read/write cycles.
 */

Bool cube_node_serialise(CubeNode node, Cube p, FILE *fd)
{
	u32			n_hyp, nominated_hyp;
	Hypothesis	*tmp;
	f32			tmp_hyp;
	
	/* Write the base structure */
	if (fwrite(node, sizeof(CNode), 1, fd) != 1) {
		error_msg(modname, "error: failed writing node.\n");
		return(False);
	}
	/* Write the holding queue if any */
	if (node->n_queued != 0) {
		if (fwrite(node->queue, sizeof(Queue), node->n_queued, fd)
														!= node->n_queued) {
			error_msgv(modname, "error: failed writing %d elements in node"
				" input queue.\n", node->n_queued);
			return(False);
		}
	}
	/* Write number of hypotheses and then the hypothesis stack */
	tmp_hyp = cube_node_extract_nhyp(node, p);
	if (tmp_hyp == p->no_data_value)
		n_hyp = 0;
	else
		n_hyp = (u32)tmp_hyp;
	if (fwrite(&n_hyp, sizeof(u32), 1, fd) != 1) {
		error_msg(modname, "error: failed writing number of hypotheses.\n");
		return(False);
	}
	if (n_hyp != 0) {
		tmp = node->depth; n_hyp = 1; nominated_hyp = 0;
		/* We regenerate the sequence numbers of the hypotheses here and find the
		 * nominated hypothesis if chosen */
		while (tmp != NULL) {
			tmp->hypothesis_num = (u16)n_hyp;
			if (tmp == node->nominated) nominated_hyp = n_hyp;
			if (fwrite(tmp, sizeof(Hypothesis), 1, fd) != 1) {
				error_msgv(modname, "error: failed writing hypothesis %d.\n", n_hyp);
				return(False);
			}
			tmp = tmp->next;
			++n_hyp;
		}
		if (fwrite(&nominated_hyp, sizeof(u32), 1, fd) != 1) {
			error_msgv(modname, "error: failed writing nominated hypothesis ref.\n");
			return(False);
		}
	}
	return(True);
}

/* Routine: cube_node_deserialise
 * Purpose:	Read a node description from file
 * Inputs:	fd		File descriptor to read from
 *			node	Node description to read into
 *			p		Cube parameter structure
 * Outputs:	False if read failed, otherwise True
 * Comment:	This routine does not check whether the node passed in has a queue
 *			or a hypothesis sequence before it overwrites them.
 */

Bool cube_node_deserialise(FILE *fd, CubeNode node, Cube p)
{
	Hypothesis	dummy, *tmp;
	u32			hyp, n_hyp;
	
	cube_node_reinit(node);
	
	if (fread(node, sizeof(CNode), 1, fd) != 1) {
		error_msg(modname, "error: failed reading node.\n");
		return(False);
	}
	if (node->n_queued != 0) {
		if ((node->queue = (Queue*)malloc(sizeof(Queue)*p->median_length))
																	== NULL) {
			error_msgv(modname, "error: failed allocating %d elements for input"
				" queue.\n", p->median_length);
			return(False);
		}
		if (fread(node->queue, sizeof(Queue), node->n_queued, fd)
															!= node->n_queued) {
			error_msgv(modname, "error: failed reading %d elements for input "
				"queue.\n", node->n_queued);
			return(False);
		}
	}
	if (fread(&n_hyp, sizeof(u32), 1, fd) != 1) {
		error_msg(modname, "error: failed reading number of hypotheses.\n");
		return(False);
	}
	
	node->depth = NULL;	/* Reset after read to ensure proper termination */
	
	for (hyp = 0; hyp < n_hyp; ++hyp) {
		if (fread(&dummy, sizeof(Hypothesis), 1, fd) != 1) {
			error_msgv(modname, "error: failed reading hypothesis %d.\n", hyp);
			return(False);
		}
		if ((tmp = (Hypothesis*)malloc(sizeof(Hypothesis))) == NULL) {
			error_msgv(modname, "error: failed allocating memory for"
				" hypothesis %d.\n", hyp);
			return(False);
		}
		memcpy(tmp, &dummy, sizeof(Hypothesis));
		/* Note that this re-link reverses the order of the linked list,
		 * and although this is not usually a problem (CUBE itself doesn't
		 * care, although the cost in insertion might change a litte), it
		 * does play havock with the debug code, which makes assumptions
		 * about the ordering of the hypotheses as they are numbered.  We
		 * know what we're doing (TM), though, so we don't need to apply
		 * the patented Reverse-o-Matic here.
		 * (28/01/03) Even more cunningly, renumbering the hypotheses on
		 * serialisation means that this reversal now rebuilds the correct
		 * numbering seequence, so the debug code is safe for use --- huzzah!
		 */
		tmp->next = node->depth;
		node->depth = tmp;
	}
	if (n_hyp != 0) {
		/* Read the nominated hypothesis number, if any hypotheses */
		u32 nominated_hyp;

		if (fread(&nominated_hyp, sizeof(u32), 1, fd) != 1) {
			error_msgv(modname, "error: failed reading nominated hypothesis"
				" number.\n");
			return(False);
		}
		/* Now we need to re-match the nominated hypothesis number in the list
		 * to make sure we're pointing at the same one
		 */
		if (nominated_hyp == 0)
			node->nominated = NULL;
		else {
			tmp = node->depth;
			while (tmp != NULL) {
				if (tmp->hypothesis_num == nominated_hyp) {
					node->nominated = tmp;
					break;
				}
				tmp = tmp->next;
			}
		}
	}
	return(True);
}

/* Routine:	cube_node_init_hypothesis
 * Purpose:	Initialise a hypothesis variable
 * Inputs:	*n	Pointer to the hypothesis
 *			im	Initial mean estimate
 *			iv	Initial variance estimate
 * Outputs:	-
 * Comment:	-
 */

static void cube_node_init_hypothesis(Hypothesis *n, f32 im, f32 iv)
{
	memset(n, 0, sizeof(Hypothesis));
	n->cur_estimate = n->pred_estimate = im;
	n->cur_variance = n->pred_variance = iv;
	n->cum_bayes_fac = 1.0f;
	n->seq_length = 0;
	n->num_samples = 1;
#if defined(TRACK_IP_SAMP_VAR) || defined(TRACK_MAX_VAR)
	n->var_estimate = 0.f;
#endif
}

/* Routine:	cube_node_new_hypothesis
 * Purpose:	Construct a new hypothesis, add to current list and return updated
 *			pointer
 * Inputs:	*root	Root of current linked list of hypotheses
 *			mean	Initial mean
 *			var		Initial variance
 * Outputs:	Returns pointer to new hypothesis, or NULL (and the original is
 *			unchanged) on failure (typically lack of memory).
 * Comment:	This constructs a new blank hypothesis, then fills it with the
 *			initial values given.  The node is then pushed onto the top of the
 *			linked list of nodes, and the pointer to the new node is returned.
 */

static Hypothesis *cube_node_new_hypothesis(Hypothesis *root, f32 mean, f32 var)
{
	Hypothesis	*rtn;
	
	if ((rtn = (Hypothesis*)malloc(sizeof(Hypothesis))) == NULL) {
		error_msg(modname, "failed getting new hypothesis memory.\n");
		return(NULL);
	}
	cube_node_init_hypothesis(rtn, mean, var);
	rtn->next = root;
	
	/* Note that this is for debugging only --- it labels the hypotheses in
	 * order as they are pushed onto the stack so that they can be identified
	 * on the output debug information.  Of course, when the sheet is written
	 * to disc and re-read, the order of the linked list gets reversed, and
	 * hence the code will be fooled into re-numbering any new hypotheses from
	 * 1 again.
	 */
	if (root != NULL)
		rtn->hypothesis_num = root->hypothesis_num + 1;
	else
		rtn->hypothesis_num = 1;
	return(rtn);
}

/* Routine:	cube_node_add_hypothesis
 * Purpose:	Add a specific depth hypothesis to the current list
 * Inputs:	node	CubeNode to work on
 *			depth	Depth to set for hypothesis (meter)
 *			var		Variance to set for hypothesis	(meter^2)
 * Outputs:	True if the hypothesis was added, otherwise False
 * Comment:	Call-through to cube_node_new_hypothesis() for external users.
 */

Bool cube_node_add_hypothesis(CubeNode node, f32 depth, f32 var)
{
	Hypothesis	*rtn;

	if ((rtn = cube_node_new_hypothesis(node->depth, depth, var)) == NULL) {
		error_msg(modname, "error: failed constructing new hypothesis.\n");
		return(False);
	}
	node->depth = rtn;
	return(True);
}

/* Routine:	cube_node_add_null_hypothesis
 * Purpose:	Add a specific depth hypothesis to the current list, with
 *			number of samples set to zero.
 * Inputs:	node	CubeNode to work on
 *			depth	Depth to set for hypothesis (meter)
 *			var		Variance to set for hypothesis	(meter^2)
 * Outputs:	True if the hypothesis was initialised, otherwise False
 * Comment:	Call-through to cube_node_new_hypothesis() for external users,
 *			with n_j = 0 rather than n_j = 1.  This is typically used for
 *			initialisation surface hypotheses, so that they can be
 *			easily differentiated from hypotheses based on data.  In the
 *			read-back code, hypotheses with n_j = 0 are not allowed to be
 *			chosen.
 */

Bool cube_node_add_null_hypothesis(CubeNode node, f32 depth, f32 var)
{
	if (!cube_node_add_hypothesis(node, depth, var)) {
		error_msg(modname, "error: failed to add basic hypothesis.\n");
		return(False);
	}
	node->depth->num_samples = 0;
	return(True);
}

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

Bool cube_node_remove_hypothesis(CubeNode node, f32 depth)
{
	f32			diff;
	u32			n_matches;
	Hypothesis	*h, *prev;
	Bool		rc = False;

	if (node->depth == NULL) {
		error_msgv(modname, "error: no hypotheses to remove at node!\n");
		return(False);
	}
	h = node->depth; n_matches = 0;
	while (h != NULL) {
		diff = (f32)fabs(depth - h->cur_estimate);
		if (diff < HYP_SELECTION_TOLERANCE) ++n_matches;
		h = h->next;
	}
	if (n_matches == 0) {
		error_msgv(modname, "error: no match to depth %.2f m in node.\n",
			depth);
	} else if (n_matches > 1) {
		error_msgv(modname, "error: more than one match to depth %.2f m"
			" in node.\n", depth);
	} else {
		prev = NULL;
		h = node->depth;
		while (h != NULL) {
			if (fabs(depth - h->cur_estimate) < HYP_SELECTION_TOLERANCE) {
				if (h == node->depth) {
					/* Removing the head of the list */
					node->depth = h->next;
				} else {
					prev->next = h->next;
				}
				if (node->nominated == h) node->nominated = NULL;
				free(h);
				break;
			}
			prev = h;
			h = h->next;
		}
		rc = True;
	}
	return(rc);
}

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

Bool cube_node_nominate_hypothesis(CubeNode node, f32 depth)
{
	Hypothesis	*tmp;
	f32			diff, min_abs_diff = FLT_MAX;

	node->nominated = NULL;	/* Reset any nomination currently in effect */
	tmp = node->depth;
	while (tmp != NULL) {
		diff = (f32)fabs(depth - tmp->cur_estimate);
		if (diff < HYP_SELECTION_TOLERANCE && diff < min_abs_diff) {
			min_abs_diff = diff;
			node->nominated = tmp;
		}
		tmp =  tmp->next;
	}
	if (node->nominated == NULL) {
		error_msgv(modname, "error: failed to find a hypothesis with depth"
			" within a metric whisker of %.2f m at node.\n", depth);
		return(False);
	}
	return(True);
}

/* Routine: cube_node_reset_nomination
 * Purpose:	Remove the nomination lock on a node
 * Inputs:	node CubeNode to work on
 * Outputs:	-
 * Comment:	This removes any nomination currently in effect, forcing the
 *			disam. engine to run for this node again.
 */

void cube_node_reset_nomination(CubeNode node)
{
	node->nominated = NULL;
}

/* Routine:	cube_node_is_nominated
 * Purpose:	Determine whether the result from a node was due to nomination or not
 * Inputs:	node	CubeNode to work on
 * Outputs:	True if the node has a nominated hypothesis marked, otherwise False
 * Comment:	-
 */

Bool cube_node_is_nominated(CubeNode node)
{
	if (node->nominated != NULL)
		return(True);
	else
		return(False);
}

/* Routine:	cube_node_set_preddepth
 * Purpose:	Set the node's notion of a 'predicted depth'
 * Inputs:	node	CubeNode to operate on
 *			depth	Depth to set (meter)
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

void cube_node_set_preddepth(CubeNode node, f32 depth, f32 var)
{
#ifdef __DEBUG__
	if (isnan(depth)) {
		error_msg(modname, "warning: setting a NaN predicted depth.\n");
	}
#endif
	node->pred_depth = depth;
	node->pred_var = var;
}

/* Routine:	cube_node_monitor
 * Purpose:	Compute West & Harrison's monitoring statistics for the node
 * Inputs:	*n	Pointer to the hypothesis on which to operate
 *			yn	New input sample which is about the be incorporated
 *			vn	Observation noise variance
 * Outputs:	True if an intervention is indicated, otherwise False
 * Comment:	This code depends on the parameters:
 *				est_offset	Offset considered to be significant
 *				bayes_fac_t	Bayes Factor threshold before intervention
 *				runlength_t	Number of bad factors to indicate sequence
 *							failure
 *			which must be set up by the user externally of the code.  The
 *			est_offset is W&H's `h' parameter (i.e., expected normalised
 *			difference between the current forecast and the observation which
 *			just indicates an outlier), bayes_fac_t is W&H's `tau' (i.e.,
 *			the minimum Bayes factor which is acceptable as evidence for the
 *			current model), and runlength_t is W&H's limit on l_t (i.e.,
 *			the number of consequtively bad Bayes factors which indicate that
 *			there has been a gradual shift away from the predictor).
 */

static Bool cube_node_monitor(Hypothesis *n, float yn, float vn, Cube p,
							  Bool verbose)
{
	f64	error, forecast_var, bayes_factor;
	
	forecast_var = n->pred_variance + vn;
	error = (yn - n->pred_estimate)/sqrt(forecast_var);
	if (error >= 0.0)
		bayes_factor =
			exp(0.5*(p->est_offset*p->est_offset - 2.0*p->est_offset*error));
	else
		bayes_factor =
			exp(0.5*(p->est_offset*p->est_offset + 2.0*p->est_offset*error));

if (verbose)
error_msgv(modname, "debug: yn=%.2f vn=%f forecast_var=%f error=%f B=%f\n",
	yn, vn, forecast_var, error, bayes_factor);

	/* Check for single component failure */
	if (bayes_factor < p->bayes_fac_t) {
		/* This is a potential outlier.  Indicate potential failure. */
if (verbose)
error_msg(modname, "debug: outlier by single component Bayes factor.\n");
		return(False);
	}
	
	/* Update monitors */
	if (n->cum_bayes_fac < 1.0)
		++n->seq_length;
	else
		n->seq_length = 1;
	n->cum_bayes_fac = bayes_factor * MIN(1.0, n->cum_bayes_fac);
	
if (verbose)
error_msgv(modname, "debug: cum_bayes_fac=%f seq_len=%d.\n",
	n->cum_bayes_fac, n->seq_length);

	/* Check for consequtive failure errors */
	if (n->cum_bayes_fac < p->bayes_fac_t || n->seq_length > p->runlength_t) {
		/* Indicate intervention required */
if (verbose)
error_msgv(modname, "debug: intervention by %s.\n",
	n->cum_bayes_fac < p->bayes_fac_t ? "cumulative Bayes factor" : "runlength");
		return(False);
	}
	
	/* Otherwise, the current model is still supported ... */
	return(True);
}

/* Routine:	cube_node_resetmonitor
 * Purpose:	Reset monitoring structure to defaults
 * Inputs:	*n	Pointer to hypothesis to reset
 * Outputs:	-
 * Comment:	-
 */

static void cube_node_resetmonitor(Hypothesis *n)
{
	n->cum_bayes_fac = 1.0;
	n->seq_length = 0;
}

/* Routine:	cube_node_update_hypothesis
 * Purpose:	Update a particular hypothesis being tracked at a node
 * Inputs:	*h			Hypothesis to be updated
 *			depth		Estimate of beam depth
 *			variance	Estimate of beam depth variance
 *			*p			Parameter structure for algorithm
 * Outputs:	Returns False if the estimate does not really match the track that
 *			the hypothesis represents (i.e., an intervention is required).
 * Comment:	This implements the standard univariate dynamic linear model update
 *			equations (West & Harrison, 'Bayesian Forecasting and Dynamic
 *			Models', Springer, 2ed, 1997, Ch. 2), along with the Bayes factor
 *			monitoring code (W&H, Ch. 11).  The only failure mode possible with
 *			this code is if the input data would cause an intervention to be
 *			requested on the current track.  In this case, it is the caller's
 *			responsibility to utilise the data point, since it will not be
 *			incorporated into the hypothesis --- typically this would mean
 *			adding a new hypothesis and pushing it onto the stack.
 */

static Bool cube_node_update_hypothesis(Hypothesis *h, f32 depth,
										f32 variance, Cube p, Bool verbose)
{
	f64		gain, sys_variance, innovation;

	/* Check current estimate with monitoring */
	if (!cube_node_monitor(h, depth, variance, p, verbose)) return(False);

#if defined(TRACK_IP_SAMP_VAR) || defined(TRACK_MAX_VAR)
	h->var_estimate = (f32)((h->num_samples + 1 - 2)*h->var_estimate/(h->num_samples + 1 - 1) +
						(depth - h->cur_estimate)*(depth - h->cur_estimate)/h->num_samples);
#endif

	/* Otherwise, update current hypothesis with new information */
	
	sys_variance = h->cur_variance * (1.0 - p->discount)/p->discount;
	
	gain = h->pred_variance/(variance + h->pred_variance);
	innovation = depth - h->pred_estimate;
	h->pred_estimate += gain*innovation;
	h->cur_estimate = h->pred_estimate;
	h->cur_variance = variance*h->pred_variance/(variance+h->pred_variance);
	h->pred_variance = h->cur_variance + sys_variance;
	
	++(h->num_samples);
	
	return(True);
}

/* Routine:	cube_node_best_hypothesis
 * Purpose:	Find the closest matching hypothesis in the current linked list.
 * Inputs:	*root	Pointer to the root of the linked list
 *			yn		Current input sample to be matched
 *			varn	Current input variance to be matched
 * Outputs:	Returns pointer to closest matching hypothesis of depth in the list
 *			provided, or NULL if there is no match (i.e., NULL root)
 * Comment:	This computes the normalised absolute error between one-step
 *			forecast for each hypothesis currently being tracked and the input
 *			sample; a pointer to the node with smallest error is returned, or
 *			NULL if there are no nodes.  If there is more than one node with
 *			the same error (unlikely in practice, but possible), then the last
 *			one in the list is chosen.  Since the list is maintained in
 *			push-down order this is the first hypothesis proposed (typically
 *			the `right' one unless the system burst fails at the start of
 *			sequence).
 */

static Hypothesis *cube_node_best_hypothesis(Hypothesis *root, f32 yn, f32 varn)
{
	Hypothesis	*rtn = NULL;
	f64			forecast_var, error, min_error = FLT_MAX;
	
	while (root != NULL) {
		forecast_var = root->pred_variance + varn;
		error = fabs((yn - root->pred_estimate)/sqrt(forecast_var));
		if (error < min_error) {
			min_error = error;
			rtn = root;
		}
		root = root->next;
	}
	return(rtn);
}

/* Routine:	cube_node_update_node
 * Purpose:	Update the CUBE equations for this node and input
 * Inputs:	node	CubeNode structure to work on
 *			depth	Depth estimate to incorporate
 *			var		Estimate of depth estimate variance
 *			*p		Filtering parameters used to get at monitoring
 *					algorithm parameters, and the discount factor for the
 *					previous variance in order to compute the current evolution
 *					noise (a.k.a. system noise variance).
 * Outputs:	-
 * Comment:	This runs the basic filter equations, using the KF formulation, and
 *          its innovations formulation.  Note that the updates have to be done
 *			in double precision to ensure that we don't have critical round-off
 *			problems. This algorithm now includes a discounted system noise
 *			variance model to set the evolution noise dynamically depending on
 *			the variance that was estimated at the previous stage (West &
 *			Harrison, 'Bayesian Forecasting and Dynamic Models', Springer, 2ed.,
 *			1997, ch.2), and a monitoring scheme and feed-back interventions to
 *			allow the code to check that the estimates are staying in touch with
 *			the input data.  The monitoring scheme is also based on West &
 *			Harrison as above, Ch.11, Sec. 11.5.1, using cumulative Bayes
 *			factors and the unidirectional level shift alternate model.
 */

static Bool cube_node_update_node(CubeNode node, f32 depth, f32 variance, Cube p)
{
#ifdef __EST_TRACE__
	static FILE *debug = NULL;
#endif
	Hypothesis	*best, *tmp;

#ifdef __EST_TRACE__
	if (node->write) {
		if (debug == NULL) {
			if ((debug = fopen("debug-kfs.txt", "w")) == NULL) {
				error_msg(modname, "*** debug: failed to open output file.\n");
				exit(1);
			}
		}
	}
#endif
	
	/* Find the best matching hypothesis for the current input sample given
	 * those currently being tracked.
	 */
	if ((best = cube_node_best_hypothesis(node->depth, depth, variance))
																	== NULL) {
		/* Didn't match a `best' hypothesis ... this only happens when there
		 * are *no* hypotheses (i.e., intialisation mode).  So we add a new
		 * hypothesis directly.
		 */
		if ((tmp = cube_node_new_hypothesis(node->depth, depth, variance))
																	== NULL) {
			error_msg(modname, "error: failed to add new depth hypothesis"
				" (initialisation).\n");
			return(False);
		}
		node->depth = tmp;
		best = node->depth;
	} else {
		/* Update the best hypothesis with the current data */
		if (!cube_node_update_hypothesis(best, depth, variance, p, node->write)) {
			/* Failed update --- indicates an intervention, so that we need to
			 * start a new hypothesis to capture the outlier/datum shift.
			 */
			cube_node_resetmonitor(best);
			if ((tmp = cube_node_new_hypothesis(node->depth, depth, variance))
																	== NULL) {
				error_msg(modname, "error: failed to add new depth hypothesis"
					" (intervention).\n");
				return(False);
			}
			node->depth = tmp; /* Re-link new root of linked list into node */
			best = node->depth;
		}
	}

#ifdef __EST_TRACE__
	if (node->write) {
		fprintf(debug, "%d %f %f %f %f\n",
				best->hypothesis_num,
				best->cur_estimate,
				best->cur_variance,
				depth, variance);
		fflush(debug);
	}
#endif

	return(True);	
}

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

static void cube_node_truncate(CubeNode node, Cube p)
{
	f32		mean = 0.0, ssd = 0.0, ssd_k, diff_sq, q;
	u32		n = node->n_queued-1;	/* i.e., n points + 1 outlier */
	s32		pt;
				
	/* First compute mean and overall sum of squared differences (SSD) */
	for (pt = 0; pt <= (s32)n; ++pt) {
		mean += node->queue[pt].depth;
		ssd += node->queue[pt].depth * node->queue[pt].depth;
	}
	ssd -= mean*mean/(n+1);
	mean /= (n+1);
	ssd_k = n*ssd/(n*n-1);

#ifdef __EEG_DEBUG__
	if (node->write) {
		if (debug_eeg == NULL)
			debug_eeg = fopen("debug-eeg.txt", "w");
		fprintf(debug_eeg, "overall mean %f, overall SSD %f\n", mean, ssd);
	}
#endif
	
	/* Run the list computing quotients; outliers are marked by setting the
	 * estimate variance to -1 (otherwise impossible).
	 */
	for (pt = 0; pt <= (s32)n; ++pt) {
		diff_sq = (node->queue[pt].depth - mean)*(node->queue[pt].depth - mean);
		q = diff_sq/(ssd_k - diff_sq/(n-1));
		if (q >= p->quotient_limit) {
#ifdef __EEG_DEBUG__
			if (node->write)
				fprintf(debug_eeg, " point %d: depth %f, variance %f, "
					"quotient %f, deleting.\n",
					pt, node->queue[pt].depth, node->queue[pt].var, q);
#endif
			node->queue[pt].var = -1.0;
		} else {
#ifdef __EEG_DEBUG__
			if (node->write)
				fprintf(debug_eeg, " point %d: depth %f, variance %f, "
					"quotient %f, keeping.\n",
					pt, node->queue[pt].depth, node->queue[pt].var, q);
#endif
		}
	}
	
	/* Now remove the marked entities from the queue */
	for (pt = 0; pt < (s32)(node->n_queued); ++pt)
		if (node->queue[pt].var < 0) {
			memmove(node->queue+pt, node->queue+pt+1,
										sizeof(Queue)*(node->n_queued-pt-1));
			--node->n_queued;
			--pt;	/* So that ++pt at top of loop brings us back to the same
					 * slot (i.e., the next point we've just droped in there).
					 */
		}
#ifdef __EEG_DEBUG__
	if (node->write) {
		fprintf(debug_eeg, " ** %d points remaining in buffer.\n",
				node->n_queued);
		for (pt = 0; pt < node->n_queued; ++pt)
			fprintf(debug_eeg, " point %d: depth = %f m, variance %f m^2.\n",
				pt, node->queue[pt].depth, node->queue[pt].var);
	}
#endif
}

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

void cube_node_queue_flush_node(CubeNode node, Cube param)
{
	s32		ex_pt, direction, scale = 1;
	
	if (node->n_queued == 0) return; /* Not allocated, or flushed already */

#ifdef __EST_TRACE__
	if (node->write) {
		FILE	*debug = fopen("debug-flush.txt", "w");
		u32		i;
		
		for (i = 0; i < node->n_queued; ++i)
			fprintf(debug, "%f %f\n", node->queue[i].depth, node->queue[i].var);
		fclose(debug);
	}
#endif

	cube_node_truncate(node, param);
	if ((node->n_queued%2) == 0) {
		/* Even number of points in the queue at starting point */
		ex_pt = node->n_queued/2-1;
		direction = +1;
	} else {
		/* Odd number of points in the queue at starting point */
		ex_pt = node->n_queued/2;
		direction = -1;
	}
	while (ex_pt >= 0) {
		cube_node_update_node(node, node->queue[ex_pt].depth,
			node->queue[ex_pt].var, param);
		ex_pt += direction*scale;
		direction = -direction;
		scale ++;
	}
	free(node->queue);
	node->queue = NULL;
	node->n_queued = 0;
}

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

static int hypothesis_sort_fn(const void *a, const void *b)
{
	Hypothesis *ha = (Hypothesis*)a, *hb = (Hypothesis*)b;
	
	if (ha->num_samples > hb->num_samples) return(-1);
	else return(1);
}

static Bool cube_node_choose_hypothesis(Hypothesis *list, Hypothesis *best,
										f32 *hyp_ratio)
{
	static Hypothesis	*buffer = NULL;
	static u32			n_buffer = 0;
	
	Hypothesis	*tmp;
	u32			num_hyp, hyp;
	f32			n_data = 0.0, ratio;
	
	/* We need to count the depth hypotheses, and then write them into a linear
	 * array for sorting.  Note that we keep a static buffer here (and hence
	 * leak a little) to avoid having to free and reallocate on each call, which
	 * can be a significant overhead with a large array of nodes.
	 */
	num_hyp = 0; tmp = list;
	while (tmp != NULL) {
		if (tmp->num_samples > 0)	/* Only count hypotheses not from initialisation */
			++num_hyp;
		tmp = tmp->next;
	}
	if (num_hyp == 0) {
#ifdef __HYP_DEBUG__
		error_msg(modname, "debug: one hypothesis, but only for initialisation surface (n_j==0)."
			"  Can't select for reconstruction.\n");
#endif
		return(False);
	}
	if (num_hyp > n_buffer) {
		/* Need to grab some more memory */
		if (buffer != NULL) { free(buffer); n_buffer = 0; }
		if ((buffer = (Hypothesis*)malloc(sizeof(Hypothesis)*num_hyp))
																	== NULL) {
			error_msgv(modname, "error: failed allocating hypothesis"
				" sort array (%d hypotheses).\n", num_hyp);
			return(False);
		}
		n_buffer = num_hyp;
	}
	tmp = list; num_hyp = 0;
	while (tmp != NULL) {
		if (tmp->num_samples > 0) {
			memcpy(buffer + num_hyp, tmp, sizeof(Hypothesis));
			++num_hyp;
		}	
		tmp = tmp->next;
	}
	qsort(buffer, num_hyp, sizeof(Hypothesis), hypothesis_sort_fn);
	memcpy(best, buffer, sizeof(Hypothesis));

	if (num_hyp > 1) {
		for (hyp = 1; hyp < num_hyp; ++hyp)
			n_data += buffer[hyp].num_samples;
		ratio = (f32)MAX_HYPOTHESIS_RATIO - (f32)buffer[0].num_samples / buffer[1].num_samples;
		if (ratio < 0.0f)
			ratio = 0.0f;
#ifdef __HYP_DEBUG__
/*
		error_msgv(modname, "debug: more than one hypothesis, hyp. strength ratio"
			" set to %.1f based on max. %.1f and %d samples in best hypothesis,"
			" with %d samples in next best.\n", ratio, MAX_HYPOTHESIS_RATIO,
			buffer[0].num_samples, buffer[1].num_samples);
*/
#endif
	} else {
#ifdef __HYP_DEBUG__
		error_msg(modname, "debug: one hypothesis, hyp. strength ratio set to 0.0.\n");
#endif
		ratio = 0.0f;
	}
	if (hyp_ratio != NULL) *hyp_ratio = ratio;
	return(True);
}

/* Routine:	cube_node_queue_fill
 * Purpose:	Insert a point into an unfilled queue
 * Inputs:	node	Pointer to nodal structure to work on
 *			depth	Depth estimate to be inserted
 *			var		Estimated variance of estimate
 * Outputs:	-
 * Comment:	Note that this routine assumes that the queue isn't already filled,
 *			and hence that it doesn't have to do bound checking before it fills
 *			in the element.  Expect bad things to happen if this isn't the case.
 */

static void cube_node_queue_fill(CubeNode node, f32 depth, f32 var)
{
	u32		i = 0;

#ifdef __EEG_DEBUG__
	if (node->write) {
		u32	pt;
		
		if (debug_eeg == NULL)
			debug_eeg = fopen("debug-eeg.txt", "w");
		fprintf(debug_eeg, "pre-fill:\n");
		for (pt = 0; pt < node->n_queued; ++pt)
			fprintf(debug_eeg, " point %d: depth = %f m, variance = %f m^2.\n",
				pt, node->queue[pt].depth, node->queue[pt].var);
	}
#endif

	while (i < node->n_queued && node->queue[i].depth > depth) ++i;
	if (i < node->n_queued)
		memmove(node->queue+i+1, node->queue+i, sizeof(Queue)*(node->n_queued-i));
	node->queue[i].depth = depth;
	node->queue[i].var = var;
	++node->n_queued;

#ifdef __EEG_DEBUG__
	if (node->write) {
		fprintf(debug_eeg, "fill: inserted (%f, %f) now %d points.\n",
			depth, var, node->n_queued);
		for (i = 0; i < node->n_queued; ++i)
			fprintf(debug_eeg, " point %d: depth = %f m, variance = %f m^2.\n",
				i, node->queue[i].depth, node->queue[i].var);
	}
#endif
}

/* Routine:	cube_node_queue_insert
 * Purpose:	Insert a point into a filled queue, returning median estimate
 * Inputs:	node	Pointer to nodal structure to work on
 *			depth	Depth estimate to be inserted
 *			var		Estimated variance of estimate
 *			*p		Pointer to algorithm parameter holding structure
 * Outputs:	*op		Filled with median value from queue (before insertion)
 * Comment:	This extracts the median value from the current queue (which must
 *			be fully filled before calling this routine), and then inserts the
 *			given point into the queue, shuffling the remaining elements to
 *			ensure that the queue remains sorted.
 */

static void cube_node_queue_insert(CubeNode node, f32 depth, f32 variance,
							  	   Cube p, Queue *op)
{
	s32		i, c = p->median_length/2;
	f64		lo_water, hi_water;

#ifdef __EEG_DEBUG__
	if (node->write) {
		if (debug_eeg == NULL)
			debug_eeg = fopen("debug-eeg.txt", "w");
		fprintf(debug_eeg, "pre-insert order:\n");
		for (i = 0; i < node->n_queued; ++i)
			fprintf(debug_eeg, " point %d: depth %f m, variance %f m^2.\n",
				i, node->queue[i].depth, node->queue[i].var);
	}
#endif

	*op = node->queue[c];	/* Center point is median since list is sorted */
	if (depth > op->depth) {
		/* Depth is in the first half of the queue, search towards start */
		i = c-1;
		while (i >= 0 && depth > node->queue[i].depth) --i;
		if (i != c-1)
			/* i.e., we've moved, so we need to shuffle data downwards */
			memmove(node->queue+i+2, node->queue+i+1, sizeof(Queue)*(c-i-1));
		node->queue[i+1].depth = depth;
		node->queue[i+1].var = variance;
	} else {
		/* Depth is in the second half of the queue, search towards end */
		i = c+1;
		while (i < (s32)(p->median_length) && depth < node->queue[i].depth) ++i;
		if (i != c+1)
			/* i.e., we've moved, so we need to shuffle data upwards */
			memmove(node->queue+c, node->queue+c+1, sizeof(Queue)*(i-c-1));
		node->queue[i-1].depth = depth;
		node->queue[i-1].var = variance;
	}

#ifdef __EEG_DEBUG__
	if (node->write) {
		fprintf(debug_eeg, "insert: removed (%f, %f) added (%f, %f).\n",
			op->depth, op->var, depth, variance);
		for (i = 0; i < node->n_queued; ++i)
			fprintf(debug_eeg, " point %d: depth %f m, variance %f m^2.\n",
				i, node->queue[i].depth, node->queue[i].var);
	}
#endif
	
	/* Compute the likely 99% confidence bound below the shallowest point, and
	 * above the deepest point in the buffer, and check that they do actually
	 * overlap somewhere in the middle.  Otherwise, with less than 1% chance of
	 * error, we are suspicious that there are outliers in the buffer somewhere,
	 * and we should attempt a round of outlier rejection.  Assuming that the
	 * errors are approximately normal, 0.5% in either tail is achieved at
	 * 2.5758 std. dev. from the mean.
	 */
	lo_water = node->queue[0].depth - CONF_99PC * sqrt(node->queue[0].var);
	hi_water = node->queue[p->median_length-1].depth +
				CONF_99PC * sqrt(node->queue[p->median_length-1].var);

#ifdef __EEG_DEBUG__
	if (node->write) {
		fprintf(debug_eeg, "debug: min d = (%f, %f) max d = (%f, %f), "
			"lo_water = %f, hi_water = %f.\n", node->queue[0].depth,
			node->queue[0].var, node->queue[p->median_length-1].depth,
			node->queue[p->median_length-1].var, lo_water, hi_water);
	}
#endif

	if (lo_water >= hi_water) /* i.e., confidence limits do not overlap */
		cube_node_truncate(node, p);	/* Remove any outliers */
}

/* Routine:	cube_node_queue_est
 * Purpose:	Insert points into the queue of estimates, and insert point into
 *			filter sequence if queue is filled
 * Inputs:	node	Node to update
 *			depth	Depth estimate
 *			var		Estimate of depth estimate variance
 *			*p		Pointer to CubeParam algorithm parameters
 * Outputs:	True if inserted, else False (no memory for queue)
 * Comment:	This inserts the depth given into the queue associated with the
 *			specified node, creating the queue if required.  After the queue\
 *			has been primed (i.e., filled with estimates), on each call this
 *			routine extracts the median value from the queue (via a call to
 *			mapsheet_nodal_queue_insert()) and then inserts it into the CUBE
 *			input sequence (via a call to mapsheet_nodal_update_node()).
 *				Note that this algorithm means that the queue will always be
 *			full, and hence must be flushed before extracting any depth
 *			estimates (this can also be done to save memory).
 */

static int queue_sort_by_z(const void *a, const void *b)
{
	f32	da = ((Queue*)a)->depth, db = ((Queue*)b)->depth;
	if (da < db) return(-1); else return(1);
}

static Bool cube_node_queue_est(CubeNode node, f32 depth, f32 variance,
						   		Cube p)
{
	Queue	op;
	
	if (node->queue == NULL) {
		/* Allocate memory for the node on first call */
		if ((node->queue = (Queue*)malloc(sizeof(Queue)*p->median_length))
			== NULL) {
			error_msgv(modname, "failed to allocate node queue (length %d).\n",
				p->median_length);
			return(False);
		}
		node->n_queued = 0;
	}
	if (node->n_queued < p->median_length) {
		cube_node_queue_fill(node, depth, variance);
	} else {
		/* Buffer is filled and sorted, so we need to extract the median
		 * element for insertion into the CUBE sequence, and then
		 * insert the current point.
		 */
		cube_node_queue_insert(node, depth, variance, p, &op);
		cube_node_update_node(node, op.depth, op.var, p);
	}
	return(True);
}

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

Bool cube_node_insert(CubeNode node, f64 node_x, f64 node_y, f64 dist_sqr,
					  Sounding *snd, Cube p)
{
	f64	dist, variance;
	f32	offset, target_depth;
#ifdef __EST_TRACE__
	static FILE *debug = NULL;
#endif

#ifdef __DEBUG__
	error_msgv(modname, "debug: inserting (%.2lf m, %.4lf m^2) into node"
		" at (%.2lf, %.2lf) m [relative].\n", snd->depth, snd->dz, node_x,
		node_y);
#endif

	if (isnan(node->pred_depth)) {
#ifdef __DEBUG__
		error_msgv(modname, "debug: not updating node at (%.2lf, %.2lf) m"
			" [relative] since pred_depth is set to NaN.\n", node_x, node_y);
#endif
		return(True);
	}

	/* Euclidean distance in projected space, i.e., distance sounding is being
	 * propagated from touchdown boresight to node estimation point.
	 */

	dist = sqrt(dist_sqr);
	
	if (node->pred_depth != p->no_data_value) {
		f64		blunder_limit;

		target_depth = node->pred_depth;
		/* Do the test for blunders here, since it makes no sense to test when
		 * there is no predicted depth.
		 */
/*		blunder_limit = MIN(target_depth - p->blunder_min, (1.0+p->blunder_pcent)*target_depth);*/
		blunder_limit = MIN(target_depth - p->blunder_min, target_depth - p->blunder_pcent*fabs(target_depth));
		blunder_limit = MIN(blunder_limit, target_depth - p->blunder_scalar*sqrt(node->pred_var));
		if (snd->depth < blunder_limit) return(True);
	} else {
		target_depth = snd->depth;
	}
	
	if (dist > MAX(p->capture_dist_scale*fabs(target_depth), 0.5)) {
if (node->write) 
	error_msgv(modname, "debug: sounding rejected from node, distance %.2lf m, target_depth %.2lf m\n",
		   dist, target_depth);
		return(True);
	} else {
if (node->write)
	error_msgv(modname, "debug: sounding accepted at node, distance %.2lf m, target_depth %.2lf m.\n",
		dist, target_depth);
	}
	
	/* Add horizontal positioning uncertainty */
	dist += CONF_95PC * sqrt(snd->dr);
	
	
	if (snd->range != 0.0 && node->pred_depth != p->no_data_value) {
		offset = node->pred_depth - snd->range;
		/* Refered variance is boresight variance multiplied by dilution factor,
		 * a function of distance and a user scale parameter.
		 */
/*		variance = (snd->dz + node->pred_var) *
					(1.0 + p->var_scale*pow(dist, p->dist_exp));*/
		variance = snd->dz*(1.0 + p->var_scale*pow(dist, p->dist_exp));
#ifdef __HYP_DEBUG__
		if (node->write)
			error_msgv(modname, "debug: dz = %.3lf, pred_var = %.3lf, dist = %.2lf, dist_exp = %.2lf\n",
				snd->dz, node->pred_var, dist, p->dist_exp);
#endif
	} else {
		offset = 0.0;
		variance = snd->dz*(1.0 + p->var_scale*pow(dist, p->dist_exp));
	}
	
	if (!cube_node_queue_est(node, snd->depth+offset, (f32)variance, p)) {
		error_msgv(modname, "failed to add data to node at (%.1lf,%.1lf) m.\n",
			node_x, node_y);
		return(False);
	}

#ifdef __EST_TRACE__
	if (node->write) {
		if (debug == NULL) {
			if ((debug = fopen("debug-ip.txt", "w")) == NULL) {
				error_msg(modname, "**** debug: failed to open ip file.\n");
				exit(1);
			}
			fprintf(debug, "# node_x node_y snd_x snd_y snd_depth dr dz"
				"prop_var d_{s,n} pred_depth depth_correct\n");
		}
		fprintf(debug, "%.2f %.2f %.2f %.2f %f %f %f %f %f %f %f\n",
			node_x, node_y, snd->east, snd->north, snd->depth,
			snd->dr, snd->dz, variance, sqrt(dist_sqr), snd->range, offset);
		fflush(debug);
	}
#endif
	
	node->nominated = NULL;	/* Adding data removes any nomination in effect */
	return(True);
}

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

Bool cube_node_extract_depth_unct(CubeNode node, Cube p, f32 *depth, f32 *unct,
								  f32 *ratio)
{
	Hypothesis	best;
	
	if (node->nominated != NULL) {
		/* User over-ride for selection */
		if (depth != NULL) *depth = (f32)node->nominated->cur_estimate;
		if (unct != NULL) *unct = (f32)(p->sd2conf_scale * sqrt(node->depth->cur_variance));
		if (ratio != NULL) *ratio = 0.0f; /* User nomination implies all's well ... */
		return(True);
	}
	if (node->depth == NULL) {
#ifdef __HYP_DEBUG__
		if (node->write) {
			error_msgv(modname, "debug: node - no hypotheses.\n");
		}
#endif
		if (depth != NULL) *depth = p->no_data_value;
		if (unct != NULL) *unct = p->no_data_value;
		if (ratio != NULL) *ratio = p->no_data_value;
	} else if (node->depth->next == NULL) {
		/* Special case: only one depth hypothesis (the usual case, we hope ...) */
		
		if (node->depth->num_samples > 0) {
			/* Only reconstruct if some data was involved in the construction of
			 * the hypothesis.  This excludes initial hypotheses from an initialisation
			 * surface, which are set up with n_j = 0.
			 */
			if (depth != NULL) *depth = (f32)(node->depth->cur_estimate);
			/* Note that we have three compile-time options in the code to report
			 * different interpretations of the 'uncertainty'.  The default is just
			 * to report CUBE's posterior variance estimate for the chosen node;
			 * defining TRACK_IP_SAMP_VAR causes the input sample variance to be
			 * estimated as a recursive function of the estimated depth; defining
			 * TRACK_MAX_VAR reports the maximum of these two.
			 */
#if defined(TRACK_MAX_VAR)
			if (unct != NULL)
				*unct = (f32)(p->sd2conf_scale *
							sqrt(MAX(node->depth->var_estimate, node->depth->cur_variance)));
#elif defined(TRACK_IP_SAMP_VAR)
			if (unct != NULL) *unct = (f32)(p->sd2conf_scale * sqrt(node->depth->var_estimate));
#else
			if (unct != NULL)
				*unct = (f32)(p->sd2conf_scale * sqrt(node->depth->cur_variance));
#endif
			if (ratio != NULL) *ratio = 0.0f; /* By defn. if there is only one hypothesis */
#ifdef __HYP_DEBUG__
			if (node->write) {
				error_msgv(modname, "debug: node - one hypothesis: z = %f m, "
					"v = %f m^2, nj = %d.\n", node->depth->cur_estimate,
					node->depth->cur_variance, node->depth->num_samples);
			}
#endif
		} else {
			if (depth != NULL) *depth = p->no_data_value;
			if (unct != NULL) *unct = p->no_data_value;
			if (ratio != NULL) *ratio = p->no_data_value;
#ifdef __HYP_DEBUG__
			if (node->write) {
				error_msgv(modname, "debug: node - one hypothesis: z = %f m, "
					"v = %f m^2, nj = %d.  Not used because it's from the initialisation set.\n",
					node->depth->cur_estimate, node->depth->cur_variance, node->depth->num_samples);
			}
#endif
		}
	} else {
		if (!cube_node_choose_hypothesis(node->depth, &best, ratio)) {
			error_msg(modname, "error: failed choosing a hypothesis.\n");
			return(False);
		}
		if (best.num_samples > 0) {
			if (depth != NULL) *depth = (f32)(best.cur_estimate);
			/* Note that we have three compile-time options in the code to report
			 * different interpretations of the 'uncertainty'.  The default is just
			 * to report CUBE's posterior variance estimate for the chosen node;
			 * defining TRACK_IP_SAMP_VAR causes the input sample variance to be
			 * estimated as a recursive function of the estimated depth; defining
			 * TRACK_MAX_VAR reports the maximum of these two.
			 */
#if defined(TRACK_MAX_VAR)
			if (unct != NULL)
				*unct = (f32)(p->sd2conf_scale *
							sqrt(MAX(best.var_estimate, best.cur_variance)));
#elif defined(TRACK_IP_SAMP_VAR)
			if (unct != NULL)
				*unct = (f32)(p->sd2conf_scale * sqrt(best.var_estimate));
#else
			if (unct != NULL)
				*unct = (f32)(p->sd2conf_scale * sqrt(best.cur_variance));
#endif

#ifdef __HYP_DEBUG__
			if (node->write) {
				error_msgv(modname, "debug: node - best hypothesis: z = %f m, "
					"v = %f m^2, nj = %d, ratio = %lf.\n", best.cur_estimate,
					best.cur_variance, best.num_samples, ratio);
			}
#endif
		} else {
			if (depth != NULL) *depth = p->no_data_value;
			if (unct != NULL) *unct = p->no_data_value;
			if (ratio != NULL) *ratio = p->no_data_value;
#ifdef __HYP_DEBUG__
			if (node->write) {
				error_msgv(modname, "debug: node - one hypothesis: z = %f m, "
					"v = %f m^2, nj = %d.  Not used because it's from the initialisation set.\n",
					node->depth->cur_estimate, node->depth->cur_variance, node->depth->num_samples);
			}
#endif
		}
	}
		
	return(True);
}

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

Bool cube_node_extract_closest_depth_unct(CubeNode node, Cube p,
										  f32 depth, f32 var,
										  f32p z, f32p v, f32p ratio)
{
	Hypothesis	*root, *soln;
	f64			error, min_error;
	u32			n_samples = 0;
	
	if (node->nominated != NULL) {
		/* User over-ride for selection */
		if (z != NULL) *z = (f32)node->nominated->cur_estimate;
		if (v != NULL) *v = (f32)(p->sd2conf_scale * sqrt(node->depth->cur_variance));
		if (ratio != NULL) *ratio = 0.0f; /* User nomination implies all's well ... */
		return(True);
	}
	root = node->depth;
	if (root == NULL || root->next == NULL) {
		/* No hypotheses, or just one (in which case why call this???) */
		return(cube_node_extract_depth_unct(node, p, z, v, ratio));
	}

#ifdef __HYP_DEBUG__
	if (node->write) {
		error_msgv(modname, "debug: multiple (%d) hypotheses:\n",
			cube_node_extract_nhyp(node, p));
		cube_node_dump_hypotheses(node);
	}
#endif

	min_error = FLT_MAX; soln = NULL;
	while (root != NULL) {
		if (root->num_samples > 0) {	/* Check that some data were used in making
										 * the hypothesis before accepting it as valid.
										 */
			error = fabs((root->cur_estimate - depth)/sqrt(var));
			if (error < min_error) {
				min_error = error;
				soln = root;
			}
			n_samples += root->num_samples;
		}
		root = root->next;
	}
	if (soln == NULL) {
		error_msg(modname, "error: internal: no 'best' hypothesis solution!"
			"  Probably a bad guide value pair.  Setting to INVALID values.\n");
		if (z != NULL) *z = (f32)(p->no_data_value);
		if (v != NULL) *v = (f32)(p->no_data_value);
		if (ratio != NULL) *ratio = (f32)(p->no_data_value);
	} else {
		/* Note that we don't have to check that N>0 for a valid hypothesis here,
		 * since the selection criterion above ensures that any 'best' solution
		 * has at least some data involved.
		 */
		if (z != NULL) *z = (f32)(soln->cur_estimate);
		/* Note that we have three compile-time options in the code to report
		 * different interpretations of the 'uncertainty'.  The default is just
		 * to report CUBE's posterior variance estimate for the chosen node;
		 * defining TRACK_IP_SAMP_VAR causes the input sample variance to be
		 * estimated as a recursive function of the estimated depth; defining
		 * TRACK_MAX_VAR reports the maximum of these two.
		 */
#if defined(TRACK_MAX_VAR)
			if (v != NULL)
				*v = (f32)(p->sd2conf_scale *
							sqrt(MAX(soln->var_estimate, soln->cur_variance)));
#elif defined(TRACK_IP_SAMP_VAR)
		if (v != NULL) *v = (f32)(p->sd2conf_scale*sqrt(soln->var_estimate));
#else
		if (v != NULL) *v = (f32)(p->sd2conf_scale*sqrt(soln->cur_variance));
#endif
		if (ratio != NULL) {
			*ratio = (f32)MAX_HYPOTHESIS_RATIO -
						(f32)soln->num_samples/(n_samples - soln->num_samples);
			if (*ratio < 0.0f) *ratio = 0.0f;
		}
#ifdef __HYP_DEBUG__
		if (node->write) {
			error_msgv(modname, "debug: best hypothesis d=%.2lf m, v=%.2lf m^2, n=%d, ratio=%.3lf.\n",
				soln->cur_estimate, soln->cur_variance, soln->num_samples, (ratio == NULL) ? 0.0 : *ratio);
		}
#endif
	}
	return(True);
}

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

Bool cube_node_extract_post_depth_unct(CubeNode node, Cube p, f32 depth, f32 var,
									   f32p z, f32p v, f32p ratio)
{
	Hypothesis	*root, *soln;
	f64			max_posterior, posterior, mean;
	u32			n_samples = 0;
	
	if (node->nominated != NULL) {
		/* User over-ride for selection */
		if (z != NULL) *z = (f32)node->nominated->cur_estimate;
		if (v != NULL) *v = (f32)(p->sd2conf_scale * sqrt(node->depth->cur_variance));
		if (ratio != NULL) *ratio = 0.0f; /* User nomination implies all's well ... */
		return(True);
	}
	root = node->depth;
	if (root == NULL || root->next == NULL) {
		/* No hypotheses, or just one (in which case why call this???) */
		return(cube_node_extract_depth_unct(node, p, z, v, ratio));
	}
	
	root = node->depth;
	max_posterior = -FLT_MAX; soln = NULL;
	while (root != NULL) {
		if (root->num_samples > 0) {	/* Check that at some data were used in
										 * generating the hypothesis before accepting it.
										 */
			mean = root->cur_estimate;
			posterior = -(depth-mean)*(depth-mean)/(2.0*var)
						+log(root->num_samples);
			if (posterior > max_posterior) {
				max_posterior = posterior;
				soln = root;
			}
			n_samples += root->num_samples;
		}
		root = root->next;
	}
	if (z != NULL) *z = (f32)(soln->cur_estimate);
	/* Note that we have three compile-time options in the code to report
	 * different interpretations of the 'uncertainty'.  The default is just
	 * to report CUBE's posterior variance estimate for the chosen node;
	 * defining TRACK_IP_SAMP_VAR causes the input sample variance to be
	 * estimated as a recursive function of the estimated depth; defining
	 * TRACK_MAX_VAR reports the maximum of these two.
	 */
#if defined(TRACK_MAX_VAR)
	if (v != NULL)
		*v = (f32)(p->sd2conf_scale *
					sqrt(MAX(soln->var_estimate, soln->cur_variance)));
#elif defined(TRACK_IP_SAMP_VAR)
	if (v != NULL) *v = (f32)(p->sd2conf_scale*sqrt(soln->var_estimate));
#else
	if (v != NULL) *v = (f32)(p->sd2conf_scale*sqrt(soln->cur_variance));
#endif
	if (ratio != NULL) {
		*ratio = (f32)MAX_HYPOTHESIS_RATIO -
					(f32)soln->num_samples/(n_samples - soln->num_samples);
		if (*ratio < 0.0f) *ratio = 0.0f;
	}
	
	return(True);
}

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

Bool cube_node_extract_depth(CubeNode node, Cube p, f32 *depth, f32 *ratio)
{
	return(cube_node_extract_depth_unct(node, p, depth, NULL, ratio));
}

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

Bool cube_node_extract_unct(CubeNode node, Cube p, f32 *unct, f32 *ratio)
{
	return(cube_node_extract_depth_unct(node, p, NULL, unct, ratio));
}

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

f32 cube_node_extract_nhyp(CubeNode node, Cube p)
{
	Hypothesis	*hyp = node->depth;
	f32			nhyp = 0;
	
	while (hyp != NULL) {
		if (hyp->num_samples > 0) nhyp += 1.0f;
		hyp = hyp->next;
	}
	if (nhyp == 0.0f) nhyp = p->no_data_value;
	return(nhyp);
}

/* Routine:	cube_node_dump_hypotheses
 * Purpose:	Dump a list of hypotheses to output for use in debugging.
 * Inputs:	node	CubeNode to dump
 * Outputs:	-
 * Comment:	-
 */

void cube_node_dump_hypotheses(CubeNode node)
{
	Hypothesis	*root;
	
	root = node->depth;
	while (root != NULL) {
		error_msgv(modname, "debug:  hyp %d, z = %f m, v = %f m^2, nj = %d.\n",
			root->hypothesis_num, root->cur_estimate, root->cur_variance,
			root->num_samples);
		root = root->next;
	}
}

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

Sounding *cube_node_enumerate_hypotheses(CubeNode node, s32 *nhyp)
{
	Hypothesis	*root;
	Sounding	*buffer;
	u32			hyp;
	
	root = node->depth;
	*nhyp = 0;
	while (root != NULL) {
		if (root->num_samples > 0) ++*nhyp;
		root = root->next;
	}
	if (*nhyp == 0) return(NULL);
	if ((buffer = (Sounding*)calloc(*nhyp, sizeof(Sounding))) == NULL) {
		error_msgv(modname, "error: out of memory allocating %d bytes for %d"
			" soundings for hypothesis enumeration.\n",
			(*nhyp)*sizeof(Sounding), *nhyp);
		*nhyp = -1;
		return(NULL);
	}
	root = node->depth; hyp = 0;
	while (root != NULL) {
		if (root->num_samples > 0) {
			buffer[hyp].file_id = (u16)(root->num_samples & 0xFFFFU);
			buffer[hyp].beam_number = (u8)hyp;
			buffer[hyp].depth = buffer[hyp].range = (f32)(root->cur_estimate);
			buffer[hyp].dz = (f32)(root->cur_variance);
			++hyp;
		}
		root = root->next;
	}
	return(buffer);
}
