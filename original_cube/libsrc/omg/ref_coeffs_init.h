int array_type;

float surf_draft;
float surf_sss_error;
float layer_depth;
float layer_ss_error;

double actual_beam_steered_angle[80];
double actual_beam_sublayer_angle[80];

int no_coeffs;

#ifndef ref_coeff
typedef struct  {
		int rec;
		float surf_draft;
		float surf_sss_error;
		float layer_depth;
		float layer_ss_error;
} ref_coeff;
#endif

ref_coeff rcoeff[200];
