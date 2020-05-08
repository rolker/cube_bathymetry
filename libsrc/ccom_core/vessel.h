/*
 * $Id: vessel.h 2 2003-02-03 20:18:41Z brc $
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
 * Revision 1.4  2002/03/14 04:10:36  brc
 * Modifications to vessel description code in order to allow dual-head systems
 * to be specified.  The normal situation is a single head, but optional port{}
 * and stbd{} clauses are allowed to extend the definition.  Note that split-head
 * transducers which are nominally one device (e.g. Elac 1180) are specified as
 * dual-head so that the different offsets are correctly represented.
 *
 * Revision 1.3  2001/05/14 04:24:39  brc
 * Updated to make module 'params'-aware
 *
 * Revision 1.2  2001/02/10 19:23:53  brc
 * C++-ified the header file to ensure correct namespace conventions when used
 * with a C++ compiler.
 *
 * Revision 1.1  2001/02/10 19:19:41  brc
 * Prototype interface to vessel description files, essentially just bodged in
 * to make the error modeling code operational.  There is at present no way
 * of defining a vessel except within the default values in the source code, and
 * no way to write out a definition.  These will obvious be along RSN.  Honest.
 *
 *
 * File:	vessel.h
 * Purpose:	Encapsulate vessel configurations
 * Date:	24 November 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __VESSEL_H__
#define __VESSEL_H__

typedef struct _vessel *Vessel;

#include <stdio.h>
#include "stdtypes.h"
#include "error.h"
#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	VESSEL_DEFAULT = 0,
	VESSEL_SINGLE_HEAD,
	VESSEL_DUAL_HEAD
} VesselSensorConfig;

typedef enum {
	VESSEL_HEAD_ANY = 0,
	VESSEL_HEAD_PORT,
	VESSEL_HEAD_STBD
} VesselHead;

typedef struct _vessel {
	f64		timestamp;			/* Timestamp for when configuration was valid */
	DeviceType	device;			/* Type of sonar system for which this definition
								 * is applicable. */
	
	/* Direct offsets, usu. measured wrt transducer head */
	f64	gps_x, gps_y, gps_z;	/* GPS offsets, m */
	f64	gps_off_sdev;			/* SDev of GPS offset measurements, m */
	f64 gps_latency_sdev;		/* SDev of GPS latency error, s. */
	f64	gps_drms;				/* Circularly symmetric GPS position error, m. */
	f64	gps_latency;			/* Latency between GPS and logger, s. */
		
	f64	imu_x, imu_y, imu_z;	/* IMU offsets, m */
	f64	imu_off_sdev;			/* SDev of IMU offset measurements, m */
	f64 imu_r, imu_p, imu_g;	/* IMU alignment, deg. */
	f64	imu_rp_align_sdev;		/* SDev of IMU roll/pitch alignment err., deg */
	f64	imu_g_align_sdev;		/* SDev of IMU heading alignment err, deg. */
	f64	imu_latency_sdev;		/* SDev of IMU latency error, s. */
	
	f64 draft;					/* Draft of tx head below water line, m */
	f64	tx_latency_sdev;		/* SDev of tx head latency, s. */
	f64	static_roll;			/* Static head roll (mounting angle), deg. */
	
	/* Measurement accuracies */
	f64	roll_sdev;				/* SDev of roll measurement, deg. */
	f64	pitch_sdev;				/* SDev of pitch measurement, deg. */
	f64	pitch_stab_sdev;		/* SDev of pitch stabilisation, deg. */
	f64	gyro_sdev;				/* SDev of gyro measurement, deg. */
	f64	svp_sdev;				/* SDev of SVP cast measurements, m/s */
	f64	surf_ss_sdev;			/* SDev of sound speed surface meas., m/s */
	
	/* Other components */
	f64	heave_fixed_sdev;		/* SDev of heave measurements, m */
	f64	heave_var_percent;		/* Percentage of measurement variable error */
	f64	draft_sdev;				/* SDev of water level measurements, m */
	f64	ddraft_sdev;			/* SDev of dynamic draft measurements, m */
	f64	loading_sdev;			/* SDev of platform loading meas., m */
	f64	sog_sdev;				/* SDev of speed-over-ground meas., m/s */
	f64	tide_measured_sdev;		/* SDev of tide guage readings, m */
	f64	tide_predicted_sdev;	/* SDev of tide prediction error, m */
} *Vessel;	/* Vessel systems parameters summary */

/* Routine:	vessel_new
 * Purpose:	Generate a blank new vessel configuration structure
 * Inputs:	-
 * Outputs:	Pointer to Vessel, or NULL on failure.
 * Comment:	This default vessel has timestamp set to epoch and all offsets and
 *			measurement std. dev.s set to zero.
 */

extern Vessel vessel_new(void);

/* Routine:	vessel_default
 * Purpose:	Make a default vessel configuration
 * Inputs:	-
 * Outputs:	Returns pointer to default vessel configuration, or NULL on error
 * Comment:	The default configuration consists of zeroed offsets, and default
 *			values representing typical measurement accuracies for the
 *			components involved in the vessel configuration.  These measurement
 *			accuracies are based on Hare, Godin & Mayer 1995, "Accuracy
 *			Estimation of Canadian Swath (multibeam) and Sweel (multi-transducer)
 *			sounding systems.
 */

extern Vessel vessel_default(void);

/* Routine:	vessel_get_config
 * Purpose:	Return vessel configuration status for default vessel
 * Inputs:	-
 * Outputs:	Returns a VesselConfig enum
 * Comment:	The library will return VESSEL_DEFAULT until a user set of
 *			parameters are actioned, after which it returns VESSEL_SINGLE_HEAD
 *			or VESSEL_DUAL_HEAD as appropriate.
 */

extern VesselSensorConfig vessel_get_config(void);

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

extern Vessel vessel_get_head(VesselHead head);

/* Routine:	vessel_release
 * Purpose:	Remove memory associated with a vessel structure
 * Inputs:	vessel	Structure to be removed
 * Outputs:	-
 * Comment:	-
 */

extern void vessel_release(Vessel vessel);

/* Routine:	vessel_new_from_file
 * Purpose:	Retrieve a vessel description from file
 * Inputs:	*f	FILE to read from
 * Outputs:	Returns pointer to Vessel structure, or NULL on error
 * Comment:	-
 */

extern Vessel vessel_new_from_file(FILE *f);

/* Routine:	vessel_write
 * Purpose:	Write a vessel description to file
 * Inputs:	vessel	Vessel description to write
 *			*f		FILE to write to
 * Outputs:	True on success, otherwise False
 * Comment:	-
 */

extern Bool vessel_write(Vessel vessel, FILE *f);

/* Routine:	vessel_execute_params
 * Purpose:	Execute parameters list in this module
 * Inputs:	*list	ParList to work through
 * Outputs:	True if list was parsed correctly, otherwise False
 * Comment:	This looks for a number of parameters and sub-parameter modules
 *			that describe the configuration of the vessel, and the measurement
 *			accuracies associated with these offsets, and the auxilliary
 *			instrument package.
 */

#include "params.h"

extern Bool vessel_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
