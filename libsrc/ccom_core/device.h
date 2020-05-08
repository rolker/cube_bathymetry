/*
 * $Id: device.h 11 2003-07-22 23:02:21Z brc $
 * $Log$
 * Revision 1.2  2003/07/22 23:02:21  brc
 * Added definitions for EM120 and SB8111 (Kaua'i, Hawai'i)
 *
 * Revision 1.10.2.4  2003/07/22 21:30:04  brc
 * Added Simrad EM120 to list of known sonars.
 *
 * Revision 1.10.2.3  2003/06/02 17:50:14  brc
 * Added definition for 8111 and removed some redundant code
 * (used to be used computing number of phase samples before
 * this was read from file).
 *
 * Revision 1.1.1.1  2003/02/03 20:18:44  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2.4.2  2003/01/29 13:06:16  dneville
 * (BRC) Moved include of sounding.h to break dependency.
 *
 * Revision 1.2.4.1  2003/01/28 14:29:27  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.10.2.2  2002/12/15 01:30:20  brc
 * Added definitions for a SeaBeam 2100
 *
 * Revision 1.10.2.1  2002/07/14 02:20:46  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.10  2002/03/14 04:26:01  brc
 * Added pulse length variable for the various systems where such information
 * is known.  Added access routines for sampling frequency and pulse length.
 * Added a prototype model for the Elac 1180.
 *
 * Revision 1.9  2001/12/07 20:49:46  brc
 * Added a prototype model for the Reson 8125 based on the assumption that it
 * operates as a standard flat-plate beam-former as far as pointing angle and
 * beam-width go.  This does not take into account anything to do with focussing,
 * which might affect all or part of this.
 *
 * Revision 1.8  2001/08/29 20:09:22  roland
 * Added float.h for the DBL_MAX definition
 *
 * Revision 1.7  2001/07/23 17:56:28  brc
 * Added Hydrosweep DS system (mainly for R/V Thompson, UW).
 *
 * Revision 1.6  2001/05/25 19:47:38  brc
 * Added stdio.h for FILE later in header.  Must have been getting this from
 * elsewhere in the standard scheme.
 *
 * Revision 1.5  2001/04/10 23:19:20  brc
 * Added prototypes for more readback functions.
 *
 * Revision 1.4  2001/02/10 17:58:52  brc
 * Added types and prototypes to support the full error modeling computation
 * code.  Mainly routines to compute per sounder per beam error.
 *
 * Revision 1.3  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
 *
 * Revision 1.2  2000/09/24 01:23:12  brc
 * Added rates and densities to the device descriptions and updated the device
 * tables to compensate.  Also added interface calls.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	device.h
 * Purpose:	Encapsulate device performance parameters indexed by ID tag
 * Date:	15 July 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __DEVICE_H__
#define __DEVICE_H__

#include "stdtypes.h"
#include "stdio.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

typedef struct _device *Device;


typedef enum {
	DEVICE_UNKNOWN = 0,	/* Not known, or not set */
	DEVICE_EM300,		/* Simrad EM300 MBES, 30kHz */
	DEVICE_EM1000,		/* Simrad EM1000 MBES, 100kHz */
	DEVICE_EM1002,		/* Simrad EM1002 MBES, 100kHz (mid-life upgrade EM1k) */
	DEVICE_EM3000,		/* Simrad EM3000 MBES, 300kHz */
	DEVICE_EM3000D,		/* Dual Simrad EM3000 MBES, 300kHz */
	DEVICE_SB8101,		/* Reson SeaBat 8101 (e.g., NOAA data in XTF) */
	DEVICE_SB9001,		/* Reson SeaBat 9001 */
	DEVICE_SB9003,		/* Reson SeaBat 9003 */
	DEVICE_SB8125,		/* Reson SeaBat 8125 Focused MBES */
	DEVICE_SB8111,		/* Reson SeaBat 8111 MBES */
	DEVICE_EM120,		/* Simrad EM120 MBES, 12kHz */
	DEVICE_HSWEEPDS,	/* Krupp Atlas Hydrosweep DS */
	DEVICE_ELAC1180,	/* Elac Nautik 1180 MBES, 180kHz 126 beams */
	DEVICE_SB2112,		/* Seabeam Industries 2112 (and 2100 series, 12kHz, 151 beams) */
	DEVICE_KLEIN595,	/* Klein analogue 100kHz single-beam sidescan sonar */
	DEVICE_KLEIN2000,	/* Klein digital 100/300kHz single-beam sidescan */
	DEVICE_KLEIN3000,	/* Klein digital 100/300kHz single-beam sidescan */
	DEVICE_KLEIN5000,	/* Klein digital 300/500kHz multi-beam sidescan series */
	DEVICE_NDEVS
} DeviceType;

#include "sounding.h"

typedef enum {
	DEVICE_IS_MB = (1<<0),		/* Device is a multi-beam (else single-beam).
								 * Note that MB \not= MBES since, e.g., K5K has
								 * multiple beams. */
	DEVICE_IS_SSS = (1<<1),		/* Device is a _true_ side-scan sonar */
	DEVICE_HAS_BATHY = (1<<2),	/* Device can generate bathymetry information */
	DEVICE_HAS_IMAGERY = (1<<3),/* Device can generate imagery (backscatter) */
	DEVICE_HAS_ATTITUDE=(1<<4),	/* Device can generate attitude datastream */
	DEVICE_IS_TOWED = (1<<5),	/* Device is towed/tethered or is being towed */
	DEVICE_IS_CALIBRATED=(1<<6),/* Device backscatter is calibrated in dB */
	DEVICE_IS_DUALFREQ=(1<<7),	/* Device uses two operating frequencies */
	DEVICE_HAS_ACCURACY =(1<<8),/* Accuracy information for device is available
								  * through device module calls */
	DEVICE_IS_STEERED = (1<<9),	/* Device has steered beams */
	DEVICE_IS_SPLITHEAD = (1<<10)	/* Device is physically in two halves (typ.
									 * one on either side of the ship) */
} DeviceProps;

typedef enum {
	DEVICE_PING_RATE,			/* Maximum repetition rate, pings/sec */
	DEVICE_BATHY_RATE,			/* Bathy packets per second */
	DEVICE_ATTITUDE_RATE,		/* Attitude packets per second */
	DEVICE_IMAGERY_RATE			/* Imagery packets per second */
} DeviceRates;					/* Rates of data generation */

typedef enum {
	DEVICE_BATHY_DENSITY,		/* Bathy data density (items/dgram) */
	DEVICE_ATTITUDE_DENSITY,	/* Attitude data density (items/dgram) */
	DEVICE_IMAGERY_DENSITY		/* Imagery data density (items/dgram) */
} DeviceDensities;

/* Construction/Destruction Methods */
extern Device device_new(void);
extern Device device_new_by_type(DeviceType type);
extern Device device_new_by_name(const char *name);
extern Device device_new_from_omg_tool(int tool_type);
extern void device_release(Device device);

/* Access Methods */
extern DeviceType device_get_device(Device device);
extern Bool	device_set_device(Device device, DeviceType type);
extern u32	device_get_properties(Device device);
extern char *device_get_name(Device device);
extern u32	device_get_nbeams(Device device);
extern Bool device_get_beamwidths(Device device, f32p across, f32p along);
extern Bool device_get_frequencies(Device device, f32p f1, f32p f2);
extern u32	device_get_rate(Device device, DeviceRates type);
extern u32	device_get_density(Device device, DeviceDensities type);
extern f32	device_get_steerangle(Device device);
extern Bool device_get_dr(Device device, f32p dr);
extern Bool device_get_samp_freq(Device device, f32p fs);
extern Bool device_get_pulse_len(Device device, f32p pl);

/* Routine:	device_compute_angerr
 * Purpose:	Compute variance of swath indicated angle
 * Inputs:	device	Device being used for measurements
 *			plat	Observation platform orientation
 *			*snd	Pointer to Sounding under consideration
 *			angle	Beam angle measured by the device, rad.
 * Outputs:	Returns variance of the angular error component of total error
 *			budget, or a very large number if the error cannot be computed
 *			directly.
 * Comment:	This routine quietly fails when the device doesn't really support
 *			the correct structure for angle estimation error modelling (i.e.,
 *			if it isn't a swath bathymeter of some kind).
 */

extern f64 device_compute_angerr(Device device, Platform *plat, Sounding *snd,
						  		 f64 angle);

/* Routine:	device_compute_rangeerr
 * Purpose:	Compute the variance of the computed range in a bathymetric system
 * Inputs:	device	Device being used for measurements
 *			plat	Observation platform orientation
 *			*snd	Pointer to sounding under consideration
 * Outputs:	Returns variance of range error component of total error budget,
 *			or a very large number if the error cannot be computed.
 * Comment:	This routine quietly fails when the device doesn't really support
 *			the correct structure for angle estimation error modelling (i.e.,
 *			if it isn't a swath bathymeter of some kind).  Note that we treat
 *			the Simrad EM series according to Simrad's published information
 *			where available, and assume that the Reson SeaBat series are of the
 *			same form as the EM950/1000/1002, since they are basically the same
 *			technology (shape of transducer, beam-forming, etc.)
 */

extern f64 device_compute_rangeerr(Device device, Platform *plat,
								   Sounding *snd);

/* Routine:	device_beam_to_angle
 * Purpose:	Compute pointing angle for a given beam number
 * Inputs:	device	Device to compute for
 *			sspeed	Design sound speed
 *			beam	Beam number to determine
 * Outputs:	Returns beam pointing angle in degrees, or -1.0 on error
 * Comment:	Note that beams are numbered from 0 at the far port to N-1 at the
 *			far starboard.  Beam pointing angles are always given relative to
 *			the transducer face, rather than relative to the vertical (unless
 *			the transducer does real-time steering, like many of the Simrad
 *			EM series).  Routine returns -1.0 if the beam requested doesn't
 *			exist in the specified device, or if the device doesn't have
 *			multiple formed beams.  The sound speed is used to determine the
 *			beam angle in many beam-formed systems.
 */

extern f64 device_beam_to_angle(Device device, f32 sspeed, u32 beam);

/* Serialisation Methods */
extern Device device_new_from_file(FILE *fd);
extern Bool device_write(Device device, FILE *fd);

#ifdef __cplusplus
}
#endif

#endif
