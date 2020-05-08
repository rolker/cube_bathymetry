/*----------------------------------------------------------------------

	DCM_rthand.c	-     John Hughes Clarke   -   Sept , 1995

	given a lever arm in which 

	y is forward, 
	x is to the right
	z is down.
	a positive pitch is bow up
	a positive roll is port up
	a positive yaw is a turn to right.

	tests out ROLL, PITCH YAW rotations



----------------------------------------------------------------------*/

#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include "DCM_rthand.h"
#include "j_phi.h"
#include "support.h"

int rotation_sequence;

void DCM_rotatef(fvect origf, attitude rotate, fvect *finalf)
{
location origd, finald;

	origd.x = origf.x;
	origd.y = origf.y;
	origd.z = origf.z;
	finald.x = finalf->x;
	finald.y = finalf->y;
	finald.z = finalf->z;
	DCM_rotate(origd, rotate, &finald);
	finalf->x = finald.x;
	finalf->y = finald.y;
	finalf->z = finald.z;

}
void DCM_unrotatef(fvect origf, attitude rotate, fvect *finalf)
{
location origd, finald;

	origd.x = origf.x;
	origd.y = origf.y;
	origd.z = origf.z;
	finald.x = finalf->x;
	finald.y = finalf->y;
	finald.z = finalf->z;
	DCM_unrotate(origd, rotate, &finald);
	finalf->x = finald.x;
	finalf->y = finald.y;
	finalf->z = finald.z;

}
void DCM_rotate(location orig, attitude rotate, location *final)
{
double cosr,cosp,cosy;
double sinr,sinp,siny;

	sinr = sin(-rotate.roll*M_PI/180.0); cosr = cos(-rotate.roll*M_PI/180.0);
	sinp = sin(-rotate.pitch*M_PI/180.0); cosp = cos(-rotate.pitch*M_PI/180.0);
	siny = sin(-rotate.yaw*M_PI/180.0); cosy = cos(-rotate.yaw*M_PI/180.0);


        final->x =
                cosp*cosy*orig.x +
                cosp*siny*orig.y -
                sinp*orig.z;

        final->y =
                (sinr*sinp*cosy-cosr*siny)*orig.x +
                (sinr*sinp*siny+cosr*cosy)*orig.y +
                sinr*cosp*orig.z;

        final->z =
                (cosr*sinp*cosy+sinr*siny)*orig.x +
                (cosr*sinp*siny-sinr*cosy)*orig.y +
                cosr*cosp*orig.z;

} 
void DCM_unrotate(location orig, attitude rotate, location *final)
{
double cosr,cosp,cosy;
double sinr,sinp,siny;

	sinr = sin(-rotate.roll*M_PI/180.0); cosr = cos(-rotate.roll*M_PI/180.0);
	sinp = sin(-rotate.pitch*M_PI/180.0); cosp = cos(-rotate.pitch*M_PI/180.0);
	siny = sin(-rotate.yaw*M_PI/180.0); cosy = cos(-rotate.yaw*M_PI/180.0);


        final->x =
                cosp*cosy*orig.x +
                (sinr*sinp*cosy-cosr*siny)*orig.y +
                (cosr*sinp*cosy+sinr*siny)*orig.z; 

        final->y =
		cosp*siny*orig.x +
                (sinr*sinp*siny+cosr*cosy)*orig.y +
                (cosr*sinp*siny-sinr*cosy)*orig.z; 

        final->z =
                -sinp*orig.x +
                sinr*cosp*orig.y +
                cosr*cosp*orig.z;

} 

 

