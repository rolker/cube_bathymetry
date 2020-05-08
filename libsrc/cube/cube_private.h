/* $Id: cube_private.h 14 2003-07-23 00:34:56Z brc $
 * $Log$
 * Revision 1.2  2003/07/23 00:34:56  brc
 * Merged modifications from development stream.
 *
 * Revision 1.3.2.2  2003/04/16 18:17:18  brc
 * Added facility to control the node capture radius computation from
 * the outside.  This is the radius about a node from which the
 * algorithm will accept data, specified as a scalar multiple of the
 * target or predicted depth (whichever is available).
 *
 * Revision 1.1.1.1  2003/02/03 20:18:42  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2.4.2  2003/01/29 13:07:26  dneville
 * (BRC) Added nominated hypothesis pointer to CNode structure.
 *
 * Revision 1.2.4.1  2003/01/28 14:29:44  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.3.2.1  2002/12/15 01:29:20  brc
 * Added entries to the parameters structure to store the values associated with
 * the blunder filters.
 *
 * Revision 1.3  2002/05/10 20:12:44  brc
 * Added CUBE_PREDSURF as a disam. method, and values for predicted depth
 * and uncertainty in the CubeNode structure.  Note that this is not currently
 * reflected in a MapSheet file version change, and hence there may be some
 * problems with versioning.
 *
 * Revision 1.2  2001/12/07 20:24:00  brc
 * Moved location of C++ wrapper so that it covers all of the file.
 *
 * Revision 1.1  2001/08/21 02:00:08  brc
 * Added headers for CUBE module.
 *
 *
 * File:	cube_private.h
 * Purpose:	Provide types shared between the control algorithm for grids of
 *			nodes, and the estimator implemented at a node.
 * Date:	30 July 2001
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2001.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __CUBE_PRIVATE_H__
#define __CUBE_PRIVATE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"

/*
 * Extraction method for depth and uncertainty surfaces.  This is used only in
 * the multiple hypothesis case as a way of choosing which hypothesis to
 * extract and report as the `current best guess'.  The CUBE_PRIOR is based
 * on approximate hypothesis probability as estimated by the number of samples
 * incorporated, CUBE_LHOOD uses a local spatial context to choose a guide
 * estimation node (i.e., the closest one with only one hypothesis) and then
 * chooses the hypothesis at the current node using a minimum distance metric;
 * CUBE_POSTERIOR combines both of these to form an approximate Bayesian
 * posterior distribution.  CUBE_PREDSURF uses the predicted surface depth and
 * variance to guide the disambiguation process.  CUBE_UNKN is a sentinel.
 */

typedef enum {
	CUBE_PRIOR = 0,
	CUBE_LHOOD,
	CUBE_POSTERIOR,
	CUBE_PREDSURF,
	CUBE_UNKN
} CubeExtractor;

typedef struct _cube_param {
	Bool	init;			/* System mapsheet initialisation marker */
	f32		no_data_value;	/* Value used to indicate 'no data' (typ. FLT_MAX) */
	CubeExtractor mthd;		/* Method used to extract information from sheet */
	f64		null_depth;		/* Depth to initialise estimates */
	f64		null_variance;	/* Variance for initialisation */
	f64		dist_exp;		/* Exponent on distance for variance scale */
	f64		inv_dist_exp;	/* 1.0/dist_exp for efficiency */
	f64		dist_scale;		/* Normalisation coefficient for distance (m) */
	f64		var_scale;		/* Variance scale dilution factor */
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
	f32		blunder_min;	/* Minimum depth difference from pred. depth to
							 * consider a blunder (m) */
	f32		blunder_pcent;	/* Percentage of predicted depth to be considered
							 * a blunder, if more than the minimum (0<p<1, typ. 0.25).
							 */
	f32		blunder_scalar;	/* Scale on initialisation surface std. dev. at a node
							 * to allow before considering deep spikes to be blunders.
							 */
	f32		capture_dist_scale;	/* Scale on predicted or estimated depth for how far out
								 * to accept data.  (unitless; typically 0.05 for
								 * hydrography but can be greater for geological mapping
								 * in flat areas with sparse data)
								 */
} CubeParam /* , *Cube */;

/*
 * The primary estimation structural element.  This maintains the median pre-
 * filter queue, the linked list of depth hypotheses, and the sample statistics
 * for a single node.  The 'write' element is purely for debugging --- setting
 * this for a node results in code all over this module writing out debugging
 * files with lots of interesting data.
 */

typedef struct _queue *pQueue;
typedef struct _depth_hypothesis *pHypothesis;

typedef struct _cube_node {
	pQueue		queue;		/* Queued points in pre-filter */
	u32			n_queued;	/* Number of elements in queue structure */
	pHypothesis	depth;		/* Depth hypotheses currently being tracked */
	pHypothesis	nominated;	/* A nominated hypothesis from the user */
	f32			pred_depth;	/* Predicted depth, or NaN for 'no update', or
							 * INVALID_DATA for 'no information available'
							 */
	f32			pred_var;	/* Variance of predicted depth, only valid if the
							 * predicted depth is (as above), meter^2 */
	Bool		write;		/* Debug write out */
} CNode /*, *CubeNode */;

/* Routine:	cube_make_nan
 * Purpose:	Construct an in-memory quiet NaN for indication purposes
 * Inputs:	-
 * Outputs:	Returns the NaN value
 * Comment:	This routine generates a quiet NaN (i.e., one that doesn't cause
 *			exceptions when used).  This can be used to indicate that there is
 *			no data, or more likely that the data is invalid (e.g., in a
 *			caching scheme).
 */

extern f32 cube_make_nan(void);

#ifdef __cplusplus
}
#endif

#endif
