
/*----------------------------------------------------------------------
	ref_coeffs.h		-	John Hughes Clarke May, 1996


	include file of all variables for empirical refraction
	coefficients for fixing refraction artifacts for which we don't 
	know the cause


----------------------------------------------------------------------*/

#ifndef ref_coeffs_
#define ref_coeffs_


#define HORIZ_LINE 0
#define EM1000_CURVED 1
#define TWIN_30DEG_LINE 2

extern int array_type;

extern float surf_draft;
extern float surf_sss_error;
extern float layer_depth;
extern float layer_ss_error;

extern double actual_beam_steered_angle[80];
extern double actual_beam_sublayer_angle[80];

typedef struct  {
		int rec;
		float surf_draft;
		float surf_sss_error;
		float layer_depth;
		float layer_ss_error;
} ref_coeff;

extern int no_coeffs;
extern ref_coeff rcoeff[200];



/* ---------------------------------------------------- */

int read_ref_coeff_file(char *name);
void get_coeffs(int recno, ref_coeff *rc);
void set_up_ref_coefficients(ref_coeff *rc);
void get_refracted_location(float depth_in, float across_in,
                 float *depth_out, float *across_out);
void calc_BeamTurning_Errors();
void calc_BeamSteering_Errors();

/* ---------------------------------------------------- */

#endif
