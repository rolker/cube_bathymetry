/*
 * $Id: vessel.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2.4.2  2003/01/28 16:38:36  dneville
 * (BRC) Added 'sonar_type' element to global section of the vessel parameter
 * clause, and a corresponding element in the Vessel description that
 * allows it to be retained.  This allows one file, with just a Vessel clause, to
 * specify the whole system being used on the survey platform (makes it easier
 * for integrators to keep things together).
 *
 * Revision 1.2.4.1  2003/01/28 14:29:35  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.4.2.1  2002/07/14 02:20:48  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.4  2002/03/14 04:10:36  brc
 * Modifications to vessel description code in order to allow dual-head systems
 * to be specified.  The normal situation is a single head, but optional port{}
 * and stbd{} clauses are allowed to extend the definition.  Note that split-head
 * transducers which are nominally one device (e.g. Elac 1180) are specified as
 * dual-head so that the different offsets are correctly represented.
 *
 * Revision 1.3  2001/05/15 01:18:08  brc
 * Modifications to make libccom compile cleanly under Linux.
 *
 * Revision 1.2  2001/05/14 04:24:39  brc
 * Updated to make module 'params'-aware
 *
 * Revision 1.1  2001/02/10 19:19:41  brc
 * Prototype interface to vessel description files, essentially just bodged in
 * to make the error modeling code operational.  There is at present no way
 * of defining a vessel except within the default values in the source code, and
 * no way to write out a definition.  These will obvious be along RSN.  Honest.
 *
 *
 * File:	vessel.c
 * Purpose:	Encapsulate vessel configurations
 * Date:	24 November 2000
 *
 * Copyright 2022, Center for Coastal and Ocean Mapping and NOAA-UNH Joint Hydrographic
 * Center, University of New Hampshire.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this
 * software and associated documentation files (the "Software"), to deal in the Software
 * without restriction, including without limitation the rights to use, copy, modify,
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies
 * or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
 * FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <float.h>
#include "stdtypes.h"
#include "error.h"
#include "vessel.h"
#include "stime.h"
#include "device.h"
#include "params.h"

#undef __DEBUG__

static char *modname = "vessel";
static char *modrev = "$Revision: 2 $";

/* - Default values for the various accuracies involved in the configuration -*/

#define DEFAULT_GPS_OFF_SDEV		0.005	/* m */
#define DEFAULT_GPS_LATENCY_SDEV	0.03	/* s. */
#define DEFAULT_GPS_DRMS			2.0		/* m. */

#define DEFAULT_IMU_OFF_SDEV		0.005	/* m */
#define DEFAULT_IMU_RP_ALIGN_SDEV	0.05	/* deg. */
#define DEFAULT_IMU_G_ALIGN_SDEV	0.10	/* deg. */
#define DEFAULT_IMU_LATENCY_SDEV	0.005	/* s. */

#define DEFAULT_TX_LATENCY_SDEV		0.005	/* Transducer latency err, s. */

#define DEFAULT_STATIC_ROLL			0.0		/* deg. */
#define MIN_STAT_ROLL				-90.0	/* deg. */
#define MAX_STAT_ROLL				 90.0	/* deg. */

#define DEFAULT_ROLL_SDEV			0.05	/* deg. */
#define DEFAULT_PITCH_SDEV			0.05	/* deg. */
#define DEFAULT_PITCH_STAB_SDEV		0.00	/* deg. */
#define DEFAULT_GYRO_SDEV			0.50	/* deg. */
#define DEFAULT_SVP_SDEV			0.52	/* m/s */
#define DEFAULT_SURF_SS_SDEV		0.50	/* m/s */

#define DEFAULT_HEAVE_FIXED_SDEV	0.05	/* m */
#define DEFAULT_HEAVE_VAR_PC		0.05	/* % */
#define DEFAULT_DRAFT_SDEV			0.02	/* m */
#define DEFAULT_DDRAFT_SDEV			0.02	/* m */
#define DEFAULT_LOADING_SDEV		0.01	/* m */
#define DEFAULT_SOG_SDEV			0.2		/* m/s */
#define DEFAULT_TIDE_MEAS_SDEV		0.02	/* m */
#define DEFAULT_TIDE_PRED_SDEV		0.02	/* m */

static struct _vessel null_vessel = {
	0.0,								/* Timestamp from epoch */
	DEVICE_UNKNOWN,						/* Device being used on vessel */
	
	/* Direct offsets for GPS */
	0.0, 0.0, 0.0,						/* Physical offsets, m */
	DEFAULT_GPS_OFF_SDEV,
	DEFAULT_GPS_LATENCY_SDEV,
	DEFAULT_GPS_DRMS,
	0.0,								/* Latency, s */
	
	/* IMU offsets and latencies */
	0.0, 0.0, 0.0,						/* Physical offsets, m */
	DEFAULT_IMU_OFF_SDEV,
	0.0, 0.0, 0.0,						/* Roll/Pitch/Gyro offsets, deg. */
	DEFAULT_IMU_RP_ALIGN_SDEV,
	DEFAULT_IMU_G_ALIGN_SDEV,
	DEFAULT_IMU_LATENCY_SDEV,
	
	/* Final measurements */
	0.0,								/* Draft, m */
	DEFAULT_TX_LATENCY_SDEV,
	DEFAULT_STATIC_ROLL,
	
	/* Measurement accuracies from the VRU */
	DEFAULT_ROLL_SDEV,
	DEFAULT_PITCH_SDEV,
	DEFAULT_PITCH_STAB_SDEV,
	DEFAULT_GYRO_SDEV,
	DEFAULT_SVP_SDEV,
	DEFAULT_SURF_SS_SDEV,
	
	/* Other measurement accuracies */
	DEFAULT_HEAVE_FIXED_SDEV,
	DEFAULT_HEAVE_VAR_PC,
	DEFAULT_DRAFT_SDEV,
	DEFAULT_DDRAFT_SDEV,
	DEFAULT_LOADING_SDEV,
	DEFAULT_SOG_SDEV,
	DEFAULT_TIDE_MEAS_SDEV,
	DEFAULT_TIDE_PRED_SDEV
};

static VesselSensorConfig		config = VESSEL_DEFAULT;
static struct _vessel 	default_vessel;
static struct _vessel 	port_head;
static struct _vessel 	stbd_head;

/* Routine:	vessel_new
 * Purpose:	Generate a blank new vessel configuration structure
 * Inputs:	-
 * Outputs:	Pointer to Vessel, or NULL on failure.
 * Comment:	This default vessel has timestamp set to epoch and all offsets and
 *			measurement std. dev.s set to zero.
 */

Vessel vessel_new(void)
{
	Vessel	rtn;
	
	if ((rtn = (Vessel)calloc(1, sizeof(struct _vessel))) == NULL) {
		error_msg(modname, "failed allocating vessel structure.\n");
		return(NULL);
	}
	return(rtn);
}

/* Routine:	vessel_default
 * Purpose:	Make a default vessel configuration
 * Inputs:	-
 * Outputs:	Returns pointer to default vessel configuration, or NULL on error
 * Comment:	The default configuration consists of zeroed offsets, and default
 *			values representing typical measurement accuracies for the
 *			components involved in the vessel configuration.  These measurement
 *			accuracies are based on Hare, Godin & Mayer 1995, "Accuracy
 *			Estimation of Canadian Swath (multibeam) and Sweel (multi-transducer)
 *			sounding systems."
 */

Vessel vessel_default(void)
{
	Vessel	rtn;
	
	if ((rtn = vessel_new()) == NULL) return(NULL);
	if (config == VESSEL_DEFAULT) {
		/* Not previously configured; push null_vessel into definition */
		memcpy(&default_vessel, &null_vessel, sizeof(struct _vessel));
		memcpy(&port_head, &null_vessel, sizeof(struct _vessel));
		memcpy(&stbd_head, &null_vessel, sizeof(struct _vessel));
	}
	if (config == VESSEL_DEFAULT || config == VESSEL_SINGLE_HEAD)
		memcpy(rtn, &default_vessel, sizeof(struct _vessel));
	else
		memcpy(rtn, &port_head, sizeof(struct _vessel));
	return(rtn);
}

/* Routine:	vessel_release
 * Purpose:	Remove memory associated with a vessel structure
 * Inputs:	vessel	Structure to be removed
 * Outputs:	-
 * Comment:	-
 */

void vessel_release(Vessel vessel)
{
	if (vessel == NULL) return;
	free(vessel);
}

/* Routine:	vessel_init_default
 * Purpose:	Initialise the default parameter structure, and the config flag
 * Inputs:	-
 * Outputs:	-
 * Comment:	-
 */

static void vessel_init_default(void)
{
	/* Reset default vessel, port and stbd head, and configuration status to
	 * the NULL state.
	 */
	memcpy(&default_vessel, &null_vessel, sizeof(struct _vessel));
	memcpy(&port_head, &null_vessel, sizeof(struct _vessel));
	memcpy(&stbd_head, &null_vessel, sizeof(struct _vessel));
	config = VESSEL_DEFAULT;	
}

/* Routine:	vessel_get_head
 * Purpose:	Return a particular head definition for dual head configurations
 * Inputs:	head	Which head to return (see comment for options)
 * Outputs:	Vessel for the head selected
 * Comment:	User may specify VESSEL_HEAD_ANY (which returns the Port head for
 *			dual head configurations, and the default head otherwise), or
 *			VESSEL_HEAD_PORT/STBD which returns the correct head iff the system
 *			is dual headed, and returns NULL otherwise.  If the system has not
 *			been configured, then VESSEL_HEAD_ANY returns the default null
 *			vessel definition, and any other selection returns NULL.
 */

Vessel vessel_get_head(VesselHead head)
{
	Vessel	rtn;
	Bool	valid = False;
	
	if ((rtn = vessel_new()) == NULL) {
		error_msgv(modname, "error: no memory for vessel buffer (%d bytes).\n",
			sizeof(struct _vessel));
		return(NULL);
	}
	
	switch (config) {
		case VESSEL_DEFAULT:
			if (head != VESSEL_HEAD_ANY) {
				error_msg(modname, "error: vessel subsystem not configured,"
					" can't supply particular head.\n");
			} else {
				/* System is not configured, so we need to initialise */
				vessel_init_default();
				memcpy(rtn, &default_vessel, sizeof(struct _vessel));
				valid = True;
			}
			break;
		case VESSEL_SINGLE_HEAD:
			if (head != VESSEL_HEAD_ANY) {
				error_msg(modname, "error: vessel only has a single head"
					" definition, can't supply specific heads.\n");
			} else {
				memcpy(rtn, &port_head, sizeof(struct _vessel));
				valid = True;
			}
			break;
		case VESSEL_DUAL_HEAD:
			switch (head) {
				case VESSEL_HEAD_PORT:
					memcpy(rtn, &port_head, sizeof(struct _vessel));
					valid = True;
					break;
				case VESSEL_HEAD_STBD:
					memcpy(rtn, &stbd_head, sizeof(struct _vessel));
					valid = True;
					break;
				case VESSEL_HEAD_ANY:
					memcpy(rtn, &port_head, sizeof(struct _vessel));
					valid = True;
					break;
				default:
					error_msgv(modname, "internal error: unknown head "
						"specification (%d) in vessel_get_head().\n",
						(u32)head);
					break;
			}
			break;
		default:
			error_msgv(modname, "internal: unknown configuration state (%d) in"
				" vessel_get_head().\n", (u32)config);
			break;
	}
	if (!valid) {
		vessel_release(rtn);
		rtn = NULL;
	}
	return(rtn);
}

/* Routine:	vessel_new_from_file
 * Purpose:	Retrieve a vessel description from file
 * Inputs:	*f	FILE to read from
 * Outputs:	Returns pointer to Vessel structure, or NULL on error
 * Comment:	-
 */

Vessel vessel_new_from_file(FILE *f)
{
	Vessel	rtn;
	
	if ((rtn = vessel_new()) == NULL) {
		error_msg(modname, "failed allocating memory for vessel description.\n");
		return(NULL);
	}
	if (fread(rtn, sizeof(struct _vessel), 1, f) != 1) {
		error_msg(modname, "failed reading vessel description from file.\n");
		vessel_release(rtn);
		return(NULL);
	}
	return(rtn);
}

/* Routine:	vessel_write
 * Purpose:	Write a vessel description to file
 * Inputs:	vessel	Vessel description to write
 *			*f		FILE to write to
 * Outputs:	True on success, otherwise False
 * Comment:	-
 */

Bool vessel_write(Vessel vessel, FILE *f)
{
	if (fwrite(vessel, sizeof(struct _vessel), 1, f) != 1) {
		error_msg(modname, "failed writing vessel description to file.\n");
		return(False);
	}
	return(True);
}

/* Routine:	vessel_get_config
 * Purpose:	Return vessel configuration status for default vessel
 * Inputs:	-
 * Outputs:	Returns a VesselSensorConfig enum
 * Comment:	The library will return VESSEL_DEFAULT until a user set of
 *			parameters are actioned, after which it returns VESSEL_SINGLE_HEAD
 *			or VESSEL_DUAL_HEAD as appropriate.
 */

VesselSensorConfig vessel_get_config(void)
{
	return(config);
}

/* Routine:	vessel_execute_sdev_params
 * Purpose:	Execute the subset of parameters which define measurement accuracies
 * Inputs:	*prefix	Parameter prefix to use in matching.
 *			*list	ParList to work through
 *			vsl		Parameter structure to fill in
 * Outputs:	True if list parsed correctly, otherwise False
 * Comment:	This looks for GPS and IMU offset errors, GPS drms and latency,
 *			IMU angle alignment errors, SVP and surface SS, draft, dynamic
 *			draft, loading, speed-over-ground and tidal measurement errors.
 *			That is, all of the standard deviations required by the MBES error
 *			model to do its thing.
 */

typedef enum {
	VSL_SD_UNKNOWN = 0,
	VSL_SD_GPS_OFF,
	VSL_SD_GPS_DRMS,
	VSL_SD_GPS_LATENCY,
	VSL_SD_IMU_OFF,
	VSL_SD_IMU_RP_ALIGN,
	VSL_SD_IMU_G_ALIGN,
	VSL_SD_IMU_LATENCY,
	VSL_SD_ROLL,
	VSL_SD_PITCH,
	VSL_SD_PITCH_STAB,
	VSL_SD_GYRO,
	VSL_SD_SVP,
	VSL_SD_SURF_SSPEED,
	VSL_SD_HEAVE_FIXED,
	VSL_SD_HEAVE_VAR,
	VSL_SD_DRAFT,
	VSL_SD_DDRAFT,
	VSL_SD_LOADING,
	VSL_SD_SOG,
	VSL_SD_TIDE_MEAS,
	VSL_SD_TIDE_PRED
} VesselSDEnum;

Bool vessel_execute_sdev_params(char *prefix, ParList *list, Vessel vsl)
{
	ParTable sdev_tab[] = {
		{ "gps_offset",		VSL_SD_GPS_OFF		},
		{ "gps_drms",		VSL_SD_GPS_DRMS		},
		{ "gps_latency",	VSL_SD_GPS_LATENCY	},
		{ "imu_offset",		VSL_SD_IMU_OFF		},
		{ "imu_rp_align",	VSL_SD_IMU_RP_ALIGN },
		{ "imu_g_align",	VSL_SD_IMU_G_ALIGN	},
		{ "imu_latency",	VSL_SD_IMU_LATENCY	},
		{ "roll",			VSL_SD_ROLL			},
		{ "pitch",			VSL_SD_PITCH		},
		{ "pitch_stab",		VSL_SD_PITCH_STAB	},
		{ "gyro",			VSL_SD_GYRO			},
		{ "svp",			VSL_SD_SVP			},
		{ "surf_sspeed",	VSL_SD_SURF_SSPEED	},
		{ "heave_fixed",	VSL_SD_HEAVE_FIXED	},
		{ "heave_var",		VSL_SD_HEAVE_VAR	},
		{ "draft",			VSL_SD_DRAFT		},
		{ "ddraft",			VSL_SD_DDRAFT		},
		{ "loading",		VSL_SD_LOADING		},
		{ "sog",			VSL_SD_SOG			},
		{ "tide_meas",		VSL_SD_TIDE_MEAS	},
		{ "tide_pred",		VSL_SD_TIDE_PRED	},
		{ NULL,				VSL_SD_UNKNOWN		}
	};
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;

	node = list;
	do {
		node = params_match(node, prefix, sdev_tab, &id, &match);
		switch (id) {
			case VSL_SD_UNKNOWN:
				break;
			case VSL_SD_GPS_OFF:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a "
						"length sdev.\n", match->data);
					return(False);
				}
				vsl->gps_off_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting GPS offset sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_GPS_DRMS:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a "
						"length sdev.\n", match->data);
					return(False);
				}
				vsl->gps_drms = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting GPS drms sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_GPS_LATENCY:
				dummy_float = params_translate_duration(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a "
						"duration sdev.\n", match->data);
					return(False);
				}
				vsl->gps_latency_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting GPS latency sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_IMU_OFF:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a "
						"length sdev.\n", match->data);
					return(False);
				}
				vsl->imu_off_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting IMU offset sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_IMU_RP_ALIGN:
				dummy_float = atof(match->data);
				if (dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a"
						" standard deviation.\n", match->data);
					return(False);
				}
				vsl->imu_rp_align_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting IMU roll/pitch alignment sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_IMU_G_ALIGN:
				dummy_float = atof(match->data);
				if (dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a"
						" standard deviation.\n", match->data);
					return(False);
				}
				vsl->imu_g_align_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting IMU gyro alignment sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_IMU_LATENCY:
				dummy_float = params_translate_duration(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a "
						"duration sdev.\n", match->data);
					return(False);
				}
				vsl->imu_latency_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting IMU latency sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_ROLL:
				dummy_float = atof(match->data);
				if (dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a"
						" standard deviation.\n", match->data);
					return(False);
				}
				vsl->roll_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting roll measurement sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_PITCH:
				dummy_float = atof(match->data);
				if (dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a"
						" standard deviation.\n", match->data);
					return(False);
				}
				vsl->pitch_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting pitch measurement sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_PITCH_STAB:
				dummy_float = atof(match->data);
				if (dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a"
						" standard deviation.\n", match->data);
					return(False);
				}
				vsl->pitch_stab_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting pitch stabilisation measurement sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_GYRO:
				dummy_float = atof(match->data);
				if (dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a"
						" standard deviation.\n", match->data);
					return(False);
				}
				vsl->gyro_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting gyro measurement sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_SVP:
				dummy_float = atof(match->data);
				if (dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a"
						" standard deviation.\n", match->data);
					return(False);
				}
				vsl->svp_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting SVP measurement sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_SURF_SSPEED:
				dummy_float = atof(match->data);
				if (dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a"
						" standard deviation.\n", match->data);
					return(False);
				}
				vsl->surf_ss_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting surface SS measurement sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_HEAVE_FIXED:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a "
						"length sdev.\n", match->data);
					return(False);
				}
				vsl->heave_fixed_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting fixed heave sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_HEAVE_VAR:
				dummy_float = atof(match->data);
				if (dummy_float < 0 || dummy_float >= 1.0) {
					error_msgv(modname, "failed to convert \"%s\" to range"
						"[0,1).\n", match->data);
					return(False);
				}
				vsl->heave_var_percent = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting variable heave percentage to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_DRAFT:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a "
						"length sdev.\n", match->data);
					return(False);
				}
				vsl->draft_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting draft sdev to %f (%s).\n", dummy_float,
	match->data);
#endif
				break;
			case VSL_SD_DDRAFT:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a "
						"length sdev.\n", match->data);
					return(False);
				}
				vsl->ddraft_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting dynamic draft sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_LOADING:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a "
						"length sdev.\n", match->data);
					return(False);
				}
				vsl->loading_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting loading sdev to %f (%s).\n", dummy_float,
	match->data);
#endif
				break;
			case VSL_SD_SOG:
				dummy_float = atof(match->data);
				if (dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a"
						" standard deviation.\n", match->data);
					return(False);
				}
				vsl->sog_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting Speed Over Ground measurement sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_TIDE_MEAS:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a "
						"length sdev.\n", match->data);
					return(False);
				}
				vsl->tide_measured_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting measured tide sdev to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SD_TIDE_PRED:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a "
						"lengthsdev .\n", match->data);
					return(False);
				}
				vsl->tide_predicted_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting predicted tide sdev to %f (%s).\n",
	dummy_float, match->data);
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

/* Routine:	vessel_execute_imu_params
 * Purpose:	Execute the subset of parameters which define the IMU setup
 * Inputs:	*prefix	Parameter prefix to use for matching
 *			*list	ParList to work through
 *			vsl		Pointer to Vessel parameter structure to fill in
 * Outputs:	True if list was parsed correctly, otherwise False
 * Comment:	This looks for offsets, and alignment angles
 */

typedef enum {
	VSL_IMU_UNKNOWN = 0,
	VSL_IMU_XOFF,
	VSL_IMU_YOFF,
	VSL_IMU_ZOFF,
	VSL_IMU_ROLL,
	VSL_IMU_PITCH,
	VSL_IMU_GYRO
} VesselIMUEnum;

static Bool vessel_execute_imu_params(char *prefix, ParList *list, Vessel vsl)
{
	ParTable imu_tab[] = {
		{ "x",			VSL_IMU_XOFF },
		{ "y",			VSL_IMU_YOFF },
		{ "z",			VSL_IMU_ZOFF },
		{ "roll",		VSL_IMU_ROLL },
		{ "pitch",		VSL_IMU_PITCH },
		{ "gyro",		VSL_IMU_GYRO },
		{ NULL,			VSL_IMU_UNKNOWN }
	};
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;

	node = list;
	do {
		node = params_match(node, prefix, imu_tab, &id, &match);
		switch (id) {
			case VSL_IMU_UNKNOWN:
				break;
			case VSL_IMU_XOFF:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to IMU "
						"x-axis offset for \"%s\".\n", match->data,
						prefix);
					return(False);
				}
				vsl->imu_x = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting IMU x-axis offset to %f (%s) for \"%s\".\n",
	dummy_float, match->data, prefix);
#endif
				break;
			case VSL_IMU_YOFF:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to IMU "
						"y-axis offset for \"%s\".\n", match->data,
						prefix);
					return(False);
				}
				vsl->imu_y = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting IMU y-axis offset to %f (%s) for \"%s\".\n",
	dummy_float, match->data, prefix);
#endif
				break;
			case VSL_IMU_ZOFF:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to IMU "
						"z-axis offset for \"%s\".\n", match->data, prefix);
					return(False);
				}
				vsl->imu_z = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting IMU z-axis offset to %f (%s) for \"%s\".\n",
	dummy_float, match->data, prefix);
#endif
				break;
			case VSL_IMU_ROLL:
				vsl->imu_r = atof(match->data);
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting IMU roll offset to %f (%s) for \"%s\".\n",
	vsl->imu_r, match->data, prefix);
#endif
				break;
			case VSL_IMU_PITCH:
				vsl->imu_p = atof(match->data);
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting IMU pitch offset to %f (%s) for \"%s\".\n",
	vsl->imu_p, match->data, prefix);
#endif
				break;
			case VSL_IMU_GYRO:
				vsl->imu_g = atof(match->data);
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting IMU gyro offset to %f (%s) for \"%s\".\n",
	vsl->imu_g, match->data, prefix);
#endif
				break;
			default:
				error_msgv(modname, "error: unknown return from parameter"
					" matching module (%d) for \"%s\".\n", id, prefix);
				return(False);
				break;
		}
	} while (node != NULL);
	return(True);
}

/* Routine:	vessel_execute_gps_params
 * Purpose:	Execute the subset of parameters which define the GPS setup
 * Inputs:	*prefix	Parameter prefix to use for matching
 *			*list	ParList to work through
 *			vsl		Pointer to Vessel parameter structure to fill in
 * Outputs:	True if list was parsed correctly, otherwise False
 * Comment:	This looks for offsets, and latency.
 */

typedef enum {
	VSL_GPS_UNKNOWN = 0,
	VSL_GPS_XOFF,
	VSL_GPS_YOFF,
	VSL_GPS_ZOFF,
	VSL_GPS_LATENCY
} VesselGPSEnum;

static Bool vessel_execute_gps_params(char *prefix, ParList *list, Vessel vsl)
{
	ParTable gps_tab[] = {
		{ "x",			VSL_GPS_XOFF },
		{ "y",			VSL_GPS_YOFF },
		{ "z",			VSL_GPS_ZOFF },
		{ "latency",	VSL_GPS_LATENCY },
		{ NULL,			VSL_GPS_UNKNOWN }
	};
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;

	node = list;
	do {
		node = params_match(node, prefix, gps_tab, &id, &match);
		switch (id) {
			case VSL_GPS_UNKNOWN:
				break;
			case VSL_GPS_XOFF:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to GPS "
						"x-axis offset for \"%s\".\n", match->data, prefix);
					return(False);
				}
				vsl->gps_x = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting GPS x-axis offset to %f (%s) for \"%s\".\n",
	dummy_float, match->data, prefix);
#endif
				break;
			case VSL_GPS_YOFF:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to GPS "
						"y-axis offset for \"%s\".\n", match->data, prefix);
					return(False);
				}
				vsl->gps_y = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting GPS y-axis offset to %f (%s) for \"%s\".\n",
	dummy_float, match->data, prefix);
#endif
				break;
			case VSL_GPS_ZOFF:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to GPS "
						"z-axis offset for \"%s\".\n", match->data, prefix);
					return(False);
				}
				vsl->gps_z = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting GPS z-axis offset to %f (%s) for \"%s\".\n",
	dummy_float, match->data, prefix);
#endif
				break;
			case VSL_GPS_LATENCY:
				dummy_float = params_translate_duration(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to GPS "
						"latency for \"%s\".\n", match->data, prefix);
					return(False);
				}
				vsl->gps_latency = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting GPS latency to %f (%s) for \"%s\".\n",
	dummy_float, match->data, prefix);
#endif
				break;
			default:
				error_msgv(modname, "error: unknown return from parameter"
					" matching module (%d) for \"%s\".\n", id, prefix);
				return(False);
				break;
		}
	} while (node != NULL);
	return(True);
}

/* Routine:	vessel_execute_head_params
 * Purpose:	Match parameters required for a static head definition
 * Inputs:	*prefix	Parameter prefix for matching
 *			*list	Parameter list to search
 *			vsl		Pointer to the parameters to fill in
 * Outputs:	True if list was parsed correctly, otherwise False
 * Comment:	This checks for the parameters used to define a tdr head ---
 *			basically everything except a timestamp.  Use prefix="vessel" to
 *			define the default head, otherwise "vessel.port", or "vessel.stbd"
 *			etc. as appropriate.  Note that this doesn't do anything about
 *			copying parameters between default and specific heads, etc.
 */

typedef enum {
	VSL_HD_UNKNOWN = 0,
	VSL_HD_DRAFT,
	VSL_HD_STAT_ROLL,
	VSL_HD_TX_LATENCY
} VslHeadEnum;

static Bool vessel_execute_head_params(char *prefix, ParList *list, Vessel vsl)
{
	ParTable base_tab[] = {
		{ "draft",			VSL_HD_DRAFT },
		{ "static_roll",	VSL_HD_STAT_ROLL },
		{ "tx_latency",		VSL_HD_TX_LATENCY },
		{ NULL,				VSL_HD_UNKNOWN }
	};
	Bool	rc;
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;
	char	buffer[256];

	node = list;
	do {
		node = params_match(node, prefix, base_tab, &id, &match);
		switch (id) {
			case VSL_HD_UNKNOWN:
				/* Matched nothing ... but that may just mean that there's
				 * nothing there for this module.
				 */
				break;
			case VSL_HD_DRAFT:
				dummy_float = params_translate_length(match->data);
				if (dummy_float == DBL_MAX) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" length measurement for \"%s\".\n", match->data,
						prefix);
					return(False);
				}
				vsl->draft = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default vessel draft to %f (%s) for \"%s\".\n",
	dummy_float, match->data, prefix);
#endif
				break;
			case VSL_HD_STAT_ROLL:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_STAT_ROLL || dummy_float > MAX_STAT_ROLL) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" angle measurement for \"%s\".\n", match->data,
						prefix);
					return(False);
				}
				vsl->static_roll = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default vessel static roll to %f (%s)"
	" for \"%s\".\n", dummy_float, match->data, prefix);
#endif
				break;
			case VSL_HD_TX_LATENCY:
				dummy_float = params_translate_duration(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" duration sdev measurement for \"%s\".\n", match->data,
						prefix);
					return(False);
				}
				vsl->tx_latency_sdev = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default vessel tx latency sdev to %f (%s)"
	" for \"%s\".\n", dummy_float, match->data, prefix);
#endif
				break;
			default:
				error_msgv(modname, "error: unknown return from parameter"
					" matching module (%d).\n", id);
				return(False);
				break;
		}
	} while (node != NULL);

	rc = True;
	sprintf(buffer, "%s.gps", prefix);
	rc &= vessel_execute_gps_params(buffer, list, vsl);
	sprintf(buffer, "%s.imu", prefix);
	rc &= vessel_execute_imu_params(buffer, list, vsl);

	return(rc);
}

/* Routine:	vessel_execute_params
 * Purpose:	Execute parameters list in this module
 * Inputs:	*list	ParList to work through
 * Outputs:	True if list was parsed correctly, otherwise False
 * Comment:	This looks for a number of parameters and sub-parameter modules
 *			that describe the configuration of the vessel, and the measurement
 *			accuracies associated with these offsets, and the auxilliary
 *			instrument package.
 */

typedef enum {
	VSL_UNKNOWN = 0,
	VSL_TIMESTAMP,
	VSL_SONARTYPE
} VslParEnum;

Bool vessel_execute_params(ParList *list)
{
	ParTable base_tab[] = {
		{ "timestamp",		VSL_TIMESTAMP },
		{ "sonar_type",		VSL_SONARTYPE },
		{ NULL,				VSL_UNKNOWN }
	};
	Bool	rc;
	ParList	*node, *match;
	Device	dev;
	u32		id;
	f64		dummy_float;

	vessel_init_default();
	
	node = list;
	do {
		node = params_match(node, "vessel", base_tab, &id, &match);
		switch (id) {
			case VSL_UNKNOWN:
				/* Matched nothing ... but that may just mean that there's
				 * nothing there for this module.
				 */
				break;
			case VSL_TIMESTAMP:
				dummy_float = stime_translate_timestamp(match->data);
				if (dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" post epoch timestamp.\n", match->data);
					return(False);
				}
				default_vessel.timestamp = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default vessel timestamp to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case VSL_SONARTYPE:
				if ((dev = device_new_by_name(match->data)) == NULL) {
					error_msgv(modname, "error: failed to convert \"%s\" to known"
						" sonar device.\n", match->data);
					return(False);
				}
				default_vessel.device = device_get_device(dev);
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default vessel device type to %s (%s).\n",
		   match->data, device_get_name(dev));
#endif
				device_release(dev);
				match->used = True;
				break;
			default:
				error_msgv(modname, "error: unknown return from parameter"
					" matching module (%d).\n", id);
				return(False);
				break;
		}
	} while (node != NULL);
	
	/* We now parse the head specific parameters from the file.  The first time,
	 * we read them to prefix 'vessel', and setting the default_vessel structure
	 * to set the base definitions.  We then read with prefices 'vessel.port'
	 * and 'vessel.stbd' to set specific heads, if they are available.  We also
	 * copy the default definition into the two heads, so that the file only
	 * needs to reset anything which is actively different between the two
	 * heads.  We then set the configuration by checking whether either of the
	 * heads is different from the default definition.
	 */

	rc = True;
	rc &= vessel_execute_sdev_params("vessel.sdev", list, &default_vessel);
			/* Note that standard deviation settings are going to be the same
			 * for any head in the set, unless for some completely obscure
			 * reason they are defined to use different IMUs and GPSs.  That
			 * would be too wierd to imagine, so we don't.
			 */
	rc &= vessel_execute_head_params("vessel", list, &default_vessel);
	memcpy(&port_head, &default_vessel, sizeof(struct _vessel));
	memcpy(&stbd_head, &default_vessel, sizeof(struct _vessel));
	config = VESSEL_SINGLE_HEAD;	/* Set default valid */
	
	rc &= vessel_execute_head_params("vessel.port", list, &port_head);	
	rc &= vessel_execute_head_params("vessel.stbd", list, &stbd_head);
	
	if (memcmp(&default_vessel, &port_head, sizeof(struct _vessel)) != 0 ||
		memcmp(&default_vessel, &stbd_head, sizeof(struct _vessel)) != 0)
		config = VESSEL_DUAL_HEAD;

	return(rc);
}
