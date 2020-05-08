#include <math.h>
#include <stdlib.h>
#include "support.h"
#include "wgs.h"
#include "j_phi.h"
/*
#define M_PI  3.14159
*/

int utmgeo (phi, lam, clam, x, y, hem)
double x, y, clam;
double *phi, *lam;
char hem;
/* subroutine to convert x (easting) and y (northing) in UTM standards
   of a given point into latitude and longitude (decimal degrees). 
   Needs to know the hem(isphere), that should be 'n' or 's'.
   call: utmgeo (&phi, &lam, clam, x, y, hem);
   Source for formulas: J.P. Snyder, Map projections used by the U.S.G.S.,
   Geol. Survey Bulletin 1532, p. 68, 1982.
   A. Malinverno
   March 3, 1986.	*/
{
	double k0 = 0.9996;	/* central scale factor */
	double x0 = 500000.0;	/* x0 to be added in standard UTM */
	double y0 = 10000000.0;	/* y0 to be added in standard UTM for southern hemisphere */
	double e2, a;
	double e4;
	double e6;
	double epr2;
	double e1;
	double e12;
	double e13;
	double e14;
	double phi1, cosphi1, sinphi1, tanphi1;
	double c1, c12, t1, t12, n0, n1, r1;
	double d, d2, d3, d4, d5, d6;
	double m, mu;
	double dx, dy;

	if(Ellipsoid == WGS84) {
	e2 = WGS84_ECCEN * WGS84_ECCEN;
	a = WGS84_RADIUS;
	}
	else if(Ellipsoid == CLK66) {
        e2 = CLK66_ECCEN * CLK66_ECCEN;
        a = CLK66_RADIUS;
	}
	else if(Ellipsoid == NAD27) {
	e2 = NAD27_ECCEN*NAD27_ECCEN;
	a = NAD27_RADIUS;
        }
        else if(Ellipsoid == AGD84) {
        e2 = AGD84_ECCEN*AGD84_ECCEN;
        a = AGD84_RADIUS;
        }
        else if(Ellipsoid == SPHER) {
        e2 = SPHER_ECCEN*SPHER_ECCEN;
        a = SPHER_RADIUS;
	} else {
	  printf("invalid Elliposid it UTM conversion\n");
		exit(-1);
	}

	e4 = e2 * e2;
	e6 = e2 * e2 * e2;
	epr2 = e2 / (1.0 - e2);
	e1 = (1.0 - sqrt(1.0 - e2)) / (1.0 + sqrt(1.0 - e2));
	e12 = e1 * e1;
	e13 = e1 * e1 * e1;
	e14 = e1 * e1 * e1 * e1;




	clam = clam * M_PI / 180.0;
	dx = x - x0;
	if (hem == 's') y = y - y0;
	dy = y;

	m = dy / k0;
	mu = m / ( a * (1.0 - e2/4.0 - 3.0*e4/64.0 - 5.0*e6/256.0));
	phi1 = mu + (3.0*e1/2.0 - 27.0*e13/32.0)*sin(2.0*mu) + (21.0*e12/16.0 - 55.0*e14/32.0)*sin(4.0*mu) + (151.0*e13/96.0)*sin(6.0*mu);
	cosphi1 = cos(phi1);
	sinphi1 = sin(phi1);
	tanphi1 = tan(phi1);
	c1 = epr2 * cosphi1 * cosphi1;
	c12 = c1 * c1;
	t1 = tanphi1 * tanphi1;
	t12 = t1 * t1;
	n0 = sqrt(1.0 - e2 * sinphi1 * sinphi1);
	n1 = a / n0;
	r1 = a * (1.0 - e2) / (n0 * n0 * n0);
	d = dx / (n1 * k0);
	d2 = d * d;
	d3 = d * d * d;
	d4 = d * d * d * d;
	d5 = d * d * d * d * d;
	d6 = d * d * d * d * d * d;
	*phi = phi1 - (n1*tanphi1/r1) * (d2/2.0 - (5.0 + 3.0*t1 + 10.0*c1 - 4.0*c12 - 9.0*epr2)*d4/24.0 + (61.0 + 90.0*t1 + 298.0*c1 + 45.0*t12 - 252.0*epr2 - 3.0*c12)*d6/720.0);
	*lam = clam + (d - (1.0 + 2*t1 + c1)*d3/6.0 + (5.0 - 2.0*c1 + 28.0*t1 - 3.0*c12 + 8.0*epr2 + 24.0*t12)*d5/120.0) / cosphi1;
	*phi = *phi * 180.0 / M_PI;
	*lam = *lam * 180.0 / M_PI;
	return(0);
}

