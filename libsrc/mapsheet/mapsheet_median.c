/*
 * $Id: mapsheet_median.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:30:00  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.9.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.9  2001/09/20 18:53:46  brc
 * Modified to use mapsheet.c for invalid data value (for consistency).
 *
 * Revision 1.8  2001/08/21 01:45:43  brc
 * Added facility to count the number of soundings actually used from each swath
 * to update the data in the mapsheet region.  This can be used, among other things,
 * to determine which data files in a set are actually used in updating the data.
 *
 * Revision 1.7  2001/05/15 01:49:12  brc
 * Turned off debugging code.
 *
 * Revision 1.6  2001/05/15 01:18:08  brc
 * Modifications to make libccom compile cleanly under Linux.
 *
 * Revision 1.5  2001/05/14 21:07:22  brc
 * Added facilities to make sub-module 'params'-aware.
 *
 * Revision 1.4  2000/12/03 19:50:35  brc
 * Fixed bug in mapsheet_median_byrow() to make sure that the code moves the
 * start-point for processing forward to the correct location (based on the
 * _start_ point passed by the driver).  This means that when processing with
 * multiple CPUs, the results actually get put in the right places!
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
 * File:	mapsheet_median.c
 * Purpose:	Median `filtering' for bined depth estimation
 * Date:	07 July 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <float.h>

#ifdef sgi
#	include <ulocks.h>
#	include <task.h>
#endif

#include "stdtypes.h"
#include "ccom_general.h"
#include "error.h"
#include "mapsheet.h"
#include "mapsheet_private.h"
#include "mapsheet_median.h"
#include "params.h"

#undef __DEBUG__

#define DEFAULT_NULL_DEPTH	0.0		/* Default depth if no hits in bin */
#define DEFAULT_SUBSAMPLE_LEVEL	500	/* Start to sub-sample points after this */
#define MIN_SUBSAMP_LEVEL		10
#define MAX_SUBSAMP_LEVEL		65536

/* Default buffer quantum size (i.e., size by which bin buffers are increased
 * when more data is presented to a full buffer).
 */
#define DEFAULT_BUFFER_QUANTUM	256
#define MIN_BUF_QUANT			10
#define MAX_BUF_QUANT			1024

/* Default buffer limit size (i.e., the maximum number of bins in the buffer
 * unless the user reduces the limit.
 */
#define DEFAULT_BUFFER_LIMIT	65536
#define MIN_BUF_LIMIT			10
#define MAX_BUF_LIMIT			65536

static char *modname = "mapsheet_median";
static char *modrev = "$Revision: 2 $";

typedef struct _median_bin_param {
	Bool	use_parallel;	/* Whether the parallel algorithm should be used */
	f32		defcooked;		/* Default value for cooked output if no inputs */
	u32		buffer_quant;	/* Bin buffer resize quantum */
	u32		buffer_limit;	/* Maximum size of buffers */
	u32		subsamp;		/* Sub-sample count limit (or -1 if none) */
#ifdef sgi
	u32		*start;			/* Start points for parallel tasks */
	u32		*end;			/* End points for parallel tasks */
#endif
} MedianParam;

static MedianParam default_param = {
	True,
	DEFAULT_NULL_DEPTH,
	DEFAULT_BUFFER_QUANTUM,
	DEFAULT_BUFFER_LIMIT,
	DEFAULT_SUBSAMPLE_LEVEL
#ifdef sgi
	, NULL, NULL
#endif
};

/* Sorting function for qsort() algorithm */
int cpb_sort(const void *a, const void *b)
{
	f32 pa = *((f32p)a), pb = *((f32p)b);
	
	if (pa < pb) return(-1); else return(1);
}

/* Routine:	mapsheet_median_byrow
 * Purpose:	Median sort a sequence of rows and write to output
 * Inputs:	start_row	First row to work on (inclusive)
 *			end_row		Last row to work on (inclusive)
 *			cols		Width of each row
 *			*p			Pointer to Params structure for the sort
 * Outputs:	Success as a Bool
 * Comment:	This sorts the data in row major order, sequentially by row.
 */

static void mapsheet_median_byrow(BinNode **ip, u32 start, u32 end,
								  u32 width, MedianParam *p, f32p op)
{
	int		row, col;
	
	op += start*width;
	for (row = start; row <= (s32)end; ++row) {
		for (col = 0; col < (s32)width; ++col) {
			if (ip[row][col].hits == 0) {
				*op++ = p->defcooked;
			} else if (p->subsamp > 0 && ip[row][col].hits > p->subsamp) {
				/* Select subsamp elements by doing first _subsamp_ steps of
				 * Gentle's permutation algorithm
				 */
				ccom_permute_n(ip[row][col].bin, sizeof(f32), p->subsamp,
							   ip[row][col].hits);
				qsort(ip[row][col].bin, p->subsamp, sizeof(f32), cpb_sort);
				*op++ = ip[row][col].bin[p->subsamp/2];
			} else {
				qsort(ip[row][col].bin, ip[row][col].hits, sizeof(f32),
					  cpb_sort);
				*op++ = ip[row][col].bin[ip[row][col].hits/2];
			}
		}
	}
}

#ifdef sgi

/* Routine:	mapsheet_median_parallel
 * Purpose:	Parallel driver routine for m_fork target
 * Inputs:	*p	Pointer to Param structure
 * Outputs:	-
 * Comment:	This splits the task up in row-major order in the hope that the
 *			data will be striped across multiple discs, and hence that the
 *			spatial discontinuity of the processing will hit up different discs
 *			in the stripe.
 */

static void mapsheet_median_parallel(BinNode **ip, u32 width,
								   f32p op, MedianParam *p)
{
	u32		proc_id = m_get_myid(), num_procs = m_get_numprocs();
	u32		start_row, end_row;
	
	start_row = p->start[proc_id];
	end_row = p->end[proc_id];
	mapsheet_median_byrow(ip, start_row, end_row, width, p, op);
}

#endif

/* Routine:	mapsheet_bin_by_median
 * Purpose:	Compute median value of each bin, with subsamping
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

Bool mapsheet_bin_by_median(BinNode **ip, u32 width, u32 height, void *par,
						    f32p op)
{
	MedianParam	*p = (MedianParam*)par;
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
		if (m_fork(mapsheet_median_parallel, ip, width, op, p) != 0) {
			error_msg(modname, "failed to fork processes for parallel median.\n");
			return(False);
		}
#else
		error_msg(modname, "internal: parallel operations not supported.\n");
		return(False);
#endif
	} else
		mapsheet_median_byrow(ip, 0, height-1, width, p, op);
	return(True);
}

static Bool mapsheet_median_init_parallelism(MedianParam *p)
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

void *mapsheet_median_init_param(void)
{
	MedianParam	*p;

	if ((p = (MedianParam*)calloc(1, sizeof(MedianParam))) == NULL)
		return(NULL);
	memcpy(p, &default_param, sizeof(MedianParam));
	if (!mapsheet_median_init_parallelism(p)) {
		error_msg(modname, "failed to initialise optional parallelism.\n");
		free(p);
		return(NULL);
	}
	if (default_param.defcooked == DEFAULT_NULL_DEPTH)
		mapsheet_get_invalid(MAP_DATA_F32, &(p->defcooked));
	return((void*)p);
}

void mapsheet_median_release_param(void *param)
{
	MedianParam	*p = (MedianParam*)param;

#ifdef sgi
	if (p->start != NULL) free(p->start);
	if (p->end != NULL) free(p->end);
#endif
	free(p);
}

Bool mapsheet_median_write_param(void *param, FILE *f)
{
	MedianParam	*p = (MedianParam*)param;
	
	if (fwrite(p, sizeof(MedianParam), 1, f) != 1) {
		error_msg(modname, "failed to write algorithm parameters.\n");
		return(False);
	}
	return(True);
}

void *mapsheet_median_read_param(FILE *f)
{
	MedianParam	*p;
	
	if ((p = (MedianParam*)calloc(1, sizeof(MedianParam))) == NULL) {
		error_msg(modname, "failed to allocate algorithm parameter space.\n");
		return(NULL);
	}
	if (fread(p, sizeof(MedianParam), 1, f) != 1) {
		error_msg(modname, "failed reading algorithm parameters from file.\n");
		free(p);
		return(NULL);
	}
	if (!mapsheet_median_init_parallelism(p)) {
		error_msg(modname, "failed initialising optional parallelism.\n");
		free(p);
		return(NULL);
	}
	return(p);
}

Bool mapsheet_median_insert_depths(MapSheet sheet, BinNode **grid, void *par,
								   Sounding *snds, u32 nsnds, u32 *nused)
{
	MedianParam	*p = (MedianParam*)par;
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

/* Routine:	mapsheet_median_limit_buffer
 * Purpose:	Set a limit on maximum size of buffer
 * Inputs:	*par	Pointer to private parameter workspace
 *			limit	Maximum size to which buffers should grow
 * Outputs:	-
 * Comment:	Note that this does not *reduce* the size of buffers currently
 *			allocated; it only stops new buffers from increasing to more than
 *			this size.
 */

void mapsheet_median_limit_buffer(void *par, u32 limit)
{
	MedianParam	*p = (MedianParam*)par;
	
	p->buffer_limit = limit;
}

/* Routine:	mapsheet_median_execute_params
 * Purpose:	Execute parameter list for this sub-module
 * Inputs:	*list	ParList to work through
 * Outputs:	True if parameters were executed sucessfully, otherwise False
 * Comment:	This looks for a default null depth, a subsample ratio (i.e.,
 *			how many elements to choose for sorting into median order), a
 *			buffer quantum and a maximum buffer level.
 */

typedef enum {
	MAP_MD_UNKNOWN = 0,
	MAP_MD_NULL_DEPTH,
	MAP_MD_BUFFER_QUANTUM,
	MAP_MD_MAX_BUFFER,
	MAP_MD_SUBSAMPLE
} MapMdParamEnum;

Bool mapsheet_median_execute_params(ParList *list)
{
	ParTable tab[] = {
		{ "null_depth",			MAP_MD_NULL_DEPTH		},
		{ "buffer_quantum",		MAP_MD_BUFFER_QUANTUM	},
		{ "max_buffer_pt",		MAP_MD_MAX_BUFFER		},
		{ "subsample_at",		MAP_MD_SUBSAMPLE		},
		{ NULL,					MAP_MD_UNKNOWN			}
	};
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;
	u32		dummy_int;

	node = list;
	do {
		node = params_match(node, "mapsheet.median", tab, &id, &match);
		switch (id) {
			case MAP_MD_UNKNOWN:
				break;
			case MAP_MD_NULL_DEPTH:
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
			case MAP_MD_BUFFER_QUANTUM:
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
			case MAP_MD_MAX_BUFFER:
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
			case MAP_MD_SUBSAMPLE:
				dummy_int = atoi(match->data);
				if (dummy_int < MIN_SUBSAMP_LEVEL ||
												dummy_int > MAX_SUBSAMP_LEVEL) {
					error_msgv(modname, "error: maximum buffer size"
						" must be in range [%d, %d] (not %d).\n",
						MIN_SUBSAMP_LEVEL, MAX_SUBSAMP_LEVEL, dummy_int);
					return(False);
				}
				default_param.subsamp = dummy_int;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting subsample level to %d (%s).\n",
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
	
	/* Check that the sub-sample level makes sense */
	if (default_param.subsamp >= default_param.buffer_limit) {
		error_msg(modname, "warning: sub-sample level set above buffer limit;"
			" reducing sub-sample level to match buffer.\n");
		default_param.subsamp = default_param.buffer_limit;
	}
	/* Check that the buffer quantum level makes sense */
	if (default_param.buffer_quant > default_param.buffer_limit) {
		error_msg(modname, "warning: buffer quantum set above buffer limit;"
			" reducing quantum to match buffer.\n");
		default_param.buffer_quant = default_param.buffer_limit;
	}
	return(True);
}
