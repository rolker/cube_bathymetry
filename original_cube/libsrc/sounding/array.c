/*----------------------------------------------------------------------

	array.c	-	John Hughes Clarke	-	July 31, 1991

	subroutines for array code.

----------------------------------------------------------------------*/

#include <stdio.h>
#include <ctype.h>
#include "OMG_HDCS_jversion.h"
#include "array.h"
#include "swapio.h"
#include "j_proj.h"
#include "j_phi.h"

#define M_PI 3.14




/*----------------------------------------------------------------*/

SW_header  get_print_SW_header(FILE *file, int print_flag)
{
  SW_header  head;
	fseek(file,0l,0);
	fread(&head, 1, sizeof(SW_header), file);
	if(print_flag)
	printf("  SW array header data  ---- %d %d - %f %f %f %f - %f %f\n", 
			head.dx,
			head.dy,
			head.xmin,
			head.xmax,
			head.ymin,
			head.ymax,
			head.xinc,
			head.yinc);

	return(head);
}
 

/*----------------------------------------------------------------*/

float   interp_JHCarray_value(JHC_header *head, float *array_pointer, 
			float x, 
			float y)
{
  float depth_offset, range_offset;
  float tl, tr, bl, br, result;
  float weight1, weight2;
  float top, bottom;
  
		depth_offset = (y-head->ymin)/head->ystep;
		range_offset = (x-head->xmin)/(head->xstep);
/*
		printf("Y %f %f X %f %f\n",
		y, depth_offset, x, range_offset);  
*/
		if(depth_offset >= head->dy ||
		   range_offset >= head->dx ||
		   depth_offset < 0.0       ||
		   range_offset < 0.0  ) {
		   result = 0.0;
		}  else {
                tl= *(array_pointer+(int)range_offset+((int)depth_offset*head->dx));
                tr= *(array_pointer+(int)range_offset+1+((int)depth_offset*head->dx));
                bl= *(array_pointer+(int)range_offset+(((int)depth_offset+1)*head->dx));
                br= *(array_pointer+(int)range_offset+1+(((int)depth_offset+1)*head->dx));
		if(tl && tr && bl && br) {
		  weight1 = range_offset - (float)(int)range_offset;
		  weight2 = 1.0 - weight1;
		  top = tl*weight2 + tr*weight1;
		  bottom = bl*weight2 + br*weight1;
		  weight1 = depth_offset - (float)(int)depth_offset;
		  weight2 = 1.0 - weight1;
		  result = top*weight2 + bottom*weight1;
		} else {
		  result = 0.0;
		}
		}                  

  return(result);
}

/*----------------------------------------------------------------*/

float   get_JHC_lookup_value(JHC_header *head, float *array_pointer, 
			float depth, 
			float range)
{
  float depth_offset, range_offset;
  float tl, tr, bl, br, result;
  float weight1, weight2;
  float top, bottom;
  
		depth_offset = -depth/head->ystep;
		range_offset = range/(head->xstep);
		if(depth_offset >= head->dy-1 ||
		   range_offset >= head->dx-1) {
		   result = 0.0;
		}  else {
/*		printf("depth %f %f range %f %f\n",
		depth, depth_offset, range, range_offset);  */
                tl= *(array_pointer+(int)range_offset+((int)depth_offset*head->dx));
                tr= *(array_pointer+(int)range_offset+1+((int)depth_offset*head->dx));
                bl= *(array_pointer+(int)range_offset+(((int)depth_offset+1)*head->dx));
                br= *(array_pointer+(int)range_offset+1+(((int)depth_offset+1)*head->dx));
		if(tl && tr && bl && br) {
		  weight1 = range_offset - (float)(int)range_offset;
		  weight2 = 1.0 - weight1;
		  top = tl*weight2 + tr*weight1;
		  bottom = bl*weight2 + br*weight1;
		  weight1 = depth_offset - (float)(int)depth_offset;
		  weight2 = 1.0 - weight1;
		  result = top*weight2 + bottom*weight1;
		} else {
		  result = 0.0;
		}
		}                  

  return(result);
}

/*----------------------------------------------------------------*/

int  write_JHC_header(FILE *file, JHC_header head)
{
  char dumhead[1024];


	memcpy(&dumhead[0], head.header_name,4);

	if(INTEL && !MOTOROLA)
	head.dx = swap_int(head.dx);
	memcpy(&dumhead[4], &head.dx, 4);
	if(INTEL && !MOTOROLA)
	head.dy = swap_int(head.dy);
	memcpy(&dumhead[8], &head.dy, 4);

	if(INTEL && !MOTOROLA)
	head.xmin = swap_double(head.xmin);
	memcpy(&dumhead[16], &head.xmin, 8);
	if(INTEL && !MOTOROLA)
	head.xmax = swap_double(head.xmax);
	memcpy(&dumhead[24], &head.xmax, 8);

	if(INTEL && !MOTOROLA)
	head.ymin = swap_double(head.ymin);
	memcpy(&dumhead[32], &head.ymin, 8);
	if(INTEL && !MOTOROLA)
	head.ymax = swap_double(head.ymax);
	memcpy(&dumhead[40], &head.ymax, 8);
	
	if(INTEL && !MOTOROLA)
	if(INTEL && !MOTOROLA)
	head.xstep = swap_double(head.xstep);
	memcpy(&dumhead[48], &head.xstep, 8);
	if(INTEL && !MOTOROLA)
	head.ystep = swap_double(head.ystep);
	memcpy(&dumhead[56], &head.ystep, 8);

	memcpy(&dumhead[64], &head.data_type, 1);
	memcpy(&dumhead[65], &head.rgb_bw, 1);
	memcpy(&dumhead[66], &head.projection, 1);
	memcpy(&dumhead[67], &head.ellipsoid, 1);

	if(INTEL && !MOTOROLA)
	head.proj_merid = swap_float(head.proj_merid);
	memcpy(&dumhead[68], &head.proj_merid, 4);

	if(INTEL && !MOTOROLA)
	head.sun_elev = swap_float(head.sun_elev);
	memcpy(&dumhead[72], &head.sun_elev, 4);
	if(INTEL && !MOTOROLA)
	head.sun_azi = swap_float(head.sun_azi);
	memcpy(&dumhead[76], &head.sun_azi, 4);
	if(INTEL && !MOTOROLA)
	head.sun_exag = swap_float(head.sun_exag);
	memcpy(&dumhead[80], &head.sun_exag, 4);

	if(INTEL && !MOTOROLA)
	head.view_elev = swap_float(head.view_elev);
	memcpy(&dumhead[84], &head.view_elev, 4);
	if(INTEL && !MOTOROLA)
	head.view_azi = swap_float(head.view_azi);
	memcpy(&dumhead[88], &head.view_azi, 4);
	if(INTEL && !MOTOROLA)
	head.view_exag = swap_float(head.view_exag);
	memcpy(&dumhead[92], &head.view_exag, 4);

	if(INTEL && !MOTOROLA)
	head.max_value = swap_float(head.max_value);
	memcpy(&dumhead[96], &head.max_value, 4);
	if(INTEL && !MOTOROLA)
	head.min_value = swap_float(head.min_value);
	memcpy(&dumhead[100], &head.min_value, 4);

	memcpy(&dumhead[104], &head.hemisphere, 1);
	memcpy(&dumhead[105], &head.unass2, 1);
	memcpy(&dumhead[106], &head.unass3, 1);
	memcpy(&dumhead[107], &head.unass4, 1);
	memcpy(&dumhead[108], head.unassigned, 4);
	memcpy(&dumhead[112], head.label_1, 72);
	memcpy(&dumhead[184], head.label_2, 72);
	memcpy(&dumhead[256], head.rgb_cmap, 768);

	fseek(file,0l,0);
       return (fwrite(dumhead, 1024,1, file));

}

/*-   newer more rational one   -------------------------------------*/
int  read_JHC_header(FILE *file, JHC_header *head)
{
  char dumhead[1024];
  int readin;

	fseek(file,0l,0);

	readin = fread(dumhead, 1024, 1, file);
	if(readin != 1) return(readin);

	memcpy(head->header_name, &dumhead[0],4);

	memcpy(&head->dx, &dumhead[4],4);
	if(INTEL && !MOTOROLA)
	head->dx = swap_int(head->dx);
	memcpy(&head->dy, &dumhead[8],4);
	if(INTEL && !MOTOROLA)
	head->dy = swap_int(head->dy);

	memcpy(&head->xmin, &dumhead[16],8);
	if(INTEL && !MOTOROLA)
	head->xmin = swap_double(head->xmin);
	memcpy(&head->xmax, &dumhead[24],8);
	if(INTEL && !MOTOROLA)
	head->xmax = swap_double(head->xmax);

	memcpy(&head->ymin, &dumhead[32],8);
	if(INTEL && !MOTOROLA)
	head->ymin = swap_double(head->ymin);
	memcpy(&head->ymax, &dumhead[40],8);
	if(INTEL && !MOTOROLA)
	head->ymax = swap_double(head->ymax);
	
	memcpy(&head->xstep, &dumhead[48],8);
	if(INTEL && !MOTOROLA)
	head->xstep = swap_double(head->xstep);
	memcpy(&head->ystep, &dumhead[56],8);
	if(INTEL && !MOTOROLA)
	head->ystep = swap_double(head->ystep);

	memcpy(&head->data_type, &dumhead[64],1);
	memcpy(&head->rgb_bw, &dumhead[65],1);
	memcpy(&head->projection, &dumhead[66],1);
	memcpy(&head->ellipsoid, &dumhead[67],1);

	memcpy(&head->proj_merid, &dumhead[68],4);
	if(INTEL && !MOTOROLA)
	head->proj_merid = swap_float(head->proj_merid);

	memcpy(&head->sun_elev, &dumhead[72],4);
	if(INTEL && !MOTOROLA)
	head->sun_elev = swap_float(head->sun_elev);
	memcpy(&head->sun_azi, &dumhead[76],4);
	if(INTEL && !MOTOROLA)
	head->sun_azi = swap_float(head->sun_azi);
	memcpy(&head->sun_exag, &dumhead[80],4);
	if(INTEL && !MOTOROLA)
	head->sun_exag = swap_float(head->sun_exag);

	memcpy(&head->view_elev, &dumhead[84],4);
	if(INTEL && !MOTOROLA)
	head->view_elev = swap_float(head->view_elev);
	memcpy(&head->view_azi, &dumhead[88],4);
	if(INTEL && !MOTOROLA)
	head->view_azi = swap_float(head->view_azi);
	memcpy(&head->view_exag, &dumhead[92],4);
	if(INTEL && !MOTOROLA)
	head->view_exag = swap_float(head->view_exag);

	memcpy(&head->max_value, &dumhead[96],4);
	if(INTEL && !MOTOROLA)
	head->max_value = swap_float(head->max_value);
	memcpy(&head->min_value, &dumhead[100],4);
	if(INTEL && !MOTOROLA)
	head->min_value = swap_float(head->min_value);

	memcpy(&head->hemisphere, &dumhead[104],1);
	memcpy(&head->unass2, &dumhead[105],1);
	memcpy(&head->unass3, &dumhead[106],1);
	memcpy(&head->unass4, &dumhead[107],1);
	memcpy(head->unassigned, &dumhead[108],4);
	memcpy(head->label_1, &dumhead[112],72);
	memcpy(head->label_2, &dumhead[184],72);
	memcpy(head->rgb_cmap, &dumhead[256],768);

	if (strncmp(head->header_name, "OMG1",4)) return(-1);

	return(readin);
}
/*----------------------------------------------------------------*/

JHC_header  get_print_JHC_header(FILE *file, int print_flag)
{
  JHC_header  head;
  char dumhead[1024];
	fseek(file,0l,0);

	if(read_JHC_header(file, &head) !=1)
		error("failed on readprint JHC head ");

	if(print_flag) {
	printf("%c%c%c%c\n", head.header_name[0], 
			head.header_name[1], 
			head.header_name[2], 
			head.header_name[3]);
	printf("  OMG1 array header data  ---- %d %d - %f %f %f %f - %f %f\n", 
			head.dx,
			head.dy,
			head.xmin,
			head.xmax,
			head.ymin,
			head.ymax,
			head.xstep,
			head.ystep);

	if(head.data_type == 0) printf("undefined data type ");
	else if(head.data_type == 1) printf("8bit data type ");
	else if(head.data_type == 2) printf("16bit data type ");
	else if(head.data_type == 3) printf("floating point data type ");
	else if(head.data_type  > 3) printf("unknown data type ");

	printf(" -- ");

	if(head.rgb_bw == 0) printf("undefined rgb_bw  ");
	else if(head.rgb_bw == 1) printf("red band data  ");
	else if(head.rgb_bw == 2) printf("green band data  ");
	else if(head.rgb_bw == 3) printf("blue band data  ");
	else if(head.rgb_bw == 4) printf("greyscale data  ");
	else if(head.rgb_bw == 5) printf("false color data (8-30)  ");
	else if(head.rgb_bw == 6) printf("false color data (10-24)  ");
	else if(head.rgb_bw == 7) printf("false color data (12-20)  ");
	else if(head.rgb_bw  > 7) printf("unknown rgb_bw  ");

	printf(" -- ");

	if(head.projection == 0) printf("Projection undefined  ");
	else if(head.projection == 1) printf("Mercator Projection  ");
	else if(head.projection == 2) {
				printf("UTM Projection  ");
	   if(head.hemisphere == 0) printf(" Northern Hemisphere\n");
	   else                     printf(" Southern Hemisphere\n");
	}
	else if(head.projection == 3) printf("gridded in minutes   ");
	else if(head.projection == 4) {
				printf("Rotated UTM projection ");
	   if(head.hemisphere == 0) printf(" Northern Hemisphere\n");
	   else                     printf(" Southern Hemisphere\n");
	}
	else if(head.projection == 5) printf("Rotated Mercator projection ");
	else if(head.projection  > 5) printf("unknown  Projection  ");

	printf(" -- ");

	if(head.ellipsoid == 0) printf("Ellipsoid undefined  ");
	else if(head.ellipsoid == 1) printf(" WGS84 Ellipsoid  ");
	else if(head.ellipsoid == 2) printf(" NAD27 Ellipsoid  ");
	else if(head.ellipsoid == 3) printf(" CLK66 Ellipsoid  ");
	else if(head.ellipsoid == 4) printf(" AGD84 Ellipsoid  ");
	else if(head.ellipsoid  > 4) printf(" Unknown Ellipsoid  ");

	printf("\n");

	printf(" merid -- %f- sun - %f %f %f - view - %f %f %f\n", 
			head.proj_merid,
			head.sun_elev,
			head.sun_azi,
			head.sun_exag,
			head.view_elev,
			head.view_azi,
			head.view_exag);

	printf(" max %f min %f\n", head.max_value, head.min_value);

/*
	printf("label 1: %72s\n", head.label_1);
	printf("label 2: %72s\n", head.label_2);
	printf("not printing header cos DEC are stuffed\n\n");
*/
	}

	return(head);
}
 
/*----------------------------------------------------------------------
	derived from 
	r4to8bit.c		-	John Hughes Clarke March, 1990

	converts a jhc_format r*4 (float) array to a 8bit  array.
----------------------------------------------------------------------*/

void packto8bit(float *fbuf,  int novals, unsigned char *bbuf, 
			float low_limit, float high_limit)
{

	int  i;
	unsigned char  *q;
        float *p;
        int temp;

  

	if(low_limit ==0.0 && high_limit==0.0) {
	  printf("finding min and max\n");
	  low_limit = 10000; high_limit = -10000;
	  for (i=0;i<novals;i++) {
	      if(*(fbuf+i) != 0.0) {
	        if(low_limit > *(fbuf+i)) low_limit = *(fbuf+i);
	        if(high_limit < *(fbuf+i)) high_limit = *(fbuf+i);
	      }
	  }
	  printf("min %f max %f\n", low_limit, high_limit);
	}


	for (i=0,p=fbuf,q=bbuf;i<novals;i++,p++,q++) {
		     if(*p != 0.0) {
			temp = ((*p -low_limit)*255)/(high_limit-low_limit);
           		if(temp > 255) temp = 255;
           		if(temp < 0) temp = 0;
			*q = (unsigned char)temp;
		    } else {
			*q=0;
		    }
        }




}

/*---------------------------------------------------------------------
	derived from 
        addSUN.c                -       John Hughes Clarke July, 1991

        takes a float array and makes a float array of cos of the angle
between the sun and normal to the face.

	basically a lambertian surface


	runs really slow compared to addSUN, not sure why.....

----------------------------------------------------------------------*/


void get_8bitSUN(float *JHCbuf,  JHC_header JHChead, 
		 float azi, float elev, float vert_exag,
		  unsigned char *grey_buf)
{
        float SUNlambert;
        float value;
        int i,j;
        float angle,  peast, psouth, pixel;

	printf("doing Sun Illumination : %f %f %f\n",
		azi, elev, vert_exag);

        for(i=0;i<JHChead.dy-1;i++){
          for(j=0;j<JHChead.dx-1;j++){
            pixel  = *(JHCbuf+JHChead.dx*i+j);
            peast  = *(JHCbuf+JHChead.dx*i+j+1);
            psouth = *(JHCbuf+JHChead.dx*(i+1)+j);
            if(pixel && peast && psouth) {

              angle =  vectangle( JHChead.xstep / vert_exag,
                                elev*M_PI/180.0,
                                azi*M_PI/180.0,
                                pixel, peast, psouth);
	      if(angle == -1) {
                SUNlambert = -0.0001;
                /* cant illuminate surface */
              } else {
                SUNlambert = cos(angle * M_PI/180.0);
                        /* intensity is cosine of angle between
                           sun and normal to plane */
                if(SUNlambert == 0.0) SUNlambert = 0.0001;
              }
            } else {
              SUNlambert = 0.0;
                /* not enough data to define slope */
            }
                value = SUNlambert *255.0;
                if(SUNlambert == 0.0)  *(grey_buf+JHChead.dx*i+j) = 255;
                else if(value < 0) *(grey_buf+JHChead.dx*i+j) = 0;
                else if(value > 254)*(grey_buf+JHChead.dx*i+j) = 254;
                else   *(grey_buf+JHChead.dx*i+j) = (unsigned char) value;
          }
          if(!(i%10)) printf("done sun %d of %d\n", i, JHChead.dy);

        }


}
void get_2D_profile(float *fdata, JHC_header *head, bin_vect coords,
                int *nosam, float *step, float *profile,
		float *min, float *max)
{

float xloc, yloc;
float xstep, ystep;
float dx, dy;
float dist;
int i;
double temp, angle;


	*min = 100000000.0;
	*max = -100000000.0;


	/* make sure x1 and y1 are the larger values */
/*
	if(coords.x2 > coords.x1) {
	  temp = coords.x2;
	  coords.x2 = coords.x1;
	  coords.x1 = temp;
	}
	if(coords.y2 > coords.y1) {
	  temp = coords.y2;
	  coords.y2 = coords.y1;
	  coords.y1 = temp;
	}
*/
	dist = sqrt(pow((coords.x1-coords.x2),2.0) +
	            pow((coords.y1-coords.y2),2.0));

	printf(" dist %f 1 %f %f 2 %f %f\n",
		dist,
		coords.x1, coords.y1,
		coords.x2, coords.y2);


/* Unless you specify a preferred number of samples (by passing this value
   as a negative number to the subroutine, it will give you samples spaced
	by the grid interval */
	if(*nosam >= 0) {
		*nosam = (int)dist;
		*step = 1.0;	
	} else {
		*nosam = -(*nosam);
		*step = dist/(float)(*nosam);
	}


	dx = coords.x1-coords.x2;
	dy = coords.y1-coords.y2;
	  xstep = *step *dx/dist;
	  ystep = *step *dy/dist;

/*
	angle = atan((coords.x1-coords.x2)/(coords.y1-coords.y2));
	xstep = *step * sin(angle);
	ystep = *step * cos(angle);
*/

	for(i=0;i<(*nosam);i++) {
	  xloc = coords.x2 + xstep*(double)i;
	  yloc = coords.y2 + ystep*(double)i;
	  *(profile+i) =
		get_JHC_lookup_value(head, fdata,
			-yloc * head->ystep, xloc * head->xstep); 
	  if(*(profile+i)) {
	    if(*(profile+i) > *max ) *max = *(profile+i);
	    if(*(profile+i) < *min ) *min = *(profile+i);
	  } else {
		/* redundant */
	    *(profile+i) =0.0;
	  }
	}

	*step *= head->xstep;	

	printf("Profile minimum : %f maximum %f\n", *min, *max);
	printf("Profile step : %f num %d\n", *step, *nosam);


}


/* ----------------------- */
int is_swathfile_inarea(JHC_header *arr_head, FILE *file, int exact)
{
double  maxtime, mintime;
double  maxlat, minlat, maxlon, minlon;
int inlat, inlon;
OMG_HDCS_summary_header  summary;
OMG_HDCS_profile_header  profile;
OMG_HDCS_beam            beams[500];
int no_beams;

double array_tlat= -90.0, array_blat= 90.0,
        array_tlon= 180.0, array_blon= -180.0;
double coords[4][2];
double x,y;
int i,k;
int ilat, ilon, itlat, itlon, iblat, iblon;




        if(arr_head->projection !=1) {
        inv_proj(0, 0,
                &coords[0][0], &coords[0][1], &x, &y,  arr_head);
        inv_proj(0, arr_head->dy,
                &coords[1][0], &coords[1][1], &x, &y,  arr_head);
        inv_proj(arr_head->dx, 0,
                &coords[2][0], &coords[2][1], &x, &y,  arr_head);
        inv_proj(arr_head->dx, arr_head->dy,
                &coords[3][0], &coords[3][1], &x, &y,  arr_head);

        for(i=0;i<4;i++) {
          if(coords[i][0] > array_blon) array_blon = coords[i][0];
          if(coords[i][0] < array_tlon) array_tlon = coords[i][0];
          if(coords[i][1] > array_tlat) array_tlat = coords[i][1];
          if(coords[i][1] < array_blat) array_blat = coords[i][1];
        }
/*
          printf(" approximate geographic bounding box is : %f %f %f %f\n",
                array_blat, array_tlat, array_tlon, array_blon);
*/
        } else {
          array_tlat= arr_head->ymax;
          array_blat= arr_head->ymin;
          array_tlon= arr_head->xmin;
          array_blon= arr_head->xmax;
        }



        OMG_HDCS_read_summary(file, &summary);
        mintime = ((double)summary.minTime)
                        * ((double)summary.timeScale) *1.0e-6
                        + ((double)summary.refTime)*100.0;
        minlat = (180.0/M_PI) * ( (double)summary.refLat*1.0E-7 +
                 (double)summary.minLat*1.0E-9*(double)summary.positionScale);
        minlon = (180.0/M_PI) * ( (double)summary.refLong*1.0E-7 +
                 (double)summary.minLong*1.0E-9*(double)summary.positionScale);
        maxtime = ((double)summary.maxTime)
                        * ((double)summary.timeScale) *1.0e-6
                        + ((double)summary.refTime)*100.0;
        maxlat = (180.0/M_PI) * ( (double)summary.refLat*1.0E-7 +
                 (double)summary.maxLat*1.0E-9*(double)summary.positionScale);
        maxlon = (180.0/M_PI) * ( (double)summary.refLong*1.0E-7 +
                 (double)summary.maxLong*1.0E-9*(double)summary.positionScale);


        inlat = inlon = 0;
        if( array_tlat > minlat && array_tlat < maxlat) inlat++;
        if( array_blat > minlat && array_blat < maxlat) inlat++;
        if( minlat > array_blat && maxlat < array_tlat) inlat++;

        if( array_tlon > minlon && array_tlon < maxlon) inlon++;
        if( array_blon > minlon && array_blon < maxlon) inlon++;
        if( minlon > array_tlon && maxlon < array_blon) inlon++;




        if(!inlat || !inlon)  return(0);

	else if(!exact)  return(1);

        /* option if you want to see if there is actually beams
        in bounds as opposed to intersecting bounds,
          note will abandon as soon as the first beam is found */
	else  {

          itlat = (array_tlat* (M_PI/180.0) *1.0E09) -
                                ((double)summary.refLat*100.0);
          iblat = (array_blat* (M_PI/180.0) *1.0E09) -
                                ((double)summary.refLat*100.0);
          itlon = (array_tlon* (M_PI/180.0) *1.0E09) -
                                ((double)summary.refLong*100.0);
          iblon = (array_blon* (M_PI/180.0) *1.0E09) -
                                ((double)summary.refLong*100.0);
 
        k=0;
        for(k=0;k<summary.numProfiles;k++) {

        OMG_HDCS_read_profile(file, k, &profile);

        /* not bother with unnavigated data */
         if(profile.status != 2) {
            OMG_HDCS_get_raw_beams(file, k, &no_beams,
                beams);
            for(i=0;i<no_beams;i++) {
              inlat = inlon = 0;
              ilat = profile.vesselLatOffset +beams[i].latOffset;
              ilon = profile.vesselLongOffset +beams[i].longOffset;
              if( ilat > iblat && ilat < itlat) inlat++;
              if( ilon > itlon && ilon < iblon) inlon++;
              if(inlat && inlon) goto in_area;
            }
          }
        } /* eo while */

        in_area:;
        if(inlat && inlon) return(2);
        else 		   return(0);

	}




}

/* ----------------------- */
/* ----------------------- */

/* structure for binary vector data files  */
/*
typedef struct {
        double   x1;
        double   y1;
        double   x2;
        double   y2;
        int     color;
        int     ticktype;
        float   value;
                } bin_vect;



int  jread_vector(FILE *file, int recno, bin_vect *bvect)
{
}
int  jwrite_vector(FILE *file, int recno, bin_vect bvect)
{
}
*/ 

/* ----------------------- */
/* ----------------------- */


