/*----------------------------------------------------------------------

	backscatter.c	-	John Hughes Clarke	-	Apr 23, 1990

	subroutines for backscatter code.

----------------------------------------------------------------------*/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "backscatter.h"
#include "j_phi.h"
#include "support.h"



/*----------------------------------------------------------------*/

float  cosangle(float pixlen, float elev, float azi,
			float pixel, float peast, float psouth)
/*	cosangle()
 */
{
/* brc, UNH-CCOM/JHC 7 April 2000.  Initialised angle == 0.0 to avoid compiler
 * warning on incomplete code.
 */
  double x1,x2,y1,y2,z1,z2,angle = 0.0;

	/* from elev and azi got to get x1, y1 and z1 of sun illumination
	*/
	x1 = sin(azi);
	y1 = cos(azi);
	z1 = tan(elev);
	
	/* then have to normalise x2, y2 and z2 from seafloor steps */
	x2 = y2 = pixlen;
	z2 = angle;
	return(angle);
/* not complete yet */
}

/* ++++++++++++++++++++++++++++++++++++++++ */

float  vectangle(float pixlen, float elev, float azi,
			float pixel, float peast, float psouth)
/*	vectangle()

	a quick translation of the 1988 FORTRAN algorythym in the old mock
	sidescan code.
	calculates the local slope for each pixel by looking at the slope to the 
	east and to the south. Thus the elevation and azimuth of the normal to
	the pixel face is found.
	combining this with the insonification direction, the angle between the 
	two vectors is returned.

		John E. Hughes Clarke  May 1990 */
{
  float 	angle, pixelev, pixazi, rawpixazi, 
		x,y,z,x2,y2,z2,z3,
		mston,mwtoe,lenns,lenazi,
		ang,newang, deltaazi;


/*	printf(" elev %f azi %f  pixlen %f \n", elev, azi, pixlen);
	printf(" pixel %f peast %f  psouth %f \n", pixel, peast, psouth); */

/*c..........work out change in elevations    */
 
               mston=pixel-psouth;
               mwtoe=peast-pixel;
               if(mston == 0.0) mston=0.0001;
               if(mwtoe == 0.0) mwtoe=0.0001;
               lenns=pixlen*(mwtoe/mston) ;
               if(lenns <  0.0)lenns= -lenns;

/*	printf("mston %f mwtoe %f lenns %f\n", mston, mwtoe, lenns); */
 
/*c......... work out azimuth of pixel face    */
 
               rawpixazi=atan(lenns/pixlen);
               if(mwtoe <  0.0){
                 if(mston >  0.0) {
                   pixazi=M_PI-rawpixazi;
                 } else {
                   pixazi=rawpixazi;
                 }
               } else {
                 if(mston >  0.0) {
                   pixazi=M_PI+rawpixazi;
                 } else {
                   pixazi=((2.0)*M_PI)-rawpixazi;
                 }
               }    
 
/* c....... now work out elevation of (the normal to) the pixel face.  */
 
               lenazi=lenns*cos(rawpixazi);
               pixelev=atan(lenazi/mwtoe);
               if(pixelev <  0.0) pixelev= -pixelev;
 
/* c    (see figure in JHC notebook to explain pixlen,lenns,lenazi,
c        mwtoe,mntos)
HA! long since lost......
c.......normalising the difference in azimuth of the GLORIA beam 
c                  and the face   */
 
               deltaazi=azi-pixazi;
               if(deltaazi <  0.0)deltaazi= -deltaazi;
               if(deltaazi >= M_PI)deltaazi=((2.0)*M_PI)-deltaazi;
 
/*  ....calculating the angle between the two vectors.
      (see figure in JHC notebook to explain obscure parameters!!!)   */
		/* ...ditto to the lost... */
 
               y=cos(pixelev);
               x=cos(elev);
               z=sqrt(pow(y,2.0)+pow(x,2.0)-(2.0*x*y*cos(deltaazi)));
               x2=sin(elev);
               ang=atan(x2/z);
               newang=(M_PI/2.0)-ang;
               z2=sqrt(pow(x2,2.0)+pow(z,2.0));
               y2=sin(pixelev);
               z3=sqrt(pow(y2,2.0)+pow(z2,2.0)-(2.0*y2*z2*cos(newang)));
               if(z3 >  sqrt(2.0)) {
/*                 printf("effective shadow at %f \n",(90.-((elev*180.)/M_PI)));    */
                 angle= -1;
               } else {
                 angle=(180./M_PI)*acos((1.+1.-pow(z3,2.0))/(2.0*1.0*1.0));
               }
 
/*	printf(" z3 %f pow %f\n", z3, pow(z3,2.0));
 
	printf(">>>>> angle %f \n", angle); */
       return(angle);
}

/*----------------------------------------------------------------*/

void  dget360offsets(double azi, double *xoffset, double *yoffset)
{
  double  azideg;

	azideg = azi*180./M_PI;
	if(azideg < 0.0) azideg += 360.0;
	if(azideg < 0. || azideg > 360.) {
		printf("shitty azimuth passed to get360offsets %f\n", azideg);
		exit(-1);
	} else if (azideg == 0.) {
	  *xoffset = 1.0;
	  *yoffset = 0.0;
	} else if (azideg == 90.) {
	  *xoffset = 0.0;
	  *yoffset = 1.0;
	} else if (azideg == 180.) {
	  *xoffset = -1.0;
	  *yoffset = 0.0;
	} else if (azideg == 270.) {
	  *xoffset = 0.0;
	  *yoffset = -1.0;
	} else if (azideg == 360.) {
	  *xoffset = 1.0;
	  *yoffset = 0.0;
	} else if (azideg < 90.) {
	  *xoffset = cos(azi);
	  *yoffset = sin(azi);
	} else if (azideg < 180.) {
	  *xoffset = cos(azi);
	  *yoffset = sin(azi);
	} else if (azideg < 270.) {
	  *xoffset = cos(azi);
	  *yoffset = sin(azi);
	} else if (azideg < 360.) {
	  *xoffset = cos(azi);
	  *yoffset = sin(azi);
	}
} 

/*----------------------------------------------------------------*/

void  get360offsets(float azi, float *xoffset, float *yoffset)
{
double dazi, d_xoffset, d_yoffset;

	dazi = azi;
	dget360offsets(dazi, &d_xoffset, &d_yoffset);
	*xoffset = d_xoffset;
	*yoffset = d_yoffset;
}

/*----------------------------------------------------------------*/

double   	bit8_to_bit12(unsigned char bit8_value)
{
  int C, S;
  double bit12_value;
  
	C = bit8_value >> 5;        
	S = bit8_value & 31l;
	bit12_value = (pow(2.0,(double)C)*(double)(S+32) -31.0);
/*	printf(" 8bit %d C %d S %d, 12bit %f \n", bit8_value, C, S, bit12_value);  */
	return(bit12_value);
}

/*----------------------------------------------------------------*/

double	inverse_TVG(double distance, double DB)
{
  double TVG, tvg;

	/*---- to allow for 1 second delay --- */
	/*---- to allow for 1 second shift to centre of pulse --- */
	distance = distance - 3000.;
        if(distance <= 500.0) distance = 500.;
	TVG = -(DB-30.*log(distance) -8.2e-4*distance) *90./DB;
	tvg=pow(10.,TVG/20.);
/*	tvg /=108889.6; */
	tvg /=1717301.625;
        return(tvg);
}

/*----------------------------------------------------------------*/

/*  function to calculate the time-varied gain and shading power loss.

	kindly provided by Neil Mitchell LDGO, July 1990
	(modified for 20 second PRP)

        This is done by a numerical integration.
	Results placed in the array "tvg_shade".		*/

int tvg_shading_loss (double tvg_shade[496], int ping_rate)
{
	double log10();

	double dt=0.1;		/*  integration interval */
	double shaded_pulse;
	double t, T=2.; 	/* only set up for 2 second pulse */
	double scan_period;
	double max_range;
	double range,attenuation,H,DB;
	double td, tvg, shaded;
	double pulse_integral = 1.4296;	/* Hamming shaded pulse integral */
	int i;                                            

	if(ping_rate == 20) {
	   max_range = 15000;
	   DB = 137.60;
	   scan_period = 20;
	}else if (ping_rate == 30) {
	   max_range = 22500;
	   DB = 149.02;
	   scan_period = 30;
	} else {
	  printf(" in tvg_shading loss, ** unusable ping rate : %d\n", ping_rate);
	  exit(-1);
	}

	for ( i = 1; i < 496; ++i )		/* ignore first pixel */
	{
		td = i * scan_period / 496.;	/* time delay of pulse compression output  */

		for ( t = (td - T); t < td; t += dt ) /* integrate over the pulse period */
		{

	/* first calculate the amplitude attenuation due to tvg.   */
			range = t * max_range / scan_period;  /* range in m */
			if ( range > 0. )
     			    H = DB - ( (30.*log10(range)  + (8.2e-4)*range));
    	  	        attenuation = H * 90. / DB;
			tvg = pow (10., -attenuation/20.);	/* voltage ratio */
			shaded = 0.54 + 0.46 * cos (2. * M_PI *(t - td + T/2.) / T);
						/* this is the pulse shading factor */
			if (t > 0. && t <= (scan_period-T) )  /* eclipsing on transmission */
			    tvg_shade [i] += shaded*shaded * tvg *tvg * dt / pulse_integral;
		}
	}
 return (0);
}
 

/*----------------------------------------------------------------*/


/*       get360azi(dx, dy, azi)
 *
 *      return a 2phi degree azimuth as a result of inputing
 *      a step to the north and to the east.
 */
void get360azi(double dx, double dy, double *azi)
{
        double phi;

        phi = 4. * atan(1.);


        if (dx == 0.0) dx = 0.000001;
        if (dy == 0.0) dy = 0.000001;

        if(dx > 0.0 && dy > 0.0) {
                *azi =atan(dx/dy);
        }
        else if(dx > 0.0 && dy < 0.0) {
                *azi = atan(-dy/dx);
                *azi = *azi + phi/2;
        }
        else if(dx < 0.0 && dy < 0.0) {
                *azi = atan(dx/dy);
                *azi = *azi + phi ;
        }
        else if(dx < 0.0 && dy > 0.0) {
                *azi = atan(dy/-dx);
                *azi = *azi + (phi*3/2);
        }
}


