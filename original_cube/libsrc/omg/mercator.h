/*----------------------------------------------------------------------
	mercator.h	-	Guy Carpenter	-	Nov 13, 1989

	This set of subroutines will project lon/lat coordinates
	into map units on a mercator projection.  Values for
	eccentricity and earth radius may be changed at runtime.

	Initially the eccen and earth radius are defined by the WGS 84
	definition, and the radius is in METERS!

	merc_init MUST be called before mercator is called.

	JEHC 93 added inverse mercator from Synder

----------------------------------------------------------------------*/

#ifndef mercator_
#define mercator_

#include "wgs.h"

extern double merc_eccen;
extern double merc_xoffset, merc_yoffset;
extern double merc_earthrad;
extern double merc_eqscale;
extern double merc_scaledrad;
extern char   merc_signflag;

void mercator (double lon, double lat, double *x, double *y);
void inv_merc(double x, double y, double *lon, double *lat);

void merc_init (	double scale,		/* relates to merc_earthrad units */
			double proj_lat,
			double origin_lon,	/* set origin */
			double origin_lat);

typedef enum {no_proj, mercator_proj, utm_proj, amg_proj,
		rotutm_proj} mapproj_type;


#define DEGTORAD(D) ((double)(D)*M_PI/180.0)
#define RADTODEG(D) ((double)(D)*180.0/M_PI)



#endif
