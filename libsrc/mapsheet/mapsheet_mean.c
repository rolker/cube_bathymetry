/*
 * $Id: mapsheet_mean.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:43  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:30:00  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.8.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.8  2001/09/20 18:52:41  brc
 * Modified to use mapsheet.c for invalid data value for consistency.
 *
 * Revision 1.7  2001/08/21 01:45:43  brc
 * Added facility to count the number of soundings actually used from each swath
 * to update the data in the mapsheet region.  This can be used, among other things,
 * to determine which data files in a set are actually used in updating the data.
 *
 * Revision 1.6  2001/05/16 21:08:25  brc
 * Added float.h for DBL_MAX, FLT_MAX, etc. in Linux
 *
 * Revision 1.5  2001/05/15 01:49:12  brc
 * Turned off debugging code.
 *
 * Revision 1.4  2001/05/14 21:06:41  brc
 * Added code to make sub-module 'params'-aware.
 *
 * Revision 1.3  2000/09/07 21:11:17  brc
 * Modified mapsheet code to allow the bin depth (i.e., number of soundings
 * held in a bin before replacement starts to occur) to be specified by the
 * user.  This allows us to deal with slightly larger areas by limiting the
 * depth in any one bin.
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
 * File:	mapsheet_mean.c
 * Purpose:	Mean binning algorithm with outlier rejection
 * Date:	07 July 2000
 *
 * Copyright 2022, Center for Coastal and Ocean Mapping and NOAA-UNH Joint Hydrographic
 * Center, University of New Hampshire.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies
 * or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>

#ifdef sgi
#	include <ulocks.h>
#	include <task.h>
#endif

#if defined(linux) || defined(WIN32)
#define FSQRT	(float)sqrt
#else
#define FSQRT fsqrt
#endif

#include "stdtypes.h"
#include "error.h"
#include "mapsheet.h"
#include "mapsheet_private.h"
#include "mapsheet_mean.h"
#include "params.h"

#undef __DEBUG__

static char *modname = "mapsheet_mean";
static char *modrev = "$Revision: 2 $";

typedef struct _mean_bin_param {
	Bool	use_parallel;	/* Whether the parallel algorithm should be used */
	Bool	use_rejection;	/* Whether outlier rejection should be used */
	u32		buffer_quant;	/* Dynamic buffer resize quantity */
	u32		buffer_limit;	/* Maximum size to grow buffer towards */
	f32		defcooked;		/* Default value for cooked output if no inputs */
#ifdef sgi
	u32		*start;			/* Start points for parallel tasks */
	u32		*end;			/* End points for parallel tasks */
#endif
	/* Variables for outlier rejection */
	f32		se_scale;		/* Standard error scale for removal bounds */
	f32		mean_ratio_eps;	/* Mean ratio epsilon lower bound */
	f32		serr_ratio_eps;	/* Std. Error ratio epsilon lower bound */
	u32		maxiter;		/* Maximum number of iterations */
} MeanParam;

/* Default null depth (i.e., if there are no hits in the bin) */
#define DEFAULT_NULL_DEPTH		0.0f

/* Std. err. limit for iterative rejection stages (as above). */
#define ITER_REJECTION_LIMIT	2.f

/* Mean ratio epsilon (i.e., below this to call it a day) */
#define MN_RATIO_EPSILON		0.01f

/* Std. err. ratio epsilon (as above) */
#define SE_RATIO_EPSILON		0.01f

/* Maximum number of rejection iterations */
#define MAX_ITERATIONS			100
#define MIN_MAX_ITER			10
#define MAX_MAX_ITER			500

/* Default buffer quantum size (i.e., size by which bin buffers are increased
 * when more data is presented to a full buffer).
 */
#define DEFAULT_BUFFER_QUANTUM	256
#define MIN_BUF_QUANT			10
#define MAX_BUF_QUANT			1024

/* Default buffer limit size (i.e., the maximum number of bins in the buffer
 * unless the user reduces the limit.)
 */
#define DEFAULT_BUFFER_LIMIT	65536
#define MIN_BUF_LIMIT			10
#define MAX_BUF_LIMIT			65536

static MeanParam default_param = {
	True,
	True,
	DEFAULT_BUFFER_QUANTUM,
	DEFAULT_BUFFER_LIMIT,
	DEFAULT_NULL_DEPTH,
#ifdef sgi
	NULL, NULL,
#endif
	ITER_REJECTION_LIMIT,
	MN_RATIO_EPSILON,
	SE_RATIO_EPSILON,
	MAX_ITERATIONS
};

/* Routine:	mapsheet_mean_reject
 * Purpose:	Attempt to remove any outliers by clipping tails until sample
 *			estimates stabilise
 * Inputs:	*data	Pointer to data array
 *			nsamp	Number of samples in the data array
 *			*p		Control parameters for the algorithm
 * Outputs:	Returns mean of reduced dataset
 * Comment:	-
 */

static f32 mapsheet_mean_reject(f32p data, u32 nsamp, MeanParam *p)
{
	f32	mean, p_mean, serr, p_serr, mratio, sratio, left, right;
	u32	samp, iter, pts;
		
	/* Compute primary estimate of mean and standard error */
	for (samp = 0; samp < nsamp; ++samp) {
		p_mean += data[samp];
		p_serr += data[samp]*data[samp];
	}
	p_mean /= nsamp;
	p_serr = FSQRT(p_serr/(nsamp-1)-p_mean*p_mean);
	
	/* Iterate, computing limits */
	iter = 1;
	do {
		left = p_mean - p_serr*p->se_scale;
		right = p_mean + p_serr*p->se_scale;
		mean = 0.f; serr = 0.f; pts = 0;
		for (samp = 0; samp < nsamp; ++samp) {
			if (data[samp] < left || data[samp] > right) continue;
			mean += data[samp];
			serr += data[samp]*data[samp];
			++pts;
		}
		mean /= pts;
		serr = FSQRT(serr/(pts-1)-mean*mean);
		mratio = (f32)fabs((mean - p_mean)/mean);
		sratio = (f32)fabs(serr - p_serr)/serr;
		p_mean = mean; p_serr = serr;
	} while ((mratio >= p->mean_ratio_eps || sratio >= p->serr_ratio_eps) &&
			  iter++ < p->maxiter);
			 
	if (iter >= p->maxiter)
		error_msgv(modname, "ran out of iterations (max %d) in rejection.\n",
			p->maxiter);

	return(mean);
}

/* Routine:	mapsheet_mean_byrow
 * Purpose:	Compute mean of a sequence of rows and write to output
 * Inputs:	*data	Pointer to start of data sequence
 *			start	First row to process
 *			end		Last row to process
 *			width	Width of each row
 *			*p		Pointer to Params structure for the sort
 * Outputs:	Success as a Bool
 * Comment:	This processes the data in row major order, sequentially by row.
 */

static void mapsheet_mean_byrow(BinNode **ip, u32 start, u32 end,
								u32 width, MeanParam *p, f32p op)
{
	u32	row, col, snd, tused;
	f32	sum;
	
	op += start*width;
	for (row = start; row <= end; ++row) {
		for (col = 0; col < width; ++col) {
			if (ip[row][col].hits == 0)
				*op++ = p->defcooked;
			else {
				sum = 0.0f; tused = 0;
				for (snd = 0; snd < ip[row][col].hits; ++snd)
					if (ip[row][col].bin[snd] < -0.5f) {
						sum += ip[row][col].bin[snd];
						tused++;
					}
				*op++ = sum/tused;
			}
		}
	}
}

/* Routine:	mapsheet_mean_byrow_reject
 * Purpose:	Compute mean of a sequence of rows and write to output
 * Inputs:	*data	Pointer to start of data sequence
 *			start	First row to process
 *			end		Last row to process
 *			width	Width of each row
 *			*p		Pointer to Params structure for the sort
 * Outputs:	Success as a Bool
 * Comment:	This processes the data in row major order, sequentially by row.
 */

static void mapsheet_mean_byrow_reject(BinNode **ip, u32 start, u32 end,
									   u32 width, MeanParam *p, f32p op)
{
	u32	row, col;
	
	op += start*width;
	for (row = start; row <= end; ++row) {
		for (col = 0; col < width; ++col) {
			if (ip[row][col].hits == 0)
				*op++ = p->defcooked;
			else
				*op++ = mapsheet_mean_reject(ip[row][col].bin,
											 ip[row][col].hits,
											 p);
		}
	}
}

#ifdef sgi

/* Routine:	mapsheet_mean_parallel
 * Purpose:	Parallel driver routine for m_fork target
 * Inputs:	*p	Pointer to Param structure
 * Outputs:	-
 * Comment:	This splits the task up in row-major order in the hope that the
 *			data will be striped across multiple discs, and hence that the
 *			spatial discontinuity of the processing will hit up different discs
 *			in the stripe.
 */

static void mapsheet_mean_parallel(BinNode **ip, u32 width,
								   f32p op, MeanParam *p)
{
	u32		proc_id = m_get_myid(), num_procs = m_get_numprocs();
	u32		start_row, end_row;
	
	start_row = p->start[proc_id];
	end_row = p->end[proc_id];
	if (p->use_rejection)
		mapsheet_mean_byrow_reject(ip, start_row, end_row, width, p, op);
	else
		mapsheet_mean_byrow(ip, start_row, end_row, width, p, op);
}

#endif

/* Routine:	mapsheet_bin_by_mean
 * Purpose:	Compute mean value of each bin, with outlier rejection
 * Inputs:	**ip	Pointer to filled (or partially filled) bin nodes
 *			width	Width of bin array
 *			height	Height of bin array
 *			*par	Parameters structure
 *			op		Pointer to output space (1D, row major)
 * Outputs:	False on error, otherwise True
 * Comment:	This processes the array in row major order, optionally in parallel
 *			(compile-time choice), in which case the array is broken into
 *			pieces according to the number of elements are in the rows.  This
 *			attempts to put the same work into each processor's range of rows,
 *			so that all processors are kept busy at all times.
 */

Bool mapsheet_bin_by_mean(BinNode **ip, u32 width, u32 height, void *par,
						  f32p op)
{
	MeanParam	*p = (MeanParam*)par;
#ifdef sgi
	u32		*counts, row, col, nprocs, nelem, task, assigned, partition,
			tot_elem;
#endif
	
	if (p == NULL) {
		error_msg(modname, "parameters structure has not been initialised!\n");
		return(False);
	}
	if (p->use_parallel) {
#ifdef sgi
		/* Determine a partitioning of the rows according to nominal number of
		 * counts that need to be processed
		 */
		if ((counts = (u32*)calloc(height, sizeof(u32))) == NULL) {
			error_msgv(modname,
				"failed to allocate counts array for task balancing (%d bytes).\n",
				(u32)(sizeof(u32)*height));
			return(False);
		}

		/* Accumulate a count of the number of elements to be sorted within each
		 * row, taking into account sub-sampling, if appropriate.
		 */
		for (row = 0; row < height; ++row)
			for (col = 0; col < width; ++col)
				counts[row] += ip[row][col].hits;

		/* Compute partition by assigning equal numbers of counts to the tasks */
		nprocs = m_get_numprocs();
		for (row = 0, tot_elem = 0; row < height; ++row)
			tot_elem += counts[row];
		nelem = tot_elem / nprocs;
		p->start[0] = 0; p->end[nprocs-1] = height-1;
		partition = 0;
		for (task = 1; task < nprocs; ++task) {
			assigned = 0;
			while (assigned < nelem && partition < height)
				assigned += counts[partition++];
			p->end[task-1] = partition-1;
			p->start[task] = partition;
		}
		free(counts);
		if (m_fork(mapsheet_mean_parallel, ip, width, op, p) != 0) {
			error_msg(modname, "failed to fork processes for parallel mean.\n");
			return(False);
		}
#else
		error_msg(modname, "internal: parallel operations not supported.\n");
		return(False);
#endif
	} else {
		if (p->use_rejection)
			mapsheet_mean_byrow_reject(ip, 0, height-1, width, p, op);
		else
			mapsheet_mean_byrow(ip, 0, height-1, width, p, op);
	}
	return(True);
}

static Bool mapsheet_mean_init_parallelism(MeanParam *p)
{
#ifdef sgi
	u32			n_procs;
#endif

#ifdef sgi
	if ((n_procs = m_get_numprocs()) > 1) {
		p->use_parallel = True;
		if ((p->start = (u32p)malloc(sizeof(u32)*n_procs)) == NULL ||
			(p->end = (u32p)malloc(sizeof(u32)*n_procs)) == NULL) {
			error_msg(modname, "failed to allocate parallel task arrays.\n");
			free(p->start); free(p->end);
			return(False);
		}
	} else
		p->use_parallel = False;
#else
	p->use_parallel = False;
#endif
	return(True);
}
	
void *mapsheet_mean_init_param(void)
{
	MeanParam	*p;

	if ((p = (MeanParam*)calloc(1, sizeof(MeanParam))) == NULL) return(NULL);
	memcpy(p, &default_param, sizeof(MeanParam));
	if (!mapsheet_mean_init_parallelism(p)) {
		error_msg(modname, "failed to initialise optional parallelism.\n");
		free(p);
		return(NULL);
	}
	if (default_param.defcooked == DEFAULT_NULL_DEPTH)
		mapsheet_get_invalid(MAP_DATA_F32,&(p->defcooked));
	return((void*)p);
}

void mapsheet_mean_release_param(void *param)
{
	MeanParam	*p = (MeanParam*)param;

#ifdef sgi
	if (p->start != NULL) free(p->start);
	if (p->end != NULL) free(p->end);
#endif
	free(p);
}

Bool mapsheet_mean_write_param(void *param, FILE *f)
{
	MeanParam	*p = (MeanParam*)param;
	
	if (fwrite(p, sizeof(MeanParam), 1, f) != 1) {
		error_msg(modname, "failed to write algorithm parameters.\n");
		return(False);
	}
	return(True);
}

void *mapsheet_mean_read_param(FILE *f)
{
	MeanParam	*p;
	
	if ((p = (MeanParam*)malloc(sizeof(MeanParam))) == NULL) {
		error_msg(modname, "failed to allocate algorithm parameter workspace.\n");
		return(NULL);
	}
	if (fread(p, sizeof(MeanParam), 1, f) != 1) {
		error_msg(modname, "failed to read algorithm parameter workspace.\n");
		free(p);
		return(NULL);
	}
	if (!mapsheet_mean_init_parallelism(p)) {
		error_msg(modname, "failed to initialise optional parallelism.\n");
		free(p);
		return(NULL);
	}
	return(p);
}

Bool mapsheet_mean_insert_depths(MapSheet sheet, BinNode **grid, void *par,
								 Sounding *snds, u32 nsnds, u32 *nused)
{
	MeanParam	*p = (MeanParam*)par;
	u32			sounding;
	s32			row, col;
	MapHdr		*hdr = sheet->hdr;
	f64			left, top;
	
	/* Offset bounds by half a pixel dimension to ensure that we round into
	 * the correct bin (i.e., coordinate of bin are center of area represented.
	 */
	left = hdr->w_bound - hdr->east_sz/2.0;
	top = hdr->n_bound + hdr->north_sz/2.0;
	
	*nused = 0;
	for (sounding = 0; sounding < nsnds; ++sounding) {
		col = (u32)((snds[sounding].east - left)/hdr->east_sz + 0.5);
		row = (u32)((top - snds[sounding].north)/hdr->north_sz + 0.5);
		if (row < 0 || row >= (s32)hdr->n_north || col < 0 || col >= (s32)hdr->n_east)
			continue;
		if (!mapsheet_bin_depth(snds[sounding].depth, grid, (u32)row, (u32)col,
								p->buffer_quant, p->buffer_limit)) {
			error_msgv(modname, "failed to insert depth %d into (%d, %d).\n",
						sounding, row, col);
			return(False);
		}
		++*nused;
	}
	return(True);
}

/* Routine:	mapsheet_mean_limit_buffer
 * Purpose:	Set a limit on maximum size of buffer
 * Inputs:	*par	Pointer to private parameter workspace
 *			limit	Maximum size to which buffers should grow
 * Outputs:	-
 * Comment:	Note that this does not *reduce* the size of buffers currently
 *			allocated; it only stops new buffers from increasing to more than
 *			this size.
 */

void mapsheet_mean_limit_buffer(void *par, u32 limit)
{
	MeanParam	*p = (MeanParam*)par;
	
	p->buffer_limit = limit;
}

/* Routine:	mapsheet_mean_execute_params
 * Purpose:	Execute parameter list for this module
 * Inputs:	*list	Linked list of ParList structures to be parsed
 * Outputs:	True if the list of parameters was read correctly, otherwise False
 * Comment:	This looks for a null reconstruction depth, a std. err. cut-off,
 *			a mean and std. dev. ratio, a maximum number of iterations, and
 *			buffer parameters.  Lack of a parameter means the hard defaults at
 *			the top of the file are used.  Trimming is always enabled, and
 *			parallelism is turned on or off as the parameter structure is cloned
 *			for the calling routine.
 */

typedef enum {
	MAP_MN_UNKNOWN = 0,
	MAP_MN_NULL_DEPTH,
	MAP_MN_REJECTION_LIMIT,
	MAP_MN_MEAN_RATIO_EPS,
	MAP_MN_SDEV_RATIO_EPS,
	MAP_MN_MAX_ITERATIONS,
	MAP_MN_BUFFER_QUANTUM,
	MAP_MN_MAX_BUFFER
} MapMnParamEnum;

Bool mapsheet_mean_execute_params(ParList *list)
{
	ParTable tab[] = {
		{ "null_depth",			MAP_MN_NULL_DEPTH		},
		{ "reject_sdev_limit",	MAP_MN_REJECTION_LIMIT	},
		{ "mean_ratio_eps",		MAP_MN_MEAN_RATIO_EPS	},
		{ "sdev_ratio_eps",		MAP_MN_SDEV_RATIO_EPS	},
		{ "max_iterations",		MAP_MN_MAX_ITERATIONS	},
		{ "buffer_quantum",		MAP_MN_BUFFER_QUANTUM	},
		{ "max_buffer_pt",		MAP_MN_MAX_BUFFER		},
		{ NULL,					MAP_MN_UNKNOWN			}
	};
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;
	u32		dummy_int;

	node = list;
	do {
		node = params_match(node, "mapsheet.mean", tab, &id, &match);
		switch (id) {
			case MAP_MN_UNKNOWN:
				break;
			case MAP_MN_NULL_DEPTH:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" length measurement.\n", match->data);
					return(False);
				}
				default_param.defcooked = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting null depth to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case MAP_MN_REJECTION_LIMIT:
				dummy_float = atof(match->data);
				if (dummy_float <= 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" rejection limit (must be positive).\n", match->data);
					return(False);
				}
				default_param.se_scale = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting rejection limit to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case MAP_MN_MEAN_RATIO_EPS:
				dummy_float = atof(match->data);
				if (dummy_float <= 0.0 || dummy_float >= 1.0) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" ratio epsilon (range (0.0, 1.0)).\n", match->data);
					return(False);
				}
				default_param.mean_ratio_eps = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting mean ratio convergence eps. to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case MAP_MN_SDEV_RATIO_EPS:
				dummy_float = atof(match->data);
				if (dummy_float <= 0.0 || dummy_float >= 1.0) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" ratio convergence epsilon (range (0.0, 1.0)).\n",
						match->data);
					return(False);
				}
				default_param.serr_ratio_eps = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting sdev ratio convergence eps. to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case MAP_MN_MAX_ITERATIONS:
				dummy_int = atoi(match->data);
				if (dummy_int < MIN_MAX_ITER || dummy_int > MAX_MAX_ITER) {
					error_msgv(modname, "error: maximum number of algorithm"
						" iterations must be in range [%d, %d] (not %d).\n",
						MIN_MAX_ITER, MAX_MAX_ITER, dummy_int);
					return(False);
				}
				default_param.maxiter = dummy_int;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting maximum iterations to %d (%s).\n",
	dummy_int, match->data);
#endif
				break;
			case MAP_MN_BUFFER_QUANTUM:
				dummy_int = atoi(match->data);
				if (dummy_int < MIN_BUF_QUANT || dummy_int > MAX_BUF_QUANT) {
					error_msgv(modname, "error: buffer allocation quantum"
						" must be in range [%d, %d] (not %d).\n",
						MIN_BUF_QUANT, MAX_BUF_QUANT, dummy_int);
					return(False);
				}
				default_param.buffer_quant = dummy_int;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting buffer quantum to %d (%s).\n",
	dummy_int, match->data);
#endif
				break;
			case MAP_MN_MAX_BUFFER:
				dummy_int = atoi(match->data);
				if (dummy_int < MIN_BUF_LIMIT || dummy_int > MAX_BUF_LIMIT) {
					error_msgv(modname, "error: maximum buffer size"
						" must be in range [%d, %d] (not %d).\n",
						MIN_BUF_LIMIT, MAX_BUF_LIMIT, dummy_int);
					return(False);
				}
				default_param.buffer_limit = dummy_int;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting buffer limit to %d (%s).\n",
	dummy_int, match->data);
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

