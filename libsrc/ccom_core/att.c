/*
 * $Id: att.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2  2002/09/26 19:27:21  dneville
 * Updating the CCOM libraries to the latest versions.
 *
 * Revision 1.4.2.1  2002/07/14 02:20:46  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.4  2001/05/14 04:18:45  brc
 * Updated to make module 'params'-aware.
 *
 * Revision 1.3  2000/09/29 20:33:23  brc
 * Moved filter design from sounding code to general (since it can be used for
 * more than one purpose), and updated sounding code to use it.  Changed filter
 * to windowed ideal filter time domain design for simplicity.
 *
 * Revision 1.2  2000/09/25 20:20:01  brc
 * Commented out debug messages.  Optionally compile with __DEBUG__
 *
 * Revision 1.1  2000/09/24 01:26:38  brc
 * Object to hold attitude data and do interpolation according to time-stamp.
 * Note that this object should not be filled with high-rate data, since the
 * search for interpolation will not be too efficient.
 *
 *
 * File:	att.c
 * Purpose:	Attitude loading and interpolation
 * Date:	18 September 2000
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
#include "stdtypes.h"
#include "att.h"
#include "error.h"
#include "params.h"

static char *modname = "att";
static char *modrev = "$Revision: 2 $";

#undef __DEBUG__

#define DEFAULT_BUFFER_QUANTUM	1024

static u32 buffer_quantum = DEFAULT_BUFFER_QUANTUM;

typedef struct _attpt {
	f64	epoch;			/* Timestamp in sec. since 00:00:00 01/01/1970 */
	f32	roll;			/* Roll in degrees, +ve port up */
	f32	pitch;			/* Pitch in degress, +ve bow up */
	f32	heave;			/* Heave in metres, +ve down */
	f32	heading;		/* Heading in degrees, +ve CW from N */
} AttPt;

typedef struct _att {
	u32	n_points;		/* Number of points currently in buffer */
	u32	n_buffer;		/* Number of buffer spaces available */
	AttPt	*lut;		/* Array of attitude points */
} sAtt /*, *Att */;

void att_release(Att att)
{
	free(att->lut);
	free(att);
}

static Att att_new_n(u32 n_points)
{
	Att rtn;
	
	if ((rtn = (Att)calloc(1, sizeof(sAtt))) == NULL)
		return(NULL);
	if ((rtn->lut = (AttPt*)malloc(sizeof(AttPt)*n_points)) == NULL) {
		free(rtn);
		return(NULL);
	}
	rtn->n_points = 0;
	rtn->n_buffer = n_points;
	return(rtn);
}

Att att_new(void)
{
	Att	rtn;
	
	if ((rtn = att_new_n(buffer_quantum)) == NULL) {
		error_msg(modname, "failed allocating default attitude buffer.\n");
		return(NULL);
	}
	return(rtn);
}

Bool att_insert(Att att, f64 epoch, f32 roll, f32 pitch, f32 heading, f32 heave)
{
	u32		target;
	AttPt	*tmp;
	
	if (att->n_points >= att->n_buffer) {
		/* Need to reallocate buffer for more space */
		target = (att->n_buffer + buffer_quantum)*sizeof(AttPt);
		if ((tmp = (AttPt*)realloc(att->lut, target)) == NULL) {
			error_msgv(modname, "failed to increase attitude buffer by %d "
				"points from %d.\n", buffer_quantum, att->n_points);
			return(False);
		}
		att->lut = tmp;
		att->n_buffer += buffer_quantum;
	}
	att->lut[att->n_points].epoch = epoch;
	att->lut[att->n_points].roll = roll;
	att->lut[att->n_points].pitch = pitch;
	att->lut[att->n_points].heading = heading;
	att->lut[att->n_points].heave = heave;
	++att->n_points;
	
#ifdef __DEBUG__
printf("%s: debug: now %d points (e=%lf, r=%f, p=%f, hd=%f, he=%f).\n",
	modname, att->n_points, epoch, roll, pitch, heading, heave);
#endif

	return(True);
}

Bool att_interp(Att att, f64 epoch, f32p roll, f32p pitch,
				f32p heading, f32p heave)
{
	u32	left, right, mid;
	f64	scale_left, scale_right;

	if (att->n_points < 2) {
		error_msgv(modname, "can't interpolate with %d points.\n", att->n_points);
		return(False);
	}
	if (epoch < att->lut[0].epoch || epoch > att->lut[att->n_points-1].epoch) {
		error_msgv(modname, "time %d ms is outside att range %d--%d ms.\n",
			(u32)epoch, (u32)att->lut[0].epoch,
			(u32)att->lut[att->n_points-1].epoch);
		return(False);
	}
	left = 0; right = att->n_points-2;
	while (right-left > 1) {
		mid = (left + right)/2;
		if (att->lut[mid].epoch <= epoch) {
			/* Keep the right hand half */
			left = mid;
		} else {
			/* Keep the left hand half */
			right = mid;
		}
	}

	/* Now we need do the linear interpolation */
	right = left + 1; /* NB: we are guaranteed that we will not exceed the
				 * array bounds by initial limits check */
	scale_left = (att->lut[right].epoch - epoch)/
				(att->lut[right].epoch - att->lut[left].epoch);
	scale_right = (epoch - att->lut[left].epoch)/
				(att->lut[right].epoch - att->lut[left].epoch);
	*roll = (f32)(scale_left*att->lut[left].roll +
		 	scale_right*att->lut[right].roll);
	*pitch = (f32)(scale_left*att->lut[left].pitch +
		 	 scale_right*att->lut[right].pitch);
	*heading = (f32)(scale_left*att->lut[left].heading +
			   scale_right*att->lut[right].heading);
	*heave = (f32)(scale_left*att->lut[left].heave +
			 scale_right*att->lut[right].heave);

	return(True);
}

void att_get_bound_times(Att att, f64p start, f64p end)
{
	if (att->n_points == 0) {
		*start = 0.0;
		*end = 0.0;
	} else {
		*start = att->lut[0].epoch;
		*end = att->lut[att->n_points-1].epoch;
	}
}

Bool att_get_point_n(Att att, u32 n, f64p epoch, f32p roll, f32p pitch,
					 f32p heading, f32p heave)
{
	if (n >= att->n_points) {
		error_msgv(modname, "point %d requested; only %d available.\n",
					n, att->n_points);
		return(False);
	}
	*roll = att->lut[n].roll;
	*pitch = att->lut[n].pitch;
	*heading = att->lut[n].heading;
	*heave = att->lut[n].heave;
	*epoch = att->lut[n].epoch;
	return(True);
}

u32 att_get_npoints(Att att)
{
	return(att->n_points);
}

/* Routine:	att_flush_before_epoch
 * Purpose:	Flush all entries from the buffer before the given epoch time
 * Inputs:	att		Attitude stream to use
 *			epoch	Timestamp for removal time
 * Outputs:	-
 * Comment:	Times are stored internally in seconds since Unix epoch (00:00:00
 *			01/01/1970), with resolution of the underlying data stream (typ.
 *			ms., but in some cases worse).
 */

void att_flush_before_epoch(Att att, f64 epoch)
{
	u32	point = 0;
	
	while (point < att->n_points && att->lut[point].epoch < epoch)
		++point;
	if (point == 0) return; /* Nothing to flush */
	memmove(att->lut, att->lut+point, sizeof(AttPt)*(att->n_points-point));
	att->n_points -= point;
}

/* Routine:	att_flush_all_but_n
 * Purpose:	Flush the start of the buffer, all but the last n elements
 * Inputs:	att		Attitude stream to use
 *			n		Number at the end of buffer to retain.
 * Outputs:	-
 * Comment:	Note that the time left in the buffer will vary based on the rate
 *			at which the data is inserted into the buffer, and may not be a
 *			linear function of the number of elements retained (depending on the
 *			input data stream).
 */

void att_flush_all_but_n(Att att, u32 n)
{
	if (n > att->n_points) return; /* Wants to keep more than we have */
	memmove(att->lut, att->lut+att->n_points-n, sizeof(AttPt)*n);
	att->n_points = n;
}

/* Routine:	att_flush_all_but_t
 * Purpose:	Flush the start of the buffer, all but the last t seconds
 * Inputs:	att		Attitude stream to use
 *			t		Time to leave in the buffer
 * Outputs:	-
 * Comment:	This routine retains all of the data in the buffer if it is less
 *			than the time requested, so time in the buffer after this call is
 *			not necessarily _t_ seconds.
 */

void att_flush_all_but_t(Att att, f64 t)
{
	s32	point = att->n_points-2;
	
	while (point >= 0 &&
		   (att->lut[att->n_points-1].epoch-att->lut[point].epoch)<t)
		--point;
	if (point <= 0) return;	/* Less data than we need */
	memmove(att->lut, att->lut+point, sizeof(AttPt)*(att->n_points-point));
	att->n_points -= point;
}

/* Routine:	att_execute_params
 * Purpose:	Execute parameters list in this module
 * Inputs:	*list	List to parameters to work from
 * Outputs:	True if parameters used were OK, otherwise False
 * Comment:	Note that not finding a parameter is not a major problem; the only
 *			difficulty is if one is found, and it's wrong!
 */

Bool att_execute_params(ParList *list)
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
		node = params_match(node, "att", tab, &id, &match);
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
