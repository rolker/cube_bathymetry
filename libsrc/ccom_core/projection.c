/*
 * $Id: projection.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:35  dneville
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
 * Revision 1.2  2000/12/04 02:07:37  brc
 * Modified UTM construction code so that it re-writes the central longitude
 * value passed to be at the center of the projection zone before writing it
 * into the projection header.  Otherwise code reading the information from
 * the Projection structure could become confused (and in particular, the
 * value written into R4 files has to be the exact value because the UNBlib
 * set doesn't check whether the projection longitude is actually a valid
 * longitude in standard UTM before it does projections).
 *
 * Revision 1.1.1.1  2000/08/10 15:53:25  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	projection.c
 * Purpose:	Projection routines with object-like encapsulation.
 * Date:	04 July 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "stdtypes.h"
#include "error.h"
#include "projection.h"
#include "gctpc/source/cproj.h"	/* Get USGS projection library headers */
#include "gctpc/source/proj.h"

#define RAD2DEG(x)	((x)*180.0/M_PI)
#define DEG2RAD(x)	((x)*M_PI/180.0)

static char *modname = "projection";
static char *modrev = "$Revision: 2 $";

/* Typedef for the projection function as defined in gctp.c */
typedef long (*ProjFunc)(f64, f64, f64p, f64p);

typedef struct _proj_params {
	f64	cen_lon, cen_lat;		/* Center for projection area of interest */
	f64	x0, y0;					/* Offsets (Mercator and Polar Stereographic) */
} ProjParam;

typedef struct _projection {
	ProjType	type;
	ProjFunc	forward;
	ProjFunc	inverse;
	ProjParam	params;
} ProjStruct;

void projection_ll_to_en_deg(Projection projection, f64 lon, f64 lat,
							 f64p east, f64p north)
{
	(*(projection->forward))(DEG2RAD(lon), DEG2RAD(lat), east, north);
}

void projection_ll_to_en_rad(Projection projection, f64 lon, f64 lat,
							 f64p east, f64p north)
{
	(*(projection->forward))(lon, lat, east, north);
}

void projection_en_to_ll_deg(Projection projection, f64 east, f64 north,
							 f64p lon, f64p lat)
{
	(*(projection->inverse))(east, north, lon, lat);
	*lon = RAD2DEG(*lon);
	*lat = RAD2DEG(*lat);
}

void projection_en_to_ll_rad(Projection projection, f64 east, f64 north,
							 f64p lon, f64p lat)
{
	(*(projection->inverse))(east, north, lon, lat);
}

ProjType projection_get_type(Projection projection)
{
	return(projection->type);
}

void projection_get_params(Projection projection, f64p cen_lon, f64p cen_lat,
						   f64p x0, f64p y0)
{
	*cen_lon = projection->params.cen_lon;
	*cen_lat = projection->params.cen_lat;
	*x0 = projection->params.x0;
	*y0 = projection->params.y0;
}

ProjEllipsd projection_get_ellipsoid(Projection projection)
{
	return(PROJECTION_WGS84);
}

static void projection_utm_make_current(Projection projection)
{
	s32		utm_zone;
	f64		params[15], ax_maj, ax_min, radius;
	
	utm_zone = calc_utm_zone(projection->params.cen_lon);
	if (projection->params.cen_lat < 0.0) utm_zone = -utm_zone;
	
	memset(params, 0, sizeof(f64)*15);
	sphdz(SPH_WGS_1984, params, &ax_maj, &ax_min, &radius);
	
	utmforint(ax_maj, ax_min, 0.9996, utm_zone); /* 0.9996 is std UTM scale */
	utminvint(ax_maj, ax_min, 0.9996, utm_zone);
}

static void projection_merc_make_current(Projection projection)
{
	f64		params[15], ax_maj, ax_min, radius;
	
	memset(params, 0, sizeof(f64)*15);
	sphdz(SPH_WGS_1984, params, &ax_maj, &ax_min, &radius);
	
	merforint(ax_maj, ax_min,
				DEG2RAD(projection->params.cen_lon),
				DEG2RAD(projection->params.cen_lat),
				projection->params.x0, projection->params.y0);
	merinvint(ax_maj, ax_min,
				DEG2RAD(projection->params.cen_lon),
				DEG2RAD(projection->params.cen_lat),
				projection->params.x0, projection->params.y0);
}

static void projection_pstereo_make_current(Projection projection)
{
	f64	params[15], ax_maj, ax_min, radius;
	
	memset(params, 0, sizeof(f64)*15);
	sphdz(SPH_WGS_1984, params, &ax_maj, &ax_min, &radius);
	
	psforint(ax_maj, ax_min,
				DEG2RAD(projection->params.cen_lon),
				DEG2RAD(projection->params.cen_lat),
				projection->params.x0,
				projection->params.y0);
	psinvint(ax_maj, ax_min,
				DEG2RAD(projection->params.cen_lon),
				DEG2RAD(projection->params.cen_lat),
				projection->params.x0,
				projection->params.y0);
}

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

Bool projection_make_current(Projection projection)
{
	switch(projection->type) {
		case PROJECTION_UTM:
			projection_utm_make_current(projection);
			break;
		case PROJECTION_MERCATOR:
			projection_merc_make_current(projection);
			break;
		case PROJECTION_POLAR_STEREO:
			projection_pstereo_make_current(projection);
			break;
		default:
			error_msgv(modname, "internal: error: projection type (%d) not"
				"known.\n", (u32)(projection->type));
			return(False);
	}
	return(True);
}

Projection projection_new_utm(f64 cen_lon, f64 cen_lat)
{
	Projection	rtn;
	s32			utm_zone;

#ifdef __OLD_VERSION__
	f64			params[15], ax_maj, ax_min, radius;
#endif

	if ((rtn = (Projection)malloc(sizeof(ProjStruct))) == NULL) return(NULL);

	rtn->type = PROJECTION_UTM;
	rtn->forward = utmfor;
	rtn->inverse = utminv;
	rtn->params.x0 = 0.0; rtn->params.y0 = 0.0;

	utm_zone = calc_utm_zone(cen_lon);
	if (cen_lat < 0.0) utm_zone = -utm_zone;
	/* For UTM, the projection longitude is normally set to the center of the
	 * zone, although the projection module does this internally.  In order to
	 * make sure that we're working on the right basis, we re-write the
	 * central longitude to the correct value.
	 */
	cen_lon = 6.0*fabs(utm_zone) /* 6.0 degrees per zone */
			  - 180.0	/* Offset so that zone 1 is at the reverse meridian */
			  - 3.0		/* Offset by half a zone to put it at centre */;
	rtn->params.cen_lon = cen_lon; rtn->params.cen_lat = cen_lat;
#ifdef __OLD_VERSION__
	memset(params, 0, sizeof(f64)*15);
	sphdz(SPH_WGS_1984, params, &ax_maj, &ax_min, &radius);
	
	utmforint(ax_maj, ax_min, 0.9996, utm_zone); /* 0.9996 is std UTM scale */
	utminvint(ax_maj, ax_min, 0.9996, utm_zone);
#endif
	projection_utm_make_current(rtn);
	return(rtn);
}

Projection projection_new_mercator(f64 cen_lon, f64 cen_lat, f64 x0, f64 y0)
{
	Projection	rtn;
#ifdef __OLD_VERSION__
	f64			params[15], ax_maj, ax_min, radius;
#endif

	if ((rtn = (Projection)malloc(sizeof(ProjStruct))) == NULL) return(NULL);

	rtn->type = PROJECTION_MERCATOR;
	rtn->forward = merfor;
	rtn->inverse = merinv;
	rtn->params.cen_lon = cen_lon; rtn->params.cen_lat = cen_lat;
	rtn->params.x0 = x0; rtn->params.y0 = y0;

#ifdef __OLD_VERSION__	
	memset(params, 0, sizeof(f64)*15);
	sphdz(SPH_WGS_1984, params, &ax_maj, &ax_min, &radius);
	
	merforint(ax_maj, ax_min, DEG2RAD(cen_lon), DEG2RAD(cen_lat), x0, y0);
	merinvint(ax_maj, ax_min, DEG2RAD(cen_lon), DEG2RAD(cen_lat), x0, y0);
#endif
	projection_merc_make_current(rtn);
	return(rtn);
}

Projection projection_new_polar_stereo(f64 cen_lon, f64 cen_lat, f64 x0, f64 y0)
{
	Projection	rtn;
#ifdef __OLD_VERSION__
	f64			params[15], ax_maj, ax_min, radius;
#endif

	if ((rtn = (Projection)malloc(sizeof(ProjStruct))) == NULL) return(NULL);

	rtn->type = PROJECTION_POLAR_STEREO;
	rtn->forward = psfor;
	rtn->inverse = psinv;
	rtn->params.cen_lon = cen_lon; rtn->params.cen_lat = cen_lat;
	rtn->params.x0 = x0; rtn->params.y0 = y0;

#ifdef __OLD_VERSION__
	memset(params, 0, sizeof(f64)*15);
	sphdz(SPH_WGS_1984, params, &ax_maj, &ax_min, &radius);
	
	psforint(ax_maj, ax_min, DEG2RAD(cen_lon), DEG2RAD(cen_lat), x0, y0);
	psinvint(ax_maj, ax_min, DEG2RAD(cen_lon), DEG2RAD(cen_lat), x0, y0);
#endif
	projection_pstereo_make_current(rtn);
	return(rtn);
}

Projection projection_new_from_file(FILE *f)
{
	u32			type;
	ProjParam	params;
	Projection	rtn;
	
	if ((rtn = (Projection)malloc(sizeof(ProjStruct))) == NULL) return(NULL);
	if (fread(&type, sizeof(u32), 1, f) != 1 || fread(&params, sizeof(ProjParam), 1, f) != 1)
		return(NULL);

	switch (type) {
		case PROJECTION_UTM:
			rtn = projection_new_utm(params.cen_lon, params.cen_lat);
			break;
		case PROJECTION_MERCATOR:
			rtn = projection_new_mercator(params.cen_lon, params.cen_lat,
										  params.x0, params.y0);
			break;
		case PROJECTION_POLAR_STEREO:
			rtn = projection_new_polar_stereo(params.cen_lon, params.cen_lat,
											  params.x0, params.y0);
			break;
		default:
			error_msgv(modname, "projection type %d unknown.\n", type);
			rtn = NULL;
			break;
	}
	return(rtn);
}

Bool projection_write_projection(FILE *f, Projection projection)
{
	if (fwrite(&(projection->type), sizeof(u32), 1, f) != 1 ||
		fwrite(&(projection->params), sizeof(ProjParam), 1, f) != 1)
		return(False);
	return(True);
}

void projection_release(Projection projection)
{
	if (projection != NULL) free(projection);
}
