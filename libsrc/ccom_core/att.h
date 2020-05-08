/*
 * $Id: att.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2  2002/09/26 19:27:21  dneville
 * Updating the CCOM libraries to the latest versions.
 *
 * Revision 1.4  2001/05/14 04:18:45  brc
 * Updated to make module 'params'-aware.
 *
 * Revision 1.3  2000/10/27 20:53:30  roland
 * libccom has now been cplusplusized!
 *
 * Revision 1.2  2000/09/29 20:33:23  brc
 * Moved filter design from sounding code to general (since it can be used for
 * more than one purpose), and updated sounding code to use it.  Changed filter
 * to windowed ideal filter time domain design for simplicity.
 *
 * Revision 1.1  2000/09/24 01:26:38  brc
 * Object to hold attitude data and do interpolation according to time-stamp.
 * Note that this object should not be filled with high-rate data, since the
 * search for interpolation will not be too efficient.
 *
 *
 * File:	att.h
 * Purpose:	Types and prototypes for attitude information
 * Date:	18 September 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __ATT_H__
#define __ATT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"

typedef struct _att *Att;

extern void att_release(Att att);
extern Att att_new(void);
extern Bool att_insert(Att att, f64 epoch, f32 roll, f32 pitch,
					   f32 heading, f32 heave);
extern Bool att_interp(Att att, f64 epoch, f32p roll, f32p pitch,
					   f32p heading, f32p heave);
extern void att_get_bound_times(Att att, f64p start, f64p end);
extern Bool att_get_point_n(Att att, u32 n, f64p epoch, f32p roll, f32p pitch,
					 		f32p heading, f32p heave);
extern u32 att_get_npoints(Att att);

/* Routine:	att_flush_before_epoch
 * Purpose:	Flush all entries from the buffer before the given epoch time
 * Inputs:	att		Attitude stream to use
 *			epoch	Timestamp for removal time
 * Outputs:	-
 * Comment:	Times are stored internally in seconds since Unix epoch (00:00:00
 *			01/01/1970), with resolution of the underlying data stream (typ.
 *			ms., but in some cases worse).
 */

extern void att_flush_before_epoch(Att att, f64 epoch);

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

extern void att_flush_all_but_n(Att att, u32 n);

/* Routine:	att_flush_all_but_t
 * Purpose:	Flush the start of the buffer, all but the last t seconds
 * Inputs:	att		Attitude stream to use
 *			t		Time to leave in the buffer
 * Outputs:	-
 * Comment:	This routine retains all of the data in the buffer if it is less
 *			than the time requested, so time in the buffer after this call is
 *			not necessarily _t_ seconds.
 */

extern void att_flush_all_but_t(Att att, f64 t);

#include "params.h"

/* Routine:	att_execute_params
 * Purpose:	Execute parameters list in this module
 * Inputs:	*list	List to parameters to work from
 * Outputs:	True if parameters used were OK, otherwise False
 * Comment:	Note that not finding a parameter is not a major problem; the only
 *			difficulty is if one is found, and it's wrong!
 */

extern Bool att_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif
#endif
