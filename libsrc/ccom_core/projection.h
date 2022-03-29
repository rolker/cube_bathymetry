/*
 * $Id: projection.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:29:35  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.3.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.3  2001/09/23 19:02:50  brc
 * Fixed bug which is inherited from the GCTPC library.  The sub-library keeps
 * in memory some constants that really should be part of the state of the
 * Projection structure.  In particular, if you have more than one projection
 * method on the go (of the same type, e.g., UTM), then the projected coordinates
 * are not necessarily in the correct system --- they may be in the
 * coordinate frame of the last projection that was constructed.  To patch around
 * this, added a projection_make_current() call, which resets the sub-library
 * to make the conversion valid.  This eats some cycle time if you change system
 * often, but should not be a problem in the most common usage implementation
 * where you make only one coordinate projection and map everything into it.
 *
 * Revision 1.2  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
 *
 * Revision 1.1.1.1  2000/08/10 15:53:25  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	projection.h
 * Purpose:	Projection routines with object-like encapsulation.
 * Date:	04 July 2000
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

#ifndef __PROJECTION_H__
#define __PROJECTION_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"

typedef struct _projection *Projection;

typedef enum _known_types {
	PROJECTION_UTM = 1,
	PROJECTION_MERCATOR = 2,
	PROJECTION_POLAR_STEREO = 3,
	PROJECTION_UNKNOWN
} ProjType;

typedef enum _known_ellipsoids {
	PROJECTION_WGS84 = 1
} ProjEllipsd;

/* --------------------------------------------------------------------------
 *                          Transformation Methods
 *
 * N.B.: Transforms (lon, lat) -> (east, north) assuming (lon, lat) are in
 * decimal degrees (_deg() version) or in rads (_rad() version).
 *
 * -------------------------------------------------------------------------- */

extern void projection_ll_to_en_deg(Projection projection, f64 lon, f64 lat,
									f64p east, f64p north);
extern void projection_ll_to_en_rad(Projection projection, f64 lon, f64 lat,
									f64p east, f64p north);
extern void projection_en_to_ll_deg(Projection projection, f64 east, f64 north,
									f64p lon, f64p lat);
extern void projection_en_to_ll_rad(Projection projection, f64 east, f64 north,
									f64p lon, f64p lat);

/* --------------------------------------------------------------------------
 *                            Convenience Methods
 * -------------------------------------------------------------------------- */

extern ProjType projection_get_type(Projection projection);
extern void projection_get_params(Projection projection,
								  f64p cen_lon, f64p cen_lat, f64p x0, f64p y0);
extern ProjEllipsd projection_get_ellipsoid(Projection projection);

/* Routine:	projection_make_current
 * Purpose:	Makes the projection passed in the current projection for the
 *			module in question
 * Inputs:	proj	Projection to work with
 * Outputs:	True if projection mode was reset, otherwise False.
 * Comment:	The underlying GCTPC system has a number of static variables for
 *			each projection type, which it uses to store such things as axis
 *			sizes, projection meridians, etc.  Unfortunately, this means that
 *			it is not currently possible to have more than one projection of
 *			the same type active at the same time unless they have identical
 *			properties.  Consequently, if you are using multiple projections
 *			in the same project (e.g., if you have multiple mapsheets in the
 *			same projection, but with different projection centers), you should
 *			call this routine whenever you change projections.
 */

extern Bool projection_make_current(Projection projection);

/* --------------------------------------------------------------------------
 *                          Constructors/Destructor
 *
 * N.B.: (cen_lon, cen_lat) should be the approximate center of the survey
 * area since it is used to determine UTM zone/central meridians.  The (x0,y0)
 * pair allow specification of false eastings/northings (resp.) for use with
 * Mercator and Polar Stereographic projections.
 * -------------------------------------------------------------------------- */

extern Projection projection_new_utm(f64 cen_lon, f64 cen_lat);
extern Projection projection_new_mercator(f64 cen_lon, f64 cen_lat,
										  f64 x0, f64 y0);
extern Projection projection_new_polar_stereo(f64 cen_lon, f64 cen_lat, f64 x0,
											  f64 y0);
extern void projection_release(Projection projection);

/* --------------------------------------------------------------------------
 *                             Serialization
 * -------------------------------------------------------------------------- */

extern Projection projection_new_from_file(FILE *f);
extern Bool projection_write_projection(FILE *f, Projection projection);

#ifdef __cplusplus
}
#endif


#endif
