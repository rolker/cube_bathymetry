/*
 * $Id: tides.h 13 2003-07-22 23:15:55Z brc $
 * $Log$
 * Revision 1.2  2003/07/22 23:15:55  brc
 * Merged modifications from development stream.
 *
 * Revision 1.4.2.2  2003/06/06 22:06:49  brc
 * Modified algorithm for generic ASCII read/write since they
 * depended on *nix extensions to the printf/scanf family that are
 * not ANSI and do not occur on the PC.  The resulting algorithm is
 * not as elegant, but it is portable.  Also added facility to have the
 * output routine write a file that is compatible with CARIS so that
 * you don't have to add headers and footers yourself.
 *
 * Revision 1.4.2.1  2003/02/12 02:41:53  brc
 * Added prototype for the tide_dump_data() routine to output ASCII.
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
 * Revision 1.4  2001/09/23 19:22:54  brc
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
 * Revision 1.3  2001/08/21 01:28:07  brc
 * Added facility to read SRD Ltd. tide files (a fairly basic ASCII format which
 * could be used for generic data).  Also fixed bug which caused over-runs when
 * computing the time bounds of the tide set.
 *
 * Revision 1.2  2001/05/14 04:24:21  brc
 * Updated to make module 'params'-aware
 *
 * Revision 1.1  2001/02/21 05:47:19  brc
 * Code to load and then interpolate tidal information files for reducing
 * soundings to datum.
 *
 *
 * File:	tides.h
 * Purpose:	Tide file manipulation and interpolation
 * Date:	20 February 2001
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __TIDES_H__
#define __TIDES_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "sounding.h"

typedef struct _tide *Tide;

#define TIDE_RD_NOAA_MEASURED	"* * * Y/M/D h:m T * * * * * *"
#define TIDE_RD_NOAA_PREDICTED	"* Y/M/D h:m T"
#define TIDE_RD_SRD_MEASURED	"D/M/Y h:m T"
#define TIDE_RD_HIPS_V1			"Y/M/D h:m:s T"
#define TIDE_RD_OMG1			"J h m T"

#define TIDE_WR_NOAA			"Y/M/D h:m T"
#define TIDE_WR_NOAA			"Y/M/D h:m T"
#define TIDE_WR_SRD_MEASURED	"D/M/Y h:m T"
#define TIDE_WR_HIPS_V1			"Y/M/D h:m:s T"
#define TIDE_WR_OMG1			"J h m T"

/* Routine:	tide_release
 * Purpose:	Release all memory associated with a Tide structure
 * Inputs:	*tide	Tide structure to release
 * Outputs:	-
 * Comment:	-
 */

extern void tide_release(Tide tide);

/* Routine:	tide_new
 * Purpose:	Construct a new blank Tide structure
 * Inputs:	-
 * Outputs:	Pointer to Tide structure, or NULL on failure
 * Comment:	The Tide structure returned is valid for use in the routines in this
 *			file, but contains no data.  Data can be added using tide_insert().
 */

extern Tide tide_new(void);

/* Routine:	tide_get_bound_times
 * Purpose:	Return bounding interpolation times from the Tide structure
 * Inputs:	tide	Tide structure to work from
 * Outputs:	*start, *end	Set to start and end times.
 * Comment:	-
 */

extern void tide_get_bound_times(Tide tide, f64p start, f64p end);

/* Routine:	tide_get_point_n
 * Purpose:	Return the nth point in the table
 * Inputs:	tide	Tide structure to use
 *			n		Point to extract
 * Outputs:	*epoch	Epoch timestamp associated with the point
 *			*mtide	Measured tide with the point
 *			Returns True if point was found, otherwise False (usu. bounds).
 * Comment:	-
 */

extern Bool tide_get_point_n(Tide tide, u32 n, f64p epoch, f32p mtide);

/* Routine:	tide_get_npoints
 * Purpose:	Return number of points currently defined in the table
 * Inputs:	tide	Tide structure to use
 * Outputs:	Returns number of points currently defined
 * Comment:	-
 */

extern u32 tide_get_npoints(Tide tide);

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

extern Bool tide_insert(Tide tide, f64 epoch, f32 mtide);

/* Routine:	tide_new_from_omg
 * Purpose:	Reads OMG formated Tide files into a new structure
 * Inputs:	*name	Name of the OMG Tide file to load
 *			year	Y2K compliant year for the tide file (i.e., YYYY)
 * Outputs:	Pointer to Tide file, or NULL on failure
 * Comment:	This can fail in the same way as the tide_new_from_noaa() above.
 *			The data format here is <jd><hr><min><tide>, ASCII.
 */

extern Tide tide_new_from_omg(char *name, u32 year);

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

extern Tide tide_new_from_srd(char *name);

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

extern Tide tide_new_from_ascii(char *name, char *fmt, u32 defyr);

/* Routine:	tide_dump_data
 * Purpose:	Write Tide structure to ASCII output with specified format
 * Inputs:	tide	Tide structure to dump
 *			*name	Name of the file to create
 *			*fmt	Format to write in (see comment)
 *			for_caris	Add CARIS V1 header and footer so that the file is readable
 * Outputs:	True if file was written correctly, otherwise False
 * Comment:	The format used here is the same as that used in tide_new_from_ascii
 *			except that duff columns ('*') are not allowed.  The format is
 *			translated into an fprintf() format string, and then used to write
 *			all of the data in the Tide structure.  No checks are done for using
 *			the same element more than once, or for using, for example, both
 *			Julian day and month/day.  Hey, it's your output, right?
 */

extern Bool tide_dump_data(Tide tide, char *name, char *fmt, Bool for_caris);

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

extern Bool tide_set_offset(Tide tide, f64 shift, f32 scale);

/* Routine: tide_get_offset
 * Purpose:	Get offsets currently being used in Tide structure
 * Inputs:	tide	Tide structure to work with
 * Outputs:	*shift	Time shift currently being applied
 *			*scale	Scale factor currently being applied
 * Comment:	Scales and shifts as specified in tide_set_offset().
 */

extern void tide_get_offset(Tide tide, f64p shift, f32p scale);

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

extern void tide_apply_offset(Tide tide, f32 t_delta, f32 a_scale);

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

extern Bool tide_interp(Tide tide, f64 epoch, f32 *mtide);

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

extern Bool tide_soundings(Tide tide, f64 epoch, Sounding *snd, u32 nsnds);

/* Routine:	tide_execute_params
 * Purpose:	Execute parameters list in this module
 * Inputs:	*list	List to parameters to work from
 * Outputs:	True if parameters used were OK, otherwise False
 * Comment:	Note that not finding a parameter is not a major problem; the only
 *			difficulty is if one is found, and it's wrong!
 */

#include "params.h"

extern Bool tide_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif
#endif
