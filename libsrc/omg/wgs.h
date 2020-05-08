/*----------------------------------------------------------------------
	wgs.h	-	

	Definitions for the  various Ellipoids
	Taken from .... Various Sources?
----------------------------------------------------------------------*/

#ifndef wgs_
#define wgs_


#define WGS84 1
#define WGS84_ECCEN  0.0818191908417579
#define WGS84_RADIUS 6378137			/* units are meters */

#define GRS80_FLATT  298.257223563 /* which is NAD83 */
#define WGS84_FLATT  298.257222101
/* h'mmm funnily enough these are reversed in the Strang and Borre book... */

#define NAD27 2
#define NAD27_ECCEN  0.082271854
#define NAD27_RADIUS 6378206.4			/* units are meters */

#define CLK66 3
#define CLK66_ECCEN 0.082271684
#define CLK66_RADIUS  6378206.4

#define AGD84 4
#define AGD84_ECCEN 0.08182018 
#define AGD84_RADIUS  6378160.0
#define AGD84_FLATT  298.25

/* to cope with Smith and Wessel circular coordinates */
#define SPHER 5
#define SPHER_ECCEN 0.00
#define SPHER_RADIUS  6378137.0

extern int Ellipsoid;
extern int Proj_Type;
extern float Map_Rotation;
extern char Hemisphere;

/* 
radius defined is a (equatorial)
		  b (polar)
 f = (a-b)/a
 f ~= 1/298ish

 f = 1/F 
 ECCEN = sqrt(2f-f*f) 
 ECCEN_SQRED = (2-f)f  / (1-f)**2 
 ECCEN_SQRED = (a**2 - b**2) / b**2 
*/
#endif
