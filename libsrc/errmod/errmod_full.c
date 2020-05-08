/*
 * $Id: errmod_full.c 15 2003-07-23 00:39:41Z brc $
 * $Log$
 * Revision 1.2  2003/07/23 00:39:41  brc
 * Merged bug fixes for minor parts of the algorithm (from K. Wing
 * Wong @ CARIS), and modified beam pointing error algorithm so
 * that it does what it's s'pos'd to.
 *
 * Revision 1.6.2.4  2003/07/22 21:31:37  brc
 * Added debug message to assist in following the hor_txerr computation.
 *
 * Revision 1.6.2.3  2003/05/20 22:01:29  brc
 * Modified in light of bug reports by K. Wing Wong at CARIS:
 * 1.  Eqn. 3.77 was squared when it shouldn't have been.
 * 2.  Eqn. 3.73 had a poor implementation that caused problems
 *     with asymmetrically mounted transducers and some dual-head
 *     implementations.
 *
 * Revision 1.1.1.1  2003/02/03 20:18:42  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2.4.1  2003/01/28 14:29:51  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.6.2.2  2002/12/15 01:30:54  brc
 * Added debug commentary.
 *
 * Revision 1.6.2.1  2002/07/14 02:20:46  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.6  2002/05/10 20:15:06  brc
 * Fixed bug in computation of Eqn 3.86 -- tx_rel.  This was due to an
 * incorrect transliteration of the equation in std. dev. form into variance
 * (a.k.a. momentary brain freeze on the part of the coder).
 *
 * Revision 1.5  2002/03/14 04:24:21  brc
 * Removed specification of static angle, since this is now defined in the
 * vessel description on a per-head basis (as it always should have been).
 * Fixed bug in use of static roll offsets which resulted from a confusion over
 * whether degrees or radians were being used.
 *
 * Revision 1.4  2001/12/07 20:51:23  brc
 * Added facility to have static roll offset specified in the parameter stack,
 * and to use it to correct launch angles w.r.t. the face plate into launch
 * angles w.r.t. the vertical.  This is required if the transducer is mounted
 * offset (e.g., in dual-head systems).
 *
 * Revision 1.3  2001/10/15 23:06:50  brc
 * Added more debugging to fix problem with wild horizontal errors when the
 * SoG reported is not valid.
 *
 * Revision 1.2  2001/05/14 04:19:32  brc
 * Updated to make modules 'params'-aware.
 *
 * Revision 1.1  2001/02/10 18:02:54  brc
 * Added routines to compute error estimates in various modes.  The errmod.c
 * routines are user-level interface; errmod_iho.c implements the basic `error
 * as a function of depth' idea inherent in IHO V3; errmod_full.c implements
 * the full MBES error model in all details.
 *
 *
 * File:	errmod_full.c
 * Purpose:	Compute errors for soundings with full MBES error model
 * Date:	24 November 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <float.h>
#include <limits.h>

#include "stdtypes.h"
#include "error.h"

#include "device.h"
#include "vessel.h"
#include "sounding.h"

#include "errmod.h"
#include "errmod_full.h"

static char *modname = "errmod_full";
static char *modrev = "$Revision: 15 $";

#undef __DEBUG__
#undef __NEVER_DEFINE__

/* As with isnan(), finite() is known as _finite() in Win32. */

#ifdef WIN32
#define finite _finite
#endif

#define DEG2RAD(x)	((x)*M_PI/180.0)
#define RAD2DEG(x)	((x)*180.0/M_PI)
#define DEFAULT_SURF_SSPEED	1500.0	/* Default surface sound speed for beam
									 * angle computations in absence of real
									 * data from the tx head. m/s.
									 */

#define MIN_SURF_SSPEED	1300.0
#define MAX_SURF_SSPEED	1800.0


static f64 default_surf_sspeed = DEFAULT_SURF_SSPEED;

typedef struct _err_full {
	/* Static variables characteristic of vessel and instruments. */
	/* Part 1: Vertical error sources and components */
	f64		vert_reduction;		/* draft + water level reduction var in m. */
	f64		heave_fixed;		/* Fixed component of heave error */
	f64		base_roll_var;		/* Combination of roll meas. and align. err */
	f64		svp_var;			/* Profile SVP measurement variance */
	f64		along_beamw_coeff;	/* Coefficient of depth for along track
								 * beamwidth component (eqn. 3.51) */
	f64		total_pitch_var;	/* Static pitch error variance */
	f64		steering_angle;		/* Transition angle (rad) */
	f64		min_angle, max_angle;	/* Angles (rad) at which beams start to be
									 * steered.  This allows asymmetry as occurs
									 * where tdrs are offset by static roll.
									 */
	f64		surf_speed_var;		/* Variance in measurement of speed of sound
								 * at the transducer, if beams are steered */
	
	/* Part 2: Horizontal error sources and components */
	f64		m_imu_off_var;		/* IMU offset determination variance, rad^2 */
	f64		m_gps_off_var;		/* GPS offset determination variance, rad^2 */
	f64		total_latency_var;	/* Variance of latency errors, s^2. */
	f64		total_gyro_var;		/* Static gyro error variance, rad^2. */
	f64		total_gps_var;		/* Nominal GPS accuracy, m^2. */
		
	/* Part 3: Common variables required for both sections */
	u32		nbeams;				/* Number of slots in beam_angles[] LUT */
	f64		*beam_angles;		/* LUT to map beam numbers to angles (rad) */
	
	/* Dynamic variables initialised on a per-ping basis */
	f64		total_roll_var;		/* Dynamically component total roll error */
	f64		total_heave_var;	/* Dynamic per ping heave error variance, m2 */
	f64		hor_latency_var;	/* Dynamic per ping latency error, m^2. */
	f64		hor_tx_relative_var;	/* Dynamic per ping GPS offset error, m^2 */
	f64		cosR, sinR, cosP, sinP;	/* Trig. values from platform orient. */
} ErrFull;

/* Routine:	errmod_full_compute_beams
 * Purpose:	Computes beam angles given the device and surface sound speed
 * Inputs:	device		Device tag to compute for
 *			sspeed		Surface sound speed (see comment)
 *			stat_roll	Static roll bias for the transducer
 *			*ws			ErrFull workspace to operate in
 * Outputs:	ws->beam_angles[] is allocated (if required) and initialised.
 *			Returns True if array is sucessfully initialised, otherwise False
 * Comment:	Note that the surface sound speed is only relevant if the system
 *			has steered beams.  If this is the case, then this routine should
 *			be called at the start of each swath's processing cycle with the
 *			current estimate of surface sound speed.  Otherwise, the values
 *			computed when this is called from the constructor are sufficient.
 */

static Bool errmod_full_compute_beams(Device device, f64 sspeed, f64 stat_roll,
								      ErrFull *ws)
{
	u32	beam, nbeams = device_get_nbeams(device);
	
	if (ws->nbeams != nbeams) {
		if (ws->beam_angles != NULL) free(ws->beam_angles);
		if ((ws->beam_angles = (f64p)malloc(sizeof(f64)*nbeams)) == NULL) {
			error_msgv(modname, "failed allocating LUT (%d beams).\n", nbeams);
			return(False);
		}
		ws->nbeams = nbeams;
	}
	for (beam = 0; beam < ws->nbeams; ++beam)
		ws->beam_angles[beam] =
			DEG2RAD(device_beam_to_angle(device, (f32)sspeed, beam) - stat_roll);
#ifdef __NEVER_DEFINE__
for (beam = 0; beam < ws->nbeams; ++beam)
	fprintf(stderr, "%le, ", ws->beam_angles[beam]);
fprintf(stderr, "\n");
#endif
	return(True);
}

/* Routine:	errmod_full_destruct
 * Purpose:	Release memory associated with full MBES computation engine
 * Inputs:	*ws	Pointer to workspace
 * Outputs:	-
 * Comment:	-
 */

void errmod_full_destruct(void *ws)
{
	ErrFull	*works = (ErrFull*)ws;
	free(works->beam_angles);
	free(works);
}

/* Routine:	errmod_full_construct
 * Purpose:	Generate internal workspace for the full MBES computation engine
 * Inputs:	device	Device in use for making soundings
 *			vessel	Vessel in which device is installed
 * Outputs:	Pointer to workspace (cast to void *) or NULL on error
 * Comment:	-
 */

void *errmod_full_construct(Device device, Vessel vessel)
{
	ErrFull	*rtn;
	f32		across_bw, along_bw;
	
	if ((rtn = (ErrFull*)calloc(1, sizeof(ErrFull))) == NULL) {
		error_msg(modname, "failed to allocate full error workspace.\n");
		return(NULL);
	}
	rtn->vert_reduction =
		vessel->draft_sdev * vessel->draft_sdev +
		vessel->ddraft_sdev * vessel->ddraft_sdev +
		vessel->loading_sdev * vessel->loading_sdev;	/* Eqn. 3.61 */
	rtn->vert_reduction +=
		vessel->tide_measured_sdev * vessel->tide_measured_sdev +
		vessel->tide_predicted_sdev * vessel->tide_predicted_sdev; /* 3.63 */

#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: vert_reduction = %lf\n", rtn->vert_reduction);
#endif

	rtn->heave_fixed = vessel->heave_fixed_sdev * vessel->heave_fixed_sdev;
#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: heave_fixed = %lf\n", rtn->heave_fixed);
#endif
	
	/* Compute some variances to save time later */
	rtn->base_roll_var = vessel->roll_sdev * vessel->roll_sdev +
						 vessel->imu_rp_align_sdev * vessel->imu_rp_align_sdev;
	rtn->base_roll_var *= DEG2RAD(1.0)*DEG2RAD(1.0);
	
	rtn->m_imu_off_var = vessel->imu_off_sdev * vessel->imu_off_sdev;
	rtn->m_gps_off_var = vessel->gps_off_sdev * vessel->gps_off_sdev;
	rtn->svp_var = vessel->svp_sdev * vessel->svp_sdev;
	
	device_get_beamwidths(device, &across_bw, &along_bw);
	rtn->along_beamw_coeff = (1.0 - cos(DEG2RAD(along_bw)/2.0))*
							 (1.0 - cos(DEG2RAD(along_bw)/2.0));
	
	rtn->total_pitch_var =
		vessel->pitch_sdev * vessel->pitch_sdev +
		vessel->imu_rp_align_sdev * vessel->imu_rp_align_sdev +
		vessel->pitch_stab_sdev * vessel->pitch_stab_sdev;
#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: total_pitch_var (deg^2) = %lf\n",
					rtn->total_pitch_var);
#endif

	/* Convert total pitch variance into rads */
	rtn->total_pitch_var *= DEG2RAD(1.0)*DEG2RAD(1.0);

	rtn->total_gyro_var = vessel->gyro_sdev * vessel->gyro_sdev +
						  vessel->imu_g_align_sdev * vessel->imu_g_align_sdev;
#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: total_gyro_var (deg^2) = %lf\n",	
					rtn->total_gyro_var);
#endif
					
	rtn->total_gyro_var *= DEG2RAD(1.0)*DEG2RAD(1.0);
	
	if ((device_get_properties(device) & DEVICE_IS_STEERED) != 0) {
		/* Device has beams which are electronically steered - so need to
		 * account for errors in surface sound speed measurement.
		 */
		rtn->surf_speed_var = vessel->surf_ss_sdev * vessel->surf_ss_sdev;
		if (device_get_steerangle(device) == 0.0) {
			/* Flat plate transducers --- min=max */
			rtn->min_angle = rtn->max_angle = -vessel->static_roll;
		} else {
			/* Conformal array with steering at outer limits */
			rtn->min_angle = -DEG2RAD(device_get_steerangle(device)) -
								vessel->static_roll;
			rtn->max_angle = DEG2RAD(device_get_steerangle(device)) -
								vessel->static_roll;
		}
#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: static roll = %le, min_steer = %le, max_steer = %le, sspeed_var = %le.\n",
	vessel->static_roll, rtn->min_angle, rtn->max_angle, rtn->surf_speed_var);
#endif
	}
	
	/* Compute total latency error */
	rtn->total_latency_var = vessel->gps_latency_sdev*vessel->gps_latency_sdev
							 + vessel->imu_latency_sdev*vessel->imu_latency_sdev
							 + vessel->tx_latency_sdev*vessel->tx_latency_sdev;
#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: total_latency_var (s^2) = %lf\n",
					rtn->total_latency_var);
#endif

	/* Compute base component of GPS error --- from measurements themselves */
	rtn->total_gps_var = vessel->gps_drms*vessel->gps_drms;
	
	/* Allocate space for, and then build, the beam number -> angle LUT */
	if (!errmod_full_compute_beams(device, default_surf_sspeed,
			vessel->static_roll, rtn)) {
		error_msg(modname, "failed allocating beam->angle LUT.\n");
		errmod_full_destruct(rtn);
		return(NULL);
	}
	
	return((void*)rtn);
}

/* Routine:	errmod_full_swath_depth
 * Purpose:	Compute measured depth error component
 * Inputs:	plat	Platform orientation
 *			*snd	Sounding under consideration
 *			vessel	Vessel configuration
 *			device	Device being used for sounding
 *			*ws		Workspace for the current configuration
 * Outputs:	Returns variance of total measured depth component of vert. error.
 * Comment:	This implements eqn. 3.52, and the various components which
 *			are required for it.
 */

static f64	errmod_full_swath_depth(Platform *plat, Sounding *snd,
									Vessel vessel, Device device, ErrFull *ws)
{
	f64	range_err,	/* Errors due to range measurement */
		angle_err,	/* Errors due to angle measurement */
		pitch_err,	/* Errors due to pitch measurement */
		beamw_err;	/* Errors due to beamwidth in along-track direction */
	f64	meas_angle;
	f64	cosT, sinT;
	
	meas_angle = ws->beam_angles[snd->beam_number];
	cosT = cos(DEG2RAD(plat->roll) + meas_angle);
	sinT = sin(DEG2RAD(plat->roll) + meas_angle);
	
	range_err = device_compute_rangeerr(device, plat, snd) +
				(snd->range*snd->range*ws->svp_var)/
					(plat->mean_speed*plat->mean_speed); /* Eqn. 3.20 */

	range_err *= ws->cosP * cosT * ws->cosP * cosT;	/* Eqn. 3.47 */
	
	angle_err = ws->total_roll_var * snd->range * snd->range *
				sinT * sinT * ws->cosP * ws->cosP;	/* Eqn. 3.48 */
	
	pitch_err = snd->range * snd->range * cosT * cosT * ws->sinP * ws->sinP
				* ws->total_pitch_var;	/* Eqn. 3.49 */
	
	beamw_err = ws->along_beamw_coeff * snd->depth * snd->depth; /* 3.51 */
#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: depth error: range_err = %lf, angle_err = %lf,"
	" pitch_err = %lf, beamw_err = %lf\n", range_err, angle_err, pitch_err,
	beamw_err);
#endif

	return(range_err + angle_err + pitch_err + beamw_err);	/* 3.52 */
}

/* Routine:	errmod_full_swath_vert
 * Purpose:	Compute vertical swath error budget
 * Inputs:	plat	Platform orientation and location
 *			*snd	Pointer to the sounding under consideration
 *			vessel	Vessel configuration for current platform
 *			device	Device being used to generate observations
 *			*ws		Workspace pointer for current configuration
 * Outputs:	Returns variance estimate of total reduced depth error
 * Comment:	This implements eqn. 3.65, 3.63, and 3.61, and uses sub-routines
 *			to compute the other components (heave and measured depth).  Result
 *			is the total variance associated with the reduced depth.  Note that
 *			for speed, eqn. 3.63 and 3.61 (water level reduction and dynamic
 *			draft variance) are pre-computed and stored in the workspace.
 */

static f64 errmod_full_swath_vert(Platform *plat, Sounding *snd,
								  Vessel vessel, Device device, ErrFull *ws)
{
	f32		vert_var;
	
	/* Compute eqn. 3.65 */
	vert_var =	(f32)(ws->vert_reduction + /* Eqns. 3.61 and 3.63 */
				+ ws->total_heave_var	/* Eqn. 3.59 */
				+ errmod_full_swath_depth(plat, snd, vessel, device, ws));
																	/* 3.52 */
	return(vert_var);
}

/* Routine:	errmod_full_swath_angerr
 * Purpose:	Compute vertical roll/pointing angle error
 * Inputs:	plat	Platform orientation
 *			*snd	Sounding under consideration
 *			vessel	Vessel configuration
 *			device	Device being used for sounding
 *			*ws		Workspace for the current configuration
 * Outputs:	Sets ws->total_roll_var appropriately
 * Comment:	This computes Eqns. 3.43 and its parents.
 */

static f64 errmod_full_swath_ang_err(Platform *plat, Sounding *snd,
									 Vessel vessel, Device device, ErrFull *ws)
{
	f64	meas_angle,
		ang_meas,
		ang_svp,
		ang_surf_speed,
		offset;
	
	meas_angle = ws->beam_angles[snd->beam_number];
	
	if (ws->surf_speed_var > 0.0 && (meas_angle < ws->min_angle || meas_angle > ws->max_angle)) {
		/* Non-zero variance due to surface speed mis-measurement - need to
		 * check the beam angle and look for steering error.
		 */
		if (meas_angle < ws->min_angle)
			offset = meas_angle - ws->min_angle;
		else if (meas_angle > ws->max_angle)
			offset = meas_angle - ws->max_angle;
		else
			offset = 0.0;
		ang_surf_speed = tan(offset)*tan(offset)*
						 ws->surf_speed_var/(plat->surf_sspeed*plat->surf_sspeed);
	} else
		ang_surf_speed = 0.0;
	
	ang_svp = tan(meas_angle)*tan(meas_angle)*ws->svp_var/
				(4.0*plat->mean_speed*plat->mean_speed);	/* Eqn. 3.34 */
	
	ang_meas = device_compute_angerr(device, plat, snd, meas_angle);

#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: angle error: angle = %le meas = %le, svp = %le, sspeed = %le"
	", base_roll = %le, total = %le\n", meas_angle, ang_meas, ang_svp, ang_surf_speed,
	ws->base_roll_var, ang_meas + ang_svp + ang_surf_speed + ws->base_roll_var);
#endif

	return(ang_meas + ang_svp + ang_surf_speed + ws->base_roll_var);
			/* Eqn. 3.43 */
}

/* Routine:	errmod_full_do_trig
 * Purpose:	Compute trigonometric values for an orientation
 * Inputs:	plat	Platform orientation to work with
 *			*ws		Workspace for current orientation to record in
 * Outputs:	- (ws->{cos,sin}{P,R} are updated).
 * Comment:	-
 */

static void errmod_full_do_trig(Platform *plat, ErrFull *ws)
{
	ws->cosP = cos(DEG2RAD(plat->pitch));
	ws->sinP = sin(DEG2RAD(plat->pitch));
	ws->cosR = cos(DEG2RAD(plat->roll));
	ws->sinR = sin(DEG2RAD(plat->roll));
}

/* Routine:	errmod_full_swath_heave
 * Purpose:	Compute induced and measured heave components
 * Inputs:	plat	Platform orientation
 *			vessel	Vessel configuration
 *			*ws		Workspace for the current configuration
 * Outputs:	Returns variance of total heave component of vertical error.
 * Comment:	This implements eqn. 3.57, 3.58, and 3.59.
 */

static f64 errmod_full_swath_heave(Platform *plat, Vessel vessel, ErrFull *ws)
{
	f64	induced,		/* Induced component of heave */
		measured,		/* Measured component of heave */
		pitch_comp,		/* Coefficient of pitch std. dev. */
		roll_comp,		/* Coefficient of roll std. dev. */
		offset_comp;	/* Coefficient of offsets *variance* */
		
	measured = vessel->heave_var_percent * 2.0 * plat->heave *
			   vessel->heave_var_percent * 2.0 * plat->heave;
			   	/* Double heave because %cnt is p-p wave height */

#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: measured variable heave = %le\n", measured);
#endif

	pitch_comp = vessel->imu_x*ws->cosP - vessel->imu_y*ws->sinR*ws->sinP
				 - vessel->imu_z*ws->cosR*ws->sinP;
	roll_comp = vessel->imu_y*ws->cosR*ws->cosP -
				vessel->imu_z*ws->sinR*ws->cosP;
	offset_comp = ws->sinP*ws->sinP + ws->sinR*ws->sinR*ws->cosP*ws->cosP +
				  (1-ws->cosR*ws->cosP)*(1-ws->cosR*ws->cosP);
	induced = ws->total_pitch_var * pitch_comp * pitch_comp +
			  ws->base_roll_var * roll_comp * roll_comp +
			  2.0 * ws->m_imu_off_var * offset_comp;		/* Eqn. 3.58 */

#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: pitch_comp = %le, roll_comp = %le, offset_comp"
	" = %le, induced = %le\n", pitch_comp, roll_comp, offset_comp, induced);
#endif

	if (measured < ws->heave_fixed)
		measured = ws->heave_fixed;	/* Eqn. 3.57 */

	return(measured + induced);	/* Eqn. 3.59 */
}

/* Routine:	errmod_full_hor_txrel
 * Purpose:	Compute variance of horizontal positioning error caused by GPS
 *			antennae not being at the transducer head
 * Inputs:	*plat	Platform motion data structure
 *			vessel	Vessel description data structure
 *			*ws		Workspace pointer for the error model
 * Outputs:	Returns approximate 95% confidence interval for error
 * Comment:	This computes eqn. 3.90, summarising the component of horizontal
 *			error due to misalignment of the GPS antennae and the tx head.
 *				Note that in keeping with the report and spreadsheet, we return
 *			twice the nominal variance in order to approximate the 95% conf.
 *			interval assuming a Gaussian distribution.
 */

static f64 errmod_full_hor_txrel(Platform *plat, Vessel vessel, ErrFull *ws)
{
	f64	offset_err, roll_err, pitch_err, heading_err;
	
	/* Eqn. 3.86 */
	offset_err = ws->cosP*ws->cosP
				 + ws->cosR*ws->cosR + ws->sinR*ws->sinR*ws->sinP*ws->sinP
				 + ws->sinR*ws->sinR + ws->cosR*ws->cosR*ws->sinP*ws->sinP;
	offset_err *= vessel->gps_off_sdev*vessel->gps_off_sdev+
				  vessel->imu_off_sdev*vessel->imu_off_sdev;
	
	/* Eqn. 3.87 */
	heading_err = vessel->gps_x*vessel->gps_x*ws->cosP*ws->cosP
				  + vessel->gps_y*vessel->gps_y*ws->cosR*ws->cosR
				  + vessel->gps_y*vessel->gps_y*ws->sinR*ws->sinR*ws->sinP*ws->sinP
				  + vessel->gps_z*vessel->gps_z*ws->sinP*ws->sinP*ws->cosR*ws->cosR
				  + vessel->gps_z*vessel->gps_z*ws->sinR*ws->sinR
				  - vessel->gps_x*vessel->gps_y*ws->cosP*ws->sinP*ws->sinR
				  - vessel->gps_x*vessel->gps_z*ws->cosP*ws->sinP*ws->cosR
				  - vessel->gps_y*vessel->gps_z*ws->cosP*ws->cosP*ws->sinR*ws->cosR;
	heading_err *= DEG2RAD(vessel->gyro_sdev)*DEG2RAD(vessel->gyro_sdev);
	
	/* Eqn. 3.88 */
	roll_err = vessel->gps_y*vessel->gps_y*(
					ws->sinR*ws->sinR + ws->cosR*ws->cosR*ws->sinP*ws->sinP)
			 + vessel->gps_z*vessel->gps_z*(
			 		ws->cosR*ws->cosR + ws->sinR*ws->sinR*ws->sinP*ws->sinP)
			 + vessel->gps_y*vessel->gps_z*
			 	ws->cosR*ws->sinR*ws->cosP*ws->cosP;
	roll_err *= DEG2RAD(vessel->roll_sdev)*DEG2RAD(vessel->roll_sdev);
	
	/* Eqn. 3.89 */
	pitch_err = vessel->gps_x*ws->sinP
				+ vessel->gps_y*ws->sinR*ws->cosP
				+ vessel->gps_z*ws->cosR*ws->cosP;
	pitch_err *= pitch_err*DEG2RAD(vessel->pitch_sdev)*DEG2RAD(vessel->pitch_sdev);

#ifdef __NEVER_DEFINE__
	error_msgv(modname, "debug: offset_err = %lf, heading_err = %lf,"
		" roll_err = %lf, pitch_err = %lf\n", offset_err, heading_err,
		roll_err, pitch_err);
#endif

	/* Eqn. 3.90 */
	return(offset_err + heading_err + roll_err + pitch_err);
}

/* Routine:	errmod_full_hor_latency
 * Purpose:	Compute approximate 95% error bound due to latency errors
 * Inputs:	*plat	Pointer to platform motion data structure
 *			vessel	Vessel description data structure
 *			*ws		Pointer to workspace for the error model
 * Outputs:	Returns total latency error
 * Comment:	We assume that the coefficients for eqn 3.100 have been pre-computed
 *			and stored in the workspace, and that the trig. functions for the
 *			current swath orientation have been computed.
 */

static f64 errmod_full_hor_latency(Platform *plat, Vessel vessel, ErrFull *ws)
{
	f64	sog_error, jitter_error, head_error, pitch_error;
	
	sog_error = vessel->gps_latency*vessel->gps_latency
				*vessel->sog_sdev*vessel->sog_sdev
				*ws->cosP*ws->cosP;							/* Eqn. 3.96 */
	jitter_error = plat->vessel_speed*plat->vessel_speed
				   *ws->total_latency_var
				   *ws->cosP*ws->cosP;						/* Eqn. 3.97 */
#ifdef __NEVER_DEFINE__
	error_msgv(modname, "debug: vessel_speed = %g, cosP = %lg\n",
		plat->vessel_speed, ws->cosP);
#endif

	head_error = plat->vessel_speed*plat->vessel_speed
				 *vessel->gps_latency*vessel->gps_latency
				 *DEG2RAD(vessel->gyro_sdev)*DEG2RAD(vessel->gyro_sdev)
				 *ws->cosP*ws->cosP;						/* Eqn. 3.98 */
	pitch_error = plat->vessel_speed*plat->vessel_speed
				  *vessel->gps_latency*vessel->gps_latency
				  *ws->total_pitch_var
				  *ws->sinP*ws->sinP;						/* Eqn. 3.99 */
#ifdef __NEVER_DEFINE__
	error_msgv(modname, "debug: sog_err = %lf, jitter_error = %lf, "
		"head_error = %lf, pitch_error = %lf, hor_latency_error = %lf\n",
			sog_error, jitter_error, head_error, pitch_error,
			sog_error+jitter_error+head_error+pitch_error);
#endif

	return(sog_error + jitter_error + head_error + pitch_error); /* Eqn. 3.100 */
}

/* Routine:	errmod_full_hor_txerr
 * Purpose:	Compute component of horizontal positioning error associated with
 *			ship attitude and offsets.
 * Inputs:	*plat	Pointer to ship's attitude structure
 *			*snd	Pointer to sounding to work with
 *			vessel	Vessel definition structure
 *			device	Primary sounding device being used on board
 *			*ws		Pointer to workspace being used
 * Outputs:	Returns approximate 95% confidence interval.
 * Comment:	This assumes, per the spreadsheet and report, that we have to work
 *			at the 95% confidence level due to the drms approximation.  We
 *			multiply the standard deviation estimate by 2.0 to approximate this.
 *				We are computing eqns. 3.77-3.82.  Note that the spreadsheet
 *			does not include any component for the along-track beam angle,
 *			unlike the report, and we ignore it here also.
 */

static f64 errmod_full_hor_txerr(Platform *plat, Sounding *snd, Vessel vessel,
								 Device device, ErrFull *ws)
{
	f64	measured_angle;
	f64	profile_err, rmeas_err;
	f64	range_err, angle_err, pitch_err, heading_err;
	
	measured_angle = ws->beam_angles[snd->beam_number];
	
	/* Eqn. 3.77 */
	profile_err = vessel->svp_sdev*snd->depth/(plat->mean_speed*cos(measured_angle));
	profile_err *= profile_err;
	rmeas_err = device_compute_rangeerr(device, plat, snd);
	range_err = (profile_err + rmeas_err)
				*(1.0-ws->cosP*ws->cosP*cos(measured_angle)*cos(measured_angle));
	
	/* Eqn. 3.78 */
	heading_err = ws->total_gyro_var*snd->depth*snd->depth*
				  (ws->sinP*ws->sinP + tan(measured_angle)*tan(measured_angle));
	
	/* Eqn. 3.79 */
	angle_err = errmod_full_swath_ang_err(plat, snd, vessel, device, ws)*
				snd->range*snd->range*
				(1.0 - ws->cosP*ws->cosP*sin(measured_angle)*sin(measured_angle));
#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: hor_txerr/angle: range = %le cosP = %le meas_ang = %le\n",
	snd->range, ws->cosP, measured_angle);
#endif

	/* Eqn. 3.80 */
	pitch_err = snd->depth*snd->depth*ws->cosP*ws->cosP*ws->total_pitch_var;
	
	/* Eqn. 3.81 gives error due to fore-aft beamwidth, but the spreadsheet
	 * doesn't use it, so neither do we.  The code would look like:
	
	device_get_beamwidths(device, &across_bw, &along_bw);
	bw_err = 0.3*DEG2RAD(along_bw)*snd->range;
	bw_err *= bw_err;
	
	 * and then added to the total at the end as the others are.
	 */
	
	/* Eqn. 3.82 */
#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: hor_txerr: range = %le heading = %le angle = %le"
	" pitch = %le total = %le\n", range_err, heading_err, angle_err,
	pitch_err, range_err + heading_err + angle_err + pitch_err);
#endif
	return(range_err + heading_err + angle_err + pitch_err);
}

/* Routine:	errmod_full_swath_horz
 * Purpose:	Compute horizontal swath error budget
 * Inputs:	plat	Platform orientation and location
 *			*snd	Pointer to the sounding under consideration
 *			vessel	Vessel configuration for current platform
 *			device	Device being used to carry out observations
 *			*ws		Workspace pointer for the current configuration
 * Outputs:	Returns variance estimate for total horizontal error budget
 * Comment:	This implements equations 3.100, 3.90, 3.82 and 3.69 for the
 *			components of the error budget, combining them with 3.70.
 */

static f64 errmod_full_swath_horz(Platform *plat, Sounding *snd,
								  Vessel vessel, Device device, ErrFull *ws)
{
	f64	hor_txerr, total;
	
	hor_txerr = errmod_full_hor_txerr(plat, snd, vessel, device, ws);
	total = ws->total_gps_var 			/* GPS drms^2 */
		   + ws->hor_latency_var		/* Total latency, eqn. 3.100 */
		   + ws->hor_tx_relative_var	/* Antenna error, eqn. 3.90 */
		   + hor_txerr;					/* Per-sounding error, eqn. 3.82 */

#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: total_gps_var = %le hor_latency_var = %le"
	" hor_tx_relative_var = %le hor_txerr = %le total = %le (%le m@95%%)\n",
	ws->total_gps_var, ws->hor_latency_var, ws->hor_tx_relative_var,
	hor_txerr, total, 1.96*sqrt(total));
#endif

	return(total);
}

/* Routine:	errmod_full_compute
 * Purpose:	Carry out computations for full MBES error model
 * Inputs:	device	Device being used to generate the soundings
 *			vessel	Vessel configuration parameters for platform
 *			plat	Orientation and location of observation platform
 *			data	Soundings structure to be filled in
 *			nSnds	Number of soundings in observation set
 *			*ws		Pointer to private workspace
 * Outputs:	True if computations succeeded, otherwise False
 * Comment:	At present, this always returns True.
 */

Bool errmod_full_compute(Device device, Vessel vessel, Platform *plat,
						 Sounding *data, u32 nSnds, void *ws)
{
	ErrFull	*space = (ErrFull*)ws;
	u32	s;
	
	errmod_full_do_trig(plat, space);	/* Pre-compute platform trig. funcs. */
	space->total_heave_var = errmod_full_swath_heave(plat, vessel, space);
	space->hor_tx_relative_var = errmod_full_hor_txrel(plat, vessel, space);
	space->hor_latency_var = errmod_full_hor_latency(plat, vessel, space);
	if (!errmod_full_compute_beams(device, plat->surf_sspeed,
												vessel->static_roll, space)) {
		error_msg(modname, "failed to generate beam angles LUT.\n");
		return(False);
	}
#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: total_heave_var = %lf\n", space->total_heave_var);
error_msgv(modname, "debug: hor. rel. tx. err = %lf\n", space->hor_tx_relative_var);
error_msgv(modname, "debug: hor. latency err. = %lf\n", space->hor_latency_var);
#endif

#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: total %d soundings\n", nSnds);
#endif

if (nSnds > device_get_nbeams(device)) {
	error_msgv(modname, "error: number of beams reported (%u) greater than maximum"
		" available to device ... something's wrong.\n", nSnds);
	return(False);
}

	for (s = 0; s < nSnds; ++s) {

#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: sounding %d (beam %d) ...\n", s,
	data[s].beam_number);
#endif

		space->total_roll_var = errmod_full_swath_ang_err(plat, data+s, vessel,
														  device, space);
#ifdef __NEVER_DEFINE__
error_msgv(modname, "debug: total_roll_var = %lf\n", space->total_roll_var);
#endif
		data[s].dz = (f32)errmod_full_swath_vert(plat, data+s, vessel, device, space);
		data[s].dr = (f32)errmod_full_swath_horz(plat, data+s, vessel, device, space);
		if (!finite(data[s].dz)) {
			error_msgv(modname, "warning: non-finite vertical error model"
				" output, beam = %d angle = %f d = %f f = %d\n",
				data[s].beam_number, space->beam_angles[data[s].beam_number],
				data[s].depth, data[s].flags);
			error_msgv(modname, "debug: total_roll_var = %lf\n",
				space->total_roll_var);
			return(False);
		}
		if (!finite(data[s].dr)) {
			error_msgv(modname, "warning: non-finite horizontal error model"
				" output, beam = %d angle = %f d = %f f = %d\n",
				data[s].beam_number, space->beam_angles[data[s].beam_number],
				data[s].depth, data[s].flags);
			error_msgv(modname, "debug: total_roll_var = %lf\n",
				space->total_roll_var);
		}
	}
	return(True);
}

/* Routine:	errmod_full_get_beam_angle
 * Purpose:	Return beam number -> angle mapping used in error modules
 * Inputs:	*ws		Workspace pointer
 *			beam	Beam number to return
 * Outputs:	Returns angle w.r.t. vertical (0 == straight down, -ve to port)
 * Comment:	This returns the angle computed by the error model with static
 *			roll taken into account, so that the angle returned is w.r.t
 *			vertical, rather than w.r.t. transducer face as it is with the
 *			device_beam_to_angle() call.
 */

f64 errmod_full_get_beam_angle(void *ws, u32 beam)
{
	ErrFull	*space = (ErrFull*)ws;
	
	if (beam >= space->nbeams) return(DBL_MAX);
	return(space->beam_angles[beam]);
}

/* Routine:	errmod_full_execute_params
 * Purpose:	Execute a list of parameters for this sub-module
 * Inputs:	*list	List of parameters to work with
 * Outputs:	True if list parsed OK, otherwise False.
 * Comment:	Currently only looks for default sound speed
 */

typedef enum {
	ERRFULL_UNKNOWN = 0,
	ERRFULL_SURF_SPEED
} ErrModFParam;

Bool errmod_full_execute_params(ParList *list)
{
	ParTable	tab[] = {
		{ "default_surf_sspeed",	ERRFULL_SURF_SPEED },
		{ NULL,						ERRFULL_UNKNOWN }
	};
	ParList	*node, *match;
	u32		id;
	
	node = list;
	do {
		node = params_match(node, "errmod.full", tab, &id, &match);
		switch (id) {
			case ERRFULL_UNKNOWN:
				/* Matched nothing ... but that may just mean that there's
				 * nothing there for this module.
				 */
				break;
			case ERRFULL_SURF_SPEED:
				default_surf_sspeed = atof(match->data);
				if (default_surf_sspeed < MIN_SURF_SSPEED ||
					default_surf_sspeed > MAX_SURF_SSPEED) {
					error_msgv(modname, "error: default surface sound speed "
						"set out of range ([%f, %f] m/s).  Resorting to "
						"standard default.\n",
						MIN_SURF_SSPEED, MAX_SURF_SSPEED);
					default_surf_sspeed = DEFAULT_SURF_SSPEED;
				}
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default surface sound speed to %f.\n",
	default_surf_sspeed);
#endif
				break;
			default:
				error_msgv(modname, "error: unknown return from parameter"
					" matching module (%d).\n", id);
				return(False);
				break;
		}
	} while (node != NULL);
	return(True);
}
