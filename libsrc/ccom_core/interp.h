/*
 * $Id: interp.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2  2002/09/26 19:27:20  dneville
 * Updating the CCOM libraries to the latest versions.
 *
 * Revision 1.1  2002/03/14 04:04:04  brc
 * General purpose interpolation code for arbitrary length vectors of f32s.
 *
 *
 * File:	interp.h
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

#ifndef __INTERP_H__
#define __INTERP_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"

typedef struct _interp *Interp;

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

extern Interp interp_new(u32 n_params, f64 time_adj);

/* Routine:	interp_release
 * Purpose:	Release all memory associated with the interpolation series
 * Inputs:	interp	Interpolation space to work on
 * Outputs:	-
 * Comment:	This also releases the Interp structure.
 */

extern void interp_release(Interp interp);

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

extern Bool interp_insert(Interp interp, f64 epoch, f32p params);

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

extern Bool interp_interp(Interp interp, f64 epoch, f32p params);

/* Routine:	interp_get_bound_times
 * Purpose:	Return bounding times on the currently available data
 * Inputs:	interp	Interpolation buffer to use
 * Outputs:	*start	Start time of data in interpolation buffers
 *			*end	End time of data in interpolation buffers
 * Comment:	Returns 0.0 for both times if there is no data in the interpolation
 *			buffers.  May also return start == end if there is only one point.
 */

extern void interp_get_bound_times(Interp interp, f64p start, f64p end);

/* Routine:	interp_get_point_n
 * Purpose:	Return a specific point in the interpolation sequence
 * Inputs:	interp	Interpolation buffer to use
 *			n		Sample to extract
 * Outputs:	epoch	Pointer to timestamp space (see comment)
 *			param	Pointer to parameter output space
 * Comment:	Note that the timestamp returned is corrected for latency if any
 *			time adjustment was supplied when the interpolation buffer was
 *			constructed.  No attempt is made to check that the parameter output
 *			space is of the correct size for the interpolation buffer.
 */

extern Bool interp_get_point_n(Interp interp, u32 n, f64p epoch, f32p param);

/* Routine:	interp_get_npoints
 * Purpose:	Return the number of points currently in the interpolation buffer
 * Inputs:	interp	Interpolation buffer to use
 * Outputs:	Returns the number of points (may be zero)
 * Comment:	-
 */

extern u32 interp_get_npoints(Interp interp);

/* Routine:	interp_get_nparams
 * Purpose:	Return the number of parameters in the interpolation buffer
 * Inputs:	interp	Interpolation buffer to use
 * Outputs:	Returns number of parameters configured into the buffer
 * Comment:	Note that this doesn't make any distinction about what the
 *			parameters actually are --- just how many to work with.
 */

extern u32 interp_get_nparams(Interp interp);

/* Routine:	interp_flush_before_epoch
 * Purpose:	Flush all entries from the buffer before the given epoch time
 * Inputs:	interp		Interpitude stream to use
 *			epoch	Timestamp for removal time
 * Outputs:	-
 * Comment:	Times are stored internally in seconds since Unix epoch (00:00:00
 *			01/01/1970), with resolution of the underlying data stream (typ.
 *			ms., but in some cases worse).
 */

extern void interp_flush_before_epoch(Interp interp, f64 epoch);

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

extern void interp_flush_all_but_n(Interp interp, u32 n);

/* Routine:	interp_flush_all_but_t
 * Purpose:	Flush the start of the buffer, all but the last t seconds
 * Inputs:	interp		Interpitude stream to use
 *			t		Time to leave in the buffer
 * Outputs:	-
 * Comment:	This routine retains all of the data in the buffer if it is less
 *			than the time requested, so time in the buffer after this call is
 *			not necessarily _t_ seconds.
 */

extern void interp_flush_all_but_t(Interp interp, f64 t);

/* Routine:	interp_dump
 * Purpose:	Dump the contents of the interpolation buffer to file
 * Inputs:	interp	Buffer to dump
 *			*name	Filename to dump to
 * Outputs:	True if file was created, otherwise False
 * Comment:	This is typically useful only for debuging.
 */

extern Bool interp_dump(Interp interp, char *name);

/* Routine:	interp_execute_params
 * Purpose:	Execute parameters list in this module
 * Inputs:	*list	List to parameters to work from
 * Outputs:	True if parameters used were OK, otherwise False
 * Comment:	Note that not finding a parameter is not a major problem; the only
 *			difficulty is if one is found, and it's wrong!
 */

#include "params.h"

extern Bool interp_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
