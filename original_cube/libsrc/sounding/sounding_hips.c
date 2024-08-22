/*
 * $Id: sounding_hips.c 16 2003-07-23 00:55:00Z brc $
 * $Log$
 * Revision 1.2  2003/07/23 00:55:00  brc
 * Merged modifications from development stream
 *
 * Revision 1.1.1.1  2003/02/03 20:18:45  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.3.2.4  2003/05/31 21:44:24  brc
 * Made the HDCS parser work its way along all pathnames, and
 * convert / to \ so that the rest of the code is more robust in a
 * mixed Unix (e.g., CygWIN) and Win32 environment.
 *
 * Revision 1.3.2.3  2003/03/24 02:17:29  brc
 * Added return code check to IO initialisation so that the reason for failure is reported
 * if the initialisation fails.
 *
 * Revision 1.3.2.2  2002/12/15 01:47:39  brc
 * Added a check to ensure that when a ping comes back with all of its
 * beams marked as 'not for use', the code doesn't die on the spot, but
 * instead reports that there are no valid beams (and doesn't waste time
 * doing any more computation).
 *
 * Revision 1.3.2.1  2002/07/14 02:20:37  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.3  2002/06/15 03:33:46  brc
 * Added 'time' variable to the attitude readers --- absolutely no idea how
 * this worked before, since they use 'time' internally, but don't have
 * anything being passed in.  Best guess is that the location in memory of the
 * time(2) function was being used (i.e., a pointer to function) and that was
 * always suitably high in the VM space in order to make the VCF read valid.
 * Also modified the ..._get_next() code to read the flags out of the beam
 * status word and add them into the flags element on the beam, truncating
 * at eight bits of status.  Currently, this gives the overall 'flagged' bit,
 * plus the most important methods of flagging (area, swath, hydrographer,
 * automatic, etc.) but excludes the quality bits.  Note that if the 'honour_
 * flags' flag is turned on by the configuration file, all beams which are
 * flagged are not even reported to the upper levels of code, so no flags will
 * be observed in the data structure.
 *
 * Revision 1.2  2002/05/10 22:07:10  brc
 * Added ability to honour the flags in a HIPS/HDCS structure based on a
 * parameter file switch.  This is made slightly more efficient by checking for
 * flags at the line and profile level, as well as the sounding level.
 *
 * Revision 1.1  2002/03/14 04:13:51  brc
 * Modules used to read CARIS/HIPS(HDCS) data via. the hipsio library.
 *
 *
 * File:	sounding_hips.c
 * Purpose:	Read and parse HIPS/IO HDCS files (with the aid of a safety net)
 * Date:	4 Feb 2002
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
#include <math.h>
#include <time.h>
#include "stdtypes.h"
#include "error.h"
#include "device.h"
#include "sounding.h"
#include "interp.h"
#include "sounding_private.h"
#include "sounding_hips.h"
#include "params.h"
#include "nav.h"

/* Headers for the HIPS/IO Library */
#ifdef WIN32
#include "HIPSio/fileio.h"
#include "HIPSio/fileioP.h"
#include "HIPSio/procssdDepths.h"
#include "HIPSio/navigation.h"
#include "HIPSio/gyro.h"
#include "HIPSio/heave.h"
#include "HIPSio/roll.h"
#include "HIPSio/pitch.h"
#include "HIPSio/sow.h"
#include "HIPSio/vesselConfig.h"
#else
#include <rpc/rpc.h>
#include "fileio.h"
#include "fileioP.h"
#include "procssdDepths.h"
#include "navigation.h"
#include "gyro.h"
#include "heave.h"
#include "roll.h"
#include "pitch.h"
#include "sow.h"
#include "vesselConfig.h"
#endif

#define MIN_SURF_SSPEED	1400.0
#define MAX_SURF_SSPEED	1600.0
#define DEFAULT_SURF_SSPEED	1500.0

#define MIN_AMP_PHASE_TRANS		5
#define MAX_AMP_PHASE_TRANS 	50
#define DEFAULT_AMP_PHASE_TRANS	12

#define RAD2DEG(x)	((x)*180.0/M_PI)
#define DEG2RAD(x)	((x)*M_PI/180.0)
#define MIN(a,b)	((a)<(b)?(a):(b))
#define MAX(a,b)	((a)>(b)?(a):(b))

#define CARIS_DATUM_FILE		"C:\\CARIS\\system\\datum.dat"
#define CARIS_TEMP_DIRECTORY	"C:\\Temp"

#ifdef WIN32
#define DIRECTORY_SEP	'\\'
#else
#define DIRECTORY_SEP	'/'
#endif

#undef __DEBUG__
#undef __DUMP_ATT__	/* Force dump of attitude data to disc */

static char *modname = "sounding_hips";
static char *modrev = "$Revision";

static f64	default_surf_sspeed = DEFAULT_SURF_SSPEED;
static u32	default_amp_phase_trans = DEFAULT_AMP_PHASE_TRANS;
static Bool honour_flags = False;
static Bool library_initialised = False;

static u32	file_counter = 0;

typedef struct {
	Interp	gyro, heave, pitch, roll, sow;
	HDCS_ProcessedDepths	fileHandle;
	HIPS_Spec	*spec;	/* Structure for path components required to open files. */
	u32		nProfiles;
	u32		curProfile;
	Bool	is_dualhead_vessel;
	f64		emin, emax;	/* Minimum and maximum epochs for which attitude data
						 * exists.
						 */
	Bool	honour_flags;		/* Copied from external parameters on init */
	f64		surf_sspeed;		/* " */
	u32		amp_phase_trans;	/* " */
	char	*project_base,		/* These parameters are required for the HIPS/IO */
			*datum,				/* initialisation, added in newer versions of the */
			*scratch;			/* library.  Also use project_base to reset location
								 * of the base data structures when switching back
								 * to this stream, in case it's changed. */
} HIPSBuffer;		/* Internal buffer structure for reading data */

/* Routine:	sounding_hips_lib_interlock
 * Purpose:	Check that the HIPS/IO library is initialised
 * Inputs:	*par	HIPSBuffer with the stream location parameters
 * Outputs:	True if library is initialised, otherwise False
 * Comment:	This adds an extra burden to evey call, and hence should be used
 *			sparingly --- it will also force a license check when it is first
 *			called.  The HIPS/IO documentation say that it should only be called
 *			once per session, before any other calls.  The code here uses a
 *			file static variable to make sure the call only happens once, and
 *			all library routines call here before making any HIPS/IO calls.
 */

static Bool sounding_hips_lib_interlock(HIPSBuffer *par)
{
	s32	rc;

	if (!library_initialised) {
		if (par == NULL) {
			error_msg(modname, "error: HIPS parameter structure is not initialised!\n");
			return(False);
		}
#ifdef WIN32
		if ((rc = HDCS_InitFileIO(par->project_base, par->datum, par->scratch)) != OK) {
#else
		if ((rc = HDCS_InitFileIO()) != OK) {
#endif
			error_msgv(modname, "error: failed to initialise HIPS/IO (error code %d).\n", rc);
		} else
			library_initialised = True;
	}
	return(library_initialised);
}

	static void sounding_hips_release_spec(HIPS_Spec *spec)
{
	if (spec != NULL) {
		free(spec->prefix);
		free(spec->project);
		free(spec->vessel);
		free(spec->day);
		free(spec->line);
	}
	free(spec);
}

void sounding_hips_release(void *buffer)
{
	HIPSBuffer	*buf = (HIPSBuffer*)buffer;
	
	if (!sounding_hips_lib_interlock(buf)) {
		error_msg(modname, "error: release can't initialise library.\n");
		return;
	}
	
	if (buf != NULL) {
		ProcessedDepthsClose(buf->fileHandle);
		sounding_hips_release_spec(buf->spec);
		interp_release(buf->gyro);
		interp_release(buf->heave);
		interp_release(buf->pitch);
		interp_release(buf->roll);
		interp_release(buf->sow);
		free(buf->project_base); free(buf->datum); free(buf->scratch);
		free(buf);
	}
}

static HIPS_Spec *sounding_hips_parse_spec(const char *filename)
{
	HIPS_Spec	*spec;
	char		*buffer, *ptr;

	if ((buffer = strdup(filename)) == NULL) return(NULL);
#ifdef WIN32
	/* Under WIN32, although it should work with / or \, things
	 * can get a little confused in some cases.  We translate all
	 * occurrences of / into \ to make sure that everything is
	 * going to work out OK later.
	 */
	ptr = buffer;
	while (*ptr != '\0') {
		if (*ptr == '/') *ptr = DIRECTORY_SEP;
		++ptr;
	}
#endif
	if ((spec = (HIPS_Spec*)calloc(1, sizeof(HIPS_Spec))) == NULL)
		return(NULL);

	if ((ptr = strrchr(buffer, DIRECTORY_SEP)) == NULL ||
		(spec->line = strdup(ptr+1)) == NULL) {
		free(buffer); sounding_hips_release_spec(spec);
		return(NULL);
	}
	*ptr = '\0';

	if ((ptr = strrchr(buffer, DIRECTORY_SEP)) == NULL ||
		(spec->day = strdup(ptr+1)) == NULL) {
		free(buffer); sounding_hips_release_spec(spec);
		return(NULL);
	}
	*ptr = '\0';
	
	if ((ptr = strrchr(buffer, DIRECTORY_SEP)) == NULL ||
		(spec->vessel = strdup(ptr+1)) == NULL) {
		free(buffer); sounding_hips_release_spec(spec);
		return(NULL);
	}
	*ptr = '\0';
	
	if ((ptr = strrchr(buffer, DIRECTORY_SEP)) == NULL)
		ptr = buffer;	/* Map whole remaining component if no more seps. */
	else ++ptr;
	if ((spec->project = strdup(ptr)) == NULL) {
		free(buffer); sounding_hips_release_spec(spec);
		return(NULL);
	}
	if (ptr == buffer)
		spec->prefix = NULL;
	else {
		*--ptr = '\0';
		if ((spec->prefix = strdup(buffer)) == NULL) {
			free(buffer); sounding_hips_release_spec(spec);
			return(NULL);
		}
	}

	free(buffer);
#ifdef __DEBUG__
	error_msgv(modname, "debug: prefix=\"%s\" project=\"%s\" vessel=\"%s\" day=\"%s\" line=\"%s\".\n",
		spec->prefix, spec->project, spec->vessel, spec->day, spec->line);
#endif
	return(spec);
}

static HIPSBuffer *sounding_hips_buffer_new(char *basedir)
{
	HIPSBuffer	*rtn;

	if ((rtn = (HIPSBuffer*)calloc(1, sizeof(HIPSBuffer))) == NULL) {
		error_msg(modname, "error: failed to allocate memory for HIPSBuffer.\n");
		return(NULL);
	}
	rtn->amp_phase_trans = default_amp_phase_trans;
	rtn->honour_flags = honour_flags;
	rtn->surf_sspeed = default_surf_sspeed;
	if ((rtn->project_base = strdup(basedir)) == NULL ||
		(rtn->datum = strdup(CARIS_DATUM_FILE)) == NULL ||
		(rtn->scratch = strdup(CARIS_TEMP_DIRECTORY)) == NULL) {
		error_msg(modname, "error: failed to allocate memory for HIPSBuffer.\n");
		sounding_hips_release(rtn);
		return(NULL);
	}
	return(rtn);
}

static Bool sounding_hips_read_gyro(f64 time, HIPSBuffer *store,
									VesselConfig *vessel)
{
	HDCS_GyroSensor	h_gyro;
	u32				lineSegs, nRecords, rec;
	f64				timestamp, minTime, maxTime, gyro, minGyro, maxGyro;
#ifndef WIN32
	f64				accuracy;
#endif
	f32				f_gyro;
	u32				summary;
	s32				status;
	
	/* Parameters read from the VCF to get at the latency value */
	char			*manuf, *model, *serial, *comment, *auth;
	u32				apply;
	f64				dx, dy, dz, latency, offset;
	u32				rcode;

#ifdef __DEBUG__
	error_msg(modname, "debug: processing gyro data ... ");
	error_flush_output();
#endif
	if ((rcode = VesselGyroSensorTmIdxConfig(vessel, time,
			&timestamp, &manuf, &model, &serial, &dx, &dy, &dz, &latency,
			&offset, &apply, &comment, &auth)) != OK) {
		error_msgv(modname, "error: failed to obtain gyro offsets from VCF (error code; %d).\n", rcode);
		return(False);
	}
	{
		char *tmp;
		if ((tmp = (char*)malloc(30000)) == NULL) {
			error_msg(modname, "error: malloc failed!\n");
			return(False);
		}
		free(tmp);
	}
//	free(manuf); free(model); free(serial); free(comment); free(auth);

#ifdef __DEBUG__
	error_msgv(NULL, "gyro at (%.3lf, %.3lf, %.3lf) m, latency"
		" %.3lf s, error %.3lf deg. ... ", dx, dy, dz, latency, offset);
#endif

	if ((store->gyro = interp_new(1, latency)) == NULL) {
		error_msg(modname, "error: failed to create Gyro interp buffer.\n");
		return(False);
	}
#ifdef WIN32
	if ((h_gyro = GyroOpen(store->spec->project, store->spec->vessel, store->spec->day,
						   store->spec->line, "query", &rcode)) == NULL) {
#else
	if ((h_gyro = GyroOpen(store->spec->project, store->spec->vessel, store->spec->day,
						   store->spec->line, "query")) == NULL) {
#endif
		error_msg(modname, "error: failed to open gyro.\n");
		return(False);
	}
	GyroSummary(h_gyro, &lineSegs, &nRecords, &minTime, &maxTime,
				&minGyro, &maxGyro, &summary);

	for (rec = 0; rec < nRecords; ++rec) {
#ifdef WIN32
		status = GyroReadSeq(h_gyro, &timestamp, &gyro, &summary);
#else
		status = GyroReadSeq(h_gyro, &timestamp, &gyro, &accuracy, &summary);
#endif
		f_gyro = (f32)RAD2DEG(gyro);
		if (status != OK || !interp_insert(store->gyro, timestamp, &f_gyro)) {
			error_msgv(modname, "error: failed inserting gyro point %d.\n",
				rec);
			GyroClose(h_gyro);
			return(False);
		}
	}
	GyroClose(h_gyro);
#ifdef __DEBUG__
#ifdef __DUMP_ATT__
	interp_dump(store->gyro, "gyro");
#endif
	error_msg(NULL, "done.\n");
#endif
	
	return(True);
}

static Bool sounding_hips_read_heave(f64 time, HIPSBuffer *store,
									 VesselConfig *vessel)
{
	HDCS_HeaveSensor	h_heave;
	u32					lineSegs, nRecords, rec;
	f64					timestamp, minTime, maxTime, heave, minHeave, maxHeave;
#ifndef WIN32
	f64					accuracy;
#endif
	f32					f_heave;
	u32					summary;
	s32					status;
	
	/* Parameters read from the VCF to get at the latency value */
	char			*manuf, *model, *serial, *comment, *auth;
	u32				apply;
	f64				dx, dy, dz, latency, offset;
	u32				rcode;

#ifdef __DEBUG__
	error_msg(modname, "debug: processing heave data ... ");
	error_flush_output();
#endif
	if ((rcode = VesselHeaveSensorTmIdxConfig(vessel, time,
			&timestamp, &manuf, &model, &serial, &dx, &dy, &dz, &latency,
			&offset, &apply, &comment, &auth)) != OK) {
		error_msgv(modname, "error: failed to obtain heave offsets from VCF (error code: %d).\n", rcode);
		return(False);
	}
//	free(manuf); free(model); free(serial); free(comment); free(auth);

#ifdef __DEBUG__
	error_msgv(NULL, "heave at (%.3lf, %.3lf, %.3lf) m, latency"
		" %.3lf s, error %.3lf m. ... ", dx, dy, dz, latency, offset);
#endif
	
	if ((store->heave = interp_new(1, latency)) == NULL) {
		error_msg(modname, "error: failed to create heave interp buffer.\n");
		return(False);
	}
#ifdef WIN32
	if ((h_heave = HeaveOpen(store->spec->project, store->spec->vessel, store->spec->day,
							 store->spec->line, "query", &rcode)) == NULL) {
#else
	if ((h_heave = HeaveOpen(store->spec->project, store->spec->vessel, store->spec->day,
							 store->spec->line, "query")) == NULL) {
#endif
		error_msg(modname, "error: failed to open heave.\n");
		return(False);
	}
	HeaveSummary(h_heave, &lineSegs, &nRecords, &minTime, &maxTime,
				&minHeave, &maxHeave, &summary);

	for (rec = 0; rec < nRecords; ++rec) {
#ifdef WIN32
		status = HeaveReadSeq(h_heave, &timestamp, &heave, &summary);
#else
		status = HeaveReadSeq(h_heave, &timestamp, &heave, &accuracy, &summary);
#endif
		f_heave = (f32)heave;
		if (status != OK || !interp_insert(store->heave, timestamp, &f_heave)) {
			error_msgv(modname, "error: failed inserting heave point %d.\n",
				rec);
			HeaveClose(h_heave);
			return(False);
		}
	}
	HeaveClose(h_heave);
#ifdef __DEBUG__
#ifdef __DUMP_ATT__
	interp_dump(store->heave, "heave");
#endif
	error_msg(NULL, "done.\n");
#endif
	
	return(True);
}

static Bool sounding_hips_read_pitch(f64 time, HIPSBuffer *store,
									 VesselConfig *vessel)
{
	HDCS_PitchSensor	h_pitch;
	u32					lineSegs, nRecords, rec;
	f64					timestamp, minTime, maxTime, pitch, minPitch, maxPitch;
#ifndef WIN32
	f64					accuracy;
#endif
	f32					f_pitch;
	u32					summary;
	s32					status;
	
	/* Parameters read from the VCF to get at the latency value */
	char			*manuf, *model, *serial, *comment, *auth;
	u32				apply;
	f64				dx, dy, dz, latency, offset;
	u32				rcode;

#ifdef __DEBUG__
	error_msg(modname, "debug: processing pitch data ... ");
	error_flush_output();
#endif
	if ((rcode = VesselPitchSensorTmIdxConfig(vessel, time,
			&timestamp, &manuf, &model, &serial, &dx, &dy, &dz, &latency,
			&offset, &apply, &comment, &auth)) != OK) {
		error_msgv(modname, "error: failed to obtain pitch offsets from VCF (error code: %d).\n", rcode);
		return(False);
	}
//	free(manuf); free(model); free(serial); free(comment); free(auth);

#ifdef __DEBUG__
	error_msgv(NULL, "pitch at (%.3lf, %.3lf, %.3lf) m, latency"
		" %.3lf s, error %.3lf deg. ... ", dx, dy, dz, latency, offset);
#endif
	
	if ((store->pitch = interp_new(1, latency)) == NULL) {
		error_msg(modname, "error: failed to create heave interp buffer.\n");
		return(False);
	}
#ifdef WIN32
	if ((h_pitch = PitchOpen(store->spec->project, store->spec->vessel, store->spec->day,
							 store->spec->line, "query", &rcode)) == NULL) {
#else
	if ((h_pitch = PitchOpen(store->spec->project, store->spec->vessel, store->spec->day,
							 store->spec->line, "query")) == NULL) {
#endif
		error_msg(modname, "error: failed to open heave.\n");
		return(False);
	}
	PitchSummary(h_pitch, &lineSegs, &nRecords, &minTime, &maxTime,
				&minPitch, &maxPitch, &summary);

	for (rec = 0; rec < nRecords; ++rec) {
#ifdef WIN32
		status = PitchReadSeq(h_pitch, &timestamp, &pitch, &summary);
#else
		status = PitchReadSeq(h_pitch, &timestamp, &pitch, &accuracy, &summary);
#endif
		f_pitch = (f32)RAD2DEG(pitch);
		if (status != OK || !interp_insert(store->pitch, timestamp, &f_pitch)) {
			error_msgv(modname, "error: failed inserting pitch point %d.\n",
				rec);
			PitchClose(h_pitch);
			return(False);
		}
	}
	PitchClose(h_pitch);
#ifdef __DEBUG__
#ifdef __DUMP_ATT__
	interp_dump(store->pitch, "pitch");
#endif
	error_msg(NULL, "done.\n");
#endif
	
	return(True);
}

static Bool sounding_hips_read_roll(f64 time, HIPSBuffer *store,
									VesselConfig *vessel)
{
	HDCS_RollSensor	h_roll;
	u32				lineSegs, nRecords, rec;
	f64				timestamp, minTime, maxTime, roll, minRoll, maxRoll;
#ifndef WIN32
	f64				accuracy;
#endif
	f32				f_roll;
	u32				summary;
	s32				status;
	
	/* Parameters read from the VCF to get at the latency value */
	char			*manuf, *model, *serial, *comment, *auth;
	u32				apply;
	f64				dx, dy, dz, latency, offset;
	u32				rcode;

#ifdef __DEBUG__
	error_msg(modname, "debug: processing roll data ... ");
	error_flush_output();
#endif
	if ((rcode = VesselRollSensorTmIdxConfig(vessel, time,
			&timestamp, &manuf, &model, &serial, &dx, &dy, &dz, &latency,
			&offset, &apply, &comment, &auth)) != OK) {
		error_msgv(modname, "error: failed to obtain roll offsets from VCF (error code: %d).\n", rcode);
		return(False);
	}
//	free(manuf); free(model); free(serial); free(comment); free(auth);

#ifdef __DEBUG__
	error_msgv(NULL, "roll at (%.3lf, %.3lf, %.3lf) m, latency"
		" %.3lf s, error %.3lf deg. ... ", dx, dy, dz, latency, offset);
#endif
	
	if ((store->roll = interp_new(1, latency)) == NULL) {
		error_msg(modname, "error: failed to create roll interp buffer.\n");
		return(False);
	}
#ifdef WIN32
	if ((h_roll = RollOpen(store->spec->project, store->spec->vessel, store->spec->day,
						   store->spec->line, "query", &rcode)) == NULL) {
#else
	if ((h_roll = RollOpen(store->spec->project, store->spec->vessel, store->spec->day,
						   store->spec->line, "query")) == NULL) {
#endif
		error_msg(modname, "error: failed to open roll.\n");
		return(False);
	}
	RollSummary(h_roll, &lineSegs, &nRecords, &minTime, &maxTime,
				&minRoll, &maxRoll, &summary);

	for (rec = 0; rec < nRecords; ++rec) {
#ifdef WIN32
		status = RollReadSeq(h_roll, &timestamp, &roll, &summary);
#else
		status = RollReadSeq(h_roll, &timestamp, &roll, &accuracy, &summary);
#endif
		f_roll = (f32)RAD2DEG(roll);
		if (status != OK || !interp_insert(store->roll, timestamp, &f_roll)) {
			error_msgv(modname, "error: failed inserting roll point %d.\n",
				rec);
			RollClose(h_roll);
			return(False);
		}
	}
	RollClose(h_roll);
#ifdef __DEBUG__
#ifdef __DUMP_ATT__
	interp_dump(store->roll, "roll");
#endif
	error_msg(NULL, "done.\n");
#endif
	
	return(True);
}

static Bool sounding_hips_read_nav(HIPSBuffer *store, Projection proj)
{
	HDCS_NavigationSensor	h_nav;
	Nav		nav;
	u32		lineSegs, nRecords, rec, coordType;
	f64		timestamp, minTime, maxTime, minLat, maxLat, minLon, maxLon,
			lat, lon, accuracy, sog;
	f32		f_sog;
	u32		summary;
	s32		status;
#ifdef WIN32
	long	rcode;
#endif

#ifdef __DEBUG__
	error_msg(modname, "debug: processing navigation data ... ");
	error_flush_output();
#endif
	
	if ((nav = nav_new()) == NULL) {
		error_msg(modname, "error: failed to create navigational data temporary"
			" buffer for SoG computation.\n");
		return(False);
	}
	
	if ((store->sow = interp_new(1, 0.0)) == NULL) {
		error_msg(modname, "error: failed to create sow interp buffer.\n");
		return(False);
	}
#ifdef WIN32
	if ((h_nav = NavigationOpen(store->spec->project, store->spec->vessel, store->spec->day,
								store->spec->line, "query", &rcode)) == NULL) {
#else
	if ((h_nav = NavigationOpen(store->spec->project, store->spec->vessel, store->spec->day,
								store->spec->line, "query")) == NULL) {
#endif
		error_msg(modname, "error: failed to open nav.\n");
		nav_release(nav);
		return(False);
	}
	NavigationSummary(h_nav, &lineSegs, &nRecords, &coordType,
					  &minTime, &maxTime,
					  &minLat, &maxLat, &minLon, &maxLon, &summary);

	for (rec = 0; rec < nRecords; ++rec) {
		status = NavigationReadSeq(h_nav, &timestamp, &lat, &lon, &accuracy,
								   &summary);
		if (status != OK || !nav_insert(nav, timestamp,
												RAD2DEG(lon), RAD2DEG(lat))) {
			error_msgv(modname, "error: failed inserting navigation point %d.\n",
				rec);
			NavigationClose(h_nav);
			nav_release(nav);
			return(False);
		}
	}
	NavigationClose(h_nav);
	
	nav_compute_sog(nav, proj);
	
	for (rec = 0; rec < nRecords; ++rec) {
		if (!nav_get_point_n_v(nav, rec, &timestamp, &lon, &lat, &sog)) {
			error_msgv(modname, "error: failed to read point %d from temp"
				" navigation buffer.\n", rec);
			nav_release(nav);
			return(False);
		}
		f_sog = (f32)sog;
		if (!interp_insert(store->sow, timestamp, &f_sog)) {
			error_msgv(modname, "error: failed to insert SoG point %d.\n",
				rec);
			nav_release(nav);
			return(False);
		}
	}
	nav_release(nav);
	
#ifdef __DEBUG__
#ifdef __DUMP_ATT__
	interp_dump(store->sow, "sow");
#endif
	error_msg(NULL, "done.\n");
#endif
	
	return(True);
}

/* Routine:	sounding_hips_get_nav
 * Purpose:	Read back all of the navigational information for a line
 * Inputs:	stream	SoundingStream to read from
 * Outputs:	Pointer to Nav structure, or NULL.
 * Comment:	This returns a new buffer filled with all of the nav data
 *			from the line.  This is allocated here, so the user is
 *			responsible for removing the information in it when done.
 */

Nav sounding_hips_get_nav(SoundingStream stream)
{
	HIPSBuffer	*store = (HIPSBuffer*)(stream->private);
	HDCS_NavigationSensor	h_nav;
	Nav		nav;
	u32		lineSegs, nRecords, rec, coordType;
	f64		timestamp, minTime, maxTime, minLat, maxLat, minLon, maxLon,
			lat, lon, accuracy;
	u32		summary;
	s32		status;
#ifdef WIN32
	long	rcode;
#endif

#ifdef __DEBUG__
	error_msg(modname, "debug: reading navigation data ... ");
	error_flush_output();
#endif
	
	if ((nav = nav_new()) == NULL) {
		error_msg(modname, "error: failed to create navigational data buffer.\n");
		return(NULL);
	}
	
#ifdef WIN32
	if ((h_nav = NavigationOpen(store->spec->project, store->spec->vessel, store->spec->day,
								store->spec->line, "query", &rcode)) == NULL) {
#else
	if ((h_nav = NavigationOpen(store->spec->project, store->spec->vessel, store->spec->day,
								store->spec->line, "query")) == NULL) {
#endif
		error_msg(modname, "error: failed to open nav.\n");
		nav_release(nav);
		return(NULL);
	}
	NavigationSummary(h_nav, &lineSegs, &nRecords, &coordType,
					  &minTime, &maxTime,
					  &minLat, &maxLat, &minLon, &maxLon, &summary);

	for (rec = 0; rec < nRecords; ++rec) {
		status = NavigationReadSeq(h_nav, &timestamp, &lat, &lon, &accuracy,
								   &summary);
		if (status != OK || !nav_insert(nav, timestamp,
												RAD2DEG(lon), RAD2DEG(lat))) {
			error_msgv(modname, "error: failed inserting navigation point %d.\n",
				rec);
			NavigationClose(h_nav);
			nav_release(nav);
			return(NULL);
		}
	}
	NavigationClose(h_nav);
		
	return(nav);
}

static Bool sounding_hips_read_sow(HIPSBuffer *store)
{
	HDCS_SOWSensor	h_sow;
	u32				lineSegs, nRecords, rec;
	f64				timestamp, minTime, maxTime, sow, minSOW, maxSOW;
#ifndef WIN32
	f64				accuracy;
#endif
	f32				f_sow;
	u32				summary;
	s32				status;
#ifdef WIN32
	long			rcode;
#endif

#ifdef __DEBUG__
	error_msg(modname, "debug: processing sow data ... ");
	error_flush_output();
#endif
	
	if ((store->sow = interp_new(1, 0.0)) == NULL) {
		error_msg(modname, "error: failed to create sow interp buffer.\n");
		return(False);
	}
#ifdef WIN32
	if ((h_sow = SOWOpen(store->spec->project, store->spec->vessel, store->spec->day,
						 store->spec->line, "query", &rcode)) == NULL) {
#else
	if ((h_sow = SOWOpen(store->spec->project, store->spec->vessel, store->spec->day,
						 store->spec->line, "query")) == NULL) {
#endif
		error_msg(modname, "error: failed to open sow.\n");
		return(False);
	}
	SOWSummary(h_sow, &lineSegs, &nRecords, &minTime, &maxTime,
				&minSOW, &maxSOW, &summary);

	for (rec = 0; rec < nRecords; ++rec) {
#ifdef WIN32
		status = SOWReadSeq(h_sow, &timestamp, &sow, &summary);
#else
		status = SOWReadSeq(h_sow, &timestamp, &sow, &accuracy, &summary);
#endif
		f_sow = (f32)sow;
		if (status == ERROR || !interp_insert(store->sow, timestamp, &f_sow)) {
			error_msgv(modname, "error: failed inserting sow point %d.\n",
				rec);
			SOWClose(h_sow);
			return(False);
		}
	}
	SOWClose(h_sow);
#ifdef __DEBUG__
	error_msg(NULL, "done.\n");
#endif
	
	return(True);
}

static Bool sounding_hips_is_vessel_dualhead(VesselConfig *vessel, Bool *dual)
{
/*	s32		numTx, trans1Num, *beam_used;
	char	*sensor, *manuf, *model, *serial, *comment, *auth;
	f64		dummy_f, *beam_off;
	
	if (VesselDepthSensorTmIdxConfig(vessel, 0.0,
			&dummy_f, &sensor, &numTx, &trans1Num, &beam_off, &beam_used,
			&manuf, &model, &serial,
			&dummy_f, &dummy_f, &dummy_f, &dummy_f, &dummy_f,
			&dummy_f, &dummy_f, &dummy_f, &dummy_f, &dummy_f,
			&dummy_f, &dummy_f, &dummy_f, &dummy_f, &dummy_f,
			&dummy_f, &comment, &auth) != OK) {
		error_msg(modname, "error: failed to get vessel configuration.\n");
		free(beam_used); free(beam_off);
		free(sensor); free(manuf); free(serial); free(comment); free(auth);
		return(False);
	}
	
	free(beam_used); free(beam_off);
	free(sensor); free(manuf); free(serial); free(comment); free(auth);

	if (numTx == trans1Num)
		*dual = False;
	else
		*dual = True;
*/
	if (vessel->depthSensorConfig != NULL &&
		vessel->depthSensorConfig->numHeads == 1)
		*dual = False;
	else
		*dual = True;
	return(True);
}

/* Routine:	sounding_hips_compute_epoch_bounds
 * Purpose:	Compute allowable ping epochs about which we allow interpolation
 * Inputs:	store	HIPSBuffer to work from
 * Outputs:	- (limits written back to store->emin, store->emax)
 * Comment:	This computes:
 *				e_{min}=max_i(min_j(e_{ij})) and
 *				e_{max}=min_i(max_j(e_{ij}))
 *			where e_{ij} are the epochs for sensor i, sample j.  This ensures
 *			that we have the minimal range for which every sensor can compute
 *			interpolations.
 */

static void sounding_hips_compute_epoch_bounds(HIPSBuffer *store)
{
	f64		start, end, min, max;
	
	interp_get_bound_times(store->gyro, &min, &max);
	interp_get_bound_times(store->roll, &start, &end);
	min = MAX(min, start); max = MIN(max, end);
	interp_get_bound_times(store->pitch, &start, &end);
	min = MAX(min, start); max = MIN(max, end);
	interp_get_bound_times(store->heave, &start, &end);
	min = MAX(min, start); max = MIN(max, end);
	interp_get_bound_times(store->sow, &start, &end);
	min = MAX(min, start); max = MIN(max, end);
	store->emin = min; store->emax = max;
#ifdef __DEBUG__
	error_msgv(modname, "debug: attitude epoch bounds (%.3lf, %.3lf) s.\n",
		store->emin, store->emax);
#endif
}

/* Routine:	sounding_hips_check_hdr
 * Purpose:	Checks file header and stores reference pointer
 * Inputs:	stream		Pointer to the SoundingStream being configured
 *			filename	Full Filename of the line directory to work on.
 * Outputs:	True if the setup was completed, otherwise False
 * Comment:	This routine reads the navigation and attitude data for the line,
 *			and then stuffs them into LUTs for further work while reading
 *			profiles.  Hence, can fail if memory fails, etc.
 */

Bool sounding_hips_check_hdr(SoundingStream stream, const char *filename)
{
	HIPSBuffer				*store;
	HIPS_Spec				*spec;
	VesselConfig			*vessel;
	u32				toolType, coordType, numLineSegs, numProfiles, numDepths;
	u32				summary;
	f64				minTime, maxTime, minDepth, maxDepth, minLat, maxLat,
					minLon, maxLon;
	time_t			time_now;
	long			rcode;

	/* Attempt to open the HIPS project */
	if ((spec = sounding_hips_parse_spec(filename)) == NULL) {
		error_msgv(modname, "error: failed to parse \"%s\" for HDCS spec.\n",
			filename);
		return(False);
	}
	
	/* Grab memory for the HIPSBuffer structure (needed to read file summary) */
	if ((store = sounding_hips_buffer_new(spec->prefix)) == NULL) {
		error_msg(modname, "failed allocation HIPS private workspace.\n");
		return(False);
	}
	store->spec = spec;

	if (!sounding_hips_lib_interlock(store)) {
		error_msg(modname, "error: check_hdr can't initialise library.\n");
		sounding_hips_release(store);
		return(False);
	}

#ifdef WIN32
	if ((store->fileHandle =
			ProcessedDepthsOpen(spec->project, spec->vessel, spec->day, spec->line, "query", &rcode)) == NULL) {
#else
	if ((store->fileHandle =
			ProcessedDepthsOpen(spec->project, spec->vessel, spec->day, spec->line, "query")) == NULL) {
#endif
		error_msgv(modname, "error: failed to open line \"%s\".\n", filename);
#ifdef WIN32
		error_msgv(modname, "error: return code: %d\n", rcode);
#endif
		sounding_hips_release(store);
		return(False);
	}
	
	if ((rcode = ProcessedDepthsSummary(store->fileHandle, &toolType, &coordType,
							   &numLineSegs, &numProfiles, &numDepths,
							   &minTime, &maxTime, &minDepth, &maxDepth,
							   &minLat, &maxLat, &minLon, &maxLon, &summary))
							   										!= OK) {
		error_msgv(modname, "error: failed to read processed depths summary"
			" for %s/%s/%s/%s (error: %d).\n", spec->project, spec->vessel, spec->day,
			spec->line, rcode);
		sounding_hips_release(spec);
		return(False);
	}
#ifdef __DEBUG__
	error_msgv(modname, "debug: nProfiles = %d nDepths = %d minDepth = %lg m"
		" maxDepth = %lg m.\n", numProfiles, numDepths, minDepth, maxDepth);
#endif

	if ((vessel = VesselConfigRead(spec->vessel)) == NULL) {
		error_msgv(modname, "can't read configuration for vessel \"%s\".\n",
			spec->vessel);
		sounding_hips_release(store);
		return(False);
	}

	if (!sounding_hips_is_vessel_dualhead(vessel, &store->is_dualhead_vessel)) {
		error_msgv(modname, "can't get dual-head status for vessel \"%s\".\n",
			spec->vessel);
		sounding_hips_release(store);
		return(False);
	}

	time_now = time(NULL);
	if (!sounding_hips_read_gyro((f64)time_now, store, vessel) ||
		!sounding_hips_read_heave((f64)time_now, store, vessel) ||
		!sounding_hips_read_pitch((f64)time_now, store, vessel) ||
		!sounding_hips_read_roll((f64)time_now, store, vessel) ||
		!sounding_hips_read_nav(store, stream->proj)) {
		error_msgv(modname, "error: failed to read attitude data for line.\n");
		sounding_hips_release(store);
		return(False);
	}

	sounding_hips_compute_epoch_bounds(store);

	VesselConfigFree(vessel);

	/* Patch private workspace into stream and return */
	store->curProfile = 0;
	stream->private = store;
	stream->release_func = sounding_hips_release;
	stream->data_avail = SOUNDING_BATHY;

	if (store->honour_flags && PD_REJECT_ENTIRE_LINE(summary)) {
		error_msgv(modname, "warning: line %s/%s/%s/%s rejected outright.\n",
			spec->project, spec->vessel, spec->day, spec->line);
		store->nProfiles = 0;
	} else {
		store->nProfiles = numProfiles;
	}

	++file_counter;
	
	return(True);
}

/* Routine:	sounding_hips_get_next
 * Purpose:	Return the next sounding packet from the GSF file
 * Inputs:	stream		SoundingStream being initialised
 *			orient		Pointer to workspace for attitude information
 *			ping		Pointer to beam storage space
 * Outputs:	FILE_OK or appropriate error code
 * Comment:	This basically reads the GSF file until a bathy packet is found,
 *			and then copies data from the GSF structures into the internal
 *			structure.  The GSF library takes care of all of the translations,
 *			scale factors, etc. in the way that a decent library should!
 */

#include <signal.h>

FileError sounding_hips_get_next(SoundingStream stream, Platform *orient,
								Ping ping)
{
	HIPSBuffer	*store = (HIPSBuffer*)stream->private;
	f64			timestamp, dummy, lat, lon, depth, angle;
	u32			prof_summary, beam_summary, numDepths, startDepth, in_beam,
				out_beam, max_beams, cross_over, offset;
	f64			scale, cosT, tx_east, tx_north;
	f32			samp_freq, pulse_len, across_bw, along_bw;
	u32			base_samples, np;
	u32			rcode;

	if (!sounding_hips_lib_interlock(store)) {
		error_msg(modname, "error: check_hdr can't initialise library.\n");
		return(FILE_NOTINIT);
	}

	do {
		if ((rcode = ProcessedDepthsReadProfileSeq(store->fileHandle, &numDepths,
				&startDepth, &timestamp, &lat, &dummy, &dummy, &lon,
				&dummy, &dummy, &dummy, &dummy, &dummy, &dummy, &dummy,
				&dummy, &dummy, &dummy, &prof_summary)) != OK) {
			error_msgv(modname, "error: failed to read profile header for "
				"profile %d (error code: %d).\n", store->curProfile, rcode);
			return(FILE_IOERR);
		}
		++store->curProfile;
		if (timestamp < store->emin ||
					(store->honour_flags && PD_PROFILE_REJECTED(prof_summary))) {
			/* We're not going to use this, but we need to clear the data
			 * in the file before reading the next profile.
			 */
#ifdef __DEBUG__
	error_msgv(modname, "debug: skipping profile %d (t=%.3lf s is out of"
		" acceptable interpolation range for attitude data).\n",
		store->curProfile, timestamp);
#endif
			for (in_beam = 0; in_beam < numDepths; ++in_beam)
				ProcessedDepthsReadSeq(store->fileHandle,
					&dummy, &dummy, &dummy, &dummy, &dummy, &depth, &dummy,
					&beam_summary);
		}
	} while ((timestamp < store->emin ||
					(store->honour_flags && PD_PROFILE_REJECTED(prof_summary)))
				&& store->curProfile < store->nProfiles);
#ifdef __DEBUG__
	error_msgv(modname, "debug: stoped at profile %d, %d depths, t=%.3lf s,"
		" lat=%lg rad, lon=%lg rad.\n",
		store->curProfile, numDepths, timestamp, lat, lon);
#endif

	if (store->curProfile == store->nProfiles || timestamp > store->emax)
		return(FILE_EOF);

#ifdef __DEBUG__
	error_msgv(modname, "debug: processing profile %d (t=%.3lf s).\n",
		store->curProfile, timestamp);
#endif

	orient->timestamp = timestamp;
	orient->latitude = RAD2DEG(lat);
	orient->longitude = RAD2DEG(lon);
	projection_ll_to_en_rad(stream->proj, lon, lat, &tx_east, &tx_north);
	
	/* There appears to be no way to get the head sound velocity, or SVPs via
	 * the HIPS/IO library.  Hence, we need to fake a little here.
	 */
	orient->surf_sspeed = (f32)store->surf_sspeed;
	orient->mean_speed = (f32)store->surf_sspeed;
	
	if (!interp_interp(store->roll, timestamp, &(orient->roll)) ||
		!interp_interp(store->pitch, timestamp, &(orient->pitch)) ||
		!interp_interp(store->gyro, timestamp, &(orient->heading)) ||
		!interp_interp(store->heave, timestamp, &(orient->heave)) ||
		!interp_interp(store->sow, timestamp, &(orient->vessel_speed))) {
		error_msgv(modname, "error: failed to interpolate attitude data for"
			" ping %d.\n", store->curProfile);
		return(FILE_INVALID);
	}
	
	/* Check conversion space */
	if (numDepths > ping->buffer_space) {
		error_msgv(modname, "error: not enough space in Ping buffer (%d slots)"
			" to transfer soundings from file (%d in profile %d).\n",
			ping->buffer_space, numDepths, store->curProfile);
		return(FILE_NOMEM);
	}
	
	ping->n_beams = 0;
	ping->bathy_pingnum = store->curProfile;
	for (in_beam = 0, out_beam = 0; in_beam < numDepths; ++in_beam) {
		if ((rcode = ProcessedDepthsReadSeq(store->fileHandle, &timestamp, &dummy, &dummy,
				&lat, &lon, &depth, &dummy, &beam_summary)) != OK) {
			error_msgv(modname, "error: failed to read depth %d for profile"
				" %d (error code: %d).\n", in_beam, store->curProfile, rcode);
			return(FILE_IOERR);
		}
		
		/* Note that we don't check for bad beams, since we want to process
		 * them all.
		 *
		 * Famous last words.  We now need to check the flags flag (;-) and
		 * reject according to flags iff set.
		 */
		
		if (store->honour_flags && ((beam_summary)&PD_DEPTH_REJECTION_MASK) != 0)
			continue;

		ping->beams[out_beam].file_id = file_counter;
		ping->beams[out_beam].backscatter = 0;
		/* We grab the top eight flags from the HIPS summary word, which are
		 * those relating to flagging in different modes.  This means that we
		 * drop off the quality flags.
		 */
		ping->beams[out_beam].flags =
			SOUNDING_PUT_VFLAGS((beam_summary&0xFF000000U)>>24);

		/* Note that lat/lon in HIPS are in radians, not decimal degrees */
		projection_ll_to_en_rad(stream->proj, lon, lat,
				&(ping->beams[out_beam].east), &(ping->beams[out_beam].north));
		ping->beams[out_beam].depth = (f32)depth;
		
		++ping->n_beams;
		++out_beam;
	}

	/* We now need to check that there are any beams left in this ping that
	 * we can use --- all of them may have been rejected!  (And the ping may
	 * not be rejected itself)
	 */
	if (ping->n_beams == 0)
		return(FILE_OK);
	
	/* We now need to fix up the beam numbering.  Under some circumstances,
	 * the number of beams in the file might not be the number that one would
	 * normally expect for the device specified.  This can happen, for example,
	 * with Elac 1180's switched to narrow mode, where only 108 beams are
	 * reported and recorded.  This means that we have to check the number of
	 * beams and offset beam numbers appropriately in order to ensure angles
	 * are computed appropriately.
	 */
	if (numDepths < (out_beam = device_get_nbeams(stream->device)))
		offset = out_beam/2 - numDepths/2;
	else
		offset = 0;
	for (out_beam = 0; out_beam < numDepths; ++out_beam)
		ping->beams[out_beam].beam_number = out_beam + offset;
	
	/* Compute scale and offset for # phase sample computation.  We assume that
	 * the sea-floor is flat at the point of intersection of the ray, and
	 * compute the time duration of the beam's intersection, given the beamwidth
	 * and launch angle.  We then multiply by the time sampling rate to give
	 * the number of samples.  We then assume that over a certain limit, phase
	 * detection has been used, and attribute the soundings appropriately.  We
	 * also set the number of phase samples in the flags component of the
	 * soundings.  Of course, it would make more sense to put the information
	 * into the files ... and we are probably making some over/under estimates
	 * of errors in the process.  *big sigh*
	 */
	device_get_samp_freq(stream->device, &samp_freq);
	device_get_pulse_len(stream->device, &pulse_len);
	base_samples = (u32)ceil(samp_freq * pulse_len/1000.0);
	device_get_beamwidths(stream->device, &across_bw, &along_bw);
	scale = 2.0*DEG2RAD(across_bw)*samp_freq/orient->mean_speed;

#ifdef __DEBUG__
	error_msgv(modname, "debug: base_samples = %d scale = %lf\n",
		base_samples, scale);
#endif

	/* We now need to do the error attribution for the profile's soundings. */
	if (stream->port_errmod == stream->stbd_errmod) {
		/* Only one error model --- check we've only got one head */
		if ((device_get_properties(stream->device) & DEVICE_IS_SPLITHEAD) != 0){
			error_msg(modname, "error: split-head sonar device, but only one"
				" error model defined --- can't attribute errors.\n");
			return(FILE_INTERNAL);
		}
		if (store->is_dualhead_vessel) {
			error_msg(modname, "error: dual head vessel system, but only one"
				" error model defined --- can't attribute errors.\n");
			return(FILE_INTERNAL);
		}
		
		/* Otherwise ... */
		for (out_beam = 0; out_beam < ping->n_beams; ++out_beam) {
			angle = errmod_get_beam_angle(stream->errormodel,
										  ping->beams[out_beam].beam_number);
			cosT = cos(angle);
			ping->beams[out_beam].range = (f32)(ping->beams[out_beam].depth/cosT);

			/* We now need to fake some depth flags so that we can determine
			 * amplitude/phase detection and footprint length.  There does not
			 * appear to be any way to extract this information from the file,
			 * so we assume: (a) that all detects are amplitude detects, and
			 * (b) that we have a flat seafloor at the intersect point, so that
			 * we can project the beam to compute number of samples.
			 */
			np = base_samples +
				(u32)ceil(scale*ping->beams[out_beam].depth*fabs(tan(angle))/cosT);
				
			if (np > store->amp_phase_trans)
				ping->beams[out_beam].flags |= SOUNDING_DETECT_MTHD_FLAG;
			else
				ping->beams[out_beam].flags |= 0;
			if (np > 255) np = 255;	/* Saturation rounding */
			ping->beams[out_beam].flags |= SOUNDING_PUTWINDOWSZ(np);			
		}
		if (!errmod_estimate(stream->errormodel, orient, ping->beams, ping->n_beams)) {
			error_msgv(modname, "error: failed to attribute errors for ping"
				" %d.\n", store->curProfile);
			return(FILE_INTERNAL);
		}
	} else {
		/* Dual error models --- check that we've got something compatible. */
		if ((device_get_properties(stream->device)&DEVICE_IS_SPLITHEAD) == 0 &&
			!store->is_dualhead_vessel) {
			error_msg(modname, "error: two error models, but only a single head"
				" setup --- can't attribute errors.\n");
			return(FILE_INTERNAL);
		}
		
		/* Otherwise ... */
		max_beams = device_get_nbeams(stream->device);

		/* Compute the cross-over point where we switch from port to stbd head,
		 * so that we use the correct error model for attribution and angles.
		 */
		cross_over = 0;
		for (out_beam = 0; out_beam < ping->n_beams; ++out_beam)
			if (ping->beams[out_beam].beam_number < max_beams/2)
				cross_over = out_beam;
/*		if (cross_over == 0) {
			error_msgv(modname, "debug: cross_over == 0 --- beam dump follows:\n");
			for (out_beam = 0; out_beam < ping->n_beams; ++out_beam) {
				error_msgv(modname, "debug:  beam = %d d = %f\n", out_beam, ping->beams[out_beam].depth);
			}
			return(FILE_OK);
		}*/
		for (out_beam = 0; out_beam <= cross_over ;++out_beam) {
			angle = errmod_get_beam_angle(stream->port_errmod,
											ping->beams[out_beam].beam_number);
			cosT = cos(angle);
			ping->beams[out_beam].range = (f32)(ping->beams[out_beam].depth/cosT);
			
			/* Fake number of bottom samples and detection flags */
			np = base_samples +
				(u32)ceil(scale*ping->beams[out_beam].depth*fabs(tan(angle))/cosT);
				
			if (np > store->amp_phase_trans)
				ping->beams[out_beam].flags |= SOUNDING_DETECT_MTHD_FLAG;
			else
				ping->beams[out_beam].flags |= 0;
			if (np > 255) np = 255;
			ping->beams[out_beam].flags |= SOUNDING_PUTWINDOWSZ(np);			
		}
		if (!errmod_estimate(stream->port_errmod, orient, ping->beams,
																cross_over+1)) {
			error_msgv(modname, "error: failed to attribute errors in port head"
				" data at ping %d.\n", store->curProfile);
			return(FILE_INTERNAL);
		}
		for (out_beam = cross_over+1; out_beam < ping->n_beams; ++out_beam) {
			angle = errmod_get_beam_angle(stream->stbd_errmod,
											ping->beams[out_beam].beam_number);
			cosT = cos(angle);
			ping->beams[out_beam].range = (f32)(ping->beams[out_beam].depth/cosT);

			/* Fake number of bottom samples and detection flags */
			np = base_samples +
				(u32)ceil(scale*ping->beams[out_beam].depth*fabs(tan(angle))/cosT);
				
			if (np > store->amp_phase_trans)
				ping->beams[out_beam].flags |= SOUNDING_DETECT_MTHD_FLAG;
			else
				ping->beams[out_beam].flags |= 0;
			if (np > 255) np = 255;
			ping->beams[out_beam].flags |= SOUNDING_PUTWINDOWSZ(np);			
		}
		if (!errmod_estimate(stream->stbd_errmod, orient,
							ping->beams + cross_over + 1,
							ping->n_beams - cross_over - 1)) {
			error_msgv(modname, "error: failed to attribute errors in stbd head"
				" data at ping %d.\n", store->curProfile);
			return(FILE_INTERNAL);
		}
	}
	for (out_beam = 0; out_beam < numDepths; ++out_beam)
		ping->beams[out_beam].depth = -ping->beams[out_beam].depth;
	
	stream->ping_num = store->curProfile;
	return(FILE_OK);
}

/* Routine:	sounding_hips_execute_params
 * Purpose:	Execute parameters for this sub-module
 * Inputs:	*list	ParList to parse parameters from
 * Outputs:	True if parameter list was executed correctly, otherwise False
 * Comment:	This looks only for the surface sound speed to set in input packets
 *			if there is not sufficient information in the packet to work it out.
 */

typedef enum {
	SND_HIPS_UNKNOWN = 0,
	SND_HIPS_AMP_PHASE_TRANS,
	SND_HIPS_SURF_SSPEED,
	SND_HIPS_HONOUR_FLAGS,
} SndHIPSParamEnum;

Bool sounding_hips_execute_params(ParList *list)
{
	ParTable	tab[] = {
		{ "default_surf_sspeed",	SND_HIPS_SURF_SSPEED		},
		{ "amp_phase_trans",		SND_HIPS_AMP_PHASE_TRANS	},
		{ "honour_flags",			SND_HIPS_HONOUR_FLAGS		},
		{ NULL,						SND_HIPS_UNKNOWN			}
	};
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;
	s32		dummy_int;
	char	*dummy_string, *src, *dst;

	node = list;
	do {
		node = params_match(node, "sounding.hips", tab, &id, &match);
		switch (id) {
			case SND_HIPS_UNKNOWN:
				break;
			case SND_HIPS_SURF_SSPEED:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_SURF_SSPEED ||
											dummy_float > MAX_SURF_SSPEED) {
					error_msgv(modname, "error: surface sound speed must be"
						" in the range [%.1f, %.1f] m/s (not %.1f).\n",
						MIN_SURF_SSPEED, MAX_SURF_SSPEED, dummy_float);
					return(False);
				}
				default_surf_sspeed = dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default surface sound speed to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case SND_HIPS_AMP_PHASE_TRANS:
				dummy_int = atoi(match->data);
				if (dummy_int < MIN_AMP_PHASE_TRANS ||
										dummy_int > MAX_AMP_PHASE_TRANS) {
					error_msgv(modname, "error: amplitude-phase transition"
						" sample count must be in range [%d, %d] samples (not"
						" %d).\n", MIN_AMP_PHASE_TRANS, MAX_AMP_PHASE_TRANS,
						dummy_int);
					return(False);
				}
				default_amp_phase_trans = dummy_int;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default amp/phase transition to %d (%s).\n",
	dummy_int, match->data);
#endif
				break;
			case SND_HIPS_HONOUR_FLAGS:
				if ((dummy_string = strdup(match->data)) == NULL) {
					error_msg(modname, "failed matching honour flags tag"
						" (no memory).\n");
					return(False);
				}
				src = dst = dummy_string;
				while (*src != '\0') *dst++ = tolower(*src++);
				if (strcmp(dummy_string, "yes") == 0 ||
					strcmp(dummy_string, "y") == 0) {
					honour_flags = True;
				} else if (strcmp(dummy_string, "no") == 0 ||
						   strcmp(dummy_string, "n") == 0) {
					honour_flags = False;
				} else {
					error_msgv(modname, "error: honour_flags tag must be"
						" y[es]|n[o] (not \"%s\").\n", match->data);
					free(dummy_string);
					return(False);
				}
				free(dummy_string);
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting HIPS flags to %s.\n", honour_flags == True ?
	"on" : "off");
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
