/*
 * $Id: sounding_omg.c 16 2003-07-23 00:55:00Z brc $
 * $Log$
 * Revision 1.2  2003/07/23 00:55:00  brc
 * Merged modifications from development stream
 *
 * Revision 1.6.2.2  2003/04/16 18:19:05  brc
 * Fixed bug in error reporting when a tool is specified in a merged
 * file that doesn't exist in the internal list.
 *
 * Revision 1.1.1.1  2003/02/03 20:18:45  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.6.2.1  2002/07/14 02:20:37  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.6  2001/08/11 00:13:48  brc
 * Fixed bug with error reporting when file can't be found.
 *
 * Revision 1.5  2001/05/14 04:23:28  brc
 * Updated to make modules 'params'-aware.
 *
 * Revision 1.4  2001/04/10 23:28:50  brc
 * Added a check on the status flag so that the code now honours SwathEd flags
 * on input (i.e., beams marked as bad are removed).  This is ***DANGEROUS***
 * since it might mean than we re-process already cleaned data, but it also
 * means that we now reject any clipped outer beams recorded in RT, and any
 * beams marked bad by the Simrad software.  Use with caution.
 *
 * Revision 1.3  2001/02/10 19:10:19  brc
 * Modified code to crack out the components required for error modeling
 * (basically surface sound speed and ship speed over ground).  However,
 * found that these are not properly managed in OMG/HDCS files, even if the
 * newer V3 version is used and the files are not compressed.  Therefore the
 * code patches around the problem with default values of 1536.0 m/s and 3.0 m/s
 * respectively.
 *
 * Revision 1.2  2000/09/25 20:19:05  brc
 * Major modification to raw file reading structure, and corresponding changes
 * to the remainder of the library to deal with the changes that this caused
 * at the sounding stream level.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	sounding_omg.c
 * Purpose:	Read soundings from OMG/HDCS file and format for use
 * Date:	14 July 2000
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
#include <ctype.h>
#include "stdtypes.h"
#include "error.h"
#include "OMG_HDCS_jversion.h"
#include "sounding.h"
#include "sounding_private.h"
#include "sounding_omg.h"
#include "params.h"

#undef __DEBUG__

static char *modname = "sounding_omg";
static char *modrev = "$Revision: 16 $";

#define MIN_SND_SPEED		1000.0	/* Min sound speed, m/s */
#define MAX_SND_SPEED		2000.0	/* Max sound speed, m/s */
#define MIN_VESSEL_SPEED	   0.1	/* Min ship speed, m/s (0.2 kt) */
#define MAX_VESSEL_SPEED      15.0	/* Max ship speed, m/s (29.2 kt) */

#define DEFAULT_MEAN_SPEED	1500.0	/* Default mean sound speed, m/s */
#define DEFAULT_SURF_SSPEED	1500.0	/* Default surface sound speed, m/s */
#define DEFAULT_VESSEL_SPEED   3.0	/* Default vessel speed, m/s */
#define DEFAULT_HONOUR_FLAGS  True	/* Flag: honour edit flags in OMG */

static f32 default_mean_speed = DEFAULT_MEAN_SPEED;
static f32 default_surf_sspeed = DEFAULT_SURF_SSPEED;
static f32 default_vessel_speed = DEFAULT_VESSEL_SPEED;
static Bool honour_flags = DEFAULT_HONOUR_FLAGS;

#define INT2GEO(x)	((x)/1.0E+7)		/* Convert integer scaled radians to
										 * linear scale as double */
#define RAD2DEG(x)	((x)*180.0/M_PI)	/* Convert radians to degrees */
#define MM2M(x)		((x)/1000.0)		/* Convert millimeters to meters */
#define DM2M(x)		((x)/10.0)			/* Convert decimeters to meters */

typedef struct {
	OMG_HDCS_summary_header	file_hdr;			/* File header summary */
	f64					min_lat, min_lon,	/* Location bounding box */
						max_lat, max_lon;
	u32					buf_sz;				/* Buffer size count */
	OMG_HDCS_beam		*buffer;			/* Buffer for beam conversion */
} OBuffer;

void sounding_omg_release(void *buffer)
{
	OBuffer	*buf = (OBuffer*)buffer;
	
	if (buf != NULL) {
		free(buf->buffer);
		free(buf);
	}
}

Bool sounding_omg_check_hdr(SoundingStream stream)
{
	OBuffer	*store;
	u32		maxbeams, target;
	Device	dev;
	f64		ref_lat, ref_lon;
	
	/* Grab memory for the OBuffer structure (needed to read file summary) */
	if ((store = (OBuffer*)calloc(1, sizeof(OBuffer))) == NULL) {
		error_msg(modname, "failed allocation private workspace.\n");
		return(False);
	}
	
	/* Read file header and check.  First four elements should be 'HDCS' */
	OMG_HDCS_read_summary(stream->fd, &(store->file_hdr));
	if (strncmp(store->file_hdr.fileID, OMG_HDCS_fileID_tag, 4) != 0) {
		error_msgv(modname, "file is not OMG/HDCS format (bad magic number "
			"\"%4s\" != \"%s\").\n", store->file_hdr.fileID,
			OMG_HDCS_fileID_tag);
		sounding_omg_release(store);
		return(False);
	}

	/* Check that the tool_type can be represented internally and make device */
	if ((dev = device_new_from_omg_tool(store->file_hdr.toolType)) == NULL) {
		error_msgv(modname, "tool type %d cannot be represented internally.\n", store->file_hdr.toolType);
		sounding_omg_release(store);
		return(False);
	}
	
	/* Now compute some lattitude/longitude bounds for the beams solutions */
	ref_lat = RAD2DEG(INT2GEO(store->file_hdr.refLat));
	ref_lon = RAD2DEG(INT2GEO(store->file_hdr.refLong));

	store->min_lat = ref_lat + RAD2DEG(INT2GEO(store->file_hdr.minLat));
	store->max_lat = ref_lat + RAD2DEG(INT2GEO(store->file_hdr.maxLat));
	store->min_lon = ref_lon + RAD2DEG(INT2GEO(store->file_hdr.minLong));
	store->max_lon = ref_lon + RAD2DEG(INT2GEO(store->file_hdr.maxLong));
	
	/* Finally, allocate an array given maximum number of beams possible */
	maxbeams = device_get_nbeams(dev);
	target = maxbeams*sizeof(OMG_HDCS_beam);
	if ((store->buffer = (OMG_HDCS_beam*)malloc(target)) == NULL) {
		error_msgv(modname, "failed to allocate conversion space for %d beams"
			" (tool = \"%s\").\n", maxbeams, device_get_name(dev));
		sounding_omg_release(store);
		return(False);
	}
	
	/* Patch private workspace into stream and return */
	stream->private = store;
	stream->release_func = sounding_omg_release;
	stream->device = dev;
	stream->data_avail = SOUNDING_BATHY;
	return(True);
}

FileError sounding_omg_get_next(SoundingStream stream, Platform *orient,
								Ping ping)
{
	s32		in_beam, out_beam, n_beams;
	OBuffer	*store = (OBuffer*)(stream->private);
	OMG_HDCS_profile_header	profile;
	f64		lon, lat;
	f32		depth;
	
	if (store == NULL) {
		error_msg(modname, "private workspace is not initialised.\n");
		return(FILE_NOTINIT);
	}
	
	if (stream->ping_num+1 >= store->file_hdr.numProfiles) return(FILE_EOF);
	
	/* Read profile header (i.e., ping summary header), and beam solutions */
	OMG_HDCS_read_profile(stream->fd, stream->ping_num+1, &profile);
	OMG_HDCS_get_raw_beams(stream->fd, stream->ping_num+1, &n_beams, store->buffer);
	
	/* Check that we've got enough space to do the conversion */
	if (ping->buffer_space < (u32)n_beams) {
		error_msgv(modname, "ping has %d beams; buffer is only %d long.\n",
			n_beams, ping->buffer_space);
		stream->ping_num++;	/* Otherwise, we'll keep reading this ping ... */
		return(FILE_INVALID);
	}
	
	locate_transducer(&profile, &(store->file_hdr),
						&(orient->latitude), &(orient->longitude));
	orient->roll = (f32)RAD2DEG(INT2GEO(profile.vesselRoll));
	orient->pitch = (f32)RAD2DEG(INT2GEO(profile.vesselPitch));
	orient->heading = (f32)RAD2DEG(INT2GEO(profile.vesselHeading));
	orient->heave = (f32)MM2M(profile.vesselHeave);
	
	/* These next three are not properly reported in the OMG/HDCS files, and
	 * hence we need to patch around the problem with reasonable values
	 */
	orient->mean_speed = orient->surf_sspeed = (f32)DM2M(profile.td_sound_speed);
	orient->mean_speed = default_mean_speed;
	orient->surf_sspeed = default_surf_sspeed;
	
	orient->vessel_speed = (f32)MM2M(profile.vesselVelocity);
	orient->vessel_speed = default_vessel_speed;

/*
error_msgv(modname, "debug: soundVelocity = %d vesselVelocity = %d td_sound_speed = %hd\n",
	profile.soundVelocity, profile.vesselVelocity, profile.td_sound_speed);
*/

	ping->n_beams = 0;
	for (in_beam = 0, out_beam = 0; in_beam < n_beams; ++in_beam) {
		depth = (f32)MM2M(-store->buffer[in_beam].observedDepth *
					 store->file_hdr.positionScale);
		if (depth > -0.5) continue; /* RT indicates bad beams with depth 0.0 */
		if (honour_flags && store->buffer[in_beam].status != 0) continue;
			/* Honour SwathEd flags */
		locate_beam(store->buffer+in_beam, &profile, &(store->file_hdr),
					&lat, &lon);
		ping->beams[out_beam].flags = 0;	/* Initialise flags on each pass */
		ping->beams[out_beam].beam_number = in_beam;
		ping->beams[out_beam].backscatter = store->buffer[in_beam].reflectivity;
		ping->beams[out_beam].file_id = ping->file_id;
		ping->beams[out_beam].flags |=
				SOUNDING_PUTWINDOWSZ(store->buffer[in_beam].no_samples);


		if ((((u32)store->buffer[in_beam].Q_factor)&0x80) != 0) {
			ping->beams[out_beam].flags |= SOUNDING_DETECT_MTHD_FLAG;
		} else {
			ping->beams[out_beam].flags &= ~SOUNDING_DETECT_MTHD_FLAG;
		}
		projection_ll_to_en_deg(stream->proj, lon, lat,
							&(ping->beams[out_beam].east),
							&(ping->beams[out_beam].north));
		ping->beams[out_beam].depth = -depth /* Need to have +ve for errmod */;
		++out_beam;
		++(ping->n_beams);
	}
	stream->ping_num++;
	return(FILE_OK);
}

/* Routine:	sounding_omg_execute_params
 * Purpose:	Execute parameters for this sub-module
 * Inputs:	*list	ParList of parameters to work down.
 * Outputs:	True if the list of parameters was read correctly, otherwise False
 * Comment:	This looks for default surface speed, default mean column speed,
 *			default vessel speed (none of which are correctly maintained in the
 *			OMG/HDCS files) and a flag to indicate whether to honour OMG/HDCS
 *			edit flags or not.
 */

typedef enum {
	SND_OMG_UNKNOWN = 0,
	SND_OMG_MEAN_SPEED,
	SND_OMG_SURF_SSPEED,
	SND_OMG_VESSEL_SPEED,
	SND_OMG_HONOUR_FLAGS
} SndOmgParamEnum;

Bool sounding_omg_execute_params(ParList *list)
{
	ParTable	tab[] = {
		{ "default_surf_sspeed",	SND_OMG_SURF_SSPEED	},
		{ "default_mean_speed",		SND_OMG_MEAN_SPEED	},
		{ "default_vessel_speed",	SND_OMG_VESSEL_SPEED},
		{ "honour_flags",			SND_OMG_HONOUR_FLAGS},
		{ NULL,						SND_OMG_UNKNOWN		}
	};
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;
	char	*dummy_string, *src, *dst;

	node = list;
	do {
		node = params_match(node, "sounding.omg", tab, &id, &match);
		switch (id) {
			case SND_OMG_UNKNOWN:
				break;
			case SND_OMG_SURF_SSPEED:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_SND_SPEED ||
											dummy_float > MAX_SND_SPEED) {
					error_msgv(modname, "error: surface sound speed must be"
						" in the range [%.1f, %.1f] m/s (not %.1f).\n",
						MIN_SND_SPEED, MAX_SND_SPEED, dummy_float);
					return(False);
				}
				default_surf_sspeed = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default surface sound speed to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case SND_OMG_MEAN_SPEED:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_SND_SPEED ||
											dummy_float > MAX_SND_SPEED) {
					error_msgv(modname, "error: mean sound speed must be"
						" in the range [%.1f, %.1f] m/s (not %.1f).\n",
						MIN_SND_SPEED, MAX_SND_SPEED, dummy_float);
					return(False);
				}
				default_mean_speed = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default surface sound speed to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;				
			case SND_OMG_VESSEL_SPEED:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_VESSEL_SPEED ||
											dummy_float > MAX_VESSEL_SPEED) {
					error_msgv(modname, "error: vessel speed must be"
						" in the range [%.1f, %.1f] m/s (not %.1f).\n",
						MIN_VESSEL_SPEED, MAX_VESSEL_SPEED, dummy_float);
					return(False);
				}
				default_vessel_speed = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting default vessel speed to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case SND_OMG_HONOUR_FLAGS:
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
					return(False);
				}
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting OMG flags to %s.\n", honour_flags == True ?
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
