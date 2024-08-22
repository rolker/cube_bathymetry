
/*----------------------------------------------------------------------
	ref_coeffs.c		-	John Hughes Clarke May, 1996


	functions for  empirical refraction coefficient software


----------------------------------------------------------------------*/
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include "ref_coeffs.h"
#include "j_phi.h"
#include "support.h"

int array_type;

float surf_draft;
float surf_sss_error;
float layer_depth;
float layer_ss_error;

double actual_beam_steered_angle[80];
double actual_beam_sublayer_angle[80];

int no_coeffs;

ref_coeff rcoeff[200];

/* ---------------------------------------------------- */

int read_ref_coeff_file(char *name)
{
FILE *coefffile;

        coefffile = fopen (name,"r");
/*
        if (!coefffile) error ("Error opening %s\n",name);
*/
        if (!coefffile) return(0);

        no_coeffs=0;
        while(fscanf(coefffile,"%d%f%f%f%f\n",
                        &rcoeff[no_coeffs].rec,
                        &rcoeff[no_coeffs].surf_draft,
                        &rcoeff[no_coeffs].surf_sss_error,
                        &rcoeff[no_coeffs].layer_depth,
                        &rcoeff[no_coeffs].layer_ss_error)== 5) {
        printf("%d - %f %f %f %f\n",
                        rcoeff[no_coeffs].rec,
                        rcoeff[no_coeffs].surf_draft,
                        rcoeff[no_coeffs].surf_sss_error,
                        rcoeff[no_coeffs].layer_depth,
                        rcoeff[no_coeffs].layer_ss_error);
          no_coeffs++;
        }

        fclose(coefffile);

	return (1);


}
/* ---------------------------------------------------- */
void get_coeffs(int recno, ref_coeff *rc)
{
  int i;
  int prev_rec, post_rec;
  float w1;

	if(recno <= rcoeff[0].rec) {
		memcpy(rc, &rcoeff[0], sizeof(ref_coeff));
	} else if (recno >= rcoeff[no_coeffs-1].rec) {
		memcpy(rc, &rcoeff[no_coeffs-1], sizeof(ref_coeff));
	} else {
	  i=0;
	  while(recno > rcoeff[i].rec) {
		i++;
	  }
		prev_rec=rcoeff[i-1].rec;
		post_rec =rcoeff[i].rec;
/*
		printf("bef %d this %d aft %d\n", prev_rec, recno, post_rec);
*/
		w1 = (float)(post_rec-recno)/(float)(post_rec-prev_rec);

	   rc->rec = recno;
	   rc->surf_draft = rcoeff[i-1].surf_draft*w1 + 
			    rcoeff[i].surf_draft*(1.0-w1);
	   rc->surf_sss_error = rcoeff[i-1].surf_sss_error*w1 + 
			    rcoeff[i].surf_sss_error*(1.0-w1);
	   rc->layer_depth = rcoeff[i-1].layer_depth*w1 + 
			    rcoeff[i].layer_depth*(1.0-w1);
	   rc->layer_ss_error = rcoeff[i-1].layer_ss_error*w1 + 
			    rcoeff[i].layer_ss_error*(1.0-w1);

/*
		printf(" w1 %f, lsse %f %f -- %f\n",
			w1, rcoeff[i-1].layer_ss_error, 
			rcoeff[i].layer_ss_error,
			rc->layer_ss_error);
*/

	}
}



/* ---------------------------------------------------- */

/* ---------------------------------------------------------------------*/
void calc_BeamSteering_Errors()
{
  int i,j;
  double intended_angle, actual_angle;

float f60DEG = 60.0*M_PI/180.0;
float f30DEG = 30.0*M_PI/180.0;

        for(i=0;i<80;i++) {
                if(array_type == HORIZ_LINE) {

                  intended_angle = (float)i* M_PI/180.0;
                  actual_angle = asin(sin(intended_angle)*
					(1500+surf_sss_error)/1500.0);
                  actual_beam_steered_angle[i] = actual_angle;

                } else if(array_type == EM1000_CURVED) {

                  if(i>60) {
                    intended_angle = (float)(i-60)* M_PI/180.0;
                    actual_angle = asin(sin(intended_angle)*
					(1500+surf_sss_error)/1500.0);
                    actual_beam_steered_angle[i] = f60DEG +actual_angle;
                  } else {
                    actual_beam_steered_angle[i] = (float)i* M_PI/180.0;
                  }

/*
                        printf("EM1000 %d %f %f\n", i,
                        actual_beam_steered_angle[i]*180.0/M_PI, 
			actual_beam_steered_angle[i]*180.0/M_PI-(float)i);
*/
                } else if(array_type == TWIN_30DEG_LINE) {

                  if(i>30) {
                    intended_angle = (float)(i-30)* M_PI/180.0;
                    actual_angle = asin(sin(intended_angle)*
					(1500+surf_sss_error)/1500.0);
                    actual_beam_steered_angle[i] = f30DEG +actual_angle;
                  } else {
                    intended_angle = (float)(30-i)* M_PI/180.0;
                    actual_angle = asin(sin(intended_angle)*
					(1500+surf_sss_error)/1500.0);
                    actual_beam_steered_angle[i] = f30DEG -actual_angle;
                  }

                }

        }




}

/* ---------------------------------------------------------------------*/
void calc_BeamTurning_Errors()
{
  int i,j;
  double intended_angle, actual_angle;


        for(i=0;i<80;i++) {
                intended_angle = (float)actual_beam_steered_angle[i];
                actual_angle = asin(sin(intended_angle)*
					(1500+layer_ss_error)/1500.0);
                actual_beam_sublayer_angle[i] = actual_angle;
        }


}

/* ---------------------------------------------------------------------*/
void set_up_ref_coefficients(ref_coeff *rc)
{
	surf_draft = rc->surf_draft;
	surf_sss_error = rc->surf_sss_error;
	layer_depth = rc->layer_depth;
	layer_ss_error = rc->layer_ss_error;

	calc_BeamSteering_Errors();
	calc_BeamTurning_Errors();
}
/* ---------------------------------------------------------------------*/
void get_refracted_location(float depth_in, float across_in,
                 float *depth_out, float *across_out)
{
double intended_depth, effective_layer_depth;
double intended_angle, actual_steered_angle, actual_sublayer_angle;
double w1, slant_range;
int iang;
float sign=1;

        if(across_in <0) sign=-1;

                intended_depth = depth_in +surf_draft;
                effective_layer_depth = layer_depth -surf_draft;
                intended_angle = atan(-fabs(across_in)/intended_depth) 
					*180.0/M_PI;
                w1 = intended_angle -(double)((int)intended_angle);
                iang= (int)intended_angle;
		if(iang >= 79) {
		*depth_out = *across_out = 0.0;
		return;
		}
                actual_steered_angle = actual_beam_steered_angle[iang]
					*(1.0-w1) +
                                       actual_beam_steered_angle[iang+1]
					*w1;
                actual_sublayer_angle = actual_beam_sublayer_angle[iang]
					*(1.0-w1) +
                                       actual_beam_sublayer_angle[iang+1]
					*w1;

                slant_range = sqrt(intended_depth*intended_depth + 
					across_in*across_in);
                *depth_out = -slant_range *  cos(actual_steered_angle);
                *across_out = slant_range *  sin(actual_steered_angle);


        if(fabs(*depth_out) > effective_layer_depth) {
                /* adjusting to find how much slantrange/time 
					used up in getting
                        to the layer */

                *across_out *= effective_layer_depth/fabs(*depth_out);
                *depth_out = -effective_layer_depth;


                /* find out how much of the slantrange/time to go */
                slant_range = sqrt(intended_depth*intended_depth + 
					across_in*across_in);
                slant_range -= effective_layer_depth/ cos(actual_steered_angle);
                *depth_out -= slant_range *  cos(actual_sublayer_angle);
                *across_out += slant_range *  sin(actual_sublayer_angle);
        }

                *across_out *= sign;
                *depth_out -= surf_draft;

        if(depth_in/(*depth_out) > 1.1 || depth_in/(*depth_out) < 0.9)
                printf("dodgy inside  %f %f -- %f %f -- %f %f intended %f\n",
                         depth_in, *depth_out, across_in, *across_out,
			sign, surf_draft, intended_angle);



}

/* ---------------------------------------------------------------------*/

/* ---------------------------------------------------- */


