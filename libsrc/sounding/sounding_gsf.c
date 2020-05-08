/*
 * $Id: sounding_gsf.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.5.2.2  2002/12/15 01:46:11  brc
 * Cleaned up some error message information to avoid a lot of noise.
 *
 * Revision 1.5.2.1  2002/07/14 02:20:37  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.5  2002/03/14 04:12:54  brc
 * Modified code to do the error attribution in this module, rather than at
 * the sounding() stage.  This is required for dual-head support, which is not
 * known at the sounding.c level, but can be applied here.
 *
 * Revision 1.4  2001/12/07 21:23:12  brc
 * Added facility to select which head of a dual-headed installation is read
 * from the GSF file (any, port or stbd).  This goes with a 'select' parameter
 * in the sounding.gsf clause.  Also verified the Reson flag preservation with
 * Shannon Byrne, and that the quality_factor array is just nibbles of data
 * matching the Reson flags cast to f32s.  Code now sets the flags correctly
 * for bottom detection type, but uses phase detection in the case where both
 * amplitude and phase detection are indicated --- presumably Reson are using
 * some form of blended solution.  Where available, phase is typically lower
 * error, which is preferred.
 *
 * Revision 1.3  2001/08/21 00:40:45  brc
 * Added some debug code (conditionally compiled) to indicate where the beams
 * are being resolved to.
 *
 * Revision 1.2  2001/05/14 04:23:28  brc
 * Updated to make modules 'params'-aware.
 *
 * Revision 1.1  2001/04/10 23:22:10  brc
 * Added to implement GSF file format read-in and reformatting.
 *
 *
 * File:	sounding_gsf.c
 * Purpose:	Read and parse GSF files (with the aid of a safety net)
 * Date:	4 March 2001
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
#include "stdtypes.h"
#include "error.h"
#include "device.h"
#include "sounding.h"
#include "sounding_private.h"
#include "sounding_gsf.h"
#include "gsf.h"
#include "params.h"

#undef __DEBUG__

static char *modname = "sounding_gsf";
static char *modrev = "$Revision: 2 $";

#define DEG2RAD(x) ((x)*M_PI/180.0)

#define MIN_SURF_SSPEED		1000.0
#define MAX_SURF_SSPEED		2000.0
#define DEFAULT_SURF_SSPEED	1500.0	/* Default if no data is available */
static f32 default_surf_sspeed = DEFAULT_SURF_SSPEED;
static u32 default_tdr = 0;

extern int gsfError;

#define GSF_KTS2MS(x) ((x)*1852.0/3600.0) /* 1852 m/nm, 3600 sec/hr. */

typedef struct {
	int			gsfHandle;
} GBuffer;		/* Internal buffer structure for reading data */

void sounding_gsf_release(void *buffer)
{
	GBuffer	*buf = (GBuffer*)buffer;
	
	if (buf != NULL) {
		gsfClose(buf->gsfHandle);
		free(buf);
	}
}

/* Routine:	sounding_gsf_check_hdr
 * Purpose:	Checks file header and stores reference pointer
 * Inputs:	stream		Pointer to the SoundingStream being configured
 *			gsfHandle	Handle returned by GSFlib for the file
 * Outputs:	True if the setup was completed, otherwise False
 * Comment:	The only potential for failure here is if the buffer to store the
 *			gsfHandle pointer runs out of memory.
 */

Bool sounding_gsf_check_hdr(SoundingStream stream, int gsfHandle)
{
	GBuffer	*store;
	
	/* Grab memory for the OBuffer structure (needed to read file summary) */
	if ((store = (GBuffer*)calloc(1, sizeof(GBuffer))) == NULL) {
		error_msg(modname, "failed allocation GSF private workspace.\n");
		return(False);
	}
	
	/* Save GSF pointer for file into private workspace */
	store->gsfHandle = gsfHandle;
		
	/* Patch private workspace into stream and return */
	stream->private = store;
	stream->release_func = sounding_gsf_release;
	stream->data_avail = SOUNDING_BATHY;
	return(True);
}

/* Routine:	sounding_gsf_get_next
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

FileError sounding_gsf_get_next(SoundingStream stream, Platform *orient,
								Ping ping)
{
	s32		in_beam, out_beam;
	Device	filedev = NULL;	/* Because we can't always trust the user/file ... */
	GBuffer	*store = (GBuffer*)(stream->private);
	Bool	done = False;
	s32		bytes_read;
	u32		nsamples;	/* Samples in the footprint of each beam */
	f32		dr,			/* Range sampling for time->sample reconversion */
			time2samp,	/* Conversion factor for time->sample conversion */
			depth;		/* Converted depth */
	gsfDataID	id;
	gsfRecords	data;
	f64		angle;		/* Beam receive angle for error computations */
	FileError	rtn;	/* Return code after error attribution */
	
	if (store == NULL) {
		error_msg(modname, "private workspace is not initialised.\n");
		return(FILE_NOTINIT);
	}
	
	/* Scan the GSF file until we get the next bathy packet */
	while (!done) {
		bytes_read = gsfRead(store->gsfHandle, GSF_NEXT_RECORD, &id, &data,
							NULL, 0);
		if (bytes_read < 0) {
			/* Something went wrong ... */
			if (gsfError == GSF_READ_TO_END_OF_FILE)
				return(FILE_EOF);
			else {
				error_msgv(modname, "underlying library error: %s.\n",
					gsfStringError());
				return(FILE_INVALID);
			}
		} else if (bytes_read == 0)
			return(FILE_EOF);
		/* Check what it was that we found.  Note that we use switch() rather
		 * than if() so that we can expand this in the future (i.e., to read
		 * more out of the GSF file) if required.
		 */
		switch(id.recordID) {
			case GSF_RECORD_SWATH_BATHYMETRY_PING:
				if (default_tdr == 0)
					/* Select any ping from any head */
					done = True;
				else if (data.mb_ping.sensor_id ==
							GSF_SWATH_BATHY_SUBRECORD_SEABAT_8101_SPECIFIC) {
#ifdef __DEBUG__
					error_msgv(modname, "debug: selecting head on 8101, mode %d.\n",
							data.mb_ping.sensor_data.gsfSeaBat8101Specific.mode);
#endif
					if ((s32)default_tdr ==
							data.mb_ping.sensor_data.gsfSeaBat8101Specific.mode) {
#ifdef __DEBUG__
						error_msgv(modname, "debug: selected head %d.\n",
							default_tdr);
#endif
						done = True;
					} else
						done = False;
				} else
					/* Get to here if selecting a head, but not using an SB8101
					 * so we accept any ping.
					 */
					done = True;
				break;
			default:
				break;
		}
	}
	
	/* Check that we've got enough space to do the conversion */
	if (ping->buffer_space < (u32)data.mb_ping.number_beams) {
		error_msgv(modname, "ping has %d beams; buffer is only %d long.\n",
			data.mb_ping.number_beams, ping->buffer_space);
		return(FILE_INVALID);
	}
	orient->longitude = data.mb_ping.longitude;
	orient->latitude = data.mb_ping.latitude;	
	orient->roll = (f32)data.mb_ping.roll;
	orient->pitch = (f32)data.mb_ping.pitch;
	orient->heading = (f32)data.mb_ping.heading;
	orient->heave = (f32)data.mb_ping.heave;		
	orient->vessel_speed = (f32)GSF_KTS2MS(data.mb_ping.speed);
	
	/* We need to fill in the sound velocity at the surface, and the mean
	 * geometric sound velocity.  We get sound velocity depending on the device
	 * used to generate the file, if known.  For this, we have to use what the
	 * file tells us is the device, rather than what the SoundingStream tells
	 * us.  This is to patch around dodgy GSF files generated by third-party
	 * writers, which don't always tell the truth about what is in the file,
	 * or are not completely specified.
	 */
	dr = -1.0;	/* This is impossible in reality, so used as a sentinel */
	switch(data.mb_ping.sensor_id) {
		case GSF_SWATH_BATHY_SUBRECORD_EM1000_SPECIFIC:
			orient->mean_speed = orient->surf_sspeed =
				(f32)data.mb_ping.sensor_data.gsfEM1000Specific.surface_velocity;
			filedev = device_new_by_type(DEVICE_EM1000);
			device_get_dr(filedev, &dr);
			break;
		case GSF_SWATH_BATHY_SUBRECORD_EM1002_SPECIFIC:
			/* We hope that EM1002 is the same as the
			 * EM3000 specific entries, since the topside processing software
			 * and data formats at the Simrad end are the same.
			 */
			orient->mean_speed = orient->surf_sspeed =
				(f32)data.mb_ping.sensor_data.gsfEM3Specific.surface_velocity;
			filedev = device_new_by_type(DEVICE_EM1002);
			device_get_dr(filedev, &dr);
		case GSF_SWATH_BATHY_SUBRECORD_EM3000_SPECIFIC:
			orient->mean_speed = orient->surf_sspeed =
				(f32)data.mb_ping.sensor_data.gsfEM3Specific.surface_velocity;
			filedev = device_new_by_type(DEVICE_EM3000);
			device_get_dr(filedev, &dr);
			break;
		case GSF_SWATH_BATHY_SUBRECORD_SEABAT_II_SPECIFIC:
			/* This is SB9000 and variants since GSF V1.04 */
			orient->mean_speed = orient->surf_sspeed =
				(f32)data.mb_ping.sensor_data.gsfSeaBatIISpecific.surface_velocity;
			filedev = device_new_by_type(DEVICE_SB9001);
			device_get_dr(filedev, &dr);
			break;
		case GSF_SWATH_BATHY_SUBRECORD_SEABAT_8101_SPECIFIC:
			orient->mean_speed = orient->surf_sspeed =
				(f32)data.mb_ping.sensor_data.gsfSeaBat8101Specific.surface_velocity;
			filedev = device_new_by_type(DEVICE_SB8101);
			device_get_dr(filedev, &dr);
			break;
		default:
			orient->mean_speed = orient->surf_sspeed = default_surf_sspeed;
			break;
	}

#ifdef __DEBUG__
error_msgv(modname, "debug: mean_speed = %f surf_sspeed = %f\n",
	orient->mean_speed, orient->surf_sspeed);
#endif

	/* We need to compute a correction factor to convert echo time in seconds
	 * into samples in the footprint for the error computation code.  This is
	 * approximately vT/(2.dr) where v is the sound speed in m/s, T is the echo
	 * time in seconds, and dr is the range sampling distance in meters.  We
	 * use the mean speed in the column, which is an approximation, and we
	 * check that the sensor has been matched so that dr is set.
	 */
	if (dr > 0) time2samp = (f32)(orient->mean_speed / (2.0 * dr));
	else time2samp = 0.0; /* This will cause errors later if the code uses
						   * the value where it shouldn't.
						   */
	
	ping->n_beams = 0;
	for (in_beam = 0, out_beam = 0; in_beam < data.mb_ping.number_beams;
																	++in_beam) {
		depth = (f32)data.mb_ping.depth[in_beam];

		if (depth == GSF_NULL_DEPTH) continue;

#ifdef __DEBUG__
		error_msgv(modname, "debug: gsf: beam %d d=%lf (x,y)=(%lf, %lf), h = %f"
			" (lon,lat) = (%lf, %lf)\n", in_beam,
			data.mb_ping.depth[in_beam],
			data.mb_ping.along_track[in_beam],
			data.mb_ping.across_track[in_beam],
			orient->heading, orient->longitude, orient->latitude);
#endif

		sounding_locate_beam((f32)data.mb_ping.along_track[in_beam],
							 (f32)data.mb_ping.across_track[in_beam],
						     orient->heading,
							 orient->longitude, orient->latitude,
							 stream->proj,
							 &(ping->beams[out_beam].east),
							 &(ping->beams[out_beam].north));

#ifdef __DEBUG__
		error_msgv(modname, "debug: gsf: beam %d (lon,lat) = (%lf, %lf), "
			"(e,n) = (%lf, %lf).\n", in_beam,
			orient->longitude, orient->latitude,
			ping->beams[out_beam].east, ping->beams[out_beam].north);
#endif

		ping->beams[out_beam].beam_number = in_beam;
		if (data.mb_ping.mc_amplitude != NULL)
			ping->beams[out_beam].backscatter =
								(u8)(2.0*data.mb_ping.mc_amplitude[in_beam]);
		else
			ping->beams[out_beam].backscatter = 0;
		ping->beams[out_beam].file_id = ping->file_id;
		
		ping->beams[out_beam].flags = 0;	/* Initialise flags on each pass */
		
		/* GSF only stores information on the echo width, not the number of
		 * samples per beam.  We fake this here by finding the range sampling
		 * rate of the sonar, if possible, and doing the division.  If there is
		 * no echo-width sub-record, or if the sonar isn't known, then we
		 * set the sample window to one sample for each beam.
		 */
		if (data.mb_ping.echo_width != NULL) {
			if (dr > 0) {
				/* Matching above found a range rate: convert to samples */
				nsamples = (u32)(data.mb_ping.echo_width[in_beam] * time2samp);
				ping->beams[out_beam].flags |= SOUNDING_PUTWINDOWSZ(nsamples);
			} else
				/* Matching above failed: range rate not known. */
				ping->beams[out_beam].flags |= SOUNDING_PUTWINDOWSZ(1);
		} else
			ping->beams[out_beam].flags |= SOUNDING_PUTWINDOWSZ(1);

		/* Now we need to track which detection method was used (again for the
		 * error model).  Unfortunately GSF has two arrays ... one is marked
		 * 'quality_factor' and one is marked 'quality_flags'.  The source
		 * comment on `quality_flags' suggests that these are Reson specific
		 * (i.e., the two bit indicator of whether amplitude or phase have been
		 * used).  The `quality_factor' has no indication except that the
		 * contents `depend on the sensor'.  At a guess, this means that Reson
		 * systems will fill the quality_flags array, and others won't, and
		 * the `quality_factor' array is whatever the sonar manufacturer sends
		 * out with the datagrams. So, our translation scheme is as follows:
		 * 1. For Reson systems, as indicated by the file sub-record type, we
		 *    convert the `quality_flags' array per beam.
		 * 2. For Simrad systems, as indicated by the file sub-record type, we
		 *    convert the `quality_factor' array per beam according to the raw
		 *    Simrad telegram descriptions.
		 * 3. Otherwise, we assume that all detections are amplitude detect,
		 *    which means that we heavily over-estimate the errors in the outer
		 *    beams.  This includes the case where the quality sub-records were
		 *    not recorded with the rest of the data.  `You do what you can,
		 *    work for a living, 'cos that's who I am ...'
		 *
		 * **** NOTE **** NOTE **** NOTE **** NOTE **** NOTE **** NOTE **** NOTE
		 * The veracity of the following translations has not been checked.
		 * That is, at the time of writing, the only data available has come
		 * to us in GSF via CARIS/HIPS export for processing of the Portsmouth
		 * Harbour dataset.  Since this is being writen after processing (but
		 * without edit flags), lots of things are missing ... and this code
		 * is always bypassed.
		 *   To make sure that this receives due attention when this is not the
		 * case, we issue *really annoyingly copious* warning messages whenever
		 * this is activated.  These are *so* annoying that you just can't work
		 * through them, and you'll be in no doubt that something Very Bad (tm)
		 * is going on in the code.
		 *   Don't say you haven't been warned ...
		 */
		 
		/* Start by assuming amplitude detection without further evidence. */
		ping->beams[out_beam].flags &= ~SOUNDING_DETECT_MTHD_FLAG;
		if (filedev == NULL) {
			/* File didn't tell us what was in the data ... assume amplitude */
			ping->beams[out_beam].flags &= ~SOUNDING_DETECT_MTHD_FLAG;
		} else {
			switch(device_get_device(filedev)) {
				case DEVICE_SB8101:
					if (data.mb_ping.quality_factor != NULL) {
						/* SB8101 series have two bits: top bit => phase, bottom
						 * bit => amplitude.  Apparently, both can be set ...
						 * They are b[3,2], and are maintained in GSF when it is
						 * properly generated (i.e., currently by SAIC) in the
						 * quality_flags element (ref: e-mail Shannon Byrne,
						 * 13 Nov. 2001).
						 */
						if (((u32)(data.mb_ping.quality_factor[in_beam]) & 0x8) != 0)
							ping->beams[out_beam].flags |=
													SOUNDING_DETECT_MTHD_FLAG;
						else
							ping->beams[out_beam].flags &=
													~SOUNDING_DETECT_MTHD_FLAG;
#ifdef __DEBUG__
						error_msgv(modname, "info: beam %d flags 0x%x.\n",
							in_beam, (u32)(data.mb_ping.quality_factor[in_beam]));
#endif
					}
					break;
				case DEVICE_SB9001:
					/* SB9000 series are all amplitude detect anyway */
					error_msg(modname, "warning: using untested GSF quality"
						" indicator code for SB9000 series data!\n");
					break;
				case DEVICE_EM1000:
				case DEVICE_EM1002:
				case DEVICE_EM3000:
					if (data.mb_ping.quality_factor != NULL) {
						if ((((u32)data.mb_ping.quality_factor[in_beam])&0x80) != 0) {
							ping->beams[out_beam].flags |=
													SOUNDING_DETECT_MTHD_FLAG;
						} else {
							ping->beams[out_beam].flags &=
													~SOUNDING_DETECT_MTHD_FLAG;
						}
					}
#ifdef __DEBUG__
					error_msgv(modname, "info: beam %d, flags 0x%x.\n",
						in_beam, (u32)data.mb_ping.quality_factor[in_beam]);
#endif
					break;
				default:
					/* Sensor not known: assume amplitude detection */
					error_msg(modname, "warning: using untested GSF quality"
						" code for unknown data type!!\n");
					break;					
			}
		}
		ping->beams[out_beam].depth = depth; /* Errmod needs +ve depth; GSF has
											  * native +ve depths */

		++out_beam;
		++(ping->n_beams);
	}
	/* Since we can have dual-headed systems in GSF data (typically either
	 * 8101 or 8125 masquerading as 8101 until next release of the library)
	 * we have to do the error modelling here because we need to know about
	 * which head is being used, etc.  This is easier than trying to pass
	 * this information upwards.
	 */
	rtn = FILE_OK;
	for (out_beam = 0; out_beam < (s32)ping->n_beams; ++out_beam) {
		angle = DEG2RAD(
					device_beam_to_angle(stream->device,
										 orient->surf_sspeed,
										 ping->beams[out_beam].beam_number));
		ping->beams[out_beam].range = (f32)(ping->beams[out_beam].depth/cos(angle));

#ifdef __DEBUG__
error_msgv(modname, "debug: beam = %d sspeed = %f angle = %f\n",
	ping->beams[out_beam].beam_number, orient->surf_sspeed, angle);
#endif
	}
	if (data.mb_ping.sensor_id == GSF_SWATH_BATHY_SUBRECORD_SEABAT_8101_SPECIFIC
		&& (data.mb_ping.sensor_data.gsfSeaBat8101Specific.mode & GSF_8101_TWO_HEADS) != 0) {
		if ((data.mb_ping.sensor_data.gsfSeaBat8101Specific.mode & GSF_8101_STBD_HEAD) == 0) {
			if (!errmod_estimate(stream->port_errmod, orient, ping->beams, ping->n_beams)) {
				error_msg(modname, "failed computing error bounds for port head"
					" soundings.\n");
				rtn = FILE_INTERNAL;
			}
		} else {
			if (!errmod_estimate(stream->stbd_errmod, orient, ping->beams, ping->n_beams)) {
				error_msg(modname, "failed computing error bounds for stbd head"
					" soundings.\n");
				rtn = FILE_INTERNAL;
			}
		}
	} else {
		if (!errmod_estimate(stream->errormodel, orient, ping->beams, ping->n_beams)) {
			error_msg(modname, "failed computing error bounds for soundings.\n");
			rtn = FILE_INTERNAL;
		}
	}
	for (out_beam = 0; out_beam < (s32)ping->n_beams; ++out_beam)
		ping->beams[out_beam].depth = -ping->beams[out_beam].depth;
	
	if (filedev != NULL) device_release(filedev);
	stream->ping_num++;
	return(rtn);
}

/* Routine:	sounding_gsf_execute_params
 * Purpose:	Execute parameters for this sub-module
 * Inputs:	*list	ParList to parse parameters from
 * Outputs:	True if parameter list was executed correctly, otherwise False
 * Comment:	This looks only for the surface sound speed to set in input packets
 *			if there is not sufficient information in the packet to work it out.
 */

typedef enum {
	SND_GSF_UNKNOWN = 0,
	SND_GSF_SURF_SSPEED,
	SND_GSF_SELECT
} SndGsfParamEnum;

Bool sounding_gsf_execute_params(ParList *list)
{
	ParTable	tab[] = {
		{ "default_surf_sspeed", SND_GSF_SURF_SSPEED	},
		{ "select",				 SND_GSF_SELECT			},
		{ NULL,					 SND_GSF_UNKNOWN		}
	};
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;

	node = list;
	do {
		node = params_match(node, "sounding.gsf", tab, &id, &match);
		switch (id) {
			case SND_GSF_UNKNOWN:
				break;
			case SND_GSF_SURF_SSPEED:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_SURF_SSPEED ||
											dummy_float > MAX_SURF_SSPEED) {
					error_msgv(modname, "error: surface sound speed must be"
						" in the range [%.1f, %.1f] m/s (not %.1f).\n",
						MIN_SURF_SSPEED, MAX_SURF_SSPEED, dummy_float);
					return(False);
				}
				default_surf_sspeed = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default surface sound speed to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case SND_GSF_SELECT:
				if (strcmp(match->data, "p") == 0 ||
					strcmp(match->data, "port") == 0) {
					default_tdr = GSF_8101_TWO_HEADS;
				} else if (strcmp(match->data, "s") == 0 ||
						   strcmp(match->data, "stbd") == 0 ||
						   strcmp(match->data, "starboard") == 0) {
					default_tdr = GSF_8101_TWO_HEADS | GSF_8101_STBD_HEAD;
				} else if (strcmp(match->data, "a") == 0 ||
						   strcmp(match->data, "any") == 0) {
					default_tdr = 0;
				} else {
					error_msgv(modname, "error: head selection not known "
						"(needs port, stbd, or any, not \"%s\").\n",
						match->data);
					return(False);
				}
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default transducer to %d (%s).\n",
	default_tdr);
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
