/*
 * $Id: mapsheet_nodal.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:30:00  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.11.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.11  2002/05/10 22:03:53  brc
 * Added small modification to initialisation sequence so that the fixed
 * uncertainty being specified can be set as a percentage of depth, rather
 * than a fixed value in meters.
 *
 * Revision 1.10  2001/09/20 19:22:35  brc
 * Made hypothesis surface construction a separate call so that it can be used
 * by mapsheet.c level code (and hence the user).  Modified summary construction
 * code to use this routine, and to use a unique name for the hypothesis grid
 * so that it doesn't conflict with the HyperCUBE grid.
 *
 * Revision 1.9  2001/08/28 16:02:01  brc
 * Added debugging on prior disambiguation method, and indicator of which
 * method is actually in use.
 *
 * Revision 1.8  2001/08/21 01:45:43  brc
 * Added facility to count the number of soundings actually used from each swath
 * to update the data in the mapsheet region.  This can be used, among other things,
 * to determine which data files in a set are actually used in updating the data.
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
#include "stdtypes.h"
#include "error.h"
#include "mapsheet.h"
#include "mapsheet_private.h"
#include "mapsheet_nodal.h"
#include "sounding.h"		/* Interface to soundings structures */
#include "device.h"			/* Interface to device performance metrics */
#include "errmod.h"
#include "ccom_general.h"
#include "params.h"

#ifdef linux
#define FSQRT (float)sqrt
#else
#define FSQRT fsqrt
#endif

#define MIN(a,b) (((a) < (b)) ? (a) : (b))

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

static char *modname = "mapsheet_nodal";
static char *modrev = "$Revision: 2 $";

typedef struct _queue {	/* Queueing structure for the pre-filter filter! */
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
typedef struct _depth_hypothesis {
	f64		cur_estimate;	/* Current depth mean estimate */
	f64		cur_variance;	/* Current depth variance estimate */
	f64		pred_estimate;	/* Current depth next-state mean prediction */
	f64		pred_variance;	/* Current depth next-state variance pred. */
	f64		cum_bayes_fac;	/* Cumulative Bayes factor for node monitoring */
	u16		seq_length;		/* Worst-case sequence length for monitoring */
	u16		hypothesis_num;	/* Index term for debuging */
	u32		num_samples;	/* Number of samples incorporated into this node */
	struct _depth_hypothesis *next;	/* Singly Linked List */
} Hypothesis;

/* 
 * Sample statistics structure.  This is used to track the input sample mean
 * and standard deviation of the data.  Note that the values are accumulated
 * un-normalised, and are only converted to the normal forms when they are
 * actually requested.
 *
 */
typedef struct _sample_stats {
	f64			sum_sq;			/* Simple sum-of-squares for sample variance */
	f64			mean;			/* Simple sum of points for sample mean */
	u32			n_points;		/* Number of points added to node sequence */
} Stats;	

/*
 * The primary estimation structural element.  This maintains the median pre-
 * filter queue, the linked list of depth hypotheses, and the sample statistics
 * for a single node.  The 'write' element is purely for debugging --- setting
 * this for a node results in code all over this module writing out debugging
 * files with lots of interesting data.
 */
typedef struct _depth_node {
	Queue		*queue;		/* Queued points in pre-filter */
	u32			n_queued;	/* Number of elements in queue structure */
	Hypothesis	*depth;		/* Depth hypotheses currently being tracked */
	Stats		stats;		/* Sample statistics structure */
	Bool		write;		/* Debug write out */
} DNode /*, DepthNode */;

/*
 * Extraction method for depth and uncertainty surfaces.  This is used only in
 * the multiple hypothesis case as a way of choosing which hypothesis to
 * extract and report as the `current best guess'.  The MAPNODAL_PRIOR is based
 * on approximate hypothesis probability as estimated by the number of samples
 * incorporated, MAPNODAL_LHOOD uses a local spatial context to choose a guide
 * estimation node (i.e., the closest one with only one hypothesis) and then
 * chooses the hypothesis at the current node using a minimum distance metric;
 * MAPNODAL_POSTERIOR combines both of these to form an approximate Bayesian
 * posterior distribution.  MAPNODAL_UNKN is a sentinel.
 */

typedef enum {
	MAPNODAL_PRIOR = 0,
	MAPNODAL_LHOOD,
	MAPNODAL_POSTERIOR,
	MAPNODAL_UNKN
} MapNodalExtractor;

typedef struct _kal_param {
	Bool	init;			/* System mapsheet initialisation marker */
	MapNodalExtractor mthd;	/* Method used to extract information from sheet */
	f64		null_depth;		/* Depth to initialise estimates */
	f64		null_variance;	/* Variance for initialisation */
	f64		dist_exp;		/* Exponent on distance for variance scale */
	f64		inv_dist_exp;	/* 1.0/dist_exp for efficiency */
	f64		dist_scale;		/* Normalisation coefficient for distance (m) */
	f64		iho_fixed;		/* Fixed portion of IHO error budget (m^2) */
	f64		iho_pcent;		/* Variable portion of IHO error budget (unitless) */
	u32		median_length;	/* Length of median pre-filter sort queue */
	f32		quotient_limit;	/* Outlier quotient upper allowable limit */
	f32		discount;		/* Discount factor for evolution noise variance */
	f32		est_offset;		/* Threshold for significant offset from current
							 * estimate to warrant an intervention */
	f32		bayes_fac_t;	/* Bayes factor threshold for either a single
							 * estimate, or the worst-case recent sequence to
							 * warrant an intervention */
	u32		runlength_t;	/* Run-length threshold for worst-case recent
							 * sequence to indicate a drift failure and hence
							 * to warrant an intervention */
	f32		min_context;	/* Minimum context search range for hypothesis
							 * disambiguation algorithm
							 */
	f32		max_context;	/* Maximum context search range */
	f32		sd2conf_scale;	/* Scale from Std. Dev. to CI */
} KalParam;

#define DEFAULT_NULL_DEPTH		0.0		/* m */
#define DEFAULT_NULL_VARIANCE	1.0e6	/* m^2 */
#define DEFAULT_NULL_SDEV		1.0e3	/* m */

#define DEFAULT_SDEV2CONF_SCALE	CONF_95PC	/* Scale for conf. int. */
#define CONF_99PC				2.56f	/* Scale for 99% CI on Unit Normal */
#define CONF_95PC				1.96f	/* Scale for 95% CI on Unit Normal */
#define CONF_90PC				1.69f	/* Scale for 90% CI on Unit Normal */
#define CONF_68PC				1.00f	/* Scale for 68% CI on Unit Normal */

#define DEFAULT_DIST_EXP		2.0		/* unitless */
#define MIN_DIST_EXP			1.0
#define MAX_DIST_EXP			10.0

#define DEFAULT_MEDIAN_LENGTH	11		/* N.B.: must be odd number for alg. */
#define MIN_MEDIAN_LENGTH		3
#define MAX_MEDIAN_LENGTH		101

#define DEFAULT_QUOTIENT_LIM	1.50	/* Approx. 0.1% F(1,6) */
#define MIN_QUOTIENT_LIM		0.10
#define MAX_QUOTIENT_LIM		255.0	/* Jorgen uses uchar to store */

#define DEFAULT_DISCOUNT		1.0		/* Approx 0.8..1.0 */
#define MIN_DISCOUNT			0.8
#define MAX_DISCOUNT			1.0

#define DEFAULT_EST_OFFSET		4.0		/* Set by West & Harrison's method of
										 * significant percentage points. */
#define MIN_EST_OFFSET			0.1
#define MAX_EST_OFFSET			10.0

#define DEFAULT_BAYES_FAC_T		0.135	/* Set by West & Harrison's method of
										 * significant evidence for M_1 */
#define MIN_BAYES_FAC_T			0.001
#define	MAX_BAYES_FAC_T			10.0

#define DEFAULT_RUNLENGTH_T		5		/* Ball-park figure following West &
										 * Harrison's method. */
#define MIN_RUNLENGTH			1
#define MAX_RUNLENGTH			10

#define DEFAULT_EXTRACTOR		MAPNODAL_LHOOD

#define DEFAULT_MIN_CONTEXT		5.0		/* Minimum context distance, m */
#define MIN_MIN_CONTEXT			0.0
#define MAX_MIN_CONTEXT			1000.0

#define DEFAULT_MAX_CONTEXT		10.0	/* Maximum context distance, m */
#define MIN_MAX_CONTEXT			1.0
#define MAX_MAX_CONTEXT			3000.0

static KalParam default_param = {
	False,					/* Map-Sheet initialisation interlock */
	DEFAULT_EXTRACTOR,
	DEFAULT_NULL_DEPTH,
	DEFAULT_NULL_VARIANCE,
	DEFAULT_DIST_EXP,
	1.0/DEFAULT_DIST_EXP,
	0.0,					/* Distance scale, filled in on clone */
	0.0, 0.0,				/* IHO limits, filled in on clone */
	DEFAULT_MEDIAN_LENGTH,
	DEFAULT_QUOTIENT_LIM,
	DEFAULT_DISCOUNT,
	DEFAULT_EST_OFFSET,
	(f32)DEFAULT_BAYES_FAC_T,
	DEFAULT_RUNLENGTH_T,
	DEFAULT_MIN_CONTEXT,
	DEFAULT_MAX_CONTEXT,
	(f32)DEFAULT_SDEV2CONF_SCALE
};

/* Routine:	mapsheet_nodal_write_param
 * Purpose:	Write parameter structure to file
 * Inputs:	param	Pointer to (opaqued) parameter structure
 *			fd		File descriptor to write on
 * Outputs:	True if write was sucessful, otherwise False
 * Comment:	-
 */

Bool mapsheet_nodal_write_param(void *param, FILE *f)
{
	if (fwrite(param, sizeof(KalParam), 1, f) != 1) {
		error_msg(modname, "failed writing algorithm parameters.\n");
		return(False);
	}
	return(True);
}

/* Routine:	mapsheet_nodal_read_param
 * Purpose:	Read parameter structure from file
 * Inputs:	fd	File descriptor to read from
 * Outputs:	Pointer to (opaqued) parameter structure, or NULL on failure.
 * Comment:	This doesn't do any version checking, which may cause problems in
 *			the future.
 */

void *mapsheet_nodal_read_param(FILE *f)
{
	KalParam	*param;
	
	if ((param = (KalParam*)calloc(1, sizeof(KalParam))) == NULL) {
		error_msg(modname, "failed to allocate algorithm parameter space.\n");
		return(NULL);
	}
	if (fread(param, sizeof(KalParam), 1, f) != 1) {
		error_msg(modname, "failed to read algorithm parameters.\n");
		free(param);
		return(NULL);
	}
	return(param);
}

/* Routine:	mapsheet_nodal_get_hypo
 * Purpose:	Return an array with number of hypotheses counted
 * Inputs:	sheet	MapSheet to work from
 * Outputs:	Returns True if sucessful, otherwise False
 *			*op is filled with the hypotheses counts
 * Comment:	This does over-flow rounding, so that nodes with more than 255
 *			hypotheses (as well as being very sick) are caped at 255.
 */

Bool mapsheet_nodal_get_hypo(MapSheet sheet, u8 *op)
{
	u32		pel, npels = sheet->hdr->n_north * sheet->hdr->n_east, n_hyp;
	DepthNode	*grid;
	Hypothesis	*root;

	grid = sheet->grid->depth_grid[0];
	for (pel = 0; pel < npels; ++pel) {
		root = grid[pel].depth;
		n_hyp = 0;
		while (root != NULL) {
			++n_hyp;
			root = root->next;
		}
		if (n_hyp > 255) n_hyp = 255;
		op[pel] = (u8)n_hyp;
	}
	return(True);
}

/* Routine:	mapsheet_nodal_extend_summary
 * Purpose:	Write extended elements from private data store to summary directory
 * Inputs:	*sheet	Sheet to write from
 *			*dir	Name of the directory to extend elements into
 * Outputs:	True on success, otherwise False
 * Comment:	This writes out a set of images in TIFF format which are only found
 *			in the nodal estimation method.  The actual components which are
 *			written depend on the algorithm currently in use ...
 */

Bool mapsheet_nodal_extend_summary(MapSheet sheet, char *dir)
{
	u8			*hypotheses;
	u32			npels = sheet->hdr->n_north * sheet->hdr->n_east;
	TIFFFlags	flgs = { False, False, False, False };
	
	/* Construct the hypothesis buffer space and then fill, clipping above */
	if ((hypotheses = (u8*)malloc(npels)) == NULL) {
		error_msgv(modname, "error: failed getting hypothesis count buffer"
			" for summary output (%d bytes).\n", npels);
		return(False);
	}
	if (!mapsheet_nodal_get_hypo(sheet, hypotheses)) {
		error_msg(modname, "error: failed getting hypothesis data.\n");
		free(hypotheses);
		return(False);
	}
	if (!ccom_write_tiff(dir, "nodal-nhypotheses.tif", hypotheses, TIFF_U8,
			sheet->hdr->n_north, sheet->hdr->n_east, &flgs)) {
		error_msgv(modname, "error: failed writing \"%s/nhypotheses.tif\" as"
			" hypothesis count data.\n", dir);
		free(hypotheses);
		return(False);
	}
	free(hypotheses);
	return(True);
}

/* Routine:	mapsheet_nodal_init_hypothesis
 * Purpose:	Initialise a hypothesis variable
 * Inputs:	*n	Pointer to the hypothesis
 *			im	Initial mean estimate
 *			iv	Initial variance estimate
 * Outputs:	-
 * Comment:	-
 */

static void mapsheet_nodal_init_hypothesis(Hypothesis *n, f32 im, f32 iv)
{
	memset(n, 0, sizeof(Hypothesis));
	n->cur_estimate = n->pred_estimate = im;
	n->cur_variance = n->pred_variance = iv;
	n->cum_bayes_fac = 1.0f;
	n->seq_length = 0;
	n->num_samples = 1;
}

/* Routine:	mapsheet_nodal_new_hypothesis
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

static Hypothesis *mapsheet_nodal_new_hypothesis(Hypothesis *root,
												 f32 mean, f32 var)
{
	Hypothesis	*rtn;
	
	if ((rtn = (Hypothesis*)malloc(sizeof(Hypothesis))) == NULL) {
		error_msg(modname, "failed getting new hyothesis memory.\n");
		return(NULL);
	}
	mapsheet_nodal_init_hypothesis(rtn, mean, var);
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

/* Routine:	mapsheet_nodal_release_hypotheses
 * Purpose:	Releases memory associated with a linked list of hypotheses
 * Inputs:	*root	Pointer to start of the linked list
 * Outputs:	-
 * Comment:	-
 */

static void mapsheet_nodal_release_hypotheses(Hypothesis *root)
{
	Hypothesis	*next;
	
	while (root != NULL) {
		next = root->next;
		free(root);
		root = next;
	}
}

/* Routine:	mapsheet_nodal_release_nodes
 * Purpose:	Release all dynamically allocated memory associated with a node
 * Inputs:	*node	Pointer to the node array
 *			nnodes	Number of nodes in the array
 * Outputs:	-
 * Comment:	-
 */

void mapsheet_nodal_release_nodes(DepthNode *node, u32 nnodes)
{
	u32	n;
	
	for (n = 0; n < nnodes; ++n) {
		mapsheet_nodal_release_hypotheses(node[n].depth);
		free(node[n].queue);
	}
}

/* Routine:	mapsheet_nodal_monitor
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

static Bool mapsheet_nodal_monitor(Hypothesis *n, float yn, float vn, KalParam *p)
{
	f32	error, forecast_var, bayes_factor;
	
	forecast_var = (f32)(n->pred_variance + vn);
	error = (f32)((yn - n->pred_estimate)/sqrt(forecast_var));
	if (error >= 0.0)
		bayes_factor =
			(f32)exp(0.5*(p->est_offset*p->est_offset - 2.0*p->est_offset*error));
	else
		bayes_factor =
			(f32)exp(0.5*(p->est_offset*p->est_offset + 2.0*p->est_offset*error));

	/* Check for single component failure */
	if (bayes_factor < p->bayes_fac_t) {
		/* This is a potential outlier.  Indicate potential failure. */
		return(False);
	}
	
	/* Update monitors */
	if (n->cum_bayes_fac < 1.0)
		++n->seq_length;
	else
		n->seq_length = 1;
	n->cum_bayes_fac = bayes_factor * MIN(1.0, n->cum_bayes_fac);
	
	/* Check for consequtive failure errors */
	if (n->cum_bayes_fac < p->bayes_fac_t ||
		n->seq_length > p->runlength_t) {
		/* Indicate intervention required */
		return(False);
	}
	
	/* Otherwise, the current model is still supported ... */
	return(True);
}


/* Routine:	mapsheet_nodal_resetmonitor
 * Purpose:	Reset monitoring structure to defaults
 * Inputs:	*n	Pointer to hypothesis to reset
 * Outputs:	-
 * Comment:	-
 */

static void mapsheet_nodal_resetmonitor(Hypothesis *n)
{
	n->cum_bayes_fac = 1.0;
	n->seq_length = 0;
}

/* Routine:	mapsheet_nodal_update_stats
 * Purpose:	Update statistics associated with the node (sample depth pointwise)
 * Inputs:	*s			Pointer to node statistics module
 *			depth		Current depth to be included
 * Outputs:	-
 * Comment:	-
 */

static void mapsheet_nodal_update_stats(Stats *s, f32 depth)
{
	s->mean += depth;
	s->sum_sq += depth*depth;
	++(s->n_points);
}

/* Routine:	mapsheet_nodal_update_hypothesis
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

static Bool mapsheet_nodal_update_hypothesis(Hypothesis *h, f32 depth,
											 f32 variance, KalParam *p)
{
	f64		gain, sys_variance, innovation;

	/* Check current estimate with monitoring */
	if (!mapsheet_nodal_monitor(h, depth, variance, p)) return(False);

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

/* Routine:	mapsheet_nodal_best_hypothesis
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

static Hypothesis *mapsheet_nodal_best_hypothesis(Hypothesis *root,
												  f32 yn, f32 varn)
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

/* Routine:	mapsheet_nodal_update_node
 * Purpose:	Update the Kalman filter equations for this node and input
 * Inputs:	*node	DepthNode structure to work on
 *			depth	Depth estimate to incorporate
 *			var		Estimate of depth estimate variance
 *			*p		Kalman filtering parameters used to get at monitoring
 *					algorithm parameters, and the discount factor for the
 *					previous variance in order to compute the current evolution
 *					noise (a.k.a. system noise variance).
 * Outputs:	-
 * Comment:	This runs the basic Kalman filter equations, using the innovations
 *			formulation.  Note that the updates have to be done in double
 *			precision to ensure that we don't have critical round-off problems.
 *				This algorithm now includes a discounted system noise variance
 *			model to set the evolution noise dynamically depending on the
 *			variance that was estimated at the previous stage (West & Harrison,
 *			'Bayesian Forecasting and Dynamic Models', Springer, 2ed., 1997,
 *			ch.2), and a monitoring scheme and feed-back interventions to allow
 *			the code to check that the estimates are staying in touch with the
 *			input data.  The monitoring scheme is also based on West & Harrison
 *			as above, Ch.11, Sec. 11.5.1, using cumulative Bayes factors and the
 *			unidirectional level shift alternate model.
 */

static Bool mapsheet_nodal_update_node(DepthNode *node, f32 depth, f32 variance,
									   KalParam *p)
{
	static FILE *debug = NULL;
	Hypothesis	*best, *tmp;

	if (node->write) {
		if (debug == NULL) {
			if ((debug = fopen("debug-kfs.txt", "w")) == NULL) {
				error_msg(modname, "*** debug: failed to open output file.\n");
				exit(1);
			}
		}
	}
	
	/* Find the best matching hypothesis for the current input sample given
	 * those currently being tracked.
	 */
	if ((best = mapsheet_nodal_best_hypothesis(node->depth, depth, variance))
																	== NULL) {
		/* Didn't match a `best' hypothesis ... this only happens when there
		 * are *no* hypotheses (i.e., intialisation mode).  So we add a new
		 * hypothesis directly.
		 */
		if ((tmp = mapsheet_nodal_new_hypothesis(node->depth, depth, variance))
																	== NULL) {
			error_msg(modname, "error: failed to add new depth hypothesis"
				" (initialisation).\n");
			return(False);
		}
		node->depth = tmp;
		best = node->depth;
	} else {
		/* Update the best hypothesis with the current data */
		if (!mapsheet_nodal_update_hypothesis(best, depth, variance, p)) {
			/* Failed update --- indicates an intervention, so that we need to
			 * start a new hypothesis to capture the outlier/datum shift.
			 */
			mapsheet_nodal_resetmonitor(best);
			if ((tmp = mapsheet_nodal_new_hypothesis(node->depth, depth,
														variance)) == NULL) {
				error_msg(modname, "error: failed to add new depth hypothesis"
					" (intervention).\n");
				return(False);
			}
			node->depth = tmp; /* Re-link new root of linked list into node */
			best = node->depth;
		}
	}
	
	/* Update overall nodal statistics */
	mapsheet_nodal_update_stats(&(node->stats), depth);

	if (node->write) {
		fprintf(debug, "%d %f %f %f %f %f %f\n",
				best->hypothesis_num,
				best->cur_estimate,
				best->cur_variance,
				depth, variance, node->stats.mean / node->stats.n_points,
				node->stats.sum_sq / node->stats.n_points -
					(node->stats.mean*node->stats.mean)/
						(node->stats.n_points*node->stats.n_points));
		fflush(debug);
	}
	
	return(True);	
}

/* Routine:	mapsheet_nodal_truncate
 * Purpose:	Truncate a buffered sequence to reject outliers
 * Inputs:	*node	DepthNode to work through
 *			*p		KalParam structure to use for quotient limit
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

static void mapsheet_nodal_truncate(DepthNode *node, KalParam *p)
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
	for (pt = 0; pt < (s32)node->n_queued; ++pt)
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

/* Routine:	mapsheet_nodal_queue_flush_node
 * Purpose:	Flush a single node queue into the Kalman filter input sequence
 * Inputs:	*node	Pointer to the DepthNode to process
 *			*param	Kalman filter parameters structure
 * Outputs:	-
 * Comment:	This flushes the queue into the input sequence in order (i.e., take
 *			current median, resort, repeat).  Since the queue is always sorted,
 *			we can just walk the list in order, rather than having to resort
 *			or shift data, etc.  When we have an even number of points, we take
 *			the shallowest of the points first; this means that we walk the list
 *			alternately to the left and right, starting to the right if the
 *			initial number of points is even, and to the left if the number of
 *			points is odd.  To avoid shifting the data, we just increase the
 *			step after every extraction, until we step off the LHS of the array.
 */

static void mapsheet_nodal_queue_flush_node(DepthNode *node, KalParam *param)
{
	s32		ex_pt, direction, scale = 1;
	
	if (node->n_queued == 0) return; /* Not allocated, or flushed already */
	
	if (node->write) {
		FILE	*debug = fopen("debug-flush.txt", "w");
		u32		i;
		
		for (i = 0; i < node->n_queued; ++i)
			fprintf(debug, "%f %f\n", node->queue[i].depth, node->queue[i].var);
		fclose(debug);
	}
	mapsheet_nodal_truncate(node, param);
	if (node->n_queued%2 == 0) {
		/* Even number of points in the queue at starting point */
		ex_pt = node->n_queued/2-1;
		direction = +1;
	} else {
		/* Odd number of points in the queue at starting point */
		ex_pt = node->n_queued/2;
		direction = -1;
	}
	while (ex_pt >= 0) {
		mapsheet_nodal_update_node(node,
							node->queue[ex_pt].depth, node->queue[ex_pt].var,
							param);
		ex_pt += direction*scale;
		direction = -direction;
		scale ++;
	}
	free(node->queue);
	node->queue = NULL;
	node->n_queued = 0;
}

/* Routine:	mapsheet_nodal_finalise
 * Purpose:	Flush all input pre-filter queues into the grid so that true
 *			estimates can be extracted
 * Inputs:	sheet	MapSheet associated with grid of DepthNodes
 *			*param	Pointer to the parameters structure for the module
 *			**ip	Pointer to the DepthNode grid
 * Outputs:	- (queues are flushed and freed)
 * Comment:	This flushes all of the queues into the Kalman filter input sequence
 *			for their respective nodes, and then frees the queue structures,
 *			returning the memory to the heap.  This routine should be called
 *			before extracting data from the grid, and may be used to free up
 *			memory for other nodes.
 */

void mapsheet_nodal_finalise(MapSheet sheet, void *param, DepthNode **ip)
{
	u32	pel, npels = sheet->hdr->n_east * sheet->hdr->n_north;

	/* ------ HACK ------ HACK ----- HACK ------ HACK ------ HACK ------ */
	error_msg(modname, "debug: HACK! Finalisation sequence aborted to avoid"
		" duff data in the output KFS.\n");
	return;
	
	for (pel = 0; pel < npels; ++pel)
		mapsheet_nodal_queue_flush_node(ip[0]+pel, (KalParam*)param);
}

/* Routine:	mapsheet_nodal_read_surface
 * Purpose:	De-serialise data for surface from input
 * Inputs:	f		File descriptor to read from
 *			sheet	Mapsheet to work on
 * Outputs:	True if sheet was read, otherwise False.
 * Comment:	The sheet is written as a sequence of DepthNode structures, as they
 *			are in memory.  The n_queued element of each is reset on write to
 *			be the number of depth hypotheses being tracked; the DepthNode is
 *			followed by this number (possibly zero) of Hypothesis structures,
 *			also as they occur in memory.  Note that the Hypothesis structures
 *			form a linked list, which is reconstructed here directly.
 */

Bool mapsheet_nodal_read_surface(FILE *f, MapSheet sheet)
{
	u32			pel, npels = sheet->hdr->n_north * sheet->hdr->n_east,
				hyp, n_hyp;
	Hypothesis	dummy, *tmp;
	DepthNode	*grid = sheet->grid->depth_grid[0];
	
	for (pel = 0; pel < npels; ++pel, ++grid) {
		if (fread(grid, sizeof(DepthNode), 1, f) != 1) {
			error_msgv(modname, "error: failed reading node %d.\n", pel);
			return(False);
		}
		n_hyp = grid->n_queued;
		grid->n_queued = 0; grid->queue = NULL; /* Reset queue */
		grid->depth = NULL;					/* Zero node root of linked list */
		for (hyp = 0; hyp < n_hyp; ++hyp) {
			if (fread(&dummy, sizeof(Hypothesis), 1, f) != 1) {
				error_msgv(modname, "error: failed reading hypothesis %d at"
					" node %d.\n", hyp, pel);
				return(False);
			}
			if ((tmp = (Hypothesis*)malloc(sizeof(Hypothesis))) == NULL) {
				error_msgv(modname, "error: failed allocating memory for"
					" hypothesis %d at node %d.\n", hyp, pel);
				return(False);
			}
			memcpy(tmp, &dummy, sizeof(Hypothesis));
			tmp->next = grid->depth;
			grid->depth = tmp;
		}
	}
	
	return(True);
}

/* Routine:	mapsheet_nodal_write_grid
 * Purpose:	Serialise a mapsheet node grid to file
 * Inputs:	f		File descriptor to write on
 *			**g		Grid of DepthNode structures to write
 *			*param	Parameters for the depth construction algorithm
 *			npels	Number of pels in the whole node grid
 * Outputs:	True if the sheet was converted and flushed, otherwise False
 * Comment:	This code writes the mapsheet as a sequence of DepthNode structures
 *			followed by a sequence of Hypothesis structures depending on how
 *			many depth estimates we're currently tracking at each node.  The
 *			n_queued element is used to hold the number of hypotheses in the
 *			file, and the queue is currently abandoned.  Note that this write
 *			sequence pops the hypotheses from the top of the stack into the file
 *			and the reader pushes them back on, in the process reversing the
 *			order.  This shouldn't matter to the rest of the code.
 */

Bool mapsheet_nodal_write_grid(FILE *f, DepthNode **g, void *param, u32 npels)
{
	u32			pel, n_hyp;
	DepthNode	*grid = g[0];
	Hypothesis	*tmp;
	
	/* ------ HACK ------ HACK ----- HACK ------ HACK ------ HACK ------ */
	error_msg(modname, "debug: HACK! Finalisation sequence aborted to avoid"
		" duff data in the output KFS.\n");
	
	for (pel = 0; pel < npels; ++pel, ++grid) {
		n_hyp = 0; tmp = grid->depth;
		while (tmp != NULL) {
			++n_hyp;
			tmp = tmp->next;
		}
		grid->n_queued = n_hyp;
		if (fwrite(grid, sizeof(DepthNode), 1, f) != 1) {
			error_msgv(modname, "error: failed writing node %d.\n", pel);
			return(False);
		}
		tmp = grid->depth; n_hyp = 0;
		while (tmp != NULL) {
			if (fwrite(tmp, sizeof(Hypothesis), 1, f) != 1) {
				error_msgv(modname, "error: failed writing hypothesis %d of"
					" node %d.\n", n_hyp, pel);
				return(False);
			}
			tmp = tmp->next;
		}
		grid->queue = NULL;
		grid->n_queued = 0;
	}
	
	return(True);
}

/* Routine:	mapsheet_modal_alloc_grid
 * Purpose:	Alocate workspace for the DepthNode estimation grid
 * Inputs:	width, height	Dimensions required for the grid
 * Outputs:	Returns pointer to the DepthNode grid, or NULL
 * Comment:	-
 */

DepthNode **mapsheet_nodal_alloc_grid(u32 width, u32 height)
{
	DepthNode	**rtn;
	u32	row;
	
	if ((rtn = (DepthNode**)calloc(height, sizeof(DepthNode*))) == NULL ||
		(rtn[0] = (DepthNode*)calloc(height*width, sizeof(DepthNode))) == NULL) {
		error_msgv(modname, "failed to allocate depth node grid (%dx%d).\n",
					width, height);
		if (rtn != NULL) { free(rtn[0]); free(rtn); rtn = NULL; }
	} else
		for (row = 1; row < height; ++row) rtn[row] = rtn[row-1] + width;
	return(rtn);
}

/* Routine:	mapsheet_nodal_choose_hypothesis
 * Purpose:	Choose the current best hypothesis for the node in question
 * Inputs:	*list	Pointer to the list of hypotheses
 *			*best	Pointer to buffer for single best hypothesis
 * Outputs:	True if the sort took place, otherwise False (no buffer space).
 * Comment:	In this context, `best' means `hypothesis with most samples',
 *			rather than through any other metric.  This may not be the `best'
 *			until add of the data is in, but it should give an idea of what's
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

static Bool mapsheet_nodal_choose_hypothesis(Hypothesis *list, Hypothesis *best)
{
	static Hypothesis	*buffer = NULL;
	static int			n_buffer = 0;
	
	Hypothesis	*tmp;
	u32			num_hyp;
	
	/* We need to count the depth hypotheses, and then write them into a linear
	 * array for sorting.  Note that we keep a static buffer here (and hence
	 * leak a little) to avoid having to free and reallocate on each call, which
	 * can be a significant overhead with a large array of nodes.
	 */
	num_hyp = 0; tmp = list;
	while (tmp != NULL) {
		++num_hyp;
		tmp = tmp->next;
	}
	if (num_hyp > (u32)n_buffer) {
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
		memcpy(buffer + num_hyp, tmp, sizeof(Hypothesis));
		tmp = tmp->next;
		++num_hyp;
	}
	qsort(buffer, num_hyp, sizeof(Hypothesis), hypothesis_sort_fn);
	memcpy(best, buffer, sizeof(Hypothesis));
	return(True);
}

/* Routine:	mapsheet_nodal_get_context_depth_unct
 * Purpose:	Extract depth surface from nodal representation, with spatial
 *			context used to choose the best hypothesis
 * Inputs:	sheet	Mapsheet for information
 *			**ip	Pointer to input grid of DepthNode structures
 *			*par	Raw parameter structure associated with grid
 * Outputs:	depth	Pointer to linear (row major) array of data to hold the
 *					output depth sequence (or NULL)
 *			unct	Pointer to linear (row major) array of data to hold the
 *					output uncertainty sequence (or NULL)
 * Comment:	This extracts the current best estimates from the DepthNode grid of
 *			structures.  This provides a current snap-shot, not necessarily the
 *			final results from the whole estimation (but then, you don't have to
 *			wait for all of the data to be available either!)
 *				Where there is no current depth hypothesis (i.e., there is no
 *			data), the depth is reported as DEFAULT_NULL_DEPTH.  Where there is
 *			only one hypothesis, that depth is reported.  Where there are
 *			multiple competing hypotheses, the algorithm searches the immediate
 *			vicinity in the grid looking for the closest node with only one
 *			depth hypothesis (and hence a presumably good node with no outliers
 *			or level shifts, etc.)  The hypothesis closest in the minimum
 *			distance sense according to the current estimate of mean and
 *			variance is chosen as the output.  Target search nodes outside the
 *			grid bounds are treated as nodes with no depth hypothesis.
 *				Note that this code can be used to extract depth, uncertainty,
 *			or both.  If _depth_ or _unct_ are non-NULL, then the data is
 *			extracted; otherwise the dataset is ignored.  An error is reported
 *			if both are NULL!
 */

Bool mapsheet_nodal_get_context_depth_unct(MapSheet sheet, DepthNode **ip,
										void *par, f32p depth, f32p unct)
{
	s32			row, col, rows = sheet->hdr->n_north, cols = sheet->hdr->n_east;
	s32			offset, dr, dc, target_r, target_c;
	f64			error, min_error, mean, var;
	Hypothesis	best, *root, *soln;
	DepthNode	*closest;
	KalParam	*p = (KalParam*)par;
#ifdef __HYP_DEBUG__
	u32			closest_r, closest_c;
#endif

	for (row = 0; row < rows; ++row) {
		for (col = 0; col < cols; ++col) {
			if (ip[row][col].depth == NULL) {

#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	error_msgv(modname, "debug: node (%d, %d) - no hypotheses.\n", row, col);
}
#endif
				if (depth != NULL) *depth++ = (f32)p->null_depth;
				if (unct != NULL)
					*unct++ = (f32)(p->sd2conf_scale * sqrt(p->null_variance));
			} else if (ip[row][col].depth->next == NULL) {
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	error_msgv(modname, "debug: node (%d, %d) - one hypothesis: z = %f m, "
		"v = %f m^2, nj = %d.\n",
		row, col,
		ip[row][col].depth->cur_estimate,
		ip[row][col].depth->cur_variance,
		ip[row][col].depth->num_samples);
}
#endif
				if (depth != NULL)
					*depth++ = (f32)ip[row][col].depth->cur_estimate;
				if (unct != NULL)
					*unct++ = (f32)(p->sd2conf_scale *
									sqrt(ip[row][col].depth->cur_variance));
			} else {
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	error_msgv(modname, "debug: node (%d, %d) - multiple (%d) hypotheses:\n",
		row, col, ip[row][col].depth->hypothesis_num);
	root = ip[row][col].depth;
	while (root != NULL) {
		error_msgv(modname, "debug:  hyp %d, z = %f m, v = %f m, nj = %d.\n",
			root->hypothesis_num, root->cur_estimate, root->cur_variance,
			root->num_samples);
		root = root->next;
	}
}
#endif
				/* Multiple depth estimates are present.  We have to search
				 * the local grid in ever increasing circles (well, square
				 * annuli) until we find the first node with only one depth
				 * hypothesis.
				 */
				closest = NULL;
				for (offset = (s32)(p->min_context);
					 offset <= (s32)(p->max_context) && closest == NULL;
																	++offset) {
					target_r = row - offset;
					if (target_r >= 0 && target_r < rows)
						for (dc = -offset; dc <= offset; ++dc) {
							target_c = col + dc;
							if (target_c < 0 || target_c >= cols) continue;
							if (ip[target_r][target_c].depth != NULL &&
								ip[target_r][target_c].depth->next == NULL) {
								closest = ip[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
							}
						}
					target_r = row + offset;
					if (target_r >= 0 && target_r < rows)
						for (dc = -offset; dc <= offset; ++dc) {
							target_c = col + dc;
							if (target_c < 0 || target_c >= cols) continue;
							if (ip[target_r][target_c].depth != NULL &&
								ip[target_r][target_c].depth->next == NULL) {
								closest = ip[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
							}
						}
					target_c = col - offset;
					if (target_c >= 0 && target_c < cols)
						for (dr = -offset+1; dr <= offset-1; ++dr) {
							target_r = row + dr;
							if (target_r < 0 || target_r >= rows) continue;
							if (ip[target_r][target_c].depth != NULL &&
								ip[target_r][target_c].depth->next == NULL) {
								closest = ip[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
							}
						}
					target_c = col + offset;
					if (target_c >= 0 && target_c < cols)
						for (dr = -offset+1; dr <= offset-1; ++dr) {
							target_r = row + dr;
							if (target_r < 0 || target_r >= rows) continue;
							if (ip[target_r][target_c].depth != NULL &&
								ip[target_r][target_c].depth->next == NULL) {
								closest = ip[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
							}
						}					
				}
				if (closest == NULL) {
					/* Didn't match anything within the allowed search region,
					 * so we reconstruct using the maximum posterior occurence.
					 */
					if (!mapsheet_nodal_choose_hypothesis(ip[row][col].depth,
																	&best)) {
						error_msgv(modname, "error: failed choosing a best"
							" hypothesis at node (%d,%d).\n", row, col);
						return(False);
					}
					if (depth != NULL) *depth++ = (f32)best.cur_estimate;
					if (unct != NULL)
						*unct++ = (f32)(p->sd2conf_scale * sqrt(best.cur_variance));
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	error_msgv(modname, "debug:  no matching closest single hypothesis node"
		" to (%d, %d) in range (+- %f nodes).  Reverting to longest-standing"
		" algorithm.\n", row, col, p->max_context);
	error_msgv(modname, "debug:  best: hyp. %d, z = %f m, v = %f m, "
		"nj = %d.\n", best.hypothesis_num, best.cur_estimate,
		best.cur_variance, best.num_samples);
}
#endif
				} else {
					/* Search for the closest current hypothesis to the
					 * specified node's single hypothesis.
					 */
					root = ip[row][col].depth;
					mean = closest->depth->cur_estimate;
					var = closest->depth->cur_variance;
					min_error = FLT_MAX; soln = NULL;
					while (root != NULL) {
						error = fabs((root->cur_estimate - mean)/sqrt(var));
						if (error < min_error) {
							min_error = error;
							soln = root;
						}
						root = root->next;
					}
					if (depth != NULL)
						*depth++ = (f32)soln->cur_estimate;
					if (unct != NULL)
						*unct++ = (f32)(p->sd2conf_scale*sqrt(best.cur_variance));
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	error_msgv(modname, "debug:  closest matching single hypothesis node at"
		"(%d, %d), z = %f m, v = %f m, nj = %d.\n", closest_r, closest_c,
		closest->depth->cur_estimate, closest->depth->cur_variance,
		closest->depth->num_samples);
	error_msgv(modname, "debug:  best match: hyp. %d.\n", soln->hypothesis_num);
}
#endif
				}
			}
		}
	}
	return(True);
}

/* Routine: mapsheet_nodal_get_context_depth
 * Purpose:	Extract just depth component of MapSheet using context specific
 *			(guide node selection) algorithm
 * Inputs:	sheet	MapSheet structure to work on
 *			**ip	Input grid of (row major) depth estimation nodes
 *			*par	Private parameter pointer for algorithm
 * Outputs:	*depth	Linear (row major) array for output
 * Comment:	This is a convenience function that just constructs a call-through
 *			to the combined depth/uncertainty extractor.
 */

static Bool mapsheet_nodal_get_context_depth(MapSheet sheet, DepthNode **ip,
											 void *par, f32p depth)
{
	return(mapsheet_nodal_get_context_depth_unct(sheet, ip, par, depth, NULL));
}

/* Routine: mapsheet_nodal_get_context_unct
 * Purpose:	Extract just uncertainty component of MapSheet using context
 *			specific (guide node selection) algorithm
 * Inputs:	sheet	MapSheet structure to work on
 *			**ip	Input grid of (row major) depth estimation nodes
 *			*par	Private parameter pointer for algorithm
 * Outputs:	*depth	Linear (row major) array for output
 * Comment:	This is a convenience function that just constructs a call-through
 *			to the combined depth/uncertainty extractor.
 */

static Bool mapsheet_nodal_get_context_unct(MapSheet sheet, DepthNode **ip,
											void *par, f32p unct)
{
	return(mapsheet_nodal_get_context_depth_unct(sheet, ip, par, NULL, unct));
}

/* Routine:	mapsheet_nodal_get_posterior_depth_unct
 * Purpose:	Extract depth surface from nodal representation, with spatial
 *			context used and prior hypothesis probability to choose the best
 *			hypothesis
 * Inputs:	sheet	Mapsheet for information
 *			**ip	Pointer to input grid of DepthNode structures
 *			*par	Raw parameter structure associated with grid
 * Outputs:	depth	Pointer to linear (row major) array of data to hold the
 *					output depth sequence (or NULL)
 *			unct	Pointer to linear (row major) array of data to hold the
 *					output uncertainty sequence (or NULL)
 * Comment:	This extracts the current best estimates from the DepthNode grid of
 *			structures.  This provides a current snap-shot, not necessarily the
 *			final results from the whole estimation (but then, you don't have to
 *			wait for all of the data to be available either!)
 *				Where there is no current depth hypothesis (i.e., there is no
 *			data), the depth is reported as DEFAULT_NULL_DEPTH.  Where there is
 *			only one hypothesis, that depth is reported.  Where there are
 *			multiple competing hypotheses, the algorithm searches the immediate
 *			vicinity in the grid looking for the closest node with only one
 *			depth hypothesis (and hence a presumably good node with no outliers
 *			or level shifts, etc.)  The hypothesis closest in the maximum
 *			probability sense according to the current estimate of mean and
 *			variance, and approximate hypothesis posterior density is chosen as
 *			the output.  Target search nodes outside the grid bounds are treated
 *			as nodes with no depth hypothesis.
 *				Note that this code can be used to extract depth, uncertainty,
 *			or both.  If _depth_ or _unct_ are non-NULL, then the data is
 *			extracted; otherwise the dataset is ignored.  An error is reported
 *			if both are NULL!
 */

Bool mapsheet_nodal_get_posterior_depth_unct(MapSheet sheet, DepthNode **ip,
										void *par, f32p depth, f32p unct)
{
	s32			row, col, rows = sheet->hdr->n_north, cols = sheet->hdr->n_east;
	s32			offset, dr, dc, target_r, target_c;
	f64			posterior, max_posterior, sample, mean, var;
	Hypothesis	best, *root, *soln;
	DepthNode	*closest;
	KalParam	*p = (KalParam*)par;
#ifdef __HYP_DEBUG__
	u32			closest_r, closest_c;
#endif

	for (row = 0; row < rows; ++row) {
		for (col = 0; col < cols; ++col) {
			if (ip[row][col].depth == NULL) {

#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	error_msgv(modname, "debug: node (%d, %d) - no hypotheses.\n", row, col);
}
#endif
				if (depth != NULL) *depth++ = (f32)p->null_depth;
				if (unct != NULL)
					*unct++ = (f32)(p->sd2conf_scale * sqrt(p->null_variance));
			} else if (ip[row][col].depth->next == NULL) {
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	error_msgv(modname, "debug: node (%d, %d) - one hypothesis: z = %f m, "
		"v = %f m^2, nj = %d.\n",
		row, col,
		ip[row][col].depth->cur_estimate,
		ip[row][col].depth->cur_variance,
		ip[row][col].depth->num_samples);
}
#endif
				if (depth != NULL)
					*depth++ = (f32)ip[row][col].depth->cur_estimate;
				if (unct != NULL)
					*unct++ = (f32)(p->sd2conf_scale *
									sqrt(ip[row][col].depth->cur_variance));
			} else {
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	error_msgv(modname, "debug: node (%d, %d) - multiple (%d) hypotheses:\n",
		row, col, ip[row][col].depth->hypothesis_num);
	root = ip[row][col].depth;
	while (root != NULL) {
		error_msgv(modname, "debug:  hyp %d, z = %f m, v = %f m, nj = %d.\n",
			root->hypothesis_num, root->cur_estimate, root->cur_variance,
			root->num_samples);
		root = root->next;
	}
}
#endif
				/* Multiple depth estimates are present.  We have to search
				 * the local grid in ever increasing circles (well, square
				 * annuli) until we find the first node with only one depth
				 * hypothesis.
				 */
				closest = NULL;
				for (offset = (s32)(p->min_context);
					 offset <= (s32)(p->max_context) && closest == NULL;
																	++offset) {
					target_r = row - offset;
					if (target_r >= 0 && target_r < rows)
						for (dc = -offset; dc <= offset; ++dc) {
							target_c = col + dc;
							if (target_c < 0 || target_c >= cols) continue;
							if (ip[target_r][target_c].depth != NULL &&
								ip[target_r][target_c].depth->next == NULL) {
								closest = ip[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
							}
						}
					target_r = row + offset;
					if (target_r >= 0 && target_r < rows)
						for (dc = -offset; dc <= offset; ++dc) {
							target_c = col + dc;
							if (target_c < 0 || target_c >= cols) continue;
							if (ip[target_r][target_c].depth != NULL &&
								ip[target_r][target_c].depth->next == NULL) {
								closest = ip[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
							}
						}
					target_c = col - offset;
					if (target_c >= 0 && target_c < cols)
						for (dr = -offset+1; dr <= offset-1; ++dr) {
							target_r = row + dr;
							if (target_r < 0 || target_r >= rows) continue;
							if (ip[target_r][target_c].depth != NULL &&
								ip[target_r][target_c].depth->next == NULL) {
								closest = ip[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
							}
						}
					target_c = col + offset;
					if (target_c >= 0 && target_c < cols)
						for (dr = -offset+1; dr <= offset-1; ++dr) {
							target_r = row + dr;
							if (target_r < 0 || target_r >= rows) continue;
							if (ip[target_r][target_c].depth != NULL &&
								ip[target_r][target_c].depth->next == NULL) {
								closest = ip[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
							}
						}					
				}
				if (closest == NULL) {
					/* Didn't match anything within the allowed search region,
					 * so we reconstruct using the maximum posterior occurence.
					 */
					if (!mapsheet_nodal_choose_hypothesis(ip[row][col].depth,
																	&best)) {
						error_msgv(modname, "error: failed choosing a best"
							" hypothesis at node (%d,%d).\n", row, col);
						return(False);
					}
					if (depth != NULL) *depth++ = (f32)best.cur_estimate;
					if (unct != NULL)
						*unct++ = (f32)(p->sd2conf_scale * sqrt(best.cur_variance));
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	error_msgv(modname, "debug:  no matching closest single hypothesis node in "
		"range (+- %d nodes).  Reverting to longest-standing algorithm.\n",
		row, col, p->max_context);
	error_msgv(modname, "debug:  best: hyp. %d, z = %f m, v = %f m, "
		"nj = %d.\n", best.hypothesis_num, best.cur_estimate,
		best.cur_variance, best.num_samples);
}
#endif
				} else {
					/* Search for the closest current hypothesis to the
					 * specified node's single hypothesis.
					 */
					root = ip[row][col].depth;
					sample = closest->depth->cur_estimate;
					max_posterior = -FLT_MAX; soln = NULL;
					while (root != NULL) {
						mean = root->cur_estimate;
						var = root->cur_variance;
						posterior = -log(sqrt(var))
									-(sample-mean)*(sample-mean)/(2.0*var)
									+log(root->num_samples);
						if (posterior > max_posterior) {
							max_posterior = posterior;
							soln = root;
						}
						root = root->next;
					}
					if (depth != NULL)
						*depth++ = (f32)soln->cur_estimate;
					if (unct != NULL)
						*unct++ = (f32)(p->sd2conf_scale*sqrt(soln->cur_variance));
#ifdef __HYP_DEBUG__
if (ip[row][col].write) {
	error_msgv(modname, "debug:  closest matching single hypothesis node at"
		"(%d, %d), z = %f m, v = %f m, nj = %d.\n", closest_r, closest_c,
		closest->depth->cur_estimate, closest->depth->cur_variance,
		closest->depth->num_samples);
	error_msgv(modname, "debug:  best match: hyp. %d.\n", soln->hypothesis_num);
}
#endif
				}
			}
		}
	}
	return(True);
}

/* Routine: mapsheet_nodal_get_posterior_depth
 * Purpose:	Extract just depth component of MapSheet using approximate posterior
 *			probability algorithm
 * Inputs:	sheet	MapSheet structure to work on
 *			**ip	Input grid of (row major) depth estimation nodes
 *			*par	Private parameter pointer for algorithm
 * Outputs:	*depth	Linear (row major) array for output
 * Comment:	This is a convenience function that just constructs a call-through
 *			to the combined depth/uncertainty extractor.
 */

static Bool mapsheet_nodal_get_posterior_depth(MapSheet sheet, DepthNode **ip,
										void *par, f32p depth)
{
	return(mapsheet_nodal_get_posterior_depth_unct(sheet,ip,par,depth,NULL));
}

/* Routine: mapsheet_nodal_get_posterior_unct
 * Purpose:	Extract just uncertainty component of MapSheet using approximate
 *			posterior probability algorithm
 * Inputs:	sheet	MapSheet structure to work on
 *			**ip	Input grid of (row major) depth estimation nodes
 *			*par	Private parameter pointer for algorithm
 * Outputs:	*depth	Linear (row major) array for output
 * Comment:	This is a convenience function that just constructs a call-through
 *			to the combined depth/uncertainty extractor.
 */

static Bool mapsheet_nodal_get_posterior_unct(MapSheet sheet, DepthNode **ip,
										void *par, f32p unct)
{
	return(mapsheet_nodal_get_posterior_depth_unct(sheet, ip, par, NULL, unct));
}

/* Routine:	mapsheet_nodal_get_prior_depth_unct
 * Purpose:	Extract both depth and uncertainty surfaces using the approximate
 *			hypothesis probability as computed by number of samples
 * Inputs:	sheet	MapSheet structure to work on
 *			**ip	Input grid of (row major) depth estimation nodes
 *			*par	Private parameter pointer for algorithm
 * Outputs:	*depth	Linear (row major) array for output
 *			*unct	Linear (row major) array for output
 *			True if extraction worked, otherwise False.
 * Comment:	This routine extracts either surface (set pointer to NULL to avoid
 *			extraction) for efficiency purposes (takes a while to work out the
 *			disambiguation, so we might as well extract both surfaces while we
 *			have them).
 */

static Bool mapsheet_nodal_get_prior_depth_unct(MapSheet sheet, DepthNode **ip,
												void *par,
												f32p depth, f32p unct)
{
	u32			npels = sheet->hdr->n_east * sheet->hdr->n_north, pel;
	DepthNode	*node = ip[0];
	Hypothesis	best;
	KalParam	*p = (KalParam*)par;
#ifdef __HYP_DEBUG__
	Hypothesis	*root;
	u32			row, col;
#endif
	
	for (pel = 0; pel < npels; ++pel, ++node) {
		if (node->depth == NULL) {
			if (depth != NULL) *depth++ = (f32)p->null_depth;
			if (unct != NULL)
				*unct++ = (f32)(p->sd2conf_scale * sqrt(p->null_variance));
		} else if (node->depth->next == NULL) {
			/* Special case: only one depth hypothesis (the usual case) */
			if (depth != NULL) *depth++ = (f32)node->depth->cur_estimate;
			if (unct != NULL)
				*unct++ = (f32)(p->sd2conf_scale * sqrt(node->depth->cur_variance));
		} else {
#ifdef __HYP_DEBUG__
if (node->write) {
	row = pel/sheet->hdr->n_east;
	col = pel % sheet->hdr->n_east;
	error_msgv(modname, "debug: node (%d, %d) - multiple (%d) hypotheses:\n",
		row, col, node->depth->hypothesis_num);
	root = node->depth;
	while (root != NULL) {
		error_msgv(modname, "debug:  hyp %d, z = %f m, v = %f m, nj = %d.\n",
			root->hypothesis_num, root->cur_estimate, root->cur_variance,
			root->num_samples);
		root = root->next;
	}
}
#endif
			if (!mapsheet_nodal_choose_hypothesis(node->depth, &best)) {
				error_msgv(modname, "error: failed choosing a hypothesis"
					" at node %d.\n", pel);
				return(False);
			}
#ifdef __HYP_DEBUG__
if (node->write) {
	error_msgv(modname, "debug:  best match: hyp. %d.\n", best.hypothesis_num);
}
#endif
			if (depth != NULL) *depth++ = (f32)best.cur_estimate;
			if (unct != NULL)
				*unct++ = (f32)(p->sd2conf_scale * sqrt(best.cur_variance));
		}
	}
	return(True);
}

/* Routine:	mapsheet_nodal_get_prior_depth
 * Purpose:	Extract depth surface from nodal representation
 * Inputs:	sheet	Mapsheet to get information from
 *			**ip	Input sequence of depth nodes
 *			*par	Raw parameter structure associated with depth node grid
 * Outputs:	op		Pointer to linear (row major) array of data to hold the
 *					output sequence.
 * Comment:	This extracts the current best estimates from the DepthNode grid
 *			of structures.  In particular, it does not flush the node pre-filter
 *			input queues into the Kalman Filter input sequence, and hence the
 *			estimates may not accurately reflect the current state of knowledge
 *			about the nodes.  Users requiring a current snapshot should call
 *			mapsheet_nodal_finalise() before reading the grid with this call.
 *				In this context, `best' means `hypothesis with most samples',
 *			rather than through any other metric.  This may not be the `best'
 *			until add of the data is in, but it should give an idea of what's
 *			going on in the data structure at any point (particularly if it
 *			changes dramatically from sample to sample).
 */

static Bool mapsheet_nodal_get_prior_depth(MapSheet sheet, DepthNode **ip,
										   void *par, f32p op)
{
	return(mapsheet_nodal_get_prior_depth_unct(sheet, ip, par, op, NULL));
}

/* Routine:	mapsheet_nodal_get_prior_uncertainty
 * Purpose:	Extract uncertainty surface from nodal representation
 * Inputs:	sheet	Mapsheet to get information from
 *			**ip	Input sequence of depth nodes
 *			*par	Raw parameter structure associated with depth node grid
 * Outputs:	op		Pointer to linear (row major) array of data to hold the
 *					output sequence.
 * Comment:	This simply extracts the current estimates from the DepthNode grid
 *			of structures.  In particular, it does not flush the node pre-filter
 *			input queues into the Kalman Filter input sequence, and hence the
 *			estimates may not accurately reflect the current state of knowledge
 *			about the nodes.  Users requiring a current snapshot should call
 *			mapsheet_nodal_finalise() before reading the grid with this call.
 *			  Note that the value returned is actually the predicted
 *			confidence interval in meters, rather than the true variance, based
 *			on the assumption that the sampling distribution is normal (i.e.,
 *			standard error x scale chosen from parameters list).
 */

static Bool mapsheet_nodal_get_prior_uncertainty(MapSheet sheet, DepthNode **ip,
										  void *par, f32p op)
{
	return(mapsheet_nodal_get_prior_depth_unct(sheet, ip, par, NULL, op));
}

/* Routine:	mapsheet_nodal_get_depth
 * Purpose:	Extract depth using the selected multiple hypothesis disambiguation
 *			algorithm
 * Inputs:	sheet	MapSheet to use for parameters
 *			**ip	Grid of DepthNode input nodes
 *			*par	Parameters for the algorithm
 * Outputs:	op		Pointer to linear output space
 * Comment:	This acts as a simple dispatching switch for the three extraction
 *			algorithms.
 */

Bool mapsheet_nodal_get_depth(MapSheet sheet, DepthNode **ip, void *par,
							  f32p op)
{
	KalParam	*p = ((KalParam*)par);
	Bool		rc;
	
	switch(p->mthd) {
		case MAPNODAL_PRIOR:
			error_msg(modname, "debug: prior disambiguation.\n");
			rc = mapsheet_nodal_get_prior_depth(sheet, ip, par, op);
			break;
		case MAPNODAL_LHOOD:
			error_msg(modname, "debug: likelihood disambiguation.\n");
			rc = mapsheet_nodal_get_context_depth(sheet, ip, par, op);
			break;
		case MAPNODAL_POSTERIOR:
			error_msg(modname, "debug: posterior disambiguation.\n");
			rc = mapsheet_nodal_get_posterior_depth(sheet, ip, par, op);
			break;
		default:
			error_msgv(modname, "error: unknown multi-hypothesis disambiguation"
				" algorithm (%d).\n", (u32)(p->mthd));
			rc = False;
			break;
	}
	return(rc);
}

/* Routine:	mapsheet_nodal_get_uncertainty
 * Purpose:	Extract uncertianty using the selected multiple hypothesis
 *			disambiguation algorithm
 * Inputs:	sheet	MapSheet to use for parameters
 *			**ip	Grid of DepthNode input nodes
 *			*par	Parameters for the algorithm
 * Outputs:	op		Pointer to linear output space
 * Comment:	This acts as a simple dispatching switch for the three extraction
 *			algorithms.
 */

Bool mapsheet_nodal_get_uncertainty(MapSheet sheet, DepthNode **ip, void *par,
							  		f32p op)
{
	KalParam	*p = ((KalParam*)par);
	Bool		rc;
	
	switch(p->mthd) {
		case MAPNODAL_PRIOR:
			rc = mapsheet_nodal_get_prior_uncertainty(sheet, ip, par, op);
			break;
		case MAPNODAL_LHOOD:
			rc = mapsheet_nodal_get_context_unct(sheet, ip, par, op);
			break;
		case MAPNODAL_POSTERIOR:
			rc = mapsheet_nodal_get_posterior_unct(sheet, ip, par, op);
			break;
		default:
			error_msgv(modname, "error: unknown multi-hypothesis disambiguation"
				" algorithm (%d).\n", (u32)(p->mthd));
			rc = False;
			break;
	}
	return(rc);
}

/* Routine:	mapsheet_nodal_get_depth_unct
 * Purpose:	Get both depth and uncertainty surface using the selected multi-
 *			hypothesis disambiguation algorithm
 * Inputs:	sheet	MapSheet to work from
 *			**ip	Grid of DepthNodes to use
 *			*par	Parameter structure for the algorithm
 * Outputs:	*depth	Linear (row major) array for depth output
 *			*unct	Linear (row major) array for uncertanty output
 * Comment:	This is basically a dispatch switch for the various algorithm
 *			implementations.  Note that it is faster (and sometimes very
 *			significantly faster) to use this if you are going to need both
 *			surfaces, since it only has to do the sorting/context search once
 *			per node, rather than twice.
 */

Bool mapsheet_nodal_get_depth_unct(MapSheet sheet, DepthNode **ip, void *par,
								   f32p depth, f32p unct)
{
	KalParam	*p = ((KalParam*)par);
	Bool		rc;
	
	switch(p->mthd) {
		case MAPNODAL_PRIOR:
			rc = mapsheet_nodal_get_prior_depth_unct(sheet, ip, par, depth, unct);
			break;
		case MAPNODAL_LHOOD:
			rc = mapsheet_nodal_get_context_depth_unct(sheet, ip, par, depth, unct);
			break;
		case MAPNODAL_POSTERIOR:
			rc = mapsheet_nodal_get_posterior_depth_unct(sheet, ip, par, depth, unct);
			break;
		default:
			error_msgv(modname, "error: unknown multi-hypothesis disambiguation"
				" algorithm (%d).\n", (u32)(p->mthd));
			rc = False;
			break;
	}
	return(rc);
}

/* Routine:	mapsheet_nodal_get_sdev
 * Purpose:	Extract standard deviation surface
 * Inputs:	sheet	Mapsheet to get information from
 *			**ip	Input sequence of depth nodes
 *			*par	Raw parameters associated with the depth node grid
 * Outputs:	op		Pointer to linear (row major) output array
 * Comment:	This converts the sum-of-squares data in the nodes into s.dev
 *			using the current depth estimate as the `mean'.
 */

void mapsheet_nodal_get_sdev(MapSheet sheet, DepthNode **ip, void *par,
							 f32p op)
{
	u32			npels = sheet->hdr->n_east * sheet->hdr->n_north, pel;
	DepthNode	*node = ip[0];
	f64			mean, var, sdev;
	KalParam	*p = (KalParam*)par;
	
	for (pel = 0; pel < npels; ++pel, ++node)
		if (node->stats.n_points != 0) {
			mean = node->stats.mean / node->stats.n_points;
			var = node->stats.sum_sq/node->stats.n_points - mean*mean;
			if (var < 0.0) var = 0.0; /* Numerical noise problems */
			sdev = sqrt(var);
			*op++ = (f32)sdev;
		} else
			*op++ = (f32)p->null_variance;	/* Null value is same in both cases */
}

/* Routine:	mapsheet_nodal_init_grid
 * Purpose:	Initialises the DepthNode grid with default parameter estimates
 * Inputs:	sheet	Mapsheet to use for sizes, etc.
 *			**ip	Grid of DepthNode estimates to use
 *			*p		Pointer to private parameters to use in initialisation
 * Outputs:	-
 * Comment:	-
 */

static void mapsheet_nodal_init_grid(MapSheet sheet, DepthNode **ip,
									 KalParam *p)
{
	u32			npels = sheet->hdr->n_east * sheet->hdr->n_north, pel;
	DepthNode	*node = ip[0];
	
	for (pel = 0; pel < npels; ++pel, ++node) {
		memset(node, 0, sizeof(DepthNode));	/* Set everything to NULL */
		node->write = False;
	}
	ip[308][351].write = True;
}

/* Routine:	mapsheet_nodal_reinit_fixunct
 * Purpose:	Reinitialise a grid with given depths, but fixed uncertainty
 * Inputs:	sheet	MapSheet to work on
 *			*data	Pointer to the depth data to be used (meters, +ve up)
 *			unct	Uncertainty to use (see below for interpretation)
 *			unct_pcnt	Flag: True => uncertainty is a percentage of the depth
 *						being used for initialisation, otherwise the uncertainty
 *						is 1 s.d. in meters.
 *			*mask	Mask image (U8, 255 => no update at node), or NULL
 *			cols,
 *			rows	Size of the depth data grid available
 * Outputs:	True if initialisation worked, otherwise False.
 * Comment:	This copies the depth data into place and initialises the variance
 *			grid to the same value.  The code checks that the size of the input
 *			data grid is compatible with the mapsheet, but doesn't check
 *			that the data is in the correct (geodetic) place --- that's up to
 *			the user to confirm.  We also check that a MAP_DEPTH_NODAL surface
 *			has been assigned to the mapsheet before starting.  Note that we
 *			initialise the data structure with predicted values equal to the
 *			current estimates (just as the main initialisation sequence does).
 */

Bool mapsheet_nodal_reinit_fixunct(MapSheet sheet, f32 *data, f32 unct,
								   Bool unct_pcnt, u8 *mask, u32 cols, u32 rows)
{
	u32	e, n;
	f32	mean, variance, base_var;
	DepthNode *grid = sheet->grid->depth_grid[0];
	KalParam *param = (KalParam*)(sheet->param->depth_param);
	
	if (cols != sheet->hdr->n_east || rows != sheet->hdr->n_north) {
		error_msgv(modname, "initialisation data size (%d x %d) not the same"
			" as mapsheet (%d x %d).\n", rows, cols,
			sheet->hdr->n_north, sheet->hdr->n_east);
		return(False);
	}
	if (sheet->hdr->depth != MAP_DEPTH_NODAL) {
		error_msgv(modname, "mapsheet doesn't have a nodal surface to"
			" initialise! (method = %d)\n", (u32)(sheet->hdr->depth));
		return(False);
	}
	
	if (unct_pcnt)
		base_var = (unct/100.0f)*(unct/100.0f);
	else
		base_var = unct * unct;
		
	for (n = 0; n < sheet->hdr->n_north; ++n) {
		for (e = 0; e < sheet->hdr->n_east; ++e, ++grid) {
			mean = *data++;
			if (mask != NULL && *mask++ == 255)
				variance = 0.0f;
			else {
				if (unct_pcnt)
					variance = base_var * mean * mean;
				else
					variance = base_var;
			}
			if (grid->depth != NULL) {
				mapsheet_nodal_release_hypotheses(grid->depth);
				grid->depth = NULL;
			}
			if ((grid->depth = mapsheet_nodal_new_hypothesis(grid->depth,
								mean, variance)) == NULL) {
				error_msgv(modname, "error: failed allocating memory for "
					"primary hypothesis at node (%d, %d).\n", e, n);
				return(False);
			}
			if (grid->queue != NULL) {
				free(grid->queue);
				grid->n_queued = 0;
				grid->queue = NULL;
			}
			memset(&(grid->stats), 0, sizeof(Stats));
		}
	}
	
	param->init = True;	/* Mark that surface has been initialised, so that it
						 * is not reset when the first new data is added.
						 */	
	return(True);
}

/* Routine:	mapsheet_nodal_queue_fill
 * Purpose:	Insert a point into an unfilled queue
 * Inputs:	*node	Pointer to nodal structure to work on
 *			depth	Depth estimate to be inserted
 *			var		Estimated variance of estimate
 * Outputs:	-
 * Comment:	Note that this routine assumes that the queue isn't already filled,
 *			and hence that it doesn't have to do bound checking before it fills
 *			in the element.  Expect bad things to happen if this isn't the case.
 */

static void mapsheet_nodal_queue_fill(DepthNode *node, f32 depth, f32 var)
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

/* Routine:	mapsheet_nodal_queue_insert
 * Purpose:	Insert a point into a filled queue, returning median estimate
 * Inputs:	*node	Pointer to nodal structure to work on
 *			depth	Depth estimate to be inserted
 *			var		Estimated variance of estimate
 *			*p		Pointer to algorithm parameter holding structure
 * Outputs:	*op		Filled with median value from queue (before insertion)
 * Comment:	This extracts the median value from the current queue (which must
 *			be fully filled before calling this routine), and then inserts the
 *			given point into the queue, shuffling the remaining elements to
 *			ensure that the queue remains sorted.
 */

static void mapsheet_nodal_queue_insert(DepthNode *node,
										f32 depth, f32 variance, KalParam *p,
										Queue *op)
{
	s32		i, c = p->median_length/2;
	float	lo_water, hi_water;

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
		while (i < (s32)p->median_length && depth < node->queue[i].depth) ++i;
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
	lo_water = (f32)(node->queue[0].depth - CONF_99PC * sqrt(node->queue[0].var));
	hi_water = (f32)(node->queue[p->median_length-1].depth +
				CONF_99PC * sqrt(node->queue[p->median_length-1].var));

#ifdef __EEG_DEBUG__
	if (node->write) {
		fprintf(debug_eeg, "debug: min d = (%f, %f) max d = (%f, %f), "
			"lo_water = %f, hi_water = %f.\n", node->queue[0].depth,
			node->queue[0].var, node->queue[p->median_length-1].depth,
			node->queue[p->median_length-1].var, lo_water, hi_water);
	}
#endif

	if (lo_water >= hi_water) /* i.e., confidence limits do not overlap */
		mapsheet_nodal_truncate(node, p);	/* Remove any outliers */
}

/* Routine:	mapsheet_nodal_queue_est
 * Purpose:	Insert points into the queue of estimates, and insert point into
 *			filter sequence if queue is filled
 * Inputs:	*node	Node to update
 *			depth	Depth estimate
 *			var		Estimate of depth estimate variance
 *			*p		Pointer to KalParam algorithm parameters
 * Outputs:	True if inserted, else False (no memory for queue)
 * Comment:	This inserts the depth given into the queue associated with the
 *			specified node, creating the queue if required.  After the queue\
 *			has been primed (i.e., filled with estimates), on each call this
 *			routine extracts the median value from the queue (via a call to
 *			mapsheet_nodal_queue_insert()) and then inserts it into the Kalman
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

static Bool mapsheet_nodal_queue_est(DepthNode *node, f32 depth, f32 variance,
									 KalParam *p)
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
		mapsheet_nodal_queue_fill(node, depth, variance);
	} else {
		/* Buffer is filled and sorted, so we need to extract the median
		 * element for insertion into the Kalman filter sequence, and then
		 * insert the current point.
		 */
		mapsheet_nodal_queue_insert(node, depth, variance, p, &op);
		mapsheet_nodal_update_node(node, op.depth, op.var, p);
	}
	return(True);
}

/* Routine:	mapsheet_nodal_insert_depths
 * Purpose:	Add a sequence of depth estimates to the mapsheet
 * Inputs:	sheet	MapSheet to work on
 *			**ip	Input DepthNode grid to add data to
 *			*par	Pointer to (opaqued) parameter structure for algorithm
 *			stream	SoundingStream which supplied the data
 *			*data	Pointer to the soundings which should be added
 *			nsnds	Number of soundings in this batch
 *			nused	Number of soundings used for incorporation into grid
 * Outputs:	True if data was added correctly, otherwise False
 * Comment:	This code integrates the soundings presented into the current mapsheet.
 *			Note that in the newer version of the algorithm, soundings are queued in
 *			a median pre-filter before going to the KF so there is a delay between
 *			presentation and readback effect.  Note that we assume at this point that
 *			the soundings presented have suitably defined error variances in depth
 *			and position associated with them, so that we don't have to try to re-
 *			generate them here.
 */

Bool mapsheet_nodal_insert_depths(MapSheet sheet, DepthNode **ip,
								  void *par, SoundingStream stream,
								  Sounding *data, u32 nsnds, u32 *nused)
{
	KalParam	*param = (KalParam*)par;
	u32			snd, x, y;
	s32			min_x, max_x, min_y, max_y;
	f64			max_var_allowed, radius, dist, dist_sqr, var_scale, variance,
				node_x, node_y, ratio;

static FILE *debug = NULL;

	if (!param->init) {
		mapsheet_nodal_init_grid(sheet, ip, param);
		param->init = True;
	}
	
	var_scale = pow(param->dist_scale, -param->dist_exp);

#ifdef __DEBUG__
error_msgv("debug", "dist_scale = %lf, var_scale = %lf, nsnds = %d,"
					" iho_fixed = %lf m^2, iho_pcent = %lf\n",
			param->dist_scale, var_scale, nsnds, param->iho_fixed,
			param->iho_pcent);
error_msg(modname, "debug: ping start ...\n");
#endif

	*nused = 0;
	for (snd = 0; snd < nsnds; ++snd) {
#ifdef __DEBUG__
error_msgv(modname, "debug: z = %f r = %f dr = %f dz = %f (e,n)=(%lf,%lf)\n",
	data[snd].depth, data[snd].range, data[snd].dr, data[snd].dz,
	data[snd].east, data[snd].north);
#endif
		/* Determine IHO S-44 derived limits on maximum variance */
		max_var_allowed = (param->iho_fixed +
							param->iho_pcent*data[snd].depth*data[snd].depth)/
								(CONF_95PC * CONF_95PC); /* 95% -> 1 s.d. */
		ratio = max_var_allowed / data[snd].dz;
		if (ratio <= 2.0) ratio = 2.0;	/* Ensure some spreading on point */
		radius = param->dist_scale * pow(ratio - 1.0, param->inv_dist_exp);
#ifdef __DEBUG__
error_msgv(modname, "debug: max_var = %lf, radius = %lf\n", max_var_allowed,
			radius);
#endif

		/* Determine coordinates of effect radius */
		min_x = (u32)(((data[snd].east - radius) - sheet->hdr->w_bound)/sheet->hdr->east_sz);
		max_x = (u32)(((data[snd].east + radius) - sheet->hdr->w_bound)/sheet->hdr->east_sz);
		min_y = (u32)((sheet->hdr->n_bound - (data[snd].north + radius))/sheet->hdr->north_sz);
		max_y = (u32)((sheet->hdr->n_bound - (data[snd].north - radius))/sheet->hdr->north_sz);
		/* Check that the sounding hits somewhere in the mapsheet */
		if (max_x < 0 || min_x >= (s32)(sheet->hdr->n_east-1) ||
								max_y < 0 || min_y >= (s32)sheet->hdr->n_north-1)
			continue;		
		/* Clip to interior of current mapsheet */
		min_x = (min_x < 0) ? 0 : min_x;
		max_x = (max_x >= (s32)sheet->hdr->n_east) ? sheet->hdr->n_east-1 : max_x;
		min_y = (min_y < 0) ? 0 : min_y;
		max_y = (max_y >= (s32)sheet->hdr->n_north) ? sheet->hdr->n_north-1 : max_y;

#ifdef __DEBUG__
		error_msgv(modname, "min_x = %d max_x = %d min_y = %d max_y = %d\n",
			min_x, max_x, min_y, max_y);
		error_msgv(modname, "touchdown (x,y) = (%lf, %lf) m\n", data[snd].east,
			data[snd].north);
#endif
		++*nused;
		for (y = min_y; y <= (u32)max_y; ++y) {
			for (x = min_x; x <= (u32)max_x; ++x) {
				node_x = sheet->hdr->w_bound + x*sheet->hdr->east_sz;
				node_y = sheet->hdr->n_bound - y*sheet->hdr->north_sz;
				
				/* Compute boresight->node distance */
				dist_sqr = (node_x - data[snd].east)*(node_x - data[snd].east) +
						   (node_y - data[snd].north)*(node_y - data[snd].north);
				dist = sqrt(dist_sqr);
				
				/* Add positioning uncertainty */
				dist += CONF_95PC * sqrt(data[snd].dr);
				
				/* Compute refered variance */
				variance = data[snd].dz*(1.0 + var_scale*pow(dist, param->dist_exp));
#ifdef __DEBUG__
if (ip[y][x].depth != NULL) {
error_msgv(modname,
	"before: node (x,y) = (%lf, %lf) m, z = %lf m, d = %lf ,"
	" var = %lf m2, z^ = %lg m, v^ = %lg m2\n",
	node_x, node_y, data[snd].depth, dist, variance,
	ip[y][x].depth->cur_estimate, ip[y][x].depth->cur_variance);
}
#endif
				if (!mapsheet_nodal_queue_est(ip[y]+x, data[snd].depth,
											  (f32)variance, param)) {
					error_msgv(modname,
						"failed to add data to node at (%d,%d).\n", y, x);
					return(False);
				}

if (ip[y][x].write) {
	if (debug == NULL) {
		if ((debug = fopen("debug-ip.txt", "w")) == NULL) {
			error_msg(modname, "**** debug: failed to open ip file.\n");
			exit(1);
		}
	}
	fprintf(debug, "%d %d %f %f\n", y, x, data[snd].depth, variance);
	fflush(debug);
}

#ifdef __DEBUG__
if (ip[y][x].depth != NULL)
error_msgv(modname, "after: z^ = %lg m, v^ = %lg m2\n", ip[y][x].depth->cur_estimate,
			ip[y][x].depth->cur_variance);
#endif
			}
		}
	}
	return(True);
}

/* Routine:	mapsheet_nodal_init_param
 * Purpose:	Initialise parameters for the algorithm to their default values
 * Inputs:	order	IHO Survey Order as defined in errmod.h
 *			dn, de	Northing and Easting spacings for sheet
 * Outputs:	Pointer to an opaqued parameter structure for the algorithm
 * Comment:	This initialises the parameter to the currently compiled values.
 *			At present, there is no other way to change the parameters than to
 *			recompile the source.
 */
#ifdef __NEVER_DEFINE__
void *mapsheet_nodal_init_param(ErrModIHOOrder order, f64 dn, f64 de)
{
	KalParam	*rtn;
	f64			iho_fixed_sdev, iho_pcent_sdev, dtm_fixed, dtm_pcent, horiz;
	
	if ((rtn = (KalParam*)calloc(1, sizeof(KalParam))) == NULL) {
		error_msg(modname, "failed allocating Kalman filter workspace.\n");
		return(NULL);
	}
	rtn->init = False;	/* Indicates that the mapsheet nodes are not set up */

	/* Data Extraction Method */
	rtn->mthd = MAPNODAL_LHOOD;	/* Spatial Context Guide Node */

	/* Initialisation variables for the surface components */
	rtn->null_depth = DEFAULT_NULL_DEPTH;
	rtn->null_variance = DEFAULT_NULL_VARIANCE;

	/* Dilution factor distance exponent */
	rtn->dist_exp = DEFAULT_DIST_EXP;
	rtn->inv_dist_exp = 1.0/DEFAULT_DIST_EXP;
	rtn->dist_scale = (dn < de) ? dn : de;

	/* IHO Survey Order limits for determining maximum allowable error */
	errmod_get_iho_limits(order, &iho_fixed_sdev, &iho_pcent_sdev,
						  &dtm_fixed, &dtm_pcent, &horiz);
	rtn->iho_fixed = iho_fixed_sdev * iho_fixed_sdev;
	rtn->iho_pcent = iho_pcent_sdev * iho_pcent_sdev;

	/* Pre-queue filter and culling parameters */
	rtn->median_length = DEFAULT_MEDIAN_LENGTH;
	rtn->quotient_limit = DEFAULT_QUOTIENT_LIM;

	/* Evolution variance discounting factor */
	rtn->discount = DEFAULT_DISCOUNT;

	/* West & Harrison style model monitoring parameters */
	rtn->est_offset = DEFAULT_EST_OFFSET;
	rtn->bayes_fac_t = DEFAULT_BAYES_FAC_T;
	rtn->runlength_t = DEFAULT_RUNLENGTH_T;

	return(rtn);
}
#endif

void *mapsheet_nodal_init_param(ErrModIHOOrder order, f64 dn, f64 de)
{
	KalParam	*rtn;
	f64			iho_fixed_sdev, iho_pcent_sdev, dtm_fixed, dtm_pcent, horiz;
	
	if ((rtn = (KalParam*)calloc(1, sizeof(KalParam))) == NULL) {
		error_msg(modname, "failed allocating Kalman filter workspace.\n");
		return(NULL);
	}
	memcpy(rtn, &default_param, sizeof(KalParam));
	
	if (rtn->null_depth == DEFAULT_NULL_DEPTH)
		mapsheet_get_invalid(MAP_DATA_F32, &(rtn->null_depth));
	if (rtn->null_variance == DEFAULT_NULL_VARIANCE)
		mapsheet_get_invalid(MAP_DATA_F32, &(rtn->null_variance));

	/* Compute distance scale based on node spacing */
	rtn->dist_scale = (dn < de) ? dn : de;
	rtn->min_context /= (f32)rtn->dist_scale;	/* Convert to nodes */
	rtn->max_context /= (f32)rtn->dist_scale;

	/* IHO Survey Order limits for determining maximum allowable error */
	errmod_get_iho_limits(order, &iho_fixed_sdev, &iho_pcent_sdev,
						  &dtm_fixed, &dtm_pcent, &horiz);
	rtn->iho_fixed = iho_fixed_sdev * iho_fixed_sdev;
	rtn->iho_pcent = iho_pcent_sdev * iho_pcent_sdev;

	return(rtn);
}

/* Routine:	mapsheet_nodal_release_param
 * Purpose:	Releases all memory associated with the algorithm parameters struct
 * Inputs:	*param	Pointer to the (opaqued) algorithm parameter structure
 * Outputs:	-
 * Comment:	-
 */

void mapsheet_nodal_release_param(void *param)
{
	KalParam	*p = (KalParam*)param;
	free(p);
}

/* Routine:	mapsheet_nodal_execute_params
 * Purpose:	Execute parameters list for this sub-module
 * Inputs:	*list	ParList linked list to work through
 * Outputs:	True if the list was parsed properly, otherwise False
 * Comment:	This looks for a whole screed of parameters used to control all
 *			aspects of the CUBE algorithm ... basically the list at the top
 *			of the source file.  All of the parameters are limit checked as
 *			far as possible.
 */

typedef enum {
	MAP_ND_UNKNOWN = 0,
	MAP_ND_NULL_DEPTH,
	MAP_ND_NULL_SDEV,
	MAP_ND_CONF_INT,
	MAP_ND_DISTANCE_EXP,
	MAP_ND_MEDIAN_LENGTH,
	MAP_ND_EEG_Q_LIM,
	MAP_ND_EVOL_DISCOUNT,
	MAP_ND_MON_OFFSET,
	MAP_ND_MON_TAU,
	MAP_ND_MON_RUNLENGTH,
	MAP_ND_EXTRACTOR,
	MAP_ND_MIN_CONTEXT,
	MAP_ND_MAX_CONTEXT
} MapNdParamEnum;

Bool mapsheet_nodal_execute_params(ParList *list)
{
	ParTable tab[] = {
		{ "null_depth",			MAP_ND_NULL_DEPTH	},
		{ "null_sdev",			MAP_ND_NULL_SDEV	},
		{ "confidence_int",		MAP_ND_CONF_INT		},
		{ "distance_exp",		MAP_ND_DISTANCE_EXP	},
		{ "median_length",		MAP_ND_MEDIAN_LENGTH},
		{ "eeg_q_limit",		MAP_ND_EEG_Q_LIM	},
		{ "evolution_discount",	MAP_ND_EVOL_DISCOUNT},
		{ "monitor_offset",		MAP_ND_MON_OFFSET	},
		{ "monitor_tau",		MAP_ND_MON_TAU		},
		{ "monitor_runlength",	MAP_ND_MON_RUNLENGTH},
		{ "extractor",			MAP_ND_EXTRACTOR	},
		{ "min_context",		MAP_ND_MIN_CONTEXT	},
		{ "max_context",		MAP_ND_MAX_CONTEXT	},
		{ NULL,					MAP_ND_UNKNOWN		}
	};
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;
	u32		dummy_int;

	node = list;
	do {
		node = params_match(node, "mapsheet.nodal", tab, &id, &match);
		switch (id) {
			case MAP_ND_UNKNOWN:
				break;
			case MAP_ND_NULL_DEPTH:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" length measurement.\n", match->data);
					return(False);
				}
				default_param.null_depth = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting null depth to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case MAP_ND_NULL_SDEV:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" length sdev measurement.\n", match->data);
					return(False);
				}
				default_param.null_variance = dummy_float * dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting null variance to %f (%s).\n",
	dummy_float*dummy_float, match->data);
#endif
				break;
			case MAP_ND_CONF_INT:
				if (strcmp(match->data, "C68") == 0) {
					default_param.sd2conf_scale = CONF_68PC;
				} else if (strcmp(match->data, "C90") == 0) {
					default_param.sd2conf_scale = CONF_90PC;
				} else if (strcmp(match->data, "C95") == 0) {
					default_param.sd2conf_scale = CONF_95PC;
				} else if (strcmp(match->data, "C99") == 0) {
					default_param.sd2conf_scale = CONF_99PC;
				} else {
					error_msg(modname, "error: confidence interval selector"
						" not in set { C68, C90, C95, C99 }.\n");
					return(False);
				}
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting confidence interval scale to %f (%s).\n",
	default_param.sd2conf_scale, match->data);
#endif
				break;
			case MAP_ND_DISTANCE_EXP:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_DIST_EXP || dummy_float > MAX_DIST_EXP) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" distance exponent (range (%.1f, %.1f)).\n",
						match->data, MIN_DIST_EXP, MAX_DIST_EXP);
					return(False);
				}
				default_param.dist_exp = dummy_float;
				default_param.inv_dist_exp = 1.0/dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting distance exponent to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case MAP_ND_MEDIAN_LENGTH:
				dummy_int = atoi(match->data);
				if (dummy_int < MIN_MEDIAN_LENGTH ||
												dummy_int > MAX_MEDIAN_LENGTH) {
					error_msgv(modname, "error: median pre-queue filter length"
						" must be in range [%d, %d] (not %d).\n",
						MIN_MEDIAN_LENGTH, MAX_MEDIAN_LENGTH, dummy_int);
					return(False);
				}
				default_param.median_length = dummy_int;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting median pre-queue length to %d (%s).\n",
	dummy_int, match->data);
#endif
				break;
			case MAP_ND_EEG_Q_LIM:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_QUOTIENT_LIM ||
											dummy_float > MAX_QUOTIENT_LIM) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" quotient threshold (range (%.1f, %.1f)).\n",
						match->data, MIN_QUOTIENT_LIM, MAX_QUOTIENT_LIM);
					return(False);
				}
				default_param.quotient_limit = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting Eeg quotient limit to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case MAP_ND_EVOL_DISCOUNT:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_DISCOUNT || dummy_float > MAX_DISCOUNT) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" discount factor (range (%.1f, %.1f)).\n",
						match->data, MIN_DISCOUNT, MAX_DISCOUNT);
					return(False);
				}
				default_param.discount = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting discount factor to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case MAP_ND_MON_OFFSET:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_EST_OFFSET ||
												dummy_float > MAX_EST_OFFSET) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" monitoring step offset (range (%.1f, %.1f)).\n",
						match->data, MIN_EST_OFFSET, MAX_EST_OFFSET);
					return(False);
				}
				default_param.est_offset = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting monitor step offet to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case MAP_ND_MON_TAU:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_BAYES_FAC_T ||
												dummy_float > MAX_BAYES_FAC_T) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" monitoring Bayes factor (range (%.1f, %.1f)).\n",
						match->data, MIN_BAYES_FAC_T, MAX_BAYES_FAC_T);
					return(False);
				}
				default_param.bayes_fac_t = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting monitor Bayes factor to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case MAP_ND_MON_RUNLENGTH:
				dummy_int = atoi(match->data);
				if (dummy_int < MIN_RUNLENGTH || dummy_int > MAX_RUNLENGTH) {
					error_msgv(modname, "error: monitor runlength limit"
						" must be in range [%d, %d] (not %d).\n",
						MIN_RUNLENGTH, MAX_RUNLENGTH, dummy_int);
					return(False);
				}
				default_param.runlength_t = dummy_int;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting monitor runlength limit to %d (%s).\n",
	dummy_int, match->data);
#endif
				break;
			case MAP_ND_EXTRACTOR:
				if (strcmp(match->data, "prior") == 0) {
					default_param.mthd = MAPNODAL_PRIOR;
				} else if (strcmp(match->data, "likelihood") == 0) {
					default_param.mthd = MAPNODAL_LHOOD;
				} else if (strcmp(match->data, "posterior") == 0) {
					default_param.mthd = MAPNODAL_POSTERIOR;
				} else {
					error_msgv(modname, "error: unrecognised hypothesis"
						" disambiguation method \"%s\" (known are: prior, "
						" likelihood, posterior).\n", match->data);
					return(False);
				}
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting hypothesis disambiguation to %s.\n",
	match->data);
#endif
				break;
			case MAP_ND_MIN_CONTEXT:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" length measurement.\n", match->data);
					return(False);
				}
				if (dummy_float < MIN_MIN_CONTEXT ||
											dummy_float > MAX_MIN_CONTEXT) {
					error_msgv(modname, "error: minimum context search distance"
						" for hypothesis disambiguation must be in the range"
						" [%f, %f] m (not %f).\n", MIN_MIN_CONTEXT,
						MAX_MIN_CONTEXT, dummy_float);
					return(False);
				}
				default_param.min_context = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting minimum search context to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case MAP_ND_MAX_CONTEXT:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" length measurement.\n", match->data);
					return(False);
				}
				if (dummy_float < MIN_MAX_CONTEXT ||
											dummy_float > MAX_MAX_CONTEXT) {
					error_msgv(modname, "error: maximum context search distance"
						" for hypothesis disambiguation must be in the range"
						" [%f, %f] m (not %f).\n", MIN_MAX_CONTEXT,
						MAX_MAX_CONTEXT, dummy_float);
					return(False);
				}
				default_param.max_context = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting maximum search context to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			default:
				error_msgv(modname, "error: unknown return from parameter"
					" matching module (%d).\n", id);
				return(False);
				break;
		}
	} while (node != NULL);
	return(True);
}
