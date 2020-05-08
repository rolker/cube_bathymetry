/*
 * $Id: nav.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:27  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.7  2002/03/14 04:07:16  brc
 * Added code to compute approximate speed-over-ground using navigation
 * positions projected into a suitable ground coordinate space.  Of course,
 * this is likely to be rather noisy ...  Speeds are reported in m/s.
 *
 * Revision 1.6  2001/05/14 04:22:22  brc
 * Updated to make module 'params'-aware.
 *
 * Revision 1.5  2001/02/10 18:15:58  brc
 * Added prototypes for new `nav with SoG' calls.
 *
 * Revision 1.4  2000/11/14 22:45:54  brc
 * Added facility to parse NEMA GGA strings for timestamp and position, and a
 * wrapper around this and nav_insert() to allow GGA strings to be inserted
 * directly.
 *
 * Revision 1.3  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
 *
 * Revision 1.2  2000/09/07 21:06:13  brc
 * Modified nav.{c,h} to allow the navigation data to be read back by the
 * user.  This is required if we need to convert the nav back to ASCII form.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:25  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	nav.h
 * Purpose:	Load and interpolate navigation information
 * Date:	13 July 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __NAV__
#define __NAV__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "projection.h"

typedef struct _nav *Nav;

extern Nav nav_new_from_omg(const char *name);
extern Nav nav_new(void);
extern Bool nav_insert(Nav nav, f64 epoch, f64 lon, f64 lat);
extern Bool nav_insert_v(Nav nav, f64 epoch, f64 lon, f64 lat, f64 sog);
extern Bool nav_interp(Nav nav, f64 epoch, f64p lon, f64p lat);
extern Bool nav_interp_v(Nav nav, f64 epoch, f64p lon, f64p lat, f64p sog);
extern void nav_release(Nav nav);
extern void nav_get_bound_times(Nav nav, f64p start, f64p end);
extern Bool nav_get_point_n(Nav nav, u32 n, f64p epoch, f64p lon, f64p lat);
extern Bool nav_get_point_n_v(Nav nav, u32 n, f64p epoch, f64p lon, f64p lat, f64p sog);
extern u32 nav_get_npoints(Nav nav);

/* Routine:	nav_parse_gga
 * Purpose:	Parse a NEMA 2.0 GGA string into Latitude, Longitude & Time
 * Inputs:	*gga	GGA string (see comment)
 * Outputs:	epoch	Timestamp from GGA string (UTC time)
 *			lat		Latitude string from GGA as decimal degrees
 *			lon		Longitude string from GGA as decimal degrees
 * Comment:	This expects the GGA string to be of the following format:
 *				$??GGA,T[T][.T[T]],D[D]MM[.M[M]],(N|S),[D]DMM[.M[M]],(E|W),*
 *			For example:
 *				$INGGA,203521.7816,4304.51332,N,07042.57241,W,2,09,1.1,1.4, ...
 *			of which just the time, latitude and longitude are retained.  As
 *			usual, northern and eastern hemispheres are taken positive.
 */

extern Bool nav_parse_gga(u8 *gga, f64p epoch, f64p lon, f64p lat);

/* Routine:	nav_insert_gga
 * Purpose:	Insert navigation information direct from a GPS GGA string
 * Inputs:	nav		Nav structure to append information to
 *			*gga	String to parse data from
 * Outputs:	True if string was parsed and inserted; False otherwise
 * Comment:	This uses nav_parse_gga() to parse the string, and nav_insert() to
 *			add it to the navigational buffer.  Return code is as returned by
 *			these two calls, or True if both return True
 */

extern Bool nav_insert_gga(Nav nav, u8p gga);

/* Routine:	nav_compute_sog
 * Purpose:	Compute SoG from Nav points
 * Inputs:	nav		Nav structure to work from
 *			proj	Projection structure to use for computations
 * Outputs:	-
 * Comment:	This computes an approximation to Speed-Over-Ground from the
 *			navigation information in the interpolation buffer.  This is done
 *			by forward differencing on projected coordinates, with the end-point
 *			set equal to the penultimate point.  Speed-Over-Ground is computed
 *			in m/s.
 */

extern void nav_compute_sog(Nav nav, Projection proj);

/* Routine:	nav_execute_params
 * Purpose:	Execute parameters list in this module
 * Inputs:	*list	List to parameters to work from
 * Outputs:	True if parameters used were OK, otherwise False
 * Comment:	Note that not finding a parameter is not a major problem; the only
 *			difficulty is if one is found, and it's wrong!
 */

#include "params.h"

extern Bool nav_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
