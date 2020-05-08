/* $Id: cube.c 14 2003-07-23 00:34:56Z brc $
 * $Log$
 * Revision 1.2  2003/07/23 00:34:56  brc
 * Merged modifications from development stream.
 *
 * Revision 1.3.2.3  2003/04/16 18:17:18  brc
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
 * Revision 1.2.4.1  2003/01/28 14:29:44  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.3.2.2  2002/12/15 01:13:03  brc
 * Added parameters for the blunder filters used to remove deep spikes which
 * are more than a fixed distance from the predicted surface, or more than a fixed
 * percentage deeper, or more than a fixed value of the predicted surface's
 * associated variance.
 *
 * Revision 1.3.2.1  2002/07/14 02:20:46  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.3  2002/05/10 19:11:32  brc
 * Added parameter code for the CUBE_PREDSURF disambiguation engine.  This
 * uses the (low resolution, interpolated) surface to generate the guide nodes
 * used when multiple hypotheses are present.  Because the low resolution
 * surface typically filters a lot more noise, this is usually a pretty good
 * guess at the surface depth, even in bursty problems.
 *
 * Revision 1.2  2001/09/20 17:43:59  brc
 * Added facility to report parameter reads (helps with serialisation debug), and
 * added use of mapsheet.c for invalid data values, so that these are guaranteed
 * to be consistent across all modules.
 *
 * Revision 1.1  2001/08/20 22:40:19  brc
 * Added CUBE algorithm as a callable entity, rather than being tied into
 * the mapsheet implementation.  This means that it is significantly easier
 * to use from other systems, and to implement in different ways (e.g., node-
 * wise or grid-wise) as might be required.
 *
 *
 * File:	cube.c
 * Purpose:	Implement 'extra-node' functionality for CUBE (i.e., things that
 *			deal with a collection of nodes).
 * Date:	30 July 2001
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2001.
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
#include "cube.h"
#include "cube_private.h"
#include "mapsheet.h"
#include "params.h"

static char *modname = "cube";
static char *modrev = "$Revision: 14 $";

/* Default parameters, and limits for the input parameter specifications.  These
 * macros are used to expand to default values for all of the parameters, and
 * to provide limits for checking inputs.  Note that there is currently no way
 * to change hard limits except to recompile the code ... but an auxilliary set
 * of algorithm parameters could, of course, be implemented.
 */

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

#define DEFAULT_QUOTIENT_LIM	30.0	/* Approx. 0.1% F(1,6) */
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

#define DEFAULT_EXTRACTOR		CUBE_LHOOD

#define DEFAULT_MIN_CONTEXT		5.0		/* Minimum context distance, m */
#define MIN_MIN_CONTEXT			0.0
#define MAX_MIN_CONTEXT			1000.0

#define DEFAULT_MAX_CONTEXT		10.0	/* Maximum context distance, m */
#define MIN_MAX_CONTEXT			1.0
#define MAX_MAX_CONTEXT			3000.0

#define DEFAULT_BLUNDER_MIN		10.0	/* Minimum blunder gap, m */
#define MIN_BLUNDER_MIN			1.0
#define MAX_BLUNDER_MIN			1000.0

#define DEFAULT_BLUNDER_PCENT	0.25	/* Percentage of depth to allow as a deep gap
										 * before declaring the depth to be a blunder */
#define MIN_BLUNDER_PCENT		0.00
#define MAX_BLUNDER_PCENT		1.00

#define DEFAULT_BLUNDER_SCALAR	3.0		/* Fixed scale of initialisation uncertainty to
										 * allow before declaring the depth to be a blunder */
#define MIN_BLUNDER_SCALAR		0.0
#define MAX_BLUNDER_SCALAR		10.0

#define DEFAULT_NODE_CAPTURE_DIST	5.00	/* Percent of predicted or estimated depth */

#define MIN_NODE_CAPTURE_DIST		1.00
#define MAX_NODE_CAPTURE_DIST		100.00

/* Default static structure for CUBE algorithm parameters.  This is maintained
 * as a static, updated by the parameter execution routine in this module.  When
 * a call to cube_init_param() is made, this is memcpy()d into private store,
 * so that it is possible for multiple CUBE algorithms to exist at the same
 * time without sharing the same parameter set.
 */

static CubeParam default_param = {
	False,					/* Initialisation interlock */
	0.0,					/* 'No Data' indicator, filled in on clone */
	DEFAULT_EXTRACTOR,
	DEFAULT_NULL_DEPTH,
	DEFAULT_NULL_VARIANCE,
	DEFAULT_DIST_EXP,
	1.0/DEFAULT_DIST_EXP,
	0.0,					/* Distance scale, filled in on clone */
	0.0,					/* Variance scale, filled in on clone */
	0.0, 0.0,				/* IHO limits, filled in on clone */
	DEFAULT_MEDIAN_LENGTH,
	DEFAULT_QUOTIENT_LIM,
	DEFAULT_DISCOUNT,
	DEFAULT_EST_OFFSET,
	(f32)DEFAULT_BAYES_FAC_T,
	DEFAULT_RUNLENGTH_T,
	DEFAULT_MIN_CONTEXT,
	DEFAULT_MAX_CONTEXT,
	(f32)DEFAULT_SDEV2CONF_SCALE,
	(f32)DEFAULT_BLUNDER_MIN,
	(f32)DEFAULT_BLUNDER_PCENT,
	(f32)DEFAULT_BLUNDER_SCALAR,
	(f32)(DEFAULT_NODE_CAPTURE_DIST/100.0f)	/* This is a percentage, hence conversion */
};

/* Routine:	cube_make_nan
 * Purpose:	Construct an in-memory quiet NaN for indication purposes
 * Inputs:	-
 * Outputs:	-
 * Comment:	This routine *must* be called before anything else in the module
 *			which uses the grid cache, since NaN is used as an indicator that
 *			the cache value is invalid (i.e., data has been sent to the node
 *			since the value was last cached).
 */

f32 cube_make_nan(void)
{
	f32	cube_nan;
	
	/* Construct a NaN in memory.  According to IEEE 754, a quiet NaN (i.e., one
	 * that doesn't cause an exception when used has sign bit zero, exponents
	 * all ones, and mantissa >= 2^{P-1} where P=24 for single precision.  Thus,
	 * its binary representation is 0x7FFFFFFF for single precision (among other
	 * possibilities, originally intended, apparently, for manufacturers to
	 * indicate what caused the NaN and where it came from originally!).
	 */
	*((u32*)&cube_nan) = 0x7FFFFFFFU;
	return(cube_nan);
}

/* Routine:	cube_get_invalid
 * Purpose:	Return value being used to represent invalid data
 * Inputs:	p	Cube parameter structure to work from
 * Outputs:	Returns invalid data marker
 * Comment:	The invalid number is not strictly defined, but is usually something
 *			obvious, like FLT_MAX or -FLT_MAX, or NaN.
 */

f32 cube_get_invalid(Cube p)
{
	return(p->no_data_value);
}

/* Routine:	cube_write_param
 * Purpose:	Write parameter structure to file
 * Inputs:	param	Pointer to (opaqued) parameter structure
 *			*f		Pointer to FILE structure for write
 * Outputs:	True if write was sucessful, otherwise False
 * Comment:	-
 */

Bool cube_write_param(Cube param, FILE *f)
{
	if (fwrite(param, sizeof(CubeParam), 1, f) != 1) {
		error_msg(modname, "failed writing algorithm parameters.\n");
		return(False);
	}
	return(True);
}

/* Routine:	cube_read_param
 * Purpose:	Read parameter structure from file
 * Inputs:	*f	File descriptor to read from
 * Outputs:	Pointer to (opaqued) parameter structure, or NULL on failure.
 * Comment:	This doesn't do any version checking, which may cause problems in
 *			the future.
 */

Cube cube_read_param(FILE *f)
{
	Cube	param;

#ifdef __DEBUG__	
	error_msgv(modname, "debug: reading CUBE parameters.\n");
#endif
	if ((param = (Cube)calloc(1, sizeof(CubeParam))) == NULL) {
		error_msg(modname, "failed to allocate algorithm parameter space.\n");
		return(NULL);
	}
	if (fread(param, sizeof(CubeParam), 1, f) != 1) {
		error_msg(modname, "failed to read algorithm parameters.\n");
		free(param);
		return(NULL);
	}
#ifdef __DEBUG__
	error_msgv(modname, "debug: median queue length %d samples.\n",
		param->median_length);
#endif
	return(param);
}

/* Routine:	cube_params
 * Purpose:	
 * Inputs:
 * Outputs:
 * Comment:
 */

void cube_describe_params(Cube p)
{
	error_msgv(modname, "debug: median queue length %d samples.\n", p->median_length);
}

/* Routine:	cube_node_init_param
 * Purpose:	Initialise parameters for the algorithm to their default values
 * Inputs:	order	IHO Survey Order as defined in errmod.h
 *			dn, de	Northing and Easting spacings for sheet
 * Outputs:	Pointer to an opaqued parameter structure for the algorithm
 * Comment:	This initialises the parameter to the currently compiled values.
 *			At present, there is no other way to change the parameters than to
 *			recompile the source.
 */

Cube cube_init_param(ErrModIHOOrder order, f64 de, f64 dn)
{
	Cube	rtn;
	f64		iho_fixed_sdev, iho_pcent_sdev, dtm_fixed, dtm_pcent, horiz;
	
	if ((rtn = (Cube)calloc(1, sizeof(CubeParam))) == NULL) {
		error_msg(modname, "failed allocating CUBE workspace.\n");
		return(NULL);
	}
	memcpy(rtn, &default_param, sizeof(CubeParam));
	
	/* Compute distance scale based on node spacing */
	rtn->dist_scale = (dn < de) ? dn : de;
	rtn->min_context /= (f32)rtn->dist_scale;	/* Convert to nodes */
	rtn->max_context /= (f32)rtn->dist_scale;
	
	/* Compute variance scaling factor for dilution function */
	rtn->var_scale = pow(rtn->dist_scale, -rtn->dist_exp);

	/* IHO Survey Order limits for determining maximum allowable error */
	errmod_get_iho_limits(order, &iho_fixed_sdev, &iho_pcent_sdev,
						  &dtm_fixed, &dtm_pcent, &horiz);
	rtn->iho_fixed = iho_fixed_sdev * iho_fixed_sdev;
	rtn->iho_pcent = iho_pcent_sdev * iho_pcent_sdev;

	mapsheet_get_invalid(MAP_DATA_F32, &(rtn->no_data_value));
	
	return(rtn);
}

/* Routine:	cube_release_param
 * Purpose:	Releases all memory associated with the algorithm parameters struct
 * Inputs:	*param	Pointer to the (opaqued) algorithm parameter structure
 * Outputs:	-
 * Comment:	-
 */

void cube_release_param(Cube param)
{
	free(param);
}

/* Routine:	cube_execute_params
 * Purpose:	Execute parameters list for this sub-module
 * Inputs:	*list	ParList linked list to work through
 * Outputs:	True if the list was parsed properly, otherwise False
 * Comment:	This looks for a whole screed of parameters used to control all
 *			aspects of the CUBE algorithm ... basically the list at the top
 *			of the source file.  All of the parameters are limit checked as
 *			far as possible.
 */

typedef enum {
	CUBE_UNKNOWN = 0,
	CUBE_NULL_DEPTH,
	CUBE_NULL_SDEV,
	CUBE_CONF_INT,
	CUBE_DISTANCE_EXP,
	CUBE_MEDIAN_LENGTH,
	CUBE_EEG_Q_LIM,
	CUBE_EVOL_DISCOUNT,
	CUBE_MON_OFFSET,
	CUBE_MON_TAU,
	CUBE_MON_RUNLENGTH,
	CUBE_EXTRACTOR,
	CUBE_MIN_CONTEXT,
	CUBE_MAX_CONTEXT,
	CUBE_BLUNDER_MIN,
	CUBE_BLUNDER_PCENT,
	CUBE_BLUNDER_SCALAR,
	CUBE_CAPTURE_DIST
} CubeParamEnum;

Bool cube_execute_params(ParList *list)
{
	ParTable tab[] = {
		{ "null_depth",			CUBE_NULL_DEPTH		},
		{ "null_sdev",			CUBE_NULL_SDEV		},
		{ "confidence_int",		CUBE_CONF_INT		},
		{ "distance_exp",		CUBE_DISTANCE_EXP	},
		{ "median_length",		CUBE_MEDIAN_LENGTH	},
		{ "eeg_q_limit",		CUBE_EEG_Q_LIM		},
		{ "evolution_discount",	CUBE_EVOL_DISCOUNT	},
		{ "monitor_offset",		CUBE_MON_OFFSET		},
		{ "monitor_tau",		CUBE_MON_TAU		},
		{ "monitor_runlength",	CUBE_MON_RUNLENGTH	},
		{ "extractor",			CUBE_EXTRACTOR		},
		{ "min_context",		CUBE_MIN_CONTEXT	},
		{ "max_context",		CUBE_MAX_CONTEXT	},
		{ "blunder_min",		CUBE_BLUNDER_MIN	},
		{ "blunder_pcent",		CUBE_BLUNDER_PCENT	},
		{ "blunder_scalar",		CUBE_BLUNDER_SCALAR },
		{ "capture_dist",		CUBE_CAPTURE_DIST	},
		{ NULL,					CUBE_UNKNOWN		}
	};
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;
	u32		dummy_int;

	node = list;
	do {
		node = params_match(node, "cube", tab, &id, &match);
		switch (id) {
			case CUBE_UNKNOWN:
				break;
			case CUBE_NULL_DEPTH:
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
			case CUBE_NULL_SDEV:
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
			case CUBE_CONF_INT:
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
			case CUBE_DISTANCE_EXP:
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
			case CUBE_MEDIAN_LENGTH:
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
			case CUBE_EEG_Q_LIM:
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
			case CUBE_EVOL_DISCOUNT:
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
			case CUBE_MON_OFFSET:
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
			case CUBE_MON_TAU:
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
			case CUBE_MON_RUNLENGTH:
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
			case CUBE_EXTRACTOR:
				if (strcmp(match->data, "prior") == 0) {
					default_param.mthd = CUBE_PRIOR;
				} else if (strcmp(match->data, "likelihood") == 0) {
					default_param.mthd = CUBE_LHOOD;
				} else if (strcmp(match->data, "posterior") == 0) {
					default_param.mthd = CUBE_POSTERIOR;
				} else if (strcmp(match->data, "predsurf") == 0) {
					default_param.mthd = CUBE_PREDSURF;
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
			case CUBE_MIN_CONTEXT:
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
			case CUBE_MAX_CONTEXT:
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
			case CUBE_BLUNDER_MIN:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" length measurement.\n", match->data);
					return(False);
				}
				if (dummy_float < MIN_BLUNDER_MIN ||
											dummy_float > MAX_BLUNDER_MIN) {
					error_msgv(modname, "error: minimum blunder filter depth"
						" must be in the range [%f, %f] m (not %f).\n",
						MIN_BLUNDER_MIN, MAX_BLUNDER_MIN, dummy_float);
					return(False);
				}
				default_param.blunder_min = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting minimum blunder filter depth to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case CUBE_BLUNDER_PCENT:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" length measurement.\n", match->data);
					return(False);
				}
				dummy_float /= 100.0;
				if (dummy_float < MIN_BLUNDER_PCENT ||
											dummy_float > MAX_BLUNDER_PCENT) {
					error_msgv(modname, "error: blunder filter percentage"
						" must be in the range [%f, %f] m (not %f).\n",
						MIN_BLUNDER_PCENT, MAX_BLUNDER_PCENT, dummy_float);
					return(False);
				}
				default_param.blunder_pcent = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting percentage blunder filter to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case CUBE_BLUNDER_SCALAR:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_BLUNDER_SCALAR ||
											dummy_float > MAX_BLUNDER_SCALAR) {
					error_msgv(modname, "error: blunder filter scalar"
						" must be in the range [%f, %f] m (not %f).\n",
						MIN_BLUNDER_SCALAR, MAX_BLUNDER_SCALAR, dummy_float);
					return(False);
				}
				default_param.blunder_scalar = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting scalar blunder filter to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case CUBE_CAPTURE_DIST:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_NODE_CAPTURE_DIST ||
					dummy_float > MAX_NODE_CAPTURE_DIST) {
					error_msgv(modname, "error: capture distance percetange must"
						" be in range (%.2f, %.2f) %% (not %.2f %%).\n",
						MIN_NODE_CAPTURE_DIST, MAX_NODE_CAPTURE_DIST, dummy_float);
					return(False);
				}
				default_param.capture_dist_scale = (f32)dummy_float/100.0f;
				match->used = True;
#ifdef __DEBUG__
				error_msgv(modname, "debug: setting node capture distance to %f (%s).\n",
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
