/*----------------------------------------------------------------------
	mercator.c		-	Guy Carpenter	-	Nov 13, 1989

	See mercator.h for function descriptions.  These routines are
	based on the formulae in Map Projections - A Working Manual
	USGS Paper 1395.


	Date line is handled in this way:

		If the origin given to init is at a negative
	(west) longitude, then the map can safely cross the
	0 meridian.  If the origin has a positive (east) long
	then the map can safely cross the date line.  This should
	work for any cases, as long as the area of projection is
	to the right of the origin.

----------------------------------------------------------------------*/

#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "support.h"

#include "mercator.h"
#include "j_phi.h"

double merc_eccen;
double merc_earthrad;
double merc_eqscale;
double merc_scaledrad;
double merc_xoffset;
double merc_yoffset;
char   merc_signflag;


void merc_init (	double scale,
			double proj_lat,
			double origin_lon,
			double origin_lat)
{
	double x,y;


        if(Ellipsoid == WGS84) {
        merc_eccen = WGS84_ECCEN;
        merc_earthrad = WGS84_RADIUS;
        }
        else if(Ellipsoid == CLK66) {
        merc_eccen = CLK66_ECCEN;
        merc_earthrad = CLK66_RADIUS;
        }
        else if(Ellipsoid == NAD27) {
        merc_eccen = NAD27_ECCEN;
        merc_earthrad = NAD27_RADIUS;
	}
        else if(Ellipsoid == AGD84) {
        merc_eccen = AGD84_ECCEN;
        merc_earthrad = AGD84_RADIUS;
	}
        else if(Ellipsoid == SPHER) {
        merc_eccen = SPHER_ECCEN;
        merc_earthrad = SPHER_RADIUS;
	} else {
		printf(" invalid Ellipsoid in mercator\n");
		exit(-1);
        }



	merc_eqscale = scale / cos(DEGTORAD(proj_lat));
	merc_scaledrad =merc_earthrad / merc_eqscale;
	merc_xoffset = 0;
	merc_yoffset = 0;
	mercator (origin_lon, origin_lat, &x, &y);
	merc_xoffset = x;
	merc_yoffset = y;

	merc_signflag = (merc_xoffset>0);	/* if true negs become 360+ */

/*
	warning ("Offsets set to %lf, %lf",x,y);
*/
}

void mercator (double lon, double lat, double *x, double *y)
{
	double sinlat, esinlat;


	if (merc_signflag && lon<0) lon+=360;
	sinlat = sin(DEGTORAD(lat));
	esinlat = merc_eccen * sinlat;
	*x =	merc_scaledrad*DEGTORAD(lon) - merc_xoffset;
	*y =	merc_scaledrad *
		log ( ((1 + sinlat)/(1 - sinlat)) *
                   pow(((1 -esinlat)/(1 +esinlat)),merc_eccen)
		    ) / 2.0
		- merc_yoffset;

/*
	*x += mapproj_xoffset;
	*y += mapproj_yoffset;
*/

}

/* added John HC 1993 , from Synder as usual */

void inv_merc(double x, double y, double *lon, double *lat)
{
double error=1000.0;
double accuracy =  (M_PI/180.0) * 1.0/(60.0*100000.0);
	/* ie accuracy required in 1/100,000th of a minute (1.8cm) */
double sinlat, esinlat;
double t, olat, otherbit;


	*lon = (180.0/M_PI) * (x+merc_xoffset)/merc_scaledrad;
	if (merc_signflag && *lon>360) *lon -= 360;

	t = pow(M_E,(-(y+merc_yoffset)/merc_scaledrad));
	olat = (M_PI/2.0)-2.0*atan(t);

	while(error > accuracy) {
	sinlat = sin(olat);
	esinlat = merc_eccen * sinlat;
	otherbit = (1-esinlat)/(1+esinlat);
	*lat = (M_PI/2.0)-2.0*atan(t*pow(otherbit,merc_eccen/2.0));

	error = fabs(olat- *lat);
	olat = *lat;
	}


	*lat *= 180.0/M_PI;
}

