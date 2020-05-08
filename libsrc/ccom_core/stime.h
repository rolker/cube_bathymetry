/*
 * $Id: stime.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:35  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.6  2002/03/14 04:05:08  brc
 * Added stime_timestamp() to generate a reasonably formatted timestamp for
 * the current time.
 *
 * Revision 1.5  2001/09/23 19:14:14  brc
 * Added code to break time into Julian day + time, rather than into Y/M/D +
 * time.  This  was essentially to support the tide.c module when it writes
 * data to disc for transfer to OMG style files (which just have J-Day, and
 * no year information).
 *
 * Revision 1.4  2001/05/14 04:24:07  brc
 * Updated to make module 'params'-aware, and added routine to convert timestamp
 * from human-readable YYYY/DD/MM:HH:MM:SS form to uniform seconds since epoch.
 *
 * Revision 1.3  2001/02/21 05:46:38  brc
 * Added stime_make_time_jday() to compute times based on Julian day, rather
 * than month/day combination.  This uses the normal definition for leap years
 * (4|y unless 100|y unless 400|y), and simply convert to month/day by LUT of
 * month durations, then passes the results to stime_make_time() for conversion.
 *
 * Revision 1.2  2000/12/03 20:53:01  brc
 * Added prototype for stime_break_time().
 *
 * Revision 1.1  2000/11/14 22:43:00  brc
 * Sub-project to encapsulate manipulation of times, and in particular conversion
 * of times from broken out form to uniform seconds.  This also has a simple
 * interpolation scheme so that systems with dual timebases can be manipulated.
 *
 *
 * File:	stime.h
 * Purpose:	Survey Time Tools (Conversion, Interpolation, etc.)
 * Date:	14 November 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __STIME_H__
#define __STIME_H__

#include "stdtypes.h"

typedef struct _stime *STime;

extern void stime_release(STime lut);
extern STime stime_new(void);
extern Bool stime_insert(STime lut, f64 key, f64 target);
extern Bool stime_interp(STime lut, f64 key, f64p result);
extern void stime_get_bound_keys(STime lut, f64p start, f64p end);
extern Bool stime_get_point_n(STime lut, u32 n, f64p key, f64p target);
extern u32 stime_get_npoints(STime lut);

/* Routine:	stime_make_time
 * Purpose:	Generate a uniform time stamp from broken out time
 * Inputs:	year	Year stamp (must be Y2K compliant - YYYY) [1900,2037]
 *			month	Month [1,12]
 *			day		Day [1,31]
 *			hr		Hours [0,23]
 *			min		Minutes [0,59]
 *			sec		Seconds [0,59]
 *			msec	Milliseconds [0,1000)
 * Outputs:	Uniform time in UTC with accuracy as good as the input data.
 * Comment:	This converts the broken out time into a uniform number of seconds
 *			since epoch (1970-01-01/00:00:00), but with resolution equal to that
 *			of the input stream.  All times are assumed to be UTC (GMT).
 *				Note that limits above are usual, but are not enforced by the
 *			code unless compiled with __DEBUG__ set, in which case a warning
 *			is issued.
 */

extern f64 stime_make_time(u32 year, u32 month, u32 day, u32 hr, u32 min,
						   u32 sec, f64 msec);

/* Routine:	stime_make_time_jday
 * Purpose:	Make a uniform time from description in Julian day
 * Inputs:	year	Year [1970, 2037]
 *			jday	Julian Day [1, 366]
 *			hr		Hour [0,23]
 *			min		Minute [0,59]
 *			sec		seconds [0,59]
 *			msec	Milliseconds [0,1000.0)
 * Outputs:	Returns uniform time in UTC with accuracy as good as the input.
 * Comment: This is a wrapper on stime_make_time() which just converts Julian
 *			day into month/day.  Comments from stime_make_time() should be
 *			taken for this code also.
 */

extern f64 stime_make_time_jday(u32 year, u32 jday, u32 hr, u32 min, u32 sec,
								f64 msec);

/* Routine:	stime_break_time
 * Purpose:	Breaks a uniform time into component form
 * Inputs:	stamp	Timestamp to break out
 * Outputs:	*year	Year [1970,2037]
 *			*month	Month [1,12]
 *			*day	Day [1,31]
 *			*hr		Hour [0,23]
 *			*min	Minute [0,59]
 *			*sec	Seconds [0,59]
 *			*msec	Milliseconds [0,1000.0)
 * Comment: This handles the YYYY/MM/DD HH:MM:SS by the standard library, and
 *			deals with the fractional seconds itself.  Times before 00:00
 *			01/01/1970 and after 03:14:07, 19/01/2038 cannot be represented
 *			at present, and will cause undefined operation.
 */

extern Bool stime_break_time(f64 stamp, u32p year, u32p month, u32p day,
					  		 u32p hr, u32p min, u32p sec, f64p msec);

/* Routine:	stime_break_time_jday
 * Purpose:	Breaks time out into component with Julian day
 * Inputs:	stamp	Timestamp to break out
 * Outputs:	*year	Year [1970,2037]
 *			*jday	Julian Day [1,365]
 *			*hr		Hour [0,23]
 *			*min	Minute [0,59]
 *			*sec	Seconds [0,59]
 *			*msec	Milliseconds [0,1000.0)
 * Comment: This handles the YYYY/MM/DD HH:MM:SS by the standard library, and
 *			deals with the fractional seconds itself.  Times before 00:00
 *			01/01/1970 and after 03:14:07, 19/01/2038 cannot be represented
 *			at present, and will cause undefined operation.
 */

extern Bool stime_break_time_jday(f64 stamp, u32p year, u32p jday,
					  			  u32p hr, u32p min, u32p sec, f64p msec);

/* Routine:	stime_execute_params
 * Purpose:	Execute parameters list in this module
 * Inputs:	*list	List to parameters to work from
 * Outputs:	True if parameters used were OK, otherwise False
 * Comment:	Note that not finding a parameter is not a major problem; the only
 *			difficulty is if one is found, and it's wrong!
 */

#include "params.h"

extern Bool stime_execute_params(ParList *list);

/* Routine:	stime_translate_timestamp
 * Purpose:	Translate a human readable timestamp into uniform seconds
 * Inputs:	*string	String to work from
 * Outputs:	Time in seconds, or -1.0 if conversion cannot be made.
 * Comment:	This assumes that the timestamp is in the form:
 *				YYYY/MM/DD[:HH:MM:SS]
 *			(i.e., optional time section, assumed zero if not present).  As
 *			always, times are refered to GMT/UTC only.
 */

extern f64 stime_translate_timestamp(char *string);

/* Routine:	stime_timestamp
 * Purpose:	Generate a human-readable timestamp for the current instant
 * Inputs:	-
 * Outputs:	Pointer to internal static buffer that contains the timestamp string
 * Comment:	This simply captures the current time, and then formats into the
 *			output buffer.  Do not attempt to de-allocate the pointer returned,
 *			since it's a static buffer in this routine, rather than on the
 *			heap.  Contents of the buffer are guaranteed until the next call to
 *			this routine, but not thereafter.  This routine is not re-entrant.
 */

extern char *stime_timestamp(void);

#endif
