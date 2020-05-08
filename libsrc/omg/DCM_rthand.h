
/*----------------------------------------------------------------------

	DCM_rthand.h	-     John Hughes Clarke   -   Sept , 1995

	Include for DCM stuff

	given a lever arm in which 

	y is forward, 
	x is to the right
	z is down.
	a positive pitch is bow up
	a positive roll is port up
	a positive yaw is a turn to right.

	tests out ROLL, PITCH YAW rotations



----------------------------------------------------------------------*/


#ifndef DCM_rthand_
#define DCM_rthand_


/* for testing in synSwath, you can alter the rotation order
we do not have the matrix functino of all of these (only the first
Tate Bryant) as that is what we really use */
/* the others are used to change the rotation order using OpenGL */

extern int rotation_sequence;

#define ROT_YPR_TB 0
#define ROT_HIPPY 1
#define ROT_YRP 2
#define ROT_PRY 3
#define ROT_RPY 4
#define ROT_RYP 5
#define ROT_PYR 6




typedef struct  {
	double roll;
	double pitch;
	double yaw;
	} attitude;

typedef struct  {
	double x;
	double y;
	double z;
	} location;

typedef struct  {
	float x;
	float y;
	float z;
	} fvect;

typedef struct  {
	short x;
	short y;
	short z;
	} svect;

typedef struct  {
	float I;
	float Q;
	} fphase;



void DCM_rotate(location orig, attitude rotate, location *final); 
void DCM_unrotate(location orig, attitude rotate, location *final);

void DCM_rotatef(fvect orig, attitude rotate, fvect *final); 
void DCM_unrotatef(fvect orig, attitude rotate, fvect *final);


 

#endif


