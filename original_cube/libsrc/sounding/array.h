/*----------------------------------------------------------------------

	array.h	-	John Hughes Clarke	-	July 31, 1991

	include file for array handling code .

----------------------------------------------------------------------*/

#ifndef array_
#define array_

#include <stdio.h>
#include <ctype.h>
#ifndef MAXDOUBLE
#include <math.h>
#endif

#define JHC_OMG_RECLEN 1024
#define JHC_VECT_RECLEN 44

/* structure for JHC OMG format arrays */
typedef struct  {
		char    header_name[4]/*="OMG1"*/;
		int		dx;
		int 	dy;
		double	xmin;
		double	xmax;
		double	ymin;
		double	ymax;
		double	xstep; /* metres if projected, else minutes */
		double  ystep; /* " */

		unsigned char   data_type;  /* 1=8bit, 2=16bit, 3=float */
		unsigned char   rgb_bw; /* 1:r, 2:g, 3:b, 4:bw 
						5:false  8-30
						6:false 10-24
						7:false  12-20 */

		unsigned char   projection; /* 0:None  1:Mercator,  2:UTM , 
					       3: no proj, gridded in minutes*/
						/* 4: UTM rotated 
						   5: Mercator rotated */
		unsigned char   ellipsoid; /*  1:WGS84    */
		float   proj_merid;

		float   sun_elev;
		float   sun_azi;
		float   sun_exag;

		float   view_elev;
		float   view_azi;
		float   view_exag;

		float   max_value;
		float   min_value;

		char  label_1[72];
		char  label_2[72];

		char  hemisphere;
		char  unass2;
		char  unass3;
		char  unass4;
		char  unassigned[4];

		char rgb_cmap[768];
		} JHC_header;

/* structure for Smith and Weissel LDGO format arrays */
typedef struct  {
		int	dx;
		int 	dy;
		double	xmin;
		double	xmax;
		double	ymin;
		double	ymax;
		double	xinc;
		double  yinc;
		} SW_header;


/* structure for binary vector data files  */
typedef struct {
        double   x1;
        double   y1;
        double   x2;
        double   y2;
        int     color;
        int     ticktype;
        float   value;
                } bin_vect;



/* old no error one */
JHC_header  get_print_JHC_header(FILE *file, int print_flag);
/* to be replaced with .. */
int  read_JHC_header(FILE *file, JHC_header *head);
int  write_JHC_header(FILE *file, JHC_header head);


SW_header  get_print_SW_header(FILE *file, int print_flag);
float   get_JHC_lookup_value(JHC_header *head, float *array_pointer, 
			float depth, 
			float range);

float   interp_JHCarray_value(JHC_header *head, float *array_pointer,
                        float x,
                        float y);

/*
int  jread_vector(FILE *file, int recno, bin_vect *bvect);
int  jwrite_vector(FILE *file, int recno, bin_vect bvect);
*/ 

void packto8bit(float *fbuf,  int novals, unsigned char *bbuf,
                        float low_limit, float high_limit);


void get_8bitSUN(float *JHCbuf,  JHC_header JHChead,
                 float azi, float elev, float vert_exag,
                  unsigned char *grey_buf);

void get_2D_profile(float *fdata, JHC_header *head, bin_vect coords,
                int *nosam, float *step, float *profile,
                float *min, float *max);


int is_swathfile_inarea(JHC_header *head, FILE *file, int exact);

#endif
