/*
 * $Id: stime.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:35  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.7  2002/03/14 04:05:08  brc
 * Added stime_timestamp() to generate a reasonably formatted timestamp for
 * the current time.
 *
 * Revision 1.6  2001/10/15 23:10:23  brc
 * Cosmetic modification of inter-line spacing.
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
 * Revision 1.2  2000/12/03 20:52:49  brc
 * Added stime_break_time() to break a uniform time into components for human
 * readable reporting.
 *
 * Revision 1.1  2000/11/14 22:43:00  brc
 * Sub-project to encapsulate manipulation of times, and in particular conversion
 * of times from broken out form to uniform seconds.  This also has a simple
 * interpolation scheme so that systems with dual timebases can be manipulated.
 *
 *
 * File:	stime.c
 * Purpose:	Survey Time Tools (conversion, interpolation, etc.)
 * Date:	14 November 2000
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
#include <time.h>
#include <limits.h>
#include <math.h>
#include "stdtypes.h"
#include "stime.h"
#include "error.h"
#include "params.h"

#undef __DEBUG__

static char *modname = "stime";
static char *modrev = "$Revision: 2 $";

#define DEFAULT_BUFFER_QUANTUM 1024

static u32 buffer_quantum = DEFAULT_BUFFER_QUANTUM;

typedef struct _stimept {
	f64	key;
	f64	target;
} STimePt;

typedef struct _stime {
	u32		n_points;
	u32		n_buffer;
	STimePt	*lut;
} STimeStruct;

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

f64 stime_make_time(u32 year, u32 month, u32 day, u32 hr, u32 min, u32 sec,
					f64 msec)
{
	struct tm	stamp;
	time_t		t;
	
#ifdef __DEBUG__
	if (year < 1900 || year > 2037 ||
		month < 1 || month > 12 ||
		day < 1 || day > 31 ||
		hr > 23 ||
		min > 59 ||
		sec > 59 ||
		msec < 0 || msec > 1000.0) {
		error_msg(modname, "warning: time elements out of range.\n");
	}
#endif
	putenv("TZ=GMT");	/* Force conversions in UTC/GMT */
	tzset();
	memset(&stamp, 0, sizeof(struct tm));
	stamp.tm_sec = sec;
	stamp.tm_min = min;
	stamp.tm_hour = hr;
	stamp.tm_mday = day;
	stamp.tm_mon = month - 1;
	stamp.tm_year = year - 1900;
	t = mktime(&stamp);
	return(t + msec/1000.0);
}

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

f64 stime_make_time_jday(u32 year, u32 jday, u32 hr, u32 min, u32 sec, f64 msec)
{
	u32	leap, month;
	static u32 months[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
	
	/* Determine whether this is a leap year.  Leap years in the Gregorian
	 * calendar are years divisible by four, unless they are also a century
	 * year, except when the century is also divisible by 400.  Thus 1900 is
	 * not a leap year (although it is divisible by 4), but 2000 is, because
	 * it is divisible by 4 and 400).
	 */
	if ((year%4) == 0) {
		/* Potentially a leap year, check for century year */
		if ((year%100) == 0) {
			/* Century year, check for 400 years */
			if ((year%400) == 0)
				leap = 1;
			else
				leap = 0;
		} else
			leap = 1;
	} else
		leap = 0;
	
	months[1] += leap;	/* Correct February */
	
	/* Compute month by reducing days until we have less than the next months
	 * total number of days.
	 */
	month = 0;
	while (jday > months[month]) {
		jday -= months[month];
		++month;
	}
	
	months[1] -= leap;	/* Uncorrect February */
	
	return(stime_make_time(year, month+1, jday, hr, min, sec, msec));
}

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

Bool stime_break_time(f64 stamp, u32p year, u32p month, u32p day,
					  u32p hr, u32p min, u32p sec, f64p msec)
{
	struct tm	*utc_time;
	time_t		unix_time = (time_t)floor(stamp);
	
	if (stamp < 0.0 || stamp > INT_MAX) {
		error_msgv(modname, "timestamp %lf is outwith representable range.\n",
			stamp);
		return(False);
	}

#ifdef __DEBUG__	
	error_msgv(modname, "debug: stamp = %lf unix_time = %d\n", stamp, unix_time);
#endif

	*msec = (stamp - unix_time)*1000.0;
	
	putenv("TZ=GMT");	/* Just in case */
	tzset();
	utc_time = gmtime(&unix_time);
	*year = utc_time->tm_year + 1900;
	*month = utc_time->tm_mon + 1;
	*day = utc_time->tm_mday;
	*hr = utc_time->tm_hour;
	*min = utc_time->tm_min;
	*sec = utc_time->tm_sec;
	return(True);
}

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

Bool stime_break_time_jday(f64 stamp, u32p year, u32p jday,
					  u32p hr, u32p min, u32p sec, f64p msec)
{
	struct tm	*utc_time;
	time_t		unix_time = (time_t)floor(stamp);
	
	if (stamp < 0.0 || stamp > INT_MAX) {
		error_msgv(modname, "timestamp %lf is outwith representable range.\n",
			stamp);
		return(False);
	}

#ifdef __DEBUG__	
	error_msgv(modname, "debug: stamp = %lf unix_time = %d\n", stamp, unix_time);
#endif

	*msec = (stamp - unix_time)*1000.0;
	
	putenv("TZ=GMT");	/* Just in case */
	tzset();
	utc_time = gmtime(&unix_time);
	*year = utc_time->tm_year + 1900;
	*jday = utc_time->tm_yday + 1;
	*hr = utc_time->tm_hour;
	*min = utc_time->tm_min;
	*sec = utc_time->tm_sec;
	return(True);
}

void stime_release(STime lut)
{
	free(lut->lut);
	free(lut);
}

static STime stime_new_n(u32 n_points)
{
	STime	rtn;
	
	if ((rtn = (STime)calloc(1, sizeof(STime))) == NULL)
		return(NULL);
	if ((rtn->lut = (STimePt*)malloc(sizeof(STimePt)*n_points)) == NULL) {
		free(rtn);
		return(NULL);
	}
	rtn->n_points = 0;
	rtn->n_buffer = n_points;
	return(rtn);
}

STime stime_new(void)
{
	STime	rtn;
	
	if ((rtn = stime_new_n(buffer_quantum)) == NULL) {
		error_msg(modname, "failed allocating default stime buffer.\n");
		return(NULL);
	}
	return(rtn);
}

Bool stime_insert(STime lut, f64 key, f64 target)
{
	u32		t_bytes;
	STimePt	*tmp;
	
	if (lut->n_points >= lut->n_buffer) {
		/* Need to reallocate buffer for more space */
		t_bytes = (lut->n_buffer + buffer_quantum)*sizeof(STimePt);
		if ((tmp =(STimePt*)realloc(lut->lut, t_bytes)) == NULL) {
			error_msgv(modname, "failed to increase stime interpolation"
				" buffer by %d elements from %d.\n", buffer_quantum,
				lut->n_buffer);
			return(False);
		}
		lut->lut = tmp;
		lut->n_buffer += buffer_quantum;
	}	
	lut->lut[lut->n_points].key = key;
	lut->lut[lut->n_points].target = target;
	++lut->n_points;
#ifdef __DEBUG__
	error_msgv(modname, "debug: now %d points (last key %lf target %lf).\n",
		lut->n_points, key, target);
#endif
	return(True);
}

Bool stime_interp(STime lut, f64 key, f64p result)
{
	u32	left, right, mid;
	f64	scale_left, scale_right;

	if (lut->n_points < 2) {
		error_msgv(modname, "can't interpolate with %d points.\n", lut->n_points);
		return(False);
	}
	if (key < lut->lut[0].key || key > lut->lut[lut->n_points-1].key) {
		error_msgv(modname, "time %lf s is outside lut range %lf--%lf s.\n",
			key, lut->lut[0].key, lut->lut[lut->n_points-1].key);
		return(False);
	}
	left = 0; right = lut->n_points-2;
	while (right-left > 1) {
		mid = (left + right)/2;
		if (lut->lut[mid].key <= key) {
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
	scale_left = (lut->lut[right].key - key)/
				(lut->lut[right].key - lut->lut[left].key);
	scale_right = (key - lut->lut[left].key)/
				(lut->lut[right].key - lut->lut[left].key);
	*result = scale_left*lut->lut[left].target +
		 	scale_right*lut->lut[right].target;

	return(True);
}

void stime_get_bound_keys(STime lut, f64p start, f64p end)
{
	if (lut->n_points == 0) {
		*start = 0.0;
		*end = 0.0;
	} else {
		*start = lut->lut[0].key;
		*end = lut->lut[lut->n_points-1].key;
	}
}

Bool stime_get_point_n(STime lut, u32 n, f64p key, f64p target)
{
	if (n >= lut->n_points) {
		error_msgv(modname, "point %d requested; only %d available.\n",
					n, lut->n_points);
		return(False);
	}
	*key = lut->lut[n].key;
	*target = lut->lut[n].target;
	return(True);
}

u32 stime_get_npoints(STime lut)
{
	return(lut->n_points);
}

/* Routine:	stime_execute_params
 * Purpose:	Execute parameters list in this module
 * Inputs:	*list	List to parameters to work from
 * Outputs:	True if parameters used were OK, otherwise False
 * Comment:	Note that not finding a parameter is not a major problem; the only
 *			difficulty is if one is found, and it's wrong!
 */

Bool stime_execute_params(ParList *list)
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
		node = params_match(node, "stime", tab, &id, &match);
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

/* Routine:	stime_translate_timestamp
 * Purpose:	Translate a human readable timestamp into uniform seconds
 * Inputs:	*string	String to work from
 * Outputs:	Time in seconds, or -1.0 if conversion cannot be made.
 * Comment:	This assumes that the timestamp is in the form:
 *				YYYY/MM/DD[:HH:MM:SS]
 *			(i.e., optional time section, assumed zero if not present).  As
 *			always, times are refered to GMT/UTC only.
 */

f64 stime_translate_timestamp(char *string)
{
	u32	year, month, day, hour, min, sec;
	
	if (strchr(string, ':') == NULL) {
		/* i.e., just date */
		hour = min = sec = 0;
		if (sscanf(string, "%d/%d/%d", &year, &month, &day) != 3) {
			error_msgv(modname, "failed converting \"%s\" to date.\n", string);
			return(-1.0);
		}
	} else {
		if (sscanf(string, "%d/%d/%d:%d:%d:%d", &year, &month, &day,
				&hour, &min, &sec) != 6) {
			error_msgv(modname, "failed converting \"%s\" to date/time.\n",
				string);
			return(-1.0);
		}
	}
	if (year < 1900 || year > 2037 ||
		month < 1 || month > 12 ||
		day < 1 || day > 31 ||
		hour > 23 ||
		min > 59 ||
		sec > 59) {
		error_msgv(modname, "elements are outwith representable range in "
			" \"%s\".\n", string);
		return(-1.0);
	}
	return(stime_make_time(year, month, day, hour, min, sec, 0.0));
}

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

char *stime_timestamp(void)
{
	static char	buffer[256];
	time_t	stamp;
	
	time(&stamp);
	strftime(buffer, 256, "%H:%M:%S %d %b %Y", gmtime(&stamp));
	return(buffer);
}
