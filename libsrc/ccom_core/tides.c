/*
 * $Id: tides.c 13 2003-07-22 23:15:55Z brc $
 * $Log$
 * Revision 1.2  2003/07/22 23:13:46  brc
 * Merged modifications from development stream.
 *
 * Revision 1.6.2.2  2003/06/06 22:06:49  brc
 * Modified algorithm for generic ASCII read/write since they
 * depended on *nix extensions to the printf/scanf family that are
 * not ANSI and do not occur on the PC.  The resulting algorithm is
 * not as elegant, but it is portable.  Also added facility to have the
 * output routine write a file that is compatible with CARIS so that
 * you don't have to add headers and footers yourself.
 *
 * Revision 1.1.1.1  2003/02/03 20:18:44  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2.4.1  2003/01/28 14:29:35  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.6.2.1  2002/07/14 02:20:48  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.6  2001/09/27 22:36:15  brc
 * Added float.h for DBL_MAX under Linux.
 *
 * Revision 1.5  2001/09/23 19:22:54  brc
 * Added generic tide reader/writer module which can translate generic ASCII
 * descriptions of the information required to specify tide corrections into
 * and out of internal format.  Known components are year, month, day, hour,
 * minute, second and tide.  The code also supports Julian day as an alternative
 * to month/day, and will default years and seconds if they are not specified.
 * There are no checks on output data format.
 *   Added parameters for tide offsets in time and amplitude so that the tide
 * data can be corrected on the fly, either as it is loaded, or as it is added
 * to the Tide structure.  The offsets can be retrieved and reset on the fly,
 * and offsets can be applied _en masse_ with one call (e.g., to remove some
 * bad corrections before adding new ones).  Default parameters are 0.0s shift
 * and 1.0 scale.
 *
 * Revision 1.4  2001/08/21 01:28:07  brc
 * Added facility to read SRD Ltd. tide files (a fairly basic ASCII format which
 * could be used for generic data).  Also fixed bug which caused over-runs when
 * computing the time bounds of the tide set.
 *
 * Revision 1.3  2001/05/14 04:24:21  brc
 * Updated to make module 'params'-aware
 *
 * Revision 1.2  2001/02/26 02:37:12  brc
 * Fixed bug in buffer allocation code which resulted in too little base memory
 * being asigned.  This leads to delay-action heap bombs.
 *
 * Revision 1.1  2001/02/21 05:47:19  brc
 * Code to load and then interpolate tidal information files for reducing
 * soundings to datum.
 *
 *
 * File:	tides.c
 * Purpose:	Tide file loading and interpolation
 * Date:	20 February 2001
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <float.h>
#include "stdtypes.h"
#include "ccom_general.h"
#include "error.h"
#include "sounding.h"
#include "stime.h"
#include "tides.h"
#include "params.h"

#undef __DEBUG__

static char *modname = "tides";
static char *modrev = "$Revision: 13 $";

#define DEFAULT_BUFFER_QUANTUM	256
#define DEFAULT_EPOCH_SHIFT		0.0
#define DEFAULT_SCALE_FACTOR	1.0

static u32 buffer_quantum = DEFAULT_BUFFER_QUANTUM;
static f64 tide_shift = DEFAULT_EPOCH_SHIFT;
static f32 tide_scale = DEFAULT_SCALE_FACTOR;

typedef struct _tidept {
	f64	epoch;			/* Timestamp in sec. since 00:00:00 01/01/1970 */
	f32	tide;			/* Tide in meters, positive up */
} TidePt;

typedef struct _tide {
	f64	tide_shift;		/* Time shift to apply to data passed in */
	f32	tide_scale;		/* Scale factor to apply to tides */
	u32	n_points;		/* Number of points currently in the buffer */
	u32	n_buffer;		/* Number of slots currently in the buffer */
	TidePt	*lut;		/* Look-up table for tides */
} sTide /* *Tide */;

/* Routine:	tide_release
 * Purpose:	Release all memory associated with a Tide structure
 * Inputs:	*tide	Tide structure to release
 * Outputs:	-
 * Comment:	-
 */

void tide_release(Tide tide)
{
	free(tide->lut);
	free(tide);
}

static Tide tide_new_n(u32 n_points)
{
	Tide	rtn;
	
	if ((rtn = (Tide)calloc(1, sizeof(struct _tide))) == NULL)
		return(NULL);
	if ((rtn->lut = (TidePt*)malloc(sizeof(TidePt)*n_points)) == NULL) {
		free(rtn);
		return(NULL);
	}
	rtn->n_points = 0;
	rtn->n_buffer = n_points;
	rtn->tide_shift = tide_shift;
	rtn->tide_scale = tide_scale;
	return(rtn);
}

/* Routine:	tide_new
 * Purpose:	Construct a new blank Tide structure
 * Inputs:	-
 * Outputs:	Pointer to Tide structure, or NULL on failure
 * Comment:	The Tide structure returned is valid for use in the routines in this
 *			file, but contains no data.  Data can be added using tide_insert().
 */

Tide tide_new(void)
{
	Tide	rtn;
	
	if ((rtn = tide_new_n(buffer_quantum)) == NULL) {
		error_msg(modname, "failed allocating default quantum buffer.\n");
		return(NULL);
	}
	return(rtn);
}

/* Routine:	tide_insert
 * Purpose:	Insert data into the Tide interpolation table
 * Inputs:	tide		Pointer to the Tide structure to use
 *			timestamp	Time associated with the tide information (see comment)
 *			mtide		Tide offset in meters
 * Outputs:	True on success, otherwise False
 * Comment:	This expects the timestamp used to correspond to seconds since epoch
 *			(00:00:00 01/01/1970, UTC).  The field is sufficient to give better
 *			than ms accuracy over the entire range of values expected.  The
 *			tide is positive up.  This routine can fail if it runs out of
 *			memory in the extension process.  We assume that the tides are
 *			inserted in time sequential order (i.e., time increases
 *			monotonically), so that we don't have to keep sorting the table.
 */

Bool tide_insert(Tide tide, f64 epoch, f32 mtide)
{
	u32		target;
	TidePt	*tmp;
	
	if (tide->n_points >= tide->n_buffer) {
		/* Need to reallocate more space for buffer */
		target = (tide->n_buffer + buffer_quantum)*sizeof(TidePt);
		if ((tmp = (TidePt*)realloc(tide->lut, target)) == NULL) {
			error_msgv(modname, "failed to increase tide buffer by %d"
				" points from %d.\n", buffer_quantum, tide->n_points);
			return(False);
		}
		tide->lut = tmp;
		tide->n_buffer += buffer_quantum;
	}
	tide->lut[tide->n_points].epoch = epoch + tide->tide_shift;
	tide->lut[tide->n_points].tide = mtide * tide->tide_scale;
	++tide->n_points;
	return(True);
}

/* Routine:	tide_new_from_omg
 * Purpose:	Reads OMG formated Tide files into a new structure
 * Inputs:	*name	Name of the OMG Tide file to load
 *			year	Y2K compliant year for the tide file (i.e., YYYY)
 * Outputs:	Pointer to Tide file, or NULL on failure
 * Comment:	This can fail if there are problems with memory, or if the file
 *			read fails.  The data format here is <jd><hr><min><tide>, ASCII.
 */

Tide tide_new_from_omg(char *name, u32 year)
{
	FILE	*ip;
	u32		day, hour, min, tide_point;
	f32		mtide;
	f64		epoch;
	Tide	rtn;
	
	if ((rtn = tide_new()) == NULL) {
		error_msg(modname, "failed to construct Tide structure for OMG input.\n");
		return(NULL);
	}
	if ((ip = fopen(name, "r")) == NULL) {
		error_msgv(modname, "failed to open \"%s\" for tide input.\n",
			name);
		tide_release(rtn);
		return(NULL);
	}
	tide_point = 0;
	while (!feof(ip)) {
		if (fscanf(ip, "%d %d %d %f\n", &day, &hour, &min, &mtide) != 4) {
			error_msgv(modname, "failed reading point %d from \"%s\" for tide"
				" input.\n", tide_point, name);
			error_msgv(modname, "warning: tide point %d ignored.\n",
				tide_point);
			while (!feof(ip) && fgetc(ip) != '\n') ; /* Clean Line */
			continue;
		}
		epoch = stime_make_time_jday(year, day, hour, min, 0, 0.0);
		if (!tide_insert(rtn, epoch, mtide)) {
			error_msgv(modname, "failed inserting point %d (t=%g s).\n",
				tide_point, epoch);
			tide_release(rtn);
			return(NULL);
		}
		ungetc(fgetc(ip), ip);
		++tide_point;
	}
	fclose(ip);
	return(rtn);
}

/* Routine:	tide_new_from_srd
 * Purpose:	Read an SRD tide guage file for tides
 * Inputs:	*name	Name of the file to read
 * Outputs:	Pointer to Tide structure with data, or NULL on failure
 * Comment:	This reads a file of the format:
 *				DD/MM/YYYY HH:MM DEPTH
 *			and parses it into the internal format.  Any lines which do not
 *			correspond to this format are ignored.  Of course, although this is
 *			the format for SRD tide guages, it is not impossible to put other
 *			data into this format ...  Note that this assumes that the tide
 *			information was recorded to the same time base as the other data
 *			being used.
 */

Tide tide_new_from_srd(char *name)
{
	FILE	*ip;
	u32		year, month, day, hour, min, tide_point;
	f32		mtide;
	f64		epoch;
	Tide	rtn;
	
	if ((rtn = tide_new()) == NULL) {
		error_msg(modname, "failed to construct Tide structure for OMG input.\n");
		return(NULL);
	}
	if ((ip = fopen(name, "r")) == NULL) {
		error_msgv(modname, "failed to open \"%s\" for tide input.\n",
			name);
		tide_release(rtn);
		return(NULL);
	}
	tide_point = 0;
	while (!feof(ip)) {
		if (fscanf(ip, "%d/%d/%d %d:%d %f\n", &day, &month, &year, &hour, &min,
																&mtide) != 6) {
			error_msgv(modname, "failed reading point %d from \"%s\" for tide"
				" input.\n", tide_point, name);
			error_msgv(modname, "warning: tide point %d ignored.\n",
				tide_point);
			while (!feof(ip) && fgetc(ip) != '\n') ; /* Clean Line */
			continue;
		}
		epoch = stime_make_time(year, month, day, hour, min, 0, 0.0);
		if (!tide_insert(rtn, epoch, mtide)) {
			error_msgv(modname, "failed inserting point %d (t=%g s).\n",
				tide_point, epoch);
			tide_release(rtn);
			return(NULL);
		}
		ungetc(fgetc(ip), ip);
		++tide_point;
	}
	fclose(ip);
	return(rtn);
}

/* Routine:	tide_new_from_ascii
 * Purpose:	Read a generic ASCII tide file given format information
 * Inputs:	*name	Name of the file to read from
 *			*fmt	Tide format to read (see comment)
 *			defyr	Default year to use (if not in format)
 * Outputs:	Returns pointer to the Tide structure holding the information, or
 *			NULL on failure.
 * Comment:	This reads a fairly generic format of tide files, assuming that they
 *			are specified in ASCII format.  The format string lists the
 *			components of the tide specification in the order in which they
 *			appear in the file, with '*' to indicate 'a column I don't care
 *			about'.  The recognition strings are:
 *				Y	Year (may be 2-digit or 4-digit; Y2K is handled by mapping
 *					years > 60 -> 1900+year, and years <= 60 -> 2000+year).
 *				M	Month, range 01-12
 *				D	Range 01-31, as appropriate for the month
 *				J	Julian day 001-366 (mutually exclusive with M, D)
 *				h	Hours 00-23
 *				m	Minutes 00-59
 *				T	Tide, m.
 *			Anything else that appears is required to appear directly in the
 *			input line; any lines which don't match the format are ignored.
 */

typedef enum {
	TIDE_ASCII_TIDE	=	1,
	TIDE_ASCII_MIN	=	2,
	TIDE_ASCII_HR	=	4,
	TIDE_ASCII_DAY	=	8,
	TIDE_ASCII_MNTH	=  16,
	TIDE_ASCII_SET	=  31,	/* Value required for all obligatory components */
	TIDE_ASCII_SEC	=  32,
	TIDE_ASCII_YEAR =  64,
	TIDE_ASCII_JDAY = 128
} TideASCIIEnum;

Tide tide_new_from_ascii(char *name, char *fmt, u32 defyr)
{
	Tide	rtn;
	u32		tide_point, line, pos, converts, minimum;
	u32		year, month, day, hour, min, sec, jday;
	s32		found;
	f32		tide;
	f64		stamp;
	char	format[512], c;
	char	buffer[512];
	FILE	*ip;
	void	*ptr[8];
	
	
	pos = 0; converts = 0; minimum = 0;
	while ((c = *fmt++) != '\0') {
		switch(c) {
			case 'Y':
				pos += sprintf(format+pos, "%%d");
				ptr[converts++] = (void*)&year;
				if ((minimum & TIDE_ASCII_YEAR) != 0) {
					error_msgv(modname, "error: cannot specify more than one"
						" year column in the same string.\n");
					return(NULL);
				}
				minimum |= TIDE_ASCII_YEAR;
				break;
			case 'J':
				pos += sprintf(format+pos, "%%d");
				ptr[converts++] = (void*)&jday;
				if ((minimum & TIDE_ASCII_JDAY) != 0) {
					error_msgv(modname, "error: cannot specify more than one"
						" Julian day column in the same string.\n");
					return(NULL);
				}
				if ((minimum & TIDE_ASCII_MNTH) != 0 ||
					(minimum & TIDE_ASCII_DAY) != 0) {
					error_msgv(modname, "error: cannot specify a Julian day"
						" and a month/day combination in the same string.\n");
					return(NULL);
				}
				minimum |= TIDE_ASCII_JDAY;
				minimum |= TIDE_ASCII_MNTH;
				minimum |= TIDE_ASCII_DAY;
				break;
			case 'M':
				pos += sprintf(format+pos, "%%d");
				ptr[converts++] = (void*)&month;
				if ((minimum & TIDE_ASCII_JDAY) != 0) {
					error_msgv(modname, "error: cannot specify a Julian day"
						" and a month column in the same string.\n");
					return(NULL);
				}
				if ((minimum & TIDE_ASCII_MNTH) != 0) {
					error_msgv(modname, "error: cannot specify more than one"
						" month column in the same string.\n");
					return(NULL);
				}
				minimum |= TIDE_ASCII_MNTH;
				break;
			case 'D':
				pos += sprintf(format+pos, "%%d");
				ptr[converts++] = (void*)&day;
				if ((minimum & TIDE_ASCII_JDAY) != 0) {
					error_msgv(modname, "error: cannot specify a Julian day"
						" and a day column in the same string.\n");
					return(NULL);
				}
				if ((minimum & TIDE_ASCII_DAY) != 0) {
					error_msgv(modname, "error: cannot specify  more than one"
						" day column in the same string.\n");
					return(NULL);
				}
				minimum |= TIDE_ASCII_DAY;
				break;
			case 'h':
				pos += sprintf(format+pos, "%%d");
				ptr[converts++] = (void*)&hour;
				if ((minimum & TIDE_ASCII_HR) != 0) {
					error_msgv(modname, "error: cannot specify more than one"
						" hour column in one string.\n");
					return(NULL);
				}
				minimum |= TIDE_ASCII_HR;
				break;
			case 'm':
				pos += sprintf(format+pos, "%%d");
				ptr[converts++] = (void*)&min;
				if ((minimum & TIDE_ASCII_MIN) != 0) {
					error_msgv(modname, "error: cannot specify more than one"
						" minute column in one string.\n");
					return(NULL);
				}
				minimum |= TIDE_ASCII_MIN;
				break;
			case 's':
				pos += sprintf(format+pos, "%%d");
				ptr[converts++] = (void*)&sec;
				if ((minimum & TIDE_ASCII_SEC) != 0) {
					error_msgv(modname, "error: cannot specify more than one"
						" seconds column in one string.\n");
					return(NULL);
				}
				minimum |= TIDE_ASCII_SEC;
				break;
			case 'T':
				pos += sprintf(format+pos, "%%f");
				ptr[converts++] = (void*)&tide;
				if ((minimum & TIDE_ASCII_TIDE) != 0) {
					error_msgv(modname, "error: cannot specify more than one"
						" tide column in one string.\n");
					return(NULL);
				}
				minimum |= TIDE_ASCII_TIDE;
				break;
			case '*':
				pos += sprintf(format+pos, "%%*s");
				break;
			default:
				format[pos++] = c;
				break;
		}
	}
	format[pos++] = '\0';
	error_msgv(modname, "debug: tide format string is \"%s\".\n",
		format);
	if ((minimum & TIDE_ASCII_SET) != TIDE_ASCII_SET) {
		error_msgv(modname, "error: not enough format parameters set to"
			" correctly parse a tide (need at least month, day, hour, minutes"
			" and tide (or Julian day instead of month/day).\n");
		return(NULL);
	}
	if ((rtn = tide_new()) == NULL) {
		error_msgv(modname, "error: no memory to create blank tide object.\n");
		return(NULL);
	}
	if ((ip = fopen(name, "r")) == NULL) {
		error_msgv(modname, "error: failed opening \"%s\" for input.\n",
			name);
		tide_release(rtn);
		return(NULL);
	}
	line = 0; tide_point = 0;
	while (fgets(buffer, 512, ip) != NULL) {
		year = defyr;
		month = day = hour = min = sec = 0;
		tide = 0.f;
		if ((found = sscanf(buffer, format, ptr[0], ptr[1], ptr[2], ptr[3], ptr[4], ptr[5],
						ptr[6], ptr[7])) == (s32)converts) {
			if (year < 100) {
				/* Correct Y2K problem by splitting at 1970 */
				if (year > 70) year += 1900;
				else year += 2000;
			}
			if ((minimum & TIDE_ASCII_JDAY) != 0)
				stamp = stime_make_time_jday(year, jday, hour, min, sec, 0.0);
			else
				stamp = stime_make_time(year, month, day, hour, min, sec, 0.0);
			if (!tide_insert(rtn, stamp, tide)) {
				error_msgv(modname, "error: failed to insert point %d from"
					" \"%s\" line %d using format \"%s\".\n",
					tide_point, name, line, format);
				tide_release(rtn);
				return(NULL);
			}
			++tide_point;
		}
		++line;		
	}
	fclose(ip);
	error_msgv(modname, "debug: converted %d points from %d lines of \"%s\".\n",
		tide_point, line, name);
	return(rtn);
}

/* Routine:	tide_dump_data
 * Purpose:	Write Tide structure to ASCII output with specified format
 * Inputs:	tide	Tide structure to dump
 *			*name	Name of the file to create
 *			*fmt	Format to write in (see comment)
 * Outputs:	True if file was written correctly, otherwise False
 * Comment:	The format used here is the same as that used in tide_new_from_ascii
 *			except that duff columns ('*') are not allowed.  The format is
 *			translated into an fprintf() format string, and then used to write
 *			all of the data in the Tide structure.  No checks are done for using
 *			the same element more than once, or for using, for example, both
 *			Julian day and month/day.  Hey, it's your output, right?
 */

typedef union {
	f32	f_data;
	u32	i_data;
} Bag;

typedef enum {
	T_OP_CATCHALL = 0,
	T_OP_YEAR,
	T_OP_MONTH,
	T_OP_DAY,
	T_OP_JDAY,
	T_OP_HOUR,
	T_OP_MIN,
	T_OP_SEC,
	T_OP_TIDE
} Op_Positions;

Bool tide_dump_data(Tide tide, char *name, char *fmt, Bool for_caris)
{
	u32		pos, pt, elem;
	char	format[512], c;
	u32		year, month, day, hour, min, sec, jday;
	s32		index[9];
	f64		msec;
	FILE	*op;
	f32		data[9];
	
	pos = 0; elem = 1;
	memset(index, T_OP_CATCHALL, sizeof(s32)*8);
	while ((c = *fmt++) != '\0') {
		switch(c) {
			case 'Y':
				pos += sprintf(format+pos, "%%04.0f");
				index[T_OP_YEAR] = elem++;
				break;
			case 'M':
				pos += sprintf(format+pos, "%%02.0f");
				index[T_OP_MONTH] = elem++;
				break;
			case 'D':
				pos += sprintf(format+pos, "%%02.0f");
				index[T_OP_DAY] = elem++;
				break;
			case 'J':
				pos += sprintf(format+pos, "%%03.0f");
				index[T_OP_JDAY] = elem++;
				break;
			case 'h':
				pos += sprintf(format+pos, "%%02.0f");
				index[T_OP_HOUR] = elem++;
				break;
			case 'm':
				pos += sprintf(format+pos, "%%02.0f");
				index[T_OP_MIN] = elem++;
				break;
			case 's':
				pos += sprintf(format+pos, "%%02.0f");
				index[T_OP_SEC] = elem++;
				break;
			case 'T':
				pos += sprintf(format+pos, "%%.3f");
				index[T_OP_TIDE] = elem++;
				break;
			default:
				format[pos++] = c;
				break;
		}
	}
	strcat(format, "\n");
	if ((op = fopen(name, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			name);
		return(False);
	}
	if (for_caris) fprintf(op, "---------------\n");
	for (pt = 0; pt < tide->n_points; ++pt) {
		stime_break_time(tide->lut[pt].epoch, &year, &month, &day, &hour, &min, &sec, &msec);
		stime_break_time_jday(tide->lut[pt].epoch, &year, &jday, &hour, &min, &sec, &msec);
		data[index[T_OP_YEAR]] = (f32)year;
		data[index[T_OP_MONTH]] = (f32)month;
		data[index[T_OP_DAY]] = (f32)day;
		data[index[T_OP_HOUR]] = (f32)hour;
		data[index[T_OP_MIN]] = (f32)min;
		data[index[T_OP_SEC]] = (f32)sec;
		data[index[T_OP_JDAY]] = (f32)jday;
		data[index[T_OP_TIDE]] = tide->lut[pt].tide;
		fprintf(op, format, data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8]);
	}
	if (for_caris) fprintf(op, "---------------\n");
	fclose(op);
	return(True);
}

/* Routine:	tide_set_offset
 * Purpose:	Set offset components on a Tide structure
 * Inputs:	tide	Tide structure to work on
 *			shift	Time shift to insert (see comment for phase)
 *			scale	Scale factor to insert (0.0,1.0]
 * Outputs:	True if offsets were set sucessfully, otherwise False
 * Comment:	This adds offsets into the Tide structure, so that all future data
 *			which is being inserted is corrected for phase/amplitude shifts
 *			from measured/predicted to worksite tides.  The time shift is set
 *			so that positive shifts result in a tide at the worksite which
 *			lags the input tides (i.e., peak occurs after that in the input).
 */

Bool tide_set_offset(Tide tide, f64 shift, f32 scale)
{
	if (scale <= 0.0f || scale > 1.f) {
		error_msgv(modname, "error: tide scale factor must be in range"
			" (0.0,1.0] (not %f).\n", scale);
		return(False);
	}
	tide->tide_shift = shift;
	tide->tide_scale = scale;
	return(True);
}

/* Routine: tide_get_offset
 * Purpose:	Get offsets currently being used in Tide structure
 * Inputs:	tide	Tide structure to work with
 * Outputs:	*shift	Time shift currently being applied
 *			*scale	Scale factor currently being applied
 * Comment:	Scales and shifts as specified in tide_set_offset().
 */

void tide_get_offset(Tide tide, f64p shift, f32p scale)
{
	*shift = tide->tide_shift;
	*scale = tide->tide_scale;
}

/* Routine:	tide_apply_offset
 * Purpose:	Apply time and amplitude offset to a set of tide readings
 * Inputs:	tide	Tide structure to operate on
 *			t_delta	Time delta in seconds to apply
 *			a_scale	Amplitude scale (fractional) to apply
 * Outputs:	-
 * Comment:	This offsets the timestamps of the values in the Tide structure,
 *			and scales the amplitudes as specified.  Note that this adds the
 *			offset and scale *in addition* to any which are already being
 *			applied through the parameters in the Tide structure (see
 *			tide_set_offset() and tide_get_offset()).  Unless you have a very
 *			good reason for doing this, you should be using them.
 */

void tide_apply_offset(Tide tide, f32 t_delta, f32 a_scale)
{
	u32	pt;
	
	for (pt = 0; pt < tide->n_points; ++pt) {
		tide->lut[pt].epoch += t_delta;
		tide->lut[pt].tide *= a_scale;
	}
}

/* Routine:	tide_interp
 * Purpose:	Interpolate a tide to specific time
 * Inputs:	tide	Tide structure to use
 *			epoch	Time at which to interpolate
 * Outputs:	*mtide	Tide computed as interpolant.
 *			True on success, otherwise False
 * Comment:	This routine uses simple linear interpolation between closest
 *			neighbours to interpolate the tides.  It can fail if the timestamp
 *			is not within the bounds of the table (e.g., if the table is still
 *			being built, or is incomplete from file, or if the timestamps are
 *			not on the same scale, etc.)
 */

Bool tide_interp(Tide tide, f64 epoch, f32 *mtide)
{
	u32	left, right, mid;
	f64	scale_left, scale_right;
	
	if (tide->n_points < 2) {
		error_msgv(modname, "can't interpolate with %d points.\n",
			tide->n_points);
		return(False);
	}
	if (epoch < tide->lut[0].epoch || epoch > tide->lut[tide->n_points-1].epoch) {
		error_msgv(modname, "time %lf s is outside tide epoch range %lf--%lf s.\n",
			epoch, tide->lut[0].epoch, tide->lut[tide->n_points-1].epoch);
		return(False);
	}
	left = 0; right = tide->n_points-2;
	while (right-left > 1) {
		mid = (left+right)/2;
		if (tide->lut[mid].epoch <= epoch) {
			/* Keep the right hand half */
			left = mid;
		} else {
			/* Keep the left hand half */
			right = mid;
		}
	}
	
	/* Now we build the linear interpolation scales, and interpolate */
	right = left + 1; /* We know this is in bounds because of initial check */
	scale_left = (tide->lut[right].epoch - epoch)/
				 (tide->lut[right].epoch - tide->lut[left].epoch);
	scale_right = (epoch - tide->lut[left].epoch)/
				  (tide->lut[right].epoch - tide->lut[left].epoch);
	
	*mtide = (f32)(scale_left * tide->lut[left].tide +
			 scale_right * tide->lut[right].tide);
	
	return(True);
}

/* Routine:	tide_soundings
 * Purpose:	Utility function to add tides to a set of soundings
 * Inputs:	tide		Tide structure to use for interpolation
 *			timestamp	Time associated with the ping
 *			*snd		Pointer to Sounding structures to work on
 *			nsnd		Number of soundings in the array
 * Outputs:	True on success, otherwise False
 * Comment:	This is essentially a utility function to avoid having to compute
 *			tide interpolations for each sounding: this computes the tide once
 *			and uses it to adjust all soundings in a sequence.  Since the tide
 *			is a *very* low frequency event, there is little lost in using the
 *			same tide for all of the soundings, and it's much faster.  Note
 *			that we assume that the soundings are given with depths -ve, so that
 *			to correct for positive up tides, we *add* the tide offset.
 */

Bool tide_soundings(Tide tide, f64 epoch, Sounding *snd, u32 nsnds)
{
	u32	beam;
	f32	mtide;
	
	if (!tide_interp(tide, epoch, &mtide)) return(False);
	for (beam = 0; beam < nsnds; ++beam) {
		snd[beam].depth += mtide;
	}
	return(True);
}

/* Routine:	tide_get_bound_times
 * Purpose:	Return bounding interpolation times from the Tide structure
 * Inputs:	tide	Tide structure to work from
 * Outputs:	*start, *end	Set to start and end times.
 * Comment:	-
 */

void tide_get_bound_times(Tide tide, f64p start, f64p end)
{
	if (tide->n_points == 0) {
		error_msg(modname, "warning: tide table has no points!\n");
		*start = *end = 0.0;
	} else {
		*start = tide->lut[0].epoch;
		*end = tide->lut[tide->n_points-1].epoch;
	}
}

/* Routine:	tide_get_point_n
 * Purpose:	Return the nth point in the table
 * Inputs:	tide	Tide structure to use
 *			n		Point to extract
 * Outputs:	*epoch	Epoch timestamp associated with the point
 *			*mtide	Measured tide with the point
 *			Returns True if point was found, otherwise False (usu. bounds).
 * Comment:	-
 */

Bool tide_get_point_n(Tide tide, u32 n, f64p epoch, f32p mtide)
{
	if (n >= tide->n_points) {
		error_msgv(modname, "warning: requested point %d and table only has"
			" %d points.\n", n, tide->n_points);
		return(False);
	}
	*epoch = tide->lut[n].epoch;
	*mtide = tide->lut[n].tide;
	return(True);
}

/* Routine:	tide_get_npoints
 * Purpose:	Return number of points currently defined in the table
 * Inputs:	tide	Tide structure to use
 * Outputs:	Returns number of points currently defined
 * Comment:	-
 */

u32 tide_get_npoints(Tide tide)
{
	return(tide->n_points);
}

/* Routine:	tide_execute_params
 * Purpose:	Execute parameters list in this module
 * Inputs:	*list	List to parameters to work from
 * Outputs:	True if parameters used were OK, otherwise False
 * Comment:	Note that not finding a parameter is not a major problem; the only
 *			difficulty is if one is found, and it's wrong!
 */

typedef enum {
	TIDE_UNKNOWN = 0,
	TIDE_BUFFER_QUANTUM,
	TIDE_TIME_OFFSET,
	TIDE_SCALE_FACTOR
} TideParamEnum;

Bool tide_execute_params(ParList *list)
{
	ParTable	tab[] = {
		{ "buffer_quantum",	TIDE_BUFFER_QUANTUM },
		{ "time_offset",	TIDE_TIME_OFFSET },
		{ "scale_factor",	TIDE_SCALE_FACTOR },
		{ NULL,				TIDE_UNKNOWN }
	};
	ParList	*node, *match;
	u32		id;
	s32		quantum;
	f64		time_delta;
	f32		scale;
	
	node = list;
	do {
		node = params_match(node, "tides", tab, &id, &match);
		switch (id) {
			case TIDE_UNKNOWN:
				/* Matched nothing ... but that may just mean that there's
				 * nothing there for this module.
				 */
				break;
			case TIDE_BUFFER_QUANTUM:
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
			case TIDE_TIME_OFFSET:
				time_delta = params_translate_duration(match->data);
				if (time_delta == DBL_MAX) {
					error_msgv(modname, "error: cannot translate \"%s\" as"
						" a time duration.\n", match->data);
					return(False);
				}
				match->used = True;
				tide_shift = time_delta;
				break;
			case TIDE_SCALE_FACTOR:
				scale = (f32)atof(match->data);
				if (scale <= 0.f || scale > 1.f) {
					error_msgv(modname, "error: scale factor out of range"
						"(0.0,1.0].\n");
					return(False);
				}
				tide_scale = scale;
				match->used = True;
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
