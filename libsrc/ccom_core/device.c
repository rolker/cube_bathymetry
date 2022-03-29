/*
 * $Id: device.c 11 2003-07-22 23:02:21Z brc $
 * $Log$
 * Revision 1.2  2003/07/22 22:59:27  brc
 * Added definitions for EM120 and SB8111 (Kaua'i, Hawai'i)
 *
 * Revision 1.15.2.4  2003/07/22 21:29:40  brc
 * Added Simrad EM120 to list of known sonars, using the same basic model as the
 * EM1000 (the EM100 sonar model in Hare et al. doesn't make any sense and has
 * a massive amplitude/phase transition point which is unrealistic).
 *
 * Revision 1.15.2.3  2003/06/02 17:50:14  brc
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
 * Revision 1.2.4.1  2003/01/28 14:29:27  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.15.2.2  2002/12/15 01:30:04  brc
 * Added a set of definitions for the SeaBeam 2100 series in order to support
 * the Santos Basin processing.
 *
 * Revision 1.15.2.2  2002/12/15 01:30:04  brc
 * Added a set of definitions for the SeaBeam 2100 series in order to support
 * the Santos Basin processing.
 *
 * Revision 1.15.2.1  2002/07/14 02:20:46  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.15  2002/06/16 02:33:24  brc
 * Modified database for EM300 to indicate that the system is using steering on
 * all beams (it's a flat array).  Previous bogus information from the MEET
 * spreadsheet.  Also modified the device angular error computations for the
 * EM300 so that they now assume a flat seafloor (S=0) rather than S=1.  This
 * significantly changes the device measurement errors, particularly in the
 * outer beams in deep water.
 *
 * Revision 1.14  2002/06/15 03:25:14  brc
 * Modified some erroneous values in the definition of the EM300 error model,
 * in particular configuring it to the the real number of phase samples if the
 * phase detection method was used, rather than trying to compute them (and
 * getting it wrong ...)
 *
 * Revision 1.13  2002/03/14 04:26:01  brc
 * Added pulse length variable for the various systems where such information
 * is known.  Added access routines for sampling frequency and pulse length.
 * Added a prototype model for the Elac 1180.
 *
 * Revision 1.12  2001/12/07 20:49:46  brc
 * Added a prototype model for the Reson 8125 based on the assumption that it
 * operates as a standard flat-plate beam-former as far as pointing angle and
 * beam-width go.  This does not take into account anything to do with focussing,
 * which might affect all or part of this.
 *
 * Revision 1.11  2001/10/15 23:04:18  brc
 * Fixed a couple of comment slepping mistooks.
 *
 * Revision 1.10  2001/08/21 01:41:00  brc
 * Added functionality for the Hydrosweep DS.
 *
 * Revision 1.9  2001/07/23 17:56:28  brc
 * Added Hydrosweep DS system (mainly for R/V Thompson, UW).
 *
 * Revision 1.8  2001/05/16 21:07:26  brc
 * Added float.h for DBL_MAX, FLT_MAX etc. under Linux
 *
 * Revision 1.7  2001/04/10 23:19:02  brc
 * Added range sampling rate as a parameter in the database since it cannot be
 * reliably computed from knowledge of other components.  Updated database
 * definitions where required from the data in the MEET (Dutch Error Model
 * Extention) Spreadsheet.
 *
 * Revision 1.6  2001/02/16 23:31:05  brc
 * Updated database from MEET spreadsheet (Dutch Hydrographic Service version
 * of the MBES error model).  This updates the computational methods to the
 * SB8101, 9001 and 9003, which appear to be modeled in the same way except in
 * that the 9001 and 9003 don't do phase detection.
 *
 * Revision 1.5  2001/02/10 17:58:03  brc
 * Added components for full error modeling work (new items in list, routines
 * to compute the range and angle error for different echo sounders as a
 * function of environmental conditions).
 *
 * Revision 1.4  2000/09/24 01:23:12  brc
 * Added rates and densities to the device descriptions and updated the device
 * tables to compensate.  Also added interface calls.
 *
 * Revision 1.3  2000/09/07 21:02:35  brc
 * Modified Simrad EM3000 definition: should have 128 beams, and not 127 as
 * the Simrad web-site claims.
 *
 * Revision 1.2  2000/08/24 15:11:35  brc
 * Modified numerous files to allow the code to compile cleanly under Linux.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	device.c
 * Purpose:	Encapsulate device performance metrics, indexed by ID tag
 * Date:	15 July 2000
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
#include <limits.h>
#include <float.h>
#include "stdtypes.h"
#include "device.h"
#include "sounding.h"

#undef __DEBUG__

static char *modname = "device";
static char *modrev = "$Revision: 11 $";

#define DEG2RAD(x) ((x)*M_PI/180.0)
#define RAD2DEG(x) ((x)*180.0/M_PI)

typedef struct _device {
	DeviceType	type;
} sDevice /*, *Device */;

typedef struct _props {
	char	*name;		/* Device full name */
	char	*recname;	/* Device recognition string */
	u32		props;		/* Device properties */
	u32		maxbeams;	/* Maximum number of beams on device */
	f32		freq_1,		/* Operating frequency 1 (or prime freq.), kHz */
			freq_2;		/* Operating frequency 2 (or zero), kHz */
	f32		max_angle;	/* Maximum angle away from nadir, deg. */
	f32		across_width,
			along_width;	/* Beam widths, degrees */
	u32		rep_rate,		/* Maximum rep. rate, pings/second */
			bathy_rate,		/* Rate of bathymetry packets per second */
			att_rate,		/* Rate of attitude packets per second */
			imagery_rate,	/* Rate of imagery packets per second */
			bathy_density,	/* Packets of bathy information per datagram */
			att_density,	/* Packets of attitude information per datagram */
			imagery_density;/* Packets of imagery per datagram */
	f32		steering_angle;	/* Angle beyond which beams are steered, deg. */
	f32		amp_samp_freq;	/* Range sampling frequency, Hz */
	f32		range_samp_dr;	/* Range sampling distance, m */
	f32		pulse_len;		/* Minimum pulse length, ms. */
} DevProps;

typedef struct {
	s32	OMG_ID;
	DeviceType	id;
} TransTbl;

static TransTbl omg2id[] = {
	{  0, DEVICE_UNKNOWN },	/* Acutally a generic single-beam echosounder */
	{  1, DEVICE_UNKNOWN },	/* Acutally an ELAC BottomChart */
	{  2, DEVICE_UNKNOWN },	/* Actually Simrad EM12D */
	{  3, DEVICE_UNKNOWN },	/* Actually Simrad EM100, depths only */
	{  4, DEVICE_UNKNOWN },	/* Actually STN-Atlas FanSweep 10 */
	{  5, DEVICE_UNKNOWN },	/* Actually General Instruments SeaBeam Classic */
	{  6, DEVICE_EM3000 },
	{  7, DEVICE_UNKNOWN },	/* Actually a ROSS Profiler (?) */
	{  8, DEVICE_UNKNOWN },	/* Actually Simrad EM12S */
	{  9, DEVICE_UNKNOWN },	/* Actually Simrad EM100+depth+amplitudes */
	{ 10, DEVICE_EM1000 },
	{ 11, DEVICE_UNKNOWN },	/* Actually RAD LADS (secondary format) (?) */
	{ 12, DEVICE_EM3000D },
	{ 13, DEVICE_UNKNOWN },	/* Actually General Inst. SeaBeam 2100 series */
	{ 14, DEVICE_UNKNOWN },	/* Actually ISIS SubMetrix */
	{ 15, DEVICE_EM1000 },	/* ... with amplitude record */
	{ 16, DEVICE_SB2112 }, /* Actually SB2K == SeaBeam 2000 series ?? */
	{ 17, DEVICE_SB9001 },
	{ 18, DEVICE_UNKNOWN },	/* Actually STN-Atlas FanSweep 10A */
	{ 19, DEVICE_UNKNOWN },	/* Actually STN-Atlas FanSweep 20 */
	{ 20, DEVICE_UNKNOWN },	/* Actually ISIS Submetrix 100SWA (?) */
	{ 21, DEVICE_UNKNOWN },	/* Actually General Inst. SeaBeam 1180 Mk. II */
	{ 22, DEVICE_SB8101 },
	{ 23, DEVICE_EM300 },
	{ 24, DEVICE_UNKNOWN },	/* Actually Simrad EM121A */
	{ 25, DEVICE_UNKNOWN },	/* Actually Simrad SM2000 (mid-column fisheries) */
	{ 26, DEVICE_UNKNOWN },	/* Actually HydroSweep MD2 */
	{ 27, DEVICE_EM1002 },
	{ 28, DEVICE_UNKNOWN },	/* Actually a Hummingbird 3D (6-beam!) MBES */
	{ 29, DEVICE_UNKNOWN },	/* Actually a Knudsen 320 */
	{ -1, DEVICE_UNKNOWN }	/* End of Table Marker */
};

#define SIMRAD_PROP	((u32)DEVICE_IS_MB | (u32)DEVICE_HAS_BATHY |\
					 (u32)DEVICE_HAS_ATTITUDE | (u32)DEVICE_HAS_IMAGERY |\
					 (u32)DEVICE_IS_CALIBRATED | (u32)DEVICE_IS_STEERED)

#define SEABAT_PROP ((u32)DEVICE_IS_MB | (u32)DEVICE_HAS_BATHY |\
					 (u32)DEVICE_HAS_ATTITUDE | (u32)DEVICE_HAS_IMAGERY)

#define ATLAS_PROP	((u32)DEVICE_IS_MB | (u32)DEVICE_HAS_BATHY |\
					 (u32)DEVICE_HAS_ATTITUDE | (u32)DEVICE_IS_STEERED)

#define ELAC_PROP	((u32)DEVICE_IS_MB | (u32)DEVICE_HAS_BATHY |\
					 (u32)DEVICE_HAS_ATTITUDE | (u32)DEVICE_IS_STEERED |\
					 (u32)DEVICE_IS_SPLITHEAD)

#define KLEIN_PROP	((u32)DEVICE_IS_SSS | (u32)DEVICE_HAS_IMAGERY |\
					 (u32)DEVICE_IS_TOWED | (u32)DEVICE_IS_DUALFREQ)

#define SEABEAM_PROP	((u32)DEVICE_IS_MB | (u32)DEVICE_HAS_BATHY |\
						 (u32)DEVICE_HAS_ATTITUDE | (u32)DEVICE_HAS_IMAGERY |\
						 (u32)DEVICE_IS_STEERED)

/* N.B.: This table must be kept in the same order as the DeviceType enum to
 * ensure that correct information is returned.
 */
static DevProps devices[DEVICE_NDEVS] = {
	{	"unknown device", "unknown",
		0,				/* Properties bit-mask */
		0,				/* Number of beams */
		0.0f, 0.0f,		/* Operating frequencies (kHz) */
		0.0f,			/* Maximum angle away from nadir, deg. */
		0.0f, 0.0f,		/* Beamwidths (across/along, resp. deg.) */
		0, 0, 0, 0,		/* Data rates (ping, bathy, attitude, imagery)/sec. */
		0, 0, 0,		/* Data density (bathy, attitude, imagery)/dgram */
		0.0,			/* Device steering angle, if any */
		0.0,			/* Device envelope sampling frequency */
		0.0,			/* Device range sampling distance, m */
		0.0				/* Device pulse length, ms. */
	},
	{	"Simrad EM300", "em300",
		SIMRAD_PROP,
		135,
		30.0f, 0.0f,
		75.0f,
		1.0f, 1.0f,
		10, 10, 200, 10,
		1, 100, 1,
		0.0,		/* From MEET spreadsheet */
		5000,		/* Guessed */
		0.15f,		/* From MEET spreadsheet */
		5.0f			/* MEET spreadsheet */
	},
	{	"Simrad EM1000", "em1000",
		SIMRAD_PROP,
		60,
		95.0f, 0.0f,
		75.0f,
		3.3f, 3.3f,	/* Product literature (MEET has 2.4 acrosstrack) */
		10, 10, 200, 10,
		1, 100, 1,
		60.0,	/* Product literature */
		10000,	/* Guessed: product literature has bandwidth 5kHz */
		0.15f,	/* Product literature: shallow mode 0.15m, deep 0.30m */
		0.20f	/* MEET spreadsheet, wide mode */
	},
	{	"Simrad EM1002", "em1002",
		SIMRAD_PROP,
		111,
		95.0f, 0.0f,	/* Operating frequency 95-98kHz depending on sector */
		75.0f,
		1.5f, 2.0f,	/* From product literature */
		10, 10, 200, 10,
		1, 100, 1,
		60.0,	/* Product literature */
		11970,	/* From product literature */
		0.0626f,	/* From product literature */
		0.20f	/* MEET Spreadsheet --- at least three available */
	},
	{
		"Simrad EM3000", "em3000",
		SIMRAD_PROP,
		128,
		300.0f, 0.0f,
		65.0f,		/* Product literature: maximum swath 130 deg. */
		1.5f, 1.5f,	/* Product literature */
		10, 10, 200, 10,
		1, 100, 1,
		0.0,		/* Product lit.: FFT beamformer, all beams are steered */
		15000,		/* Product literature */
		0.05f,		/* Product literature */
		0.15f		/* MEET Spreadsheet */
	},
	{	"Simrad EM3000D", "em3000d",
		SIMRAD_PROP | (u32)DEVICE_IS_SPLITHEAD,
		254,
		300.0f, 0.0f,
		65.0f,		/* Aperture is limited in dual-head mode */
		1.5f, 1.5f,
		10, 10, 200, 10,
		1, 100, 1,
		0.0,		/* Product literature: FFT beamformer as above */
		15000,		/* Product literature */
		0.05f,		/* Product literature */
		0.15f		/* MEET Spreadsheet */
	},
	{
		"Reson SeaBat 8101", "sb8101",
		SEABAT_PROP | (u32)DEVICE_IS_STEERED,
		101,
		240.0f, 0.0f,	/* Product literature */
		75.0f,		/* Product literature */
		1.5f, 1.5f,
		30, 30, 0, 30,
		1, 0, 1,
		60.0,		/* From MEET spreadsheet, although they're guessing */
		10000,		/* Guessed */
		0.05f,		/* From MEET spreadsheet */
		0.15f		/* MEET Spreadsheet --- variable */
	},
	{	"Reson SeaBat 9001", "sb9001",
		SEABAT_PROP,
		60,
		455.0f, 0.0f,
		45.0f,
		1.5f, 1.5f,
		15, 15, 0, 15,
		1, 0, 1,
		361.0,		/* Product literature: no steered beams */
		10000,		/* Guessed */
		0.05f,		/* From MEET spreadsheet */
		0.07f		/* Rob Hare V2.0 */
	},
	{	"Reson SeaBat 9003", "sb9003",
		SEABAT_PROP,
		40,
		455.0f, 0.0f,
		60.0f,
		3.0f, 1.5f,
		15, 15, 0, 15,
		1, 0, 1,
		361.0,		/* Product literature: no steered beams */
		10000,		/* Guessed */
		0.05f,		/* From MEET spreadsheet */
		0.07f
	},
	{
		"Reson SeaBat 8125", "sb8125",
		SEABAT_PROP | (u32)DEVICE_IS_STEERED,
		240,
		455.0f, 0.0f,	/* Product literature */
		60.0f,		/* Product literature */
		0.5f, 1.0f,
		40, 40, 0, 40,
		1, 0, 1,
		0.0,		/* Flat plate transducer - all beams are steered */
		20000,		/* Guessed */
		0.006f,		/* Product literature - two wavelengths at 455kHz*/
		0.07f		/* As 8101 --- variable */
	},
	{
		"Reson SeaBat 8111", "sb8111",
		SEABAT_PROP,
		101,
		100.0f, 0.0f,	/* Product literature */
		75.0f,			/* Product literature */
		1.5f, 1.5f,		/* Product literature --- although along-track can be
						 * adjusted in steps from 1.5 - 6.0 deg. */
		35, 35, 0, 35,	/* Product literature */
		1, 0, 1,
		361.0,			/* Guess --- No steered beams? */
		80000,			/* Guessed - two samples in 3.7cm for nominal range cell. */
		0.037f,			/* Product literature */
		0.15f			/* Guess --- probably variable? */
	},
	{	"Simrad EM120", "em120",
		SIMRAD_PROP,
		191,
		12.0f, 0.0f,
		75.0f,
		2.0f, 1.0f,
		10, 10, 200, 10,
		1, 100, 1,
		0.0,		/* From MEET spreadsheet */
		2500,		/* Guessed */
		0.60f,		/* From MEET spreadsheet */
		2.0f			/* MEET spreadsheet */
	},
	{	"Atlas Hydrosweep DS", "hsweepds",
		ATLAS_PROP,
		59,
		15.5, 0.0,	/* Manufacturer's information (R/V T. G. Thompson) */
		45.0,
		2.3f, 2.3f,
		10, 10, 10, 0,
		1, 1, 0,
		0.0,		/* Flat plate transducer - all beams are steered */
		5000.0,		/* Guessed */
		0.10f,		/* Acoustic Wavelength */
		5.0f			/* Guessed - EM300 equivalent */
	},
	{	"Elac Nautik 1180",	"en1180",
		ELAC_PROP,
		126,
		180.0, 0.0,
		77.0,
		1.5, 1.5,
		10, 10, 10, 0,
		1, 1, 0,
		0.0,		/* Pair of flat-plate transducers, all beams are steered */
		24000.0,	/* At most --- maximum bandwidth 12kHz */
		0.06f,		/* One sample at 24kHz - Unrealistic, really */
		0.15f		/* Variable --- this is the minimum */
	},
	{	"Seabeam 2112", "sb2112",
		SEABEAM_PROP,
		151,
		12.0, 0.0,
		75.0,
		2.0f, 2.0f,
		10, 10, 10, 0,
		1, 1, 0,
		0.0,		/* Flat plate transducer - all beams are steered */
		5000.0,		/* Probable - mostly like the Hydrosweep DS */
		0.30f,		/* One sample at 5kHz */
		2.5f		/* Probable */
	},
	{	"Klein 595", "k595",	/* Klein analog 100/500kHz sidescan */
		KLEIN_PROP,
		2,
		100.0f, 500.0f,	/* Note that not all fish are dual frequency */
		0.0f,
		60.0f, 1.5f,
		10, 0, 0, 10,
		0, 0, 1,
		0.0,
		10000,		/* Guessed */
		0.0f,		/* Unknown */
		0.0f		/* Unknown */
	},
	{	"Klein 2000", "k2000",	/* Klein digital dual frequency (2260 NV) */
		KLEIN_PROP,
		2,
		100.0f, 500.0f,	/* Note that not all fish are dual frequency */
		0.0f,
		40.0f, 1.0f,
		10, 0, 10, 10,
		0, 1, 1,
		0.0,
		10000,		/* Guessed */
		0.0f,		/* Unknown */
		0.025f		/* User's Manual --- This is the minimum (max 0.5ms) */
	},
	{	"Klein 3000", "k3000",	/* Klein digital dual frequency + K5K topside */
		KLEIN_PROP,
		2,
		100.0f, 500.0f,	/* Note that not all fish are dual frequency */
		0.0f,
		40.0f, 1.0f,
		10, 0, 10, 10,
		0, 1, 1,
		0.0,
		27900,		/* Guessed from product literature */
		0.0f,		/* Unknown */
		0.025f		/* Guessed */
	},
	{	"Klein 5000", "k5000",	/* Note this is worst case for 5400/5500/5800 */
		KLEIN_PROP | (u32)DEVICE_IS_MB | (u32)DEVICE_HAS_ATTITUDE,
		16,				/* This is 5800 series in maximal mode */
		455.0f, 0.0f,
		0.0f,
		40.0f, 1.0f,
		10, 80, 10, 80,	/* Assumes port/stbd are buffered together per beam */
		8, 1, 8,
		0.0,
		28437.5,	/* 1/16th of transmit frequency: round-robin sampling */
		0.0f,		/* Unknown */
		0.025f		/* Guessed */
	}
};

Device device_new(void)
{
	Device	rtn;
	
	if ((rtn = (Device)calloc(1, sizeof(sDevice))) == NULL) {
		error_msg(modname, "failed to allocate workspace.\n");
		return(NULL);
	}
	return(rtn);
}

Device device_new_by_type(DeviceType type)
{
	Device	rtn;
	
	if (type <= DEVICE_UNKNOWN || type >= DEVICE_NDEVS) {
		error_msgv(modname, "internal: device %d not known.\n", (u32)type);
		return(NULL);
	}
	if ((rtn = device_new()) == NULL) {
		error_msg(modname, "failed to allocate device space.\n");
		return(NULL);
	}
	rtn->type = type;
	return(rtn);
}

Device device_new_by_name(const char *name)
{
	u32		dev;
	Device	rtn;
	
	for (dev = 0; dev < DEVICE_NDEVS; ++dev)
		if (strcmp(devices[dev].recname, name) == 0) break;
	if (dev == DEVICE_NDEVS) {
		error_msgv(modname, "device \"%s\" not recognised.\n", name);
		return(NULL);
	}
	if ((rtn = device_new()) == NULL) {
		error_msg(modname, "failed to allocate device space.\n");
		return(NULL);
	}
	rtn->type = (DeviceType)dev;
	return(rtn);
}

void device_release(Device device)
{
	free(device);
}

Device device_new_from_omg_tool(s32 tool_type)
{
	s32		device = 0;
	Device	rtn;
	
	/* Run linear search through translation table */
	while (omg2id[device].OMG_ID != tool_type && omg2id[device].OMG_ID != -1)
		++device;
	
	if (omg2id[device].OMG_ID == -1) return(NULL);
	if ((rtn = device_new()) == NULL) return(NULL);
	rtn->type = omg2id[device].id;
	return(rtn);
}

Bool device_set_device(Device device, DeviceType type)
{
	if (type <= DEVICE_UNKNOWN || type >= DEVICE_NDEVS) {
		error_msgv(modname, "device %d not known.\n", (u32)type);
		return(False);
	}
	device->type = type;
	return(True);
}

DeviceType device_get_device(Device device)
{
	return(device->type);
}

u32 device_get_properties(Device device)
{
	if (device->type >= DEVICE_NDEVS) {
		error_msgv(modname, "internal: device type %d not known.\n",
					(u32)(device->type));
		return(0);
	}
	return(devices[device->type].props);
}

char *device_get_name(Device device)
{
	if (device->type >= DEVICE_NDEVS) {
		error_msgv(modname, "internal: device type %d not known.\n",
					(u32)(device->type));
		return(NULL);
	}
	return(devices[device->type].name);
}

u32 device_get_nbeams(Device device)
{
	if (device->type >= DEVICE_NDEVS) {
		error_msgv(modname, "internal: device type %d not known.\n",
					(u32)(device->type));
		return(0);
	}
	return(devices[device->type].maxbeams);
}

Bool device_get_beamwidths(Device device, f32p across, f32p along)
{
	if (device->type >= DEVICE_NDEVS) {
		error_msgv(modname, "internal: device type %d not known.\n",
					(u32)(device->type));
		return(False);
	}
	*across = devices[device->type].across_width;
	*along = devices[device->type].along_width;
	return(True);
}

Bool device_get_frequencies(Device device, f32p f1, f32p f2)
{
	if (device->type >= DEVICE_NDEVS) {
		error_msgv(modname, "internal: device type %d not known.\n",
					(u32)(device->type));
		return(False);
	}
	*f1 = devices[device->type].freq_1;
	*f2 = devices[device->type].freq_2;
	return(True);
}

Bool device_get_dr(Device device, f32p dr)
{
	if (device->type >= DEVICE_NDEVS) {
		error_msgv(modname, "internal: device tyep %d not known.\n",
				(u32)(device->type));
		return(False);
	}
	*dr = devices[device->type].range_samp_dr;
	return(True);
}

Bool device_get_samp_freq(Device device, f32p fs)
{
	if (device->type >= DEVICE_NDEVS) {
		error_msgv(modname, "internal: device tyep %d not known.\n",
				(u32)(device->type));
		return(False);
	}
	*fs = devices[device->type].amp_samp_freq;
	return(True);
}

Bool device_get_pulse_len(Device device, f32p pl)
{
	if (device->type >= DEVICE_NDEVS) {
		error_msgv(modname, "internal: device tyep %d not known.\n",
				(u32)(device->type));
		return(False);
	}
	*pl = devices[device->type].pulse_len;
	return(True);
}

Device device_new_from_file(FILE *fd)
{
	Device	rtn;
	
	if ((rtn = device_new()) == NULL) {
		error_msg(modname, "failed allocating device buffer\n");
		return(NULL);
	}
	if (fread(&(rtn->type), sizeof(u32), 1, fd) != 1) {
		error_msg(modname, "failed reading device info from file.\n");
		device_release(rtn);
		return(NULL);
	}
	if (rtn->type <= DEVICE_UNKNOWN || rtn->type >= DEVICE_NDEVS) {
		error_msgv(modname, "device %d not known.\n", (u32)(rtn->type));
		device_release(rtn);
		return(NULL);
	}
	return(rtn);
}

Bool device_write(Device device, FILE *fd)
{
	if (fwrite(&(device->type), sizeof(u32), 1, fd) != 1) return(False);
	return(True);
}

u32 device_get_rate(Device device, DeviceRates type)
{
	u32	rtn = 0;
	
	switch(type) {
		case DEVICE_PING_RATE:
			rtn = devices[device->type].rep_rate;
			break;
		case DEVICE_BATHY_RATE:
			rtn = devices[device->type].bathy_rate;
			break;
		case DEVICE_ATTITUDE_RATE:
			rtn = devices[device->type].att_rate;
			break;
		case DEVICE_IMAGERY_RATE:
			rtn = devices[device->type].imagery_rate;
			break;
		default:
			error_msgv(modname, "internal: request for rate of undefined data"
				" type (%d).\n", (u32)type);
			break;
	}
	return(rtn);
}

u32 device_get_density(Device device, DeviceDensities type)
{
	u32		rtn = 0;
	
	switch(type) {
		case DEVICE_BATHY_DENSITY:
			rtn = devices[device->type].bathy_density;
			break;
		case DEVICE_ATTITUDE_DENSITY:
			rtn = devices[device->type].att_density;
			break;
		case DEVICE_IMAGERY_DENSITY:
			rtn = devices[device->type].imagery_density;
			break;
		default:
			error_msgv(modname, "internal: request for density of undefined "
				"data type (%d).\n", (u32)type);
			break;
	}
	return(rtn);
}

f32 device_get_steerangle(Device device)
{
	if (device->type >= DEVICE_NDEVS) {
		error_msgv(modname, "internal: device type %d not known.\n",
					(u32)(device->type));
		return(0);
	}
	return(devices[device->type].steering_angle);
}

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

#define SIMRAD_EM300_K1		0.5		/* Dimensionless system constant */
#define SIMRAD_EM300_K2		0.5		/* Dimensionless system constany */
#define SIMRAD_EM300_TBW	1.0		/* Transmit beamwidth, deg. */
#define SIMRAD_EM300_RBW	1.0		/* Receive beamwidth, deg. */
#define SIMRAD_EM300_DR		0.15	/* Range sampling distance, m */
#define SIMRAD_EM120_K1		0.5		/* Dimensionless system constant */
#define SIMRAD_EM120_K2		0.5		/* Dimensionless system constany */
#define SIMRAD_EM120_TBW	1.0		/* Transmit beamwidth, deg. */
#define SIMRAD_EM120_RBW	2.0		/* Receive beamwidth, deg. */
#define SIMRAD_EM120_DR		0.6	/* Range sampling distance, m */
#define NOMINAL_SSPEED_ONEWAY	750.0	/* Nominal one-way sound speed, m/s */

f64 device_compute_angerr(Device device, Platform *plat, Sounding *snd,
						  f64 angle)
{
	f64		rtn = 0.0,
			bw,		/* Beam Width */
			np,		/* Number of Phase Samples */
			drp,	/* Sampling range in phase samping mode */
			crosstrack;	/* Cross-track distance to located sounding */
	Bool	detect_amp = SOUNDING_ISAMPDET(snd->flags);
	
	switch(device->type) {
		case DEVICE_EM300:
			/* Treat EM300 as EM100 in shallow mode */
			if (detect_amp) {
				rtn = SIMRAD_EM300_K1*tan(DEG2RAD(SIMRAD_EM300_RBW))*
					  SIMRAD_EM300_K1*tan(DEG2RAD(SIMRAD_EM300_RBW));
			} else {
				np = SOUNDING_GETWINDOWSZ(snd->flags);
				rtn = tan(SIMRAD_EM300_K2*DEG2RAD(SIMRAD_EM300_RBW)/sqrt(np))*
					  tan(SIMRAD_EM300_K2*DEG2RAD(SIMRAD_EM300_RBW)/sqrt(np));
#ifdef __DEBUG__
error_msgv(modname, "debug: depth = %lf m, range = %lf m, np = %d samples,"
	" angle error = %lf rad^2.\n", snd->depth, snd->range, np, rtn);
#endif
			}
			break;
		case DEVICE_EM120:
			bw = DEG2RAD(devices[device->type].across_width)/cos(angle);
			np = SOUNDING_GETWINDOWSZ(snd->flags);
			if (SOUNDING_ISAMPDET(snd->flags)) {
				rtn = bw/12.0;
			} else {
				if (np == 0) np = 1;
				rtn = 0.2*bw/sqrt(np);
			}
#ifdef __DEBUG__
error_msgv(modname, "debug: angle = %lf, drp = %lf, crosstrack = %lf, "
	"np = %lf, total = %lf\n", angle, drp, crosstrack, np, rtn);
#endif
			rtn *= rtn;	/* Dealing with variances */
			break;
		case DEVICE_EM1000:	/* Note that we treat these all the same way,     */
		case DEVICE_EM1002:	/* since they are effectively the same technology!*/
		case DEVICE_SB8101:
		case DEVICE_SB8111:
			bw = DEG2RAD(devices[device->type].across_width);
			np = SOUNDING_GETWINDOWSZ(snd->flags);
			if (SOUNDING_ISAMPDET(snd->flags)) {
				rtn = bw/12.0;
			} else {
/*				drp = NOMINAL_SSPEED_ONEWAY/
						devices[device->type].amp_samp_freq;
				crosstrack = snd->depth*tan(fabs(angle));
				np = floor(bw*crosstrack/(drp*cos(fabs(angle))));*/
				if (np == 0) np = 1;
				rtn = 0.2*bw/sqrt(np);
			}
#ifdef __DEBUG__
error_msgv(modname, "debug: angle = %lf, drp = %lf, crosstrack = %lf, "
	"np = %lf, total = %lf\n", angle, drp, crosstrack, np, rtn);
#endif
			rtn *= rtn;	/* Dealing with variances */
			break;
		case DEVICE_ELAC1180:
			/* Note that the Elac 1180 is a split flat-plate transducer, but
			 * the beam width is fixed and it doesn't do phase detections, so
			 * we can treat it quite simply.  Note that this has a separate
			 * entry (rather than being subsumed into the 9001/9003 entry) so
			 * that we can fiddle with it to make it fit the data...
			 */
			bw = DEG2RAD(devices[device->type].across_width);
			rtn = bw/12.0;
			rtn *= rtn;
			break;
		case DEVICE_SB9001:
		case DEVICE_SB9003:
			/* 9001 and 9003 have no phase detect, so we're always working in
			 * the amplitude mode --- uniform distribution of error.
			 */
			bw = DEG2RAD(devices[device->type].across_width);
			rtn = bw/12.0;
			rtn *= rtn;
			break;
		case DEVICE_HSWEEPDS:
		case DEVICE_SB2112:
			/* Treat hydrosweep as a basic flat-plate beamformer in amplitude
			 * detect mode only.  In fact (2001-07-19) it appears that things
			 * might be even worse!  Dijkstra reports the early DS series
			 * devices like the ones on the original AGOR-23 class ships
			 * (Thompson, Ewing, Revell) used a threshold detection
			 * method for their bottom pick, which would cause significant
			 * difficulties in soft bottoms where the return is quite diffuse.
			 * In particular, this model is probably not adequate in those
			 * cases, where significant artifact is typically observed (see,
			 * e.g., the R/V Thompson TN130 cruise data in the Nootka Fracture
			 * Zone, particularly in the deeper (2500m) water.
			 *
			 * However: the Hydrosweep DS is going away, and the newer versions
			 * have a different beamformer anyway, so there is little point in
			 * getting too fussy with this for legacy data.
			 */
			/* Added SeaBeam 2112 systems 2002-09-12 --- they have a more complex
			 * beam-former than the HS/DS, but it's all basically amplitude
			 * detect, although the outer beams have a more direct direction of
			 * arrival/time of arrival bottom detection the further out you go,
			 * which might be more useful.  There is no reliable way to determine
			 * what the detection method was, however, so ...
			 */
			bw = DEG2RAD(devices[device->type].across_width);
			rtn = bw/12.0;	/* Uniform distribution */
			rtn *= rtn;
			break;
		case DEVICE_EM3000:
		case DEVICE_EM3000D:
			bw = DEG2RAD(devices[device->type].across_width)/cos(angle);
				/* Beamwidth is a function of angle due to flat plate and
				   FFT beamformer.
				 */
			np = SOUNDING_GETWINDOWSZ(snd->flags);
			if (SOUNDING_ISAMPDET(snd->flags)) {
				rtn = bw/12.0;	/* Uniform distribution */

#ifdef __DEBUG__
error_msgv(modname, "debug: (amp det) flags = %d angle = %le bw = %le "
	"np = %le rtn = %le\n", snd->flags, angle, bw, np, rtn);
#endif

			} else {
				drp = NOMINAL_SSPEED_ONEWAY/
						devices[device->type].amp_samp_freq;
				crosstrack = snd->depth*tan(fabs(angle));
/*				np = floor(bw*crosstrack/(drp*cos(fabs(angle))));*/
				if (np == 0) np = 1;
					/* Avoid anomalous phase detection problems: for some
					 * reason, EM3Ks sometimes pick phase detects at very low
					 * angles of incidence, resulting in small np; left
					 * undetected, this causes rtn -> inf with lots of knock-on
					 * errors.  This is a bodge, but until we can get np from
					 * raw files, this is as good as it gets.
					 */
				rtn = 0.2*bw/sqrt(np);

#ifdef __DEBUG__
error_msgv(modname, "debug: (phase det) flags = %d angle = %le bw = %le "
	"drp = %le crosstk = %le np = %le rtn = %le\n", snd->flags, angle, bw, drp,
	crosstrack, np, rtn);
#endif

			}
			rtn *= rtn;
			break;
		case DEVICE_SB8125:
			/* Note that we treat the SB8125 as the EM3000 since both are
			 * flat plate all-beamformed devices.  Confirmation that beam
			 * width across-track is only 0.5 deg. from Rich Lear during
			 * ScapaMAP cruise (06/2001) with 8125 on board S/V Scimitar.
			 */
			bw = DEG2RAD(devices[device->type].across_width)/cos(angle);
				/* Beamwidth is a function of angle due to flat plate and
				   FFT beamformer.
				 */
			np = SOUNDING_GETWINDOWSZ(snd->flags);
			if (SOUNDING_ISAMPDET(snd->flags)) {
				rtn = bw/12.0;	/* Uniform distribution */

#ifdef __DEBUG__
error_msgv(modname, "debug: (amp det) flags = %d angle = %le bw = %le "
	"np = %le rtn = %le\n", snd->flags, angle, bw, np, rtn);
#endif

			} else {
/*				drp = NOMINAL_SSPEED_ONEWAY/
						devices[device->type].amp_samp_freq;
				crosstrack = snd->depth*tan(fabs(angle));*/
				if (np == 0) np = 1;
					/* Avoid anomalous phase detection problems */
				rtn = 0.2*bw/sqrt(np);

#ifdef __DEBUG__
error_msgv(modname, "debug: (phase det) flags = %d angle = %le bw = %le "
	"drp = %le crosstk = %le np = %le rtn = %le\n", snd->flags, angle, bw, drp,
	crosstrack, np, rtn);
#endif

			}
			rtn *= rtn;
			break;
		default:
			rtn = DBL_MAX;
			break;
	}
	return(rtn);
}

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

#define SIMRAD_EM300_DEPTH_DR	0.003	/* Range error as % of depth */
#define SIMRAD_EM300_BEST_DR	0.15	/* Minimum range error, m */
#define SIMRAD_EM120_DEPTH_DR	0.003	/* Range error as % of depth */
#define SIMRAD_EM120_BEST_DR	0.6	/* Minimum range error, m */
#define SIMRAD_EM1000_DRS		0.15	/* Range sampling distance, m */
#define SIMRAD_EM1000_PULSELEN	0.2e-3	/* Pulselength, s */
#define SIMRAD_EM1002_DRS		0.0626	/* Range sampling distance, m */
#define SIMRAD_EM1002_PULSELEN	0.2e-3	/* Pulselength, s */
#define SIMRAD_EM3000_DRS		0.05	/* Range sampling distance, m */
#define SIMRAD_EM3000_PULSELEN	0.033333e-3	/* Pulselength, s */
#define SEABAT_8101_DRS			0.05
#define SEABAT_8101_PULSELEN	0.004e-3
#define SEABAT_8111_DRS			0.037
#define SEABAT_8111_PULSELEN	0.01e-3
#define SEABAT_8125_DRS			0.006	 /* MEET, from Reson */
#define SEABAT_8125_PULSELEN	0.011e-3 /* 0.011-0.292, MEET from Reson */
#define SEABAT_9001_DRS			0.05
#define SEABAT_9001_PULSELEN	0.07e-3	
#define SEABAT_9003_DRS			0.05
#define SEABAT_9003_PULSELEN	0.07e-3
#define ATLAS_HSWEEPDS_DEPTH_DR	0.006	/* Range error as % of depth */
#define ATLAS_HSWEEPDS_BEST_DR	0.15	/* Minimum range error, m */
#define ELAC_1180_DRS			0.06	 /* MEET, from Reson */
#define ELAC_1180_PULSELEN		0.15e-3 /* 0.15-1ms, Elac Spec */
#define SEABEAM_2112_DEPTH_DR	0.005	/* Range error as % of depth */
#define SEABEAM_2112_BEST_DR	0.15	/* Minimum range error, m */

f64 device_compute_rangeerr(Device device, Platform *plat, Sounding *snd)
{
	f64	rtn = DBL_MAX;
	
	switch(device->type) {
		case DEVICE_EM300:
			/* Treat EM300 as EM100 in shallow mode */
			rtn = SIMRAD_EM300_DEPTH_DR * snd->depth;
			if (rtn < SIMRAD_EM300_BEST_DR) rtn = SIMRAD_EM300_BEST_DR;
			rtn *= rtn;
			break;
		case DEVICE_EM120:
			rtn = SIMRAD_EM120_DEPTH_DR * snd->depth;
			if (rtn < SIMRAD_EM120_BEST_DR) rtn = SIMRAD_EM120_BEST_DR;
			rtn *= rtn;
			break;
		case DEVICE_HSWEEPDS:
			/* Treat original DS series as EM deep water systems */
			rtn = ATLAS_HSWEEPDS_DEPTH_DR * snd->depth;
			if (rtn < ATLAS_HSWEEPDS_BEST_DR) rtn = ATLAS_HSWEEPDS_BEST_DR;
			rtn *= rtn;
			break;
		case DEVICE_SB2112:
			/* Treat as original EM deep water systems */
			rtn = SEABEAM_2112_DEPTH_DR * snd->depth;
			if (rtn < SEABEAM_2112_BEST_DR) rtn = SEABEAM_2112_BEST_DR;
			rtn *= rtn;
			break;
		case DEVICE_EM1000:
			rtn = (SIMRAD_EM1000_DRS/2.0)*(SIMRAD_EM1000_DRS/2.0) +
				  (plat->mean_speed*SIMRAD_EM1000_PULSELEN/4.0)*
				  (plat->mean_speed*SIMRAD_EM1000_PULSELEN/4.0);
			break;
		case DEVICE_EM1002:
			rtn = (SIMRAD_EM1002_DRS/2.0)*(SIMRAD_EM1002_DRS/2.0) +
				  (plat->mean_speed*SIMRAD_EM1002_PULSELEN/4.0)*
				  (plat->mean_speed*SIMRAD_EM1002_PULSELEN/4.0);
			break;
		case DEVICE_EM3000:
		case DEVICE_EM3000D:
			rtn = (SIMRAD_EM3000_DRS/2.0)*(SIMRAD_EM3000_DRS/2.0) +
				  (plat->mean_speed*SIMRAD_EM3000_PULSELEN/4.0)*
				  (plat->mean_speed*SIMRAD_EM3000_PULSELEN/4.0);
			break;
		case DEVICE_SB8125:
			rtn = (SEABAT_8125_DRS/2.0)*(SEABAT_8125_DRS/2.0) +
				  (plat->mean_speed*SEABAT_8125_PULSELEN/4.0)*
				  (plat->mean_speed*SEABAT_8125_PULSELEN/4.0);
			break;
		case DEVICE_ELAC1180:
			rtn = (ELAC_1180_DRS/2.0)*(ELAC_1180_DRS/2.0) +
				  (plat->mean_speed*ELAC_1180_PULSELEN/4.0)*
				  (plat->mean_speed*ELAC_1180_PULSELEN/4.0);
			break;
		case DEVICE_SB8101:
			rtn = (SEABAT_8101_DRS/2.0)*(SEABAT_8101_DRS/2.0) +
				  (plat->mean_speed*SEABAT_8101_PULSELEN/4.0)*
				  (plat->mean_speed*SEABAT_8101_PULSELEN/4.0);
			break;
		case DEVICE_SB8111:
			rtn = (SEABAT_8111_DRS/2.0)*(SEABAT_8111_DRS/2.0) +
				  (plat->mean_speed*SEABAT_8111_PULSELEN/4.0)*
				  (plat->mean_speed*SEABAT_8111_PULSELEN/4.0);
			break;
		case DEVICE_SB9001:
			rtn = (SEABAT_9001_DRS/2.0)*(SEABAT_9001_DRS/2.0) +
				  (plat->mean_speed*SEABAT_9001_PULSELEN/4.0)*
				  (plat->mean_speed*SEABAT_9001_PULSELEN/4.0);
		case DEVICE_SB9003:
			rtn = (SEABAT_9003_DRS/2.0)*(SEABAT_9003_DRS/2.0) +
				  (plat->mean_speed*SEABAT_9003_PULSELEN/4.0)*
				  (plat->mean_speed*SEABAT_9003_PULSELEN/4.0);
			break;
		default:
			break;
	}
	return(rtn);
}

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

f64 device_beam_to_angle(Device device, f32 sspeed, u32 beam)
{
	f64	rtn, offset, wavelength, beamspacing;
	s32	mid_point;
	
	if (beam > devices[device->type].maxbeams) {
		error_msgv(modname, "device %d (%s) only has %d beams (requested"
			" angle for %d).\n",
			(u32)(device->type),
			devices[device->type].name,
			devices[device->type].maxbeams,
			beam);
		return(-1.0);
	}
	switch(device->type) {
		case DEVICE_EM120:
		case DEVICE_EM300:
		case DEVICE_EM1000:
		case DEVICE_EM1002:
		case DEVICE_HSWEEPDS:
		case DEVICE_SB2112:
			/* Assume that beams are in equi-angular mode, and are spread
			 * evenly across the opening angle of the aperture.
			 */
			offset = 2.0*devices[device->type].max_angle/
						(devices[device->type].maxbeams-1);
			mid_point = devices[device->type].maxbeams/2;
			rtn = ((s32)beam-mid_point)*offset;
			break;
		case DEVICE_EM3000:
			/* From Hare et al. Spreadsheet */
			mid_point = devices[device->type].maxbeams/2;
			wavelength = sspeed/(devices[device->type].freq_1*1000.0);
			rtn = RAD2DEG(asin(3.0637*((s32)beam-mid_point)*wavelength));
			break;
		case DEVICE_SB8101:
		case DEVICE_SB8111:
		case DEVICE_SB9001:
		case DEVICE_SB9003:
		case DEVICE_SB8125:
			offset = 2.0*devices[device->type].max_angle/
						(devices[device->type].maxbeams-1);
			mid_point = devices[device->type].maxbeams/2;
			rtn = ((s32)beam-mid_point)*offset;
			break;
		case DEVICE_EM3000D:
			/* The EM3000D is a dual-head configuration of EM3000's.  They have
			 * the same form as a generic split-head transducer pair, with the
			 * port head rolled up by about 20 deg., and the stbd rolled down
			 * by about the same amount.  The systems, however, don't use all of
			 * their possible opening aperture (since they would be looking out
			 * at 85 deg. otherwise), and we have to compensate here.
			 */
			beamspacing = 0.9524; /* Total 120 deg. aperture on face */
			mid_point = devices[device->type].maxbeams/2;
			if ((s32)beam < mid_point) {
				/* Port head --- first beam is at 55 deg. port to the face */
				rtn = -55.0 + beamspacing*beam;
			} else {
				/* Stbd head --- first beam is at 65 deg. port to the face */
				rtn = -65.0 + beamspacing*((s32)beam - mid_point);
			}
			break;
		case DEVICE_ELAC1180:
			beamspacing = 1.25;	/* Degrees, Elac Spec. */
			mid_point = devices[device->type].maxbeams/2;
			/* The Elac 1180 is a split-head transducer, so we need to compute
			 * separately for port and starboard heads.  Note that this is set
			 * up for the Elac firmware implemented on the NOAA Ship Rainier,
			 * where the inner-most beam on each side is set up to have its
			 * inside 3dB point at nadir w.r.t. the transducer.  This fixes
			 * interference problems resulting in inconsistent depths in the
			 * overlap region on typical setups, but leaves a skunk-stripe in
			 * the data directly below the transducer.
			 */
			if ((s32)beam < mid_point) {
				/* Port side */
				rtn = 37.25 + beamspacing*((s32)beam - (mid_point-1));
			} else {
				/* Starboard side */
				rtn = -37.25 + beamspacing*((s32)beam - mid_point);
			}
			break;
		default:
			rtn = -1.0;
			break;
	}
	return(rtn);
}
