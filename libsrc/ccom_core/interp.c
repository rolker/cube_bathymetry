/*
 * $Id: interp.c 23 2006-01-20 16:23:34Z brc $
 * $Log$
 * Revision 1.2  2006/01/20 16:23:34  brc
 * Fixed bug in interp_release() that caused only some of the memory to be
 * released due to the quantum buffering scheme, hence causing a leak.
 *
 * Revision 1.1.1.1  2003/02/03 20:18:44  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2  2002/09/26 19:27:20  dneville
 * Updating the CCOM libraries to the latest versions.
 *
 * Revision 1.1.2.1  2002/07/14 02:20:46  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.1  2002/03/14 04:04:04  brc
 * General purpose interpolation code for arbitrary length vectors of f32s.
 *
 *
 * File:	interp.c
 * Purpose:	General Linear Interpolation in N Variables
 * Date:	2002-02-05
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
#include "ccom_general.h"
#include "error.h"
#include "stdtypes.h"
#include "interp.h"
#include "params.h"

static char *modname = "interp";
static char *modrev = "$Revision: 23 $";

#undef __DEBUG__

#define DEFAULT_BUFFER_QUANTUM	1024

static u32 buffer_quantum = DEFAULT_BUFFER_QUANTUM;

typedef struct _interppt {
	f64	epoch;		/* Timestamp in sec. since 00:00:00 01/01/1970 */
	f32	*param;		/* Parameters to be interpolated at each time */
} InterpPt;

typedef struct _interp {
	f64	time_adj;		/* Time stamp adjustment for interpolation */
	u32	n_points;		/* Number of points currently in buffer */
	u32	n_buffer;		/* Number of buffer spaces available */
	u32	n_param;		/* Number of parameters being interpolated each time */
	u32	last_soln;		/* Last solution computed in interpolation buffer */
	InterpPt	*lut;		/* Array of interpolation points */
} sInterp /*, *Interp */;

/* Routine:	interp_release
 * Purpose:	Release all memory associated with the interpolation series
 * Inputs:	interp	Interpolation space to work on
 * Outputs:	-
 * Comment:	This also releases the Interp structure.
 */

void interp_release(Interp interp)
{
	u32	pt;
	
	if (interp == NULL) return;
	
	if (interp->lut != NULL) {
		for (pt = 0; pt < interp->n_buffer; ++pt)
			free(interp->lut[pt].param);
		free(interp->lut);
	}
	free(interp);
}

static Bool interp_init_pts(InterpPt *base, u32 n_points, u32 n_param)
{
	u32		pt;
	
	memset(base, 0, sizeof(InterpPt)*n_points);
	for (pt = 0; pt < n_points; ++pt) {
		if ((base[pt].param = (f32*)malloc(sizeof(f32)*n_param)) == NULL) {
			return(False);
		}
	}
	return(True);
}

static Interp interp_new_n(u32 n_points, u32 n_param, f64 time_adj)
{
	Interp	rtn;
	
	if ((rtn = (Interp)calloc(1, sizeof(sInterp))) == NULL)
		return(NULL);
	if ((rtn->lut = (InterpPt*)calloc(n_points, sizeof(InterpPt))) == NULL) {
		free(rtn);
		return(NULL);
	}
	rtn->n_points = 0;
	rtn->n_buffer = n_points;
	rtn->n_param = n_param;
	rtn->time_adj = time_adj;
	rtn->last_soln = 0;
	if (!interp_init_pts(rtn->lut, n_points, n_param)) {
		interp_release(rtn);
		return(NULL);
	}
	return(rtn);
}

/* Routine:	interp_new
 * Purpose:	Construct a generic linear interpolation buffer with time adjusts
 * Inputs:	n_params	Number of parameters to be interpolated
 *			time_adj	Timestamp adjustment for entries (see comment)
 * Outputs:	Pointer to (opaqued) buffer, or NULL on failure
 * Comment:	This sets up a default buffer with (buffer_quantum) spaces in it,
 *			ready for interpolation.  The time adjustment is applied to all
 *			entries as they are inserted into the buffer, and may be used to
 *			apply latency corrections for attitude/navigation data, etc.  The
 *			sense is positive latency, so that a positive value indicates that
 *			the time stamp is later than the actual time at which the values
 *			were measured.  That is, time stamp t with latency l means that the
 *			actual time of measurement was t-l.
 */

Interp interp_new(u32 n_params, f64 time_adj)
{
	Interp	rtn;
	
	if ((rtn = interp_new_n(buffer_quantum, n_params, time_adj)) == NULL) {
		error_msg(modname, "failed allocating default interpolation buffer.\n");
		return(NULL);
	}
	return(rtn);
}

/* Routine:	interp_insert
 * Purpose:	Add a new point into the interpolation buffers
 * Inputs:	interp	Interpolation buffer to use
 *			epoch	Time stamp for the data (exclusive of any latency)
 *			params	Pointer to parameters array to use
 * Outputs:	True if point is inserted, False otherwise
 * Comment:	This might fail if insufficient memory is available to add new
 *			points.  Note that the buffer is extended one quantum space at a
 *			time, which might mean that a fair bit of extra memory is required
 *			if the data goes a single point over the current space.
 */

Bool interp_insert(Interp interp, f64 epoch, f32p params)
{
	u32			target;
	InterpPt	*tmp;
	
	if (interp->n_points >= interp->n_buffer) {
		/* Need to reallocate buffer for more space */
		target = (interp->n_buffer + buffer_quantum)*sizeof(InterpPt);
		if ((tmp = (InterpPt*)realloc(interp->lut, target)) == NULL) {
			error_msgv(modname, "failed to increase interpolation buffer by %d "
				"points from %d.\n", buffer_quantum, interp->n_points);
			return(False);
		}
		interp->lut = tmp;
		if (!interp_init_pts(interp->lut+interp->n_buffer,
							 buffer_quantum,
							 interp->n_param)) {
			error_msgv(modname, "error: failed to initialise interpolation "
				"buffer space (%d points, %d params).\n", buffer_quantum,
				interp->n_param);
			return(False);
		}
		interp->n_buffer += buffer_quantum;
	}
	interp->lut[interp->n_points].epoch = epoch - interp->time_adj;
	memcpy(interp->lut[interp->n_points].param, params,
												sizeof(f32)*interp->n_param);
	++interp->n_points;
	
#ifdef __DEBUG__
printf("%s: debug: now %d points (e=%lf).\n", modname, interp->n_points, epoch);
#endif

	return(True);
}

/* Routine:	interp_interp
 * Purpose:	Carry out interpolation to a specific time
 * Inputs:	interp	Interpolation buffer to use
 *			epoch	Timestamp for the interpolation required
 * Outputs:	params	Pointer to space for parameters
 * Comment:	This carries out the interpolation.  Note that the code is tuned to
 *			do linear-in-time interpolation if it can (i.e., it expects that the
 *			code will call for interpolations with monotonically increasing
 *			time stamps.  This means that we don't have to do a binary search
 *			over the whole sequence every time we do an interpolation, which
 *			can result in a very significant speed-up.  The code will fall
 *			back to the full interpolation solution if this linear test does
 *			not work.
 */

Bool interp_interp(Interp interp, f64 epoch, f32p params)
{
	u32	left, right, mid, p;
	f64	scale_left, scale_right;

	if (interp->n_points < 2) {
		error_msgv(modname, "can't interpolate with %d points.\n", interp->n_points);
		return(False);
	}
	if (epoch < interp->lut[0].epoch || epoch > interp->lut[interp->n_points-1].epoch) {
		error_msgv(modname, "time %lf s is outside interp range %lf--%lf s.\n",
			epoch, interp->lut[0].epoch, interp->lut[interp->n_points-1].epoch);
		return(False);
	}
	if (interp->lut[interp->last_soln].epoch < epoch &&
		epoch < interp->lut[interp->last_soln+1].epoch) {
		left = interp->last_soln;
	} else if (interp->last_soln < interp->n_points-2 &&
			   interp->lut[interp->last_soln+1].epoch < epoch &&
			   epoch < interp->lut[interp->last_soln+2].epoch) {
		/* Normal linear sequence -- we've move forward one bracket */
		left = interp->last_soln+1;
	} else {
		/* OK: two test cases for usual call sequence (i.e., monotonic
		 * increase in time) didn't work, so now we need to find the elements
		 * which bracket the time from scratch.  To make things a little more
		 * efficient, we use a bracketed bisection (almost a binary search -
		 * except here the key isn't actually in the table, and we look for the
		 * entry with the closest smaller ping epoch --- i.e., we look for
		 * arg max { X : x_i \sup E, x_i < e, 0 <= i < N_i }, where e is the
		 * search epoch, and E = {e_i, 0 <= i < N_i} are the nav epochs).
		 */

		left = 0; right = interp->n_points-2;
		while (right-left > 1) {
			mid = (left + right)/2;
			if (interp->lut[mid].epoch <= epoch) {
				/* Keep the right hand half */
				left = mid;
			} else {
				/* Keep the left hand half */
				right = mid;
			}
		}
	}

	/* Now we need do the linear interpolation */
	right = left + 1; /* NB: we are guaranteed that we will not exceed the
				 	   * array bounds by initial limits check */
	scale_left = (interp->lut[right].epoch - epoch)/
				(interp->lut[right].epoch - interp->lut[left].epoch);
	scale_right = (epoch - interp->lut[left].epoch)/
				(interp->lut[right].epoch - interp->lut[left].epoch);
	
	for (p = 0; p < interp->n_param; ++p) {
		params[p] = (f32)(scale_left*interp->lut[left].param[p] +
					scale_right*interp->lut[right].param[p]);
	}
	interp->last_soln = left;

	return(True);
}

/* Routine:	interp_get_bound_times
 * Purpose:	Return bounding times on the currently available data
 * Inputs:	interp	Interpolation buffer to use
 * Outputs:	*start	Start time of data in interpolation buffers
 *			*end	End time of data in interpolation buffers
 * Comment:	Returns 0.0 for both times if there is no data in the interpolation
 *			buffers.  May also return start == end if there is only one point.
 */

void interp_get_bound_times(Interp interp, f64p start, f64p end)
{
	if (interp->n_points == 0) {
		*start = 0.0;
		*end = 0.0;
	} else {
		*start = interp->lut[0].epoch;
		*end = interp->lut[interp->n_points-1].epoch;
	}
}

/* Routine:	interp_get_bound_times
 * Purpose:	Return bounding times on the currently available data
 * Inputs:	interp	Interpolation buffer to use
 * Outputs:	*start	Start time of data in interpolation buffers
 *			*end	End time of data in interpolation buffers
 * Comment:	Returns 0.0 for both times if there is no data in the interpolation
 *			buffers.  May also return start == end if there is only one point.
 */

Bool interp_get_point_n(Interp interp, u32 n, f64p epoch, f32p param)
{
	if (n >= interp->n_points) {
		error_msgv(modname, "point %d requested; only %d available.\n",
					n, interp->n_points);
		return(False);
	}
	memcpy(param, interp->lut + n, sizeof(f32)*interp->n_param);
	return(True);
}

/* Routine:	interp_get_npoints
 * Purpose:	Return the number of points currently in the interpolation buffer
 * Inputs:	interp	Interpolation buffer to use
 * Outputs:	Returns the number of points (may be zero)
 * Comment:	-
 */

u32 interp_get_npoints(Interp interp)
{
	return(interp->n_points);
}

/* Routine:	interp_get_nparams
 * Purpose:	Return the number of parameters in the interpolation buffer
 * Inputs:	interp	Interpolation buffer to use
 * Outputs:	Returns number of parameters configured into the buffer
 * Comment:	Note that this doesn't make any distinction about what the
 *			parameters actually are --- just how many to work with.
 */

u32 interp_get_nparams(Interp interp)
{
	return(interp->n_param);
}

/* Routine:	interp_flush_before_epoch
 * Purpose:	Flush all entries from the buffer before the given epoch time
 * Inputs:	interp		Interpitude stream to use
 *			epoch	Timestamp for removal time
 * Outputs:	-
 * Comment:	Times are stored internally in seconds since Unix epoch (00:00:00
 *			01/01/1970), with resolution of the underlying data stream (typ.
 *			ms., but in some cases worse).
 */

void interp_flush_before_epoch(Interp interp, f64 epoch)
{
	u32	point = 0, p;
	
	while (point < interp->n_points && interp->lut[point].epoch < epoch)
		++point;
	if (point == 0) return; /* Nothing to flush */
	for (p = 0; p < point; ++p) free(interp->lut[p].param);
	memmove(interp->lut, interp->lut+point, sizeof(InterpPt)*(interp->n_points-point));
	interp->n_points -= point;
}

/* Routine:	interp_flush_all_but_n
 * Purpose:	Flush the start of the buffer, all but the last n elements
 * Inputs:	interp		Interpitude stream to use
 *			n		Number at the end of buffer to retain.
 * Outputs:	-
 * Comment:	Note that the time left in the buffer will vary based on the rate
 *			at which the data is inserted into the buffer, and may not be a
 *			linear function of the number of elements retained (depending on the
 *			input data stream).
 */

void interp_flush_all_but_n(Interp interp, u32 n)
{
	u32	p;
	
	if (n > interp->n_points) return; /* Wants to keep more than we have */
	for (p = 0; p < interp->n_points-n; ++p) free(interp->lut[p].param);
	memmove(interp->lut, interp->lut+interp->n_points-n, sizeof(InterpPt)*n);
	interp->n_points = n;
}

/* Routine:	interp_flush_all_but_t
 * Purpose:	Flush the start of the buffer, all but the last t seconds
 * Inputs:	interp		Interpitude stream to use
 *			t		Time to leave in the buffer
 * Outputs:	-
 * Comment:	This routine retains all of the data in the buffer if it is less
 *			than the time requested, so time in the buffer after this call is
 *			not necessarily _t_ seconds.
 */

void interp_flush_all_but_t(Interp interp, f64 t)
{
	s32	p;
	s32	point = interp->n_points-2;
	
	while (point >= 0 &&
		   (interp->lut[interp->n_points-1].epoch-interp->lut[point].epoch)<t)
		--point;
	if (point <= 0) return;	/* Less data than we need */
	for (p = 0; p < point; ++p) free(interp->lut[p].param);
	memmove(interp->lut, interp->lut+point, sizeof(InterpPt)*(interp->n_points-point));
	interp->n_points -= point;
}

/* Routine:	interp_dump
 * Purpose:	Dump the contents of the interpolation buffer to file
 * Inputs:	interp	Buffer to dump
 *			*name	Filename to dump to
 * Outputs:	True if file was created, otherwise False
 * Comment:	This is typically useful only for debuging.
 */

Bool interp_dump(Interp interp, char *name)
{
	FILE	*f;
	u32		rec, par;
	
	if ((f = fopen(name, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			name);
		return(False);
	}
	for (rec = 0; rec < interp->n_points; ++rec) {
		fprintf(f, "%d %.3lf ", rec, interp->lut[rec].epoch);
		for (par = 0; par < interp->n_param; ++par)
			fprintf(f, "%g ", interp->lut[rec].param[par]);
		fprintf(f, "\n");
	}
	fclose(f);
	return(True);
}

/* Routine:	interp_execute_params
 * Purpose:	Execute parameters list in this module
 * Inputs:	*list	List to parameters to work from
 * Outputs:	True if parameters used were OK, otherwise False
 * Comment:	Note that not finding a parameter is not a major problem; the only
 *			difficulty is if one is found, and it's wrong!
 */

Bool interp_execute_params(ParList *list)
{
	ParTable	tab[] = {
		{ "buffer_quantum",	1 },
		{ NULL,				0 }
	};
	ParList	*node, *match;
	u32		id;
	s32		quantum;
	
	node = list;
	do {
		node = params_match(node, "interp", tab, &id, &match);
		switch (id) {
			case 0:
				/* Matched nothing ... but that may just mean that there's
				 * nothing there for this module.
				 */
				break;
			case 1:
				quantum = atoi(match->data);
				if (quantum <= 0 || quantum > 10*DEFAULT_BUFFER_QUANTUM) {
					error_msgv(modname, "buffer quantum (%d) out of range."
						" Switching back to default.\n", quantum);
					quantum = DEFAULT_BUFFER_QUANTUM;
				}
				buffer_quantum = quantum;
				match->used = True;

#ifdef __DEBUG__
error_msgv(modname, "debug: setting buffer quantum to %d.\n", buffer_quantum);
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
