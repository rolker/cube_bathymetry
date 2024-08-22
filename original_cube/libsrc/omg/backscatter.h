/*----------------------------------------------------------------------

	backscatter.h	-	John Hughes Clarke	-	Apr 23, 1990

	include file for backscatter code.

----------------------------------------------------------------------*/

#include <stdio.h>
#include <ctype.h>
#ifndef MAXDOUBLE
#include <math.h>
#endif


/* #define 	M_PI  			3.1415926535 */
#define 	TWELVE_BIT_TO_BAR 	3.628E-12
#define		ABSORP_COEFF_PORT	0.000056
#define		ABSORP_COEFF_STBD	0.000050
#define 	PORT_BEAM_WIDTH		2.01
#define		STBD_BEAM_WIDTH		2.16
#define		PULSE_CORR_WIDTH	0.03
#define		SOURCE_POWER		4500
#define		PORT_DIRECT_INDEX	138.039
#define		STBD_DIRECT_INDEX	133.415

#define		SOUND_SPEED		1500.0

#define		ARRAY_LENGTH		5.33
#define		ARRAY_WIDTH		0.31
#define		TRANSDUCER_RADIUS	0.057
#define 	TRANSDUCER_SPACING	0.17
#define		PORT_FREQ		6762.5
#define		STBD_FREQ		6287.5


void  	dget360offsets(double azi, double *xoffset, double *yoffset);
void  	get360offsets(float azi, float *xoffset, float *yoffset);
void get360azi(double dx, double dy, double *azi);

float  	vectangle(float pixlen, float elev, float azi,
			float pixel, float peast, float psouth);
double   bit8_to_bit12(unsigned char bit8_value);
double	inverse_TVG(double distance, double DB);
int	tvg_shading_loss(double tvg_shade[496], int ping_rate);


