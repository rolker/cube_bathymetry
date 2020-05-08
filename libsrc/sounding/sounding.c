/*
 * $Id: sounding.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.18.2.3  2003/01/21 23:02:11  brc
 * Fixed (finally) the bug that causes the stream to crash if a sounding_release() is
 * called before the stream is properly initialised.
 *
 * Revision 1.18.2.2  2002/12/15 01:45:23  brc
 * Added code to allow the sounding stream to properly accomodate dual-head
 * systems where there are significantly different specifications for the vessel
 * structures of each head (e.g., split head systems).  This frees us from calling
 * the vessel module for a default specification and hoping that it is correct, since
 * the user can now provide the vessel specifications directly.
 *
 * Revision 1.18.2.1  2002/07/14 02:20:37  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.18  2002/06/16 02:36:08  brc
 * Turned off debugging flags.
 *
 * Revision 1.17  2002/03/14 04:17:27  brc
 * Modifications to allow construction of dual-head vessel descriptions and
 * associated error models.  Modifications to allow CARIS/HIPS(HDCS) data
 * module to be used for input.
 *
 * Revision 1.16  2001/12/07 21:16:51  brc
 * Added sounding_new_from() mashaller to allow the user to specify just a
 * type of file, rather than having to work out which new call to use.
 *
 * Revision 1.15  2001/09/23 19:08:14  brc
 * Added code to re-incorporte the SOUNDING_NATIVE method as a useful entity!
 * This now does a basic dump of all of the components that are in memory for
 * a single ping, which allows the code to dump a summary of everything required
 * for integrating data in one file (including error assignments).  This was
 * intended to support large-area survey where the same data file may have to
 * be offered to multiple mapsheets, and it would be inefficient to read and
 * translate a raw datagram stream each time (it might even be impossible).
 *
 * Revision 1.14  2001/08/21 00:38:44  brc
 * Removed restriction that the along and across track errors are both zero, so
 * that systems which don't resolve along-track values are accomodated.
 *
 * Revision 1.13  2001/08/20 22:37:03  brc
 * Fixed bug in sounding_release() which caused the Device entry to be
 * released with the rest of the sounding stream, even though it isn't
 * always created by the sounding stream code itself.  This means that the
 * caller who provided the information would loose the reference to the
 * data unexpectedly, leading to heap problems.
 *
 * Revision 1.12  2001/07/23 17:57:27  brc
 * Fixed bug in opening file for binary output caused by left-over code from
 * when this was implemented using open() rather than fopen().
 *
 * Revision 1.11  2001/06/03 01:10:07  brc
 * Added interface to allow external callers to get at the point location
 * code (geodetic arc construction).
 *
 * Revision 1.10  2001/05/14 04:23:28  brc
 * Updated to make modules 'params'-aware.
 *
 * Revision 1.9  2001/04/10 23:25:58  brc
 * Added links for GSF format raw files.  Added a depth gate and angle gate
 * facility and interface functions.  Added beam location code previously in
 * read_simrad.c as an internal function only available to this module.
 *
 * Revision 1.8  2001/02/26 03:17:23  brc
 * Made include of ieeefp.h conditional on compilation architecture so that it is
 * only used when needed (the routines required, isnan() and finite() are in the
 * standard math.h under Linux, for example).
 *
 * Revision 1.7  2001/02/11 17:56:14  brc
 * Added facility to gather imagery data into the input stream.  This means that
 * raw streams with imagery components now honour the SOUNDING_IMAGERY component.
 * Currently, only the Simrad reader is defined directly, and as a function of
 * this, imagery packets don't get geo-referenced until they are matched with a
 * suitable bathy packet.  The code in read_simrad.c/simrad_translate_x() makes
 * sure that (a) space is set aside for this to happen, and (b) that whenever a
 * bathy packet is read, the imagery packet is tagged (if it exists) and
 * whenever an imagery packet is read, the beams are tagged if the associated
 * bathy packet exists.  In practice, you need to request and read both
 * SOUNDING_IMAGERY and SOUNDING_BATHY packets if this is going to work, and
 * read the bathy stream every time you read an imagery packet, either until
 * you hit a match, or run out of bathy.  See utilities/dumpimagery.c for
 * an example of this in action.
 *
 * Revision 1.6  2001/02/10 19:02:43  brc
 * Added full error modeling code.  Updated startup sequence for each of the
 * alternative readers so that they expect a vessel description and build an
 * error model to go with it.  Also added code to sounding_get_next() to build
 * error estimates from the raw soundings passed up from the lower-level readers
 * and then invert the depths so that they are -ve down as expected by the
 * mapsheet integration code (need to +ve down for error estimation).
 *
 * Revision 1.5  2000/12/03 20:51:38  brc
 * Removed release of the SoundingStream's copy of the Projection from the
 * target mapsheet when the stream is released.  This caused the projection
 * memory to be removed, resulting in some wierd effects when it was used
 * elsewhere!  In effect, the SoundingStream only has a copy of the projection
 * so that it can refer the data into the mapsheet's coordinate system, and
 * it might be a better idea to have the soundings only reported in LL rather
 * than whatever the mapsheet is trying to use (since soundings may be going
 * to more than one destination, and we don't want to necessarily have them
 * all the same).
 *
 * Revision 1.4  2000/09/25 20:19:05  brc
 * Major modification to raw file reading structure, and corresponding changes
 * to the remainder of the library to deal with the changes that this caused
 * at the sounding stream level.
 *
 * Revision 1.3  2000/09/07 21:13:43  brc
 * Removed .install*: mistake in original import.
 *
 * Revision 1.2  2000/08/28 16:51:53  brc
 * Interim check-in to allow collaborative test of a coredumper under Linux.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	sounding.c
 * Purpose:	Routines to map external data into internal format
 * Date:	8 July 2000
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
#include <limits.h>

#ifdef sgi				/* This is only needed on IRIX since under Linux, these
						 * routines are defined in math.h and are included into
						 * the namespace by default.
						 */
#include <ieeefp.h>		/* Debug: needed for isnan()/finite() */
#endif

#include "stdtypes.h"
#include "error.h"
#include "sounding.h"
#include "sounding_private.h"
#include "sounding_native.h"
#include "sounding_plain.h"
#include "sounding_omg.h"
#include "sounding_raw.h"
#include "sounding_gsf.h"
#include "sounding_hips.h"
#include "gsf.h"
#include "projection.h"
#include "device.h"
#include "vessel.h"
#include "errmod.h"
#include "params.h"

static char *modname = "sounding";
static char *modrev = "$Revision: 2 $";

#undef __DEBUG__

#define MIN_SURF_SSPEED		1000.0
#define MAX_SURF_SSPEED		2000.0
#define DEFAULT_SURF_SSPEED	1500.0
	/* This is used for plain files where the information is not available in
	 * order to allow the error modelling to operate.
	 */

static f32 default_surf_sspeed = DEFAULT_SURF_SSPEED;

#define DEG2RAD(x) ((x)*M_PI/180.0)
#define RAD2DEG(x) ((x)*180.0/M_PI)

static void sounding_release_ping(Ping r)
{
	u32		beam;
	
	free(r->beams);
	if (r->ibeams != NULL) {
		for (beam = 0; beam < r->n_beams; ++beam)
			free(r->ibeams[beam].data);
		free(r->ibeams);
	}
	free(r);
}

void sounding_release(SoundingStream stream)
{
	if (stream == NULL) return;
	if (stream->fd != NULL) fclose(stream->fd);
	if (stream->current != NULL) sounding_release_ping(stream->current);
	if (stream->release_func != NULL)
		(*(stream->release_func))(stream->private);
	if (stream->port_errmod == stream->stbd_errmod)
		if (stream->errormodel != NULL) errmod_release(stream->errormodel);
	else {
		if (stream->port_errmod != NULL) errmod_release(stream->port_errmod);
		if (stream->stbd_errmod != NULL) errmod_release(stream->stbd_errmod);
	}
	if (stream->stbd_vsl == stream->port_vsl)
		if (stream->port_vsl != NULL) vessel_release(stream->port_vsl);
	else {
		if (stream->port_vsl != NULL) vessel_release(stream->port_vsl);
		if (stream->stbd_vsl != NULL) vessel_release(stream->stbd_vsl);
	}
	free(stream);
}

static Ping sounding_new_ping(u32 maxbeams)
{
	Ping	rtn;
	
	if ((rtn = (Ping)calloc(1, sizeof(sPing))) == NULL) return(NULL);
	if ((rtn->beams = (Sounding*)malloc(sizeof(Sounding)*maxbeams)) == NULL) {
		error_msg(modname, "failed allocating beam buffer.\n");
		sounding_release_ping(rtn);
		return(NULL);
	}
	if ((rtn->ibeams = (Imagery*)calloc(maxbeams, sizeof(Imagery))) == NULL) {
		error_msgv(modname, "failed allocating %d imagery beam buffer.\n",
			maxbeams);
		sounding_release_ping(rtn);
		return(NULL);
	}
	rtn->buffer_space = maxbeams;
	rtn->n_beams = 0;
	rtn->n_ibeams = 0;

#ifdef __DEBUG__
	printf("ping::allocate: %d beams in buffer.\n", maxbeams);
#endif

	return(rtn);
}

SoundingStream sounding_new(Projection proj)
{
	Header	rtn;
	
	if ((rtn = (Header)calloc(1, sizeof(Hdr))) == NULL) return(NULL);
	rtn->proj = proj;
	rtn->ping_num = -1;
	rtn->data_avail = 0;
	
	/* Set depth and angle gates to maximum allowable */
	rtn->min_depth = FLT_MAX;
	rtn->max_depth = -FLT_MAX;
	rtn->min_angle = -(f32)(M_PI/2.0);
	rtn->max_angle = (f32)(M_PI/2.0);
	
	return(rtn);
}

static SoundingStream sounding_new_from_file(const char *name, Projection proj)
{
	SoundingStream rtn;
	
	if ((rtn = sounding_new(proj)) == NULL) return(NULL);
	if ((rtn->fd = fopen(name, "rb")) == NULL) {
		error_msgv(modname, "couldn't open \"%s\" for reading.\n", name);
		sounding_release(rtn);
		return(NULL);
	}
	return(rtn);
}

/* Routine:	sounding_construct_errormodel
 * Purpose:	Generate a single, or if device is dual-headed, dual error models
 * Inputs:	device	Device to construct the error model for
 *			mthd	Error model to use
 *			stream	SoundingStream module to set up with error models
 * Outputs:	Returns True if sucessful, otherwise False.
 * Comment:	Note that this does not check that the _stream_ error models are
 *			not currently being used before reseting the pointers, and hence
 *			may (under some circumstances) be a potential memory leak.
 */

static Bool sounding_construct_errormodel(Device device, ErrMthd mthd,
										  SoundingStream stream)
{
	if (vessel_get_config() == VESSEL_DUAL_HEAD) {
#ifdef __DEBUG__
	error_msg(modname, "debug: configuring dual vessel error models.\n");
#endif
		if ((stream->port_vsl = vessel_get_head(VESSEL_HEAD_PORT)) == NULL) {
			error_msg(modname, "error: failed to get port head definition"
				" for dual headed vessel configuration.\n");
			return(False);
		}
		if ((stream->port_errmod =
						errmod_new(device, stream->port_vsl, mthd)) == NULL) {
			error_msg(modname, "error: failed to construct port error model"
				" for dual headed vessel configuration.\n");
			vessel_release(stream->port_vsl);
			stream->port_vsl = NULL;
			return(False);
		}
		stream->errormodel = stream->port_errmod;
			/* NB: this means that we use the port head error model for any
			 * systems that don't know about the dual-headed modifications.
			 */
		if ((stream->stbd_vsl = vessel_get_head(VESSEL_HEAD_STBD)) == NULL) {
			error_msg(modname, "error: failed to get starboard head definition"
				" for dual headed vessel configuration.\n");
			vessel_release(stream->port_vsl);
			stream->port_vsl = NULL;
			errmod_release(stream->port_errmod);
			stream->port_errmod = stream->errormodel = NULL;
			return(False);
		}
		if ((stream->stbd_errmod =
						errmod_new(device, stream->stbd_vsl, mthd)) == NULL) {
			error_msg(modname, "error: failed to construct starboard error"
				" model for dual headed vessel configuration.\n");
			vessel_release(stream->stbd_vsl);
			vessel_release(stream->port_vsl);
			stream->stbd_vsl = NULL; stream->port_vsl = NULL;
			errmod_release(stream->port_errmod);
			stream->port_errmod = stream->errormodel = NULL;
			return(False);
		}
	} else {
#ifdef __DEBUG__
	error_msg(modname, "debug: configuring single vessel error model.\n");
#endif
		/* Either VESSEL_DEFAULT == not defined, or VESSEL_SINGLE_HEAD */
		if ((device_get_properties(device) & DEVICE_IS_SPLITHEAD) != 0) {
			error_msgv(modname, "warning: device \"%s\" has separated sonar"
				" elements, and is modelled as a dual-headed device.\n",
				device_get_name(device));
			error_msg(modname, "warning: You should have a dual-headed vessel"
				" description to allow specification of alignment offsets"
				" for each and thus correct error modelling.\n");
		}
		if ((stream->port_vsl = vessel_get_head(VESSEL_HEAD_ANY)) == NULL) {
			error_msg(modname, "error: failed to construct default head"
				" description.\n");
			return(False);
		}
		if ((stream->errormodel =
						errmod_new(device, stream->port_vsl, mthd)) == NULL) {
			error_msg(modname, "error: failed to construct error model for"
				" single headed vessel configuration.\n");
			vessel_release(stream->port_vsl);
			stream->port_vsl = NULL;
			return(False);
		}
		stream->port_errmod = stream->stbd_errmod = stream->errormodel;
			/* So that dual-headed capable code will use the same model for
			 * any system which incorrectly claims to be dual-headed data.
			 */
		stream->stbd_vsl = stream->port_vsl;	/* Just in case (TM) */
	}
	return(True);
}

/* Routine:	sounding_construct_errmodel_specific
 * Purpose:	Set up one or more error models according to the vessel
 *			descriptions passed
 * Inputs:	device	Device doing the generation
 *			mthd	Error attribution method to use
 *			v_port	Port head Vessel description (or NULL)
 *			v_stbd	Stbd head Vessel description (or NULL)
 * Outputs:	stream	SoundingStream in which to write the Vessels and
 *					ErrMod descriptions
 * Comment: This routine is designed to be a plug-in replacement for
 *			sounding_construct_errormodel() above in that it can do the same
 *			function (if v_port = v_stbd = NULL), but it has the extra
 *			functionality that if the Vessel descriptions are defined,
 *			they are used to set up the stream, rather than using whatever
 *			happens to be defined at the time in the Vessel module's
 *			parameters structure.
 *				For single head systems, use v_port, and set v_stbd = NULL;
 *			for dual-head or split-head systems, set both values.  To set
 *			up with default values, set both NULL.  It is an error to have
 *			v_port = NULL and v_stbd != NULL.
 */

static Bool sounding_construct_errormodel_specific(
				Device device, ErrMthd mthd, Vessel v_port, Vessel v_stbd,
				SoundingStream stream)
{
	Bool	dual_head = False;

	/* Set up the Vessel descriptions pair before doing anything else */
	if (v_port == NULL && v_stbd != NULL) {
		error_msgv(modname, "error: can't configure an error model when port"
			" head is not defined but starboard is!\n");
		return(False);
	} else if (v_port == NULL && v_stbd == NULL) {
		stream->port_vsl = stream->stbd_vsl = NULL;
		/* Configure with default vessel description that currently exists */
		if (vessel_get_config() == VESSEL_DUAL_HEAD) {
			dual_head = True;
			if ((stream->port_vsl = vessel_get_head(VESSEL_HEAD_PORT)) == NULL) {
				error_msg(modname, "error: failed to get port head definition"
					" for dual headed vessel configuration.\n");
			}
			if ((stream->stbd_vsl = vessel_get_head(VESSEL_HEAD_STBD)) == NULL) {
				error_msg(modname, "error: failed to get starboard head definition"
					" for dual headed vessel configuration.\n");
			}
		} else {
			/* Default single head mode --- check the device to make sure it's not
			 * a split-head for which the user has cocked-up the description.
			 */
			if ((device_get_properties(device) & DEVICE_IS_SPLITHEAD) != 0) {
				error_msgv(modname, "warning: device \"%s\" has separated sonar"
					" elements, and is modelled as a dual-headed device.\n",
					device_get_name(device));
				error_msg(modname, "warning: You should have a dual-headed vessel"
					" description to allow specification of alignment offsets"
					" for each and thus correct error modelling.\n");
			}
			if ((stream->port_vsl = vessel_get_head(VESSEL_HEAD_ANY)) == NULL) {
				error_msg(modname, "error: failed to construct default head"
					" description.\n");
			}
		}
	} else {
		/* Configure with description in v_port and (possibly) v_stbd */
		if ((stream->port_vsl = vessel_default()) == NULL ||
			(v_stbd != NULL && (stream->stbd_vsl = vessel_default()) == NULL)) {
			error_msgv(modname, "error: failed to get buffer space for error model"
				" vessel descriptions.\n");
			return(False);
		}
		memcpy(stream->port_vsl, v_port, sizeof(struct _vessel));
		if (v_stbd != NULL) {
			memcpy(stream->stbd_vsl, v_stbd, sizeof(struct _vessel));
			dual_head = True;
		}
	}

	/* Check that enough built for us to continue */
	if (stream->port_vsl == NULL || (dual_head && stream->stbd_vsl == NULL)) {
		error_msgv(modname, "error: build of Vessel descriptions for error model"
			" setup failed.\n");
		if (stream->port_vsl != NULL) vessel_release(stream->port_vsl);
		if (stream->stbd_vsl != NULL) vessel_release(stream->stbd_vsl);
		stream->port_vsl = stream->stbd_vsl = NULL;
		return(False);
	}

	/* Next, build error models if possible, and for each Vessel that exists */
	if (stream->port_vsl != NULL &&
		(stream->port_errmod = errmod_new(device, stream->port_vsl, mthd)) == NULL)
		error_msgv(modname, "error: failed generating the error model for the port"
			" (default) head.\n");
	if (stream->stbd_vsl != NULL &&
		(stream->stbd_errmod = errmod_new(device, stream->stbd_vsl, mthd)) == NULL)
		error_msgv(modname, "error: failed generating the error model for the stbd"
			" (aux) head.\n");

	/* Check build of the error models */
	if (stream->port_errmod == NULL || (dual_head && stream->stbd_errmod == NULL)) {
		error_msg(modname, "error: failed generating the error model for SoundingStream.\n");
		if (stream->port_vsl != NULL) vessel_release(stream->port_vsl);
		if (stream->stbd_vsl != NULL) vessel_release(stream->stbd_vsl);
		stream->port_vsl = stream->stbd_vsl = NULL;
		if (stream->port_errmod != NULL) errmod_release(stream->port_errmod);
		if (stream->stbd_errmod != NULL) errmod_release(stream->stbd_errmod);
		stream->errormodel = stream->port_errmod = stream->stbd_errmod = NULL;
		return(False);
	}
	if (stream->stbd_vsl == NULL) stream->stbd_vsl = stream->port_vsl;
	if (stream->stbd_errmod == NULL) stream->stbd_errmod = stream->port_errmod;
	stream->errormodel = stream->port_errmod;	/* Default for single-head calls on dual-head systems */

	return(True);
}

/* Routine:	sounding_new_from_plain
 * Purpose:	Initialise a stream for a JBOS file.
 * Inputs:	*name		Name to read from
 *			maxbeams	Number of beams to read at a time
 *			proj		Projection to use to convert to mapsheet space
 *			device		Generator used in constructing the data
 *			vessel		Vessel configuration for data platform
 *			mthd		Error attribution method to use
 * Outputs:	Pointer to stream, or NULL on failure
 * Comment:	This is about as basic as it gets --- a stream with no header,
 *			containing data about which we know nothing.  This should *only*
 *			be used as a last resort!  You need to specify the device that was
 *			used, and the vessel configuration ... and if you get it wrong,
 *			expect all sorts of bad things to happen (including, but not
 *			limited to core dumps as the beam_numbers in the file don't match
 *			what the device_*() code expects, if they even exist).  You would
 *			be well advised to use ERRMTHD_IHO for this sort of data, unless
 *			you know that the data has valid beam numbers, etc.
 */

static SoundingStream sounding_new_plain(const char *name, u32 maxbeams,
										 Projection proj, Device device)
{
	SoundingStream rtn;
	
	if ((rtn = sounding_new_from_file(name, proj)) == NULL) {
		error_msg(modname, "error: failed initialising stream.\n");
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_PLAIN;
	/* We can create the current ping before checking header, since a plain
	 * file doesn't have any informaion about generator or sizes --- this is
	 * given explicitly
	 */
	if ((rtn->current = sounding_new_ping(maxbeams)) == NULL) {
		error_msg(modname, "error: failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}
	if (!sounding_plain_check_hdr(rtn)) {
		error_msg(modname, "error: failed header check.\n");
		sounding_release(rtn);
		return(NULL);
	}
	rtn->device = device;

	return(rtn);
}

SoundingStream sounding_new_from_plain(const char *name, u32 maxbeams,
									   Projection proj, Device device,
									   Vessel vessel, ErrMthd mthd)
{
	SoundingStream rtn;
	
	if ((rtn = sounding_new_from_file(name, proj)) == NULL) {
		error_msg(modname, "error: failed initialising stream.\n");
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_PLAIN;
	/* We can create the current ping before checking header, since a plain
	 * file doesn't have any informaion about generator or sizes --- this is
	 * given explicitly
	 */
	if ((rtn->current = sounding_new_ping(maxbeams)) == NULL) {
		error_msg(modname, "error: failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}
	if (!sounding_plain_check_hdr(rtn)) {
		error_msg(modname, "error: failed header check.\n");
		sounding_release(rtn);
		return(NULL);
	}
	rtn->device = device;
	if (!sounding_construct_errormodel(device, mthd, rtn)) {
		error_msg(modname, "error: failed generating error attribution model.\n");
		sounding_release(rtn);
		return(NULL);
	}
	return(rtn);
}

/* Routine:	sounding_new_from_native
 * Purpose:	Open a stream in native format
 * Inputs:	*name	Name of the file to open
 *			proj	Projection to convert into mapsheet space
 *			mthd	Method to use in generating error attributes
 * Outputs:	Pointer to SoundingStream, or NULL on failure
 * Comment:	This routine assumes that the Vessel configuration was saved when
 *			the file was constructed, and hence that there is no need to set
 *			a new profile.  The error model is constructed based on the device
 *			and vessel configuration status stored in the file.
 */

static SoundingStream sounding_new_native(const char *name, Projection proj)
{
	SoundingStream	rtn;
	u32				maxbeams;
	Vessel			vessel;
	
	if ((rtn = sounding_new_from_file(name, proj)) == NULL) {
		error_msg(modname, "error: failed initialising stream.\n");
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_NATIVE;
	if (!sounding_native_check_hdr(rtn)) {
		error_msg(modname, "error: failed header check.\n");
		sounding_release(rtn);
		return(NULL);
	}
	maxbeams = device_get_nbeams(rtn->device);
	if ((rtn->current = sounding_new_ping(maxbeams)) == NULL) {
		error_msg(modname, "error: failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}
	/* Get and then discard the vessel information --- it isn't important
	 * for use here, since the error attribution was done when the data
	 * was translated into native format.
	 */
	if ((vessel = sounding_native_get_vessel(rtn->private)) == NULL) {
		error_msg(modname, "error: failed getting vessel info from stream.\n");
		sounding_release(rtn);
		return(NULL);
	}
	vessel_release(vessel);

	return(rtn);
}

SoundingStream sounding_new_from_native(const char *name, Projection proj,
										ErrMthd mthd)
{
	SoundingStream	rtn;
	u32				maxbeams;
	Vessel			vessel;
	
	if ((rtn = sounding_new_from_file(name, proj)) == NULL) {
		error_msg(modname, "failed initialising stream.\n");
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_NATIVE;
	if (!sounding_native_check_hdr(rtn)) {
		error_msg(modname, "failed header check.\n");
		sounding_release(rtn);
		return(NULL);
	}
	maxbeams = device_get_nbeams(rtn->device);
	if ((rtn->current = sounding_new_ping(maxbeams)) == NULL) {
		error_msg(modname, "failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}
	/* Attempt to configure the error model */
	if ((vessel = sounding_native_get_vessel(rtn->private)) == NULL) {
		error_msg(modname, "failed getting vessel info from stream.\n");
		sounding_release(rtn);
		return(NULL);
	}
	/* Attempt to construct an error model using specified method */
	if (!sounding_construct_errormodel(rtn->device, mthd, rtn)) {
		error_msg(modname, "failed generating error attribution model.\n");
		sounding_release(rtn);
		return(NULL);
	}	
	return(rtn);	
}

/* Routine:	sounding_new_from_omg
 * Purpose:	Opens a .merged OMG/HDCS file and initialises stream parameters
 * Inputs:	*name	Name of file to open
 *			proj	Projection parameters to use for conversion to mapsheet
 *			vessel	Vessel configuration for the platform in use
 *			mthd	Error modeling method to use in attributing accuracies
 * Outputs:	Returns pointer to SoundingStream opaque type, or NULL on failure
 * Comment:	This opens whatever .merged file the libunb.a code understands,
 *			and parses out the device that generated the data, etc. to
 *			initialise the internal stream.  An error model is constructed
 *			locally using the device from the OMG/HDCS file, the _vessel_
 *			and _mthd_.  Of course, the code has no way of checking that the
 *			_vessel_ is correct ...
 */

static SoundingStream sounding_new_omg(const char *name, Projection proj)
{
	SoundingStream	rtn;
	u32				maxbeams;
	
	if ((rtn = sounding_new_from_file(name, proj)) == NULL) {
		error_msg(modname, "error: failed initialising stream.\n");
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_OMG;
	if (!sounding_omg_check_hdr(rtn)) {
		error_msg(modname, "error: failed header check.\n");
		sounding_release(rtn);
		return(NULL);
	}
	maxbeams = device_get_nbeams(rtn->device);

#ifdef __DEBUG__
	error_msgv(modname, "device::maxbeams = %d\n", maxbeams);
#endif

	if ((rtn->current = sounding_new_ping(maxbeams)) == NULL) {
		error_msg(modname, "error: failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}

	return(rtn);
}

SoundingStream sounding_new_from_omg(const char *name, Projection proj,
									 Vessel vessel, ErrMthd mthd)
{
	SoundingStream	rtn;
	u32				maxbeams;
	
	if ((rtn = sounding_new_from_file(name, proj)) == NULL) {
		error_msg(modname, "failed initialising stream.\n");
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_OMG;
	if (!sounding_omg_check_hdr(rtn)) {
		error_msg(modname, "failed header check.\n");
		sounding_release(rtn);
		return(NULL);
	}
	maxbeams = device_get_nbeams(rtn->device);

#ifdef __DEBUG__
	error_msgv(modname, "device::maxbeams = %d\n", maxbeams);
#endif

	if ((rtn->current = sounding_new_ping(maxbeams)) == NULL) {
		error_msg(modname, "failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}
	/* Attempt to construct an error model using specified method */
	if (!sounding_construct_errormodel(rtn->device, mthd, rtn)) {
		error_msg(modname, "failed generating error attribution model.\n");
		sounding_release(rtn);
		return(NULL);
	}
	return(rtn);
}

/* Routine:	sounding_new_from_gsf
 * Purpose:	Opens a GSF file, checks header and initialises streams
 * Inputs:	*name	Name of the file to work from
 *			proj	Projection parameters to use for conversion to mapsheet
 *			device	Device structure used in data file
 *			vessel	Vessel description structure for the survey platform
 *			mthd	Error modeling method to use in attributing accuracies
 * Outputs:	Returns pointer to SoundingStream opaque type, or NULL on failure
 * Comment: This uses the supplied GSF library (at least V1.10) to open files,
 *			and understands most of what it does (the limitation is to known
 *			and supported multibeam systems).  The code here has no way to
 *			check, a priori, that the device provided actually matches that
 *			which was used to generate the data, but it assumes that it is the
 *			case initially and gripes massively if this later turns out not to
 *			be the case ... you have been warned.  There is also no checking of
 *			the vessel description; caveat emptor.
 */

static SoundingStream sounding_new_gsf(char *name, Projection proj, Device device)
{
	SoundingStream	rtn;
	int				gsfHandle;
	
	if ((rtn = sounding_new(proj)) == NULL) {
		error_msg(modname, "error: failed initialising stream.\n");
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_GSF;
	rtn->device = device;
	if ((rtn->current = sounding_new_ping(device_get_nbeams(device))) == NULL) {
		error_msg(modname, "error: failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}
	if (gsfOpen(name, GSF_READONLY, &gsfHandle) < 0) {
		error_msgv(modname, "error: underlying library failed opening \"%s\": %s.\n",
			name, gsfStringError());
		sounding_release(rtn);
		return(NULL);
	}
	if (!sounding_gsf_check_hdr(rtn, gsfHandle)) {
		error_msg(modname, "error: failed initialising stream reader.\n");
		sounding_release(rtn);
		return(NULL);
	}
	return(rtn);
}

SoundingStream sounding_new_from_gsf(char *name, Projection proj,
									 Device device, Vessel vessel, ErrMthd mthd)
{
	SoundingStream	rtn;
	int				gsfHandle;
	
	if ((rtn = sounding_new(proj)) == NULL) {
		error_msg(modname, "failed initialising stream.\n");
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_GSF;
	rtn->device = device;
	if ((rtn->current = sounding_new_ping(device_get_nbeams(device))) == NULL) {
		error_msg(modname, "failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}
	if (gsfOpen(name, GSF_READONLY, &gsfHandle) < 0) {
		error_msgv(modname, "underlying library failed opening \"%s\": %s.\n",
			name, gsfStringError());
		sounding_release(rtn);
		return(NULL);
	}
	if (!sounding_gsf_check_hdr(rtn, gsfHandle)) {
		error_msg(modname, "failed initialising stream reader.\n");
		sounding_release(rtn);
		return(NULL);
	}
	if (!sounding_construct_errormodel(rtn->device, mthd, rtn)) {
		error_msg(modname, "failed generating error attribution model.\n");
		sounding_release(rtn);
		return(NULL);
	}
	return(rtn);
}

/* Routine:	sounding_new_from_hips
 * Purpose:	Opens a HIPS file, checks header and initialises streams
 * Inputs:	*name	Name of the file to work from
 *			proj	Projection parameters to use for conversion to mapsheet
 *			device	Device structure used in data file
 *			vessel	Vessel description structure for the survey platform
 *			mthd	Error modeling method to use in attributing accuracies
 * Outputs:	Returns pointer to SoundingStream opaque type, or NULL on failure
 * Comment: This uses the supplied HIPS library, parsing the *name to match
 *			the HIPS 'Project/Vessel/Day/Line' convention.  Note that the code
 *			here does not check that the vessel and device descriptions match
 *			those in the file, and *very* strange things might happen if they
 *			don't.  In particular, the underlying library will read some of the
 *			vessel detail parameters from the HIPS VCFs (i.e., individual
 *			component latencies), etc.
 */

static SoundingStream sounding_new_hips(char *name, Projection proj, Device device)
{
	SoundingStream	rtn;
	
	if ((rtn = sounding_new(proj)) == NULL) {
		error_msg(modname, "error: failed initialising stream.\n");
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_HIPS;
	rtn->device = device;
	if ((rtn->current = sounding_new_ping(device_get_nbeams(device))) == NULL) {
		error_msg(modname, "error: failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}
	if (!sounding_hips_check_hdr(rtn, name)) {
		error_msgv(modname, "error: underlying library failed opening \"%s\".\n",
			name);
		sounding_release(rtn);
		return(NULL);
	}
	return(rtn);
}

SoundingStream sounding_new_from_hips(char *name, Projection proj,
									 Device device, Vessel vessel, ErrMthd mthd)
{
	SoundingStream	rtn;
	
	if ((rtn = sounding_new(proj)) == NULL) {
		error_msg(modname, "failed initialising stream.\n");
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_HIPS;
	rtn->device = device;
	if ((rtn->current = sounding_new_ping(device_get_nbeams(device))) == NULL) {
		error_msg(modname, "failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}
	if (!sounding_hips_check_hdr(rtn, name)) {
		error_msgv(modname, "underlying library failed opening \"%s\".\n",
			name);
		sounding_release(rtn);
		return(NULL);
	}
	if (!sounding_construct_errormodel(rtn->device, mthd, rtn)) {
		error_msg(modname, "failed generating error attribution model.\n");
		sounding_release(rtn);
		return(NULL);
	}
	return(rtn);
}

/* Routine:	sounding_new_from_raw
 * Purpose:	Generate a sounding stream from a manufacturer's raw datastream
 * Inputs:	*name		Name of the file to read
 *			proj		Projection for the mapsheet in use
 *			dev			Device associated with the file
 *			vessel		Vessel configuration structure for data platform
 *			mthd		Error attribution method to use
 *			data_reqd	Logical OR of the SStreamTypes enum elements
 * Outputs:	Pointer to opaque type for sounding stream, or NULL
 * Comment:	Note that the code has no way to check that the stream is in fact
 *			the same as the device passed here ... caveat emptor.  Invalid
 *			elements in data_reqd are not checked.
 */

static SoundingStream sounding_new_raw(const char *name, Projection proj,
									   Device dev, u32 data_reqd)
{
	SoundingStream	rtn;
	u32				maxbeams;
	
	if ((rtn = sounding_new_from_file(name, proj)) == NULL) {
		error_msg(modname, "failed initialising stream.\n");
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_RAW;
	if (!sounding_raw_configure(rtn, dev, data_reqd)) {
		error_msgv(modname, "failed to configure raw data stream for device "
			"\"%s\".\n", device_get_name(dev));
		sounding_release(rtn);
		return(NULL);
	}
	maxbeams = device_get_nbeams(rtn->device);
	if ((rtn->current = sounding_new_ping(maxbeams)) == NULL) {
		error_msg(modname, "failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}
	return(rtn);
}

SoundingStream sounding_new_from_raw(const char *name, Projection proj,
									 Device dev, Vessel vessel, ErrMthd mthd,
									 u32 data_reqd)
{
	SoundingStream	rtn;
	u32				maxbeams;
	
	if ((rtn = sounding_new_from_file(name, proj)) == NULL) {
		error_msg(modname, "failed initialising stream.\n");
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_RAW;
	if (!sounding_raw_configure(rtn, dev, data_reqd)) {
		error_msgv(modname, "failed to configure raw data stream for device "
			"\"%s\".\n", device_get_name(dev));
		sounding_release(rtn);
		return(NULL);
	}
	maxbeams = device_get_nbeams(rtn->device);
	if ((rtn->current = sounding_new_ping(maxbeams)) == NULL) {
		error_msg(modname, "failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}
	/* Attempt to construct an error model using specified method */
	if (!sounding_construct_errormodel(rtn->device, mthd, rtn)) {
		error_msg(modname, "failed generating error attribution model.\n");
		sounding_release(rtn);
		return(NULL);
	}
	return(rtn);
}

/* Routine:	sounding_new_from
 * Purpose:	Construct a sounding stream given parameters and type of file
 * Inputs:	type	Type of file to expect on input stream
 *			name	Filename to use for input file
 *			proj	Projection method information
 *			dev		Device generating the data (see comment)
 *			vessel	Vessel description for the survey platform (see comment)
 *			mthd	Error computation method (see comment)
 *			param	Generic u32 parameter for underlying code
 * Outputs:	Returns a pointer to the opened, initialised SoundingStream or NULL
 *			on failure to initialise
 * Comment:	This call acts as a marshalling point for the various file opening
 *			calls so that the caller only has to set the _type_ variable, and
 *			not worry about switch(){} ing for type in user code.
 *				Note that not all of the underlying file types require all of
 *			the parameters passed.  For example, native streams don't need
 *			either a Device or Vessel description since these are constructed
 *			when the files are generated and stored internally.  If you are
 *			*certain* that these are not required, you can of course pass NULL
 *			instead of something valid.  However, if it all goes horribly pear-
 *			shaped, don't say you weren't warned ...
 */

SoundingStream sounding_new_from(SndIpType type, char *name,
								 Projection proj, Device dev, Vessel vessel,
								 ErrMthd mthd, u32 param)
{
	SoundingStream	stream;
	
	switch(type) {
		case SND_IP_MERGED:
			/*stream = sounding_new_from_omg(name, proj, vessel, mthd);*/
			stream = sounding_new_omg(name, proj);
			break;
		case SND_IP_GSF:
/*			stream = sounding_new_from_gsf(name, proj, dev, vessel, mthd);*/
			stream = sounding_new_gsf(name, proj, dev);
			break;
		case SND_IP_HIPS:
/*			stream = sounding_new_from_hips(name, proj, dev, vessel, mthd);*/
			stream = sounding_new_hips(name, proj, dev);
			break;
		case SND_IP_RAW:
/*			stream = sounding_new_from_raw(name, proj, dev, vessel, mthd, param);*/
			stream = sounding_new_raw(name, proj, dev, param);
			break;
		case SND_IP_NATIVE:
			/* The native format only provides bathy, and doesn't need the
			 * device or vessel configurations since it provides its own which
			 * were built when it was made.
			 */
/*			stream = sounding_new_from_native(name, proj, mthd);*/
			stream = sounding_new_native(name, proj);
			break;
		default:
			error_msgv(modname, "error: sounding stream input type %d not"
				" known.\n", (u32)type);
			stream = NULL;
			break;
	}
	/* Attempt to construct an error model using specified method */
	if (stream != NULL &&
		!sounding_construct_errormodel_specific(stream->device, mthd, NULL, NULL, stream)) {
		error_msg(modname, "error: failed generating error attribution model.\n");
		sounding_release(stream);
		return(NULL);
	}
	return(stream);
}

/* Routine:	sounding_new_from_dual
 * Purpose:	Construct a sounding stream given parameters and type of file
 * Inputs:	type	Type of file to expect on input stream
 *			name	Filename to use for input file
 *			proj	Projection method information
 *			dev		Device generating the data (see comment)
 *			v_port	Vessel description for port head (see comment)
 *			v_stbd	Vessel description for starboard head (or NULL)
 *			mthd	Error computation method (see comment)
 *			param	Generic u32 parameter for underlying code
 * Outputs:	Returns a pointer to the opened, initialised SoundingStream or NULL
 *			on failure to initialise
 * Comment:	This call acts as a marshalling point for the various file opening
 *			calls so that the caller only has to set the _type_ variable, and
 *			not worry about switch(){} ing for type in user code.
 *				Note that not all of the underlying file types require all of
 *			the parameters passed.  For example, native streams don't need
 *			either a Device or Vessel description since these are constructed
 *			when the files are generated and stored internally.  If you are
 *			*certain* that these are not required, you can of course pass NULL
 *			instead of something valid.  However, if it all goes horribly pear-
 *			shaped, don't say you weren't warned ...
 *				The only difference between this any sounding_new_from() is that
 *			this routine can have a dual head specification, which means that you
 *			can use it to create multiple SoundingStreams simultaneously or
 *			consecutively with different devices and vessels.  Note that it is an
 *			undocumented feature of the current implementation that the Vessel
 *			passed to sounding_new_from() is basically ignored in favour of the
 *			default vessel that happens to be implemented when the SoundingStream
 *			is created, mainly due to the fact that the Vessel structure doesn't
 *			have any facility to record the various offsets required for dual-head
 *			systems.  This routine allows the specification to be made exactly.
 */

extern SoundingStream sounding_new_from_dual(SndIpType type, char *name,
								 Projection proj, Device dev,
								 Vessel v_port, Vessel v_stbd,
								 ErrMthd mthd, u32 param)
{
	SoundingStream	stream;
	
	switch(type) {
		case SND_IP_MERGED:
			stream = sounding_new_omg(name, proj);
			break;
		case SND_IP_GSF:
			stream = sounding_new_gsf(name, proj, dev);
			break;
		case SND_IP_HIPS:
			stream = sounding_new_hips(name, proj, dev);
			break;
		case SND_IP_RAW:
			stream = sounding_new_raw(name, proj, dev, param);
			break;
		case SND_IP_NATIVE:
			/* The native format only provides bathy, and doesn't need the
			 * device or vessel configurations since it provides its own which
			 * were built when it was made.
			 */
			stream = sounding_new_native(name, proj);
			break;
		default:
			error_msgv(modname, "error: sounding stream input type %d not"
				" known.\n", (u32)type);
			stream = NULL;
			break;
	}
	if (stream == NULL) {
		error_msgv(modname, "error: failed to build SoundingStream for \"%s\".\n", name);
		sounding_release(stream);
		return(NULL);
	}

	/* Attempt to construct an error model using specified method */
	if (!sounding_construct_errormodel_specific(stream->device, mthd, v_port, v_stbd, stream)) {
		error_msgv(modname, "error: failed generating error attribution model for \"%s\".\n", name);
		sounding_release(stream);
		return(NULL);
	}
	return(stream);
}

/* Routine:	sounding_get_nav
 * Purpose:	Return currently available navigation information
 * Inputs:	stream	SoundingStream to read from
 * Outputs:	*nav	Pointer to Nav structures available currently, or NULL if
 *					no nav data is currently available.
 * Comment:	This reads back all of the currently available navigation data that
 *			the underlying stream can provide.  Note that the extent of this
 *			navigation data depends on the type of stream --- HIPS I/O data has
 *			all of the navigation for the line available on-line; RAW data may
 *			have all data read from the file so far; most other systems can't
 *			provide accumulated data, just reading it as they go along.  Note
 *			that a return of NULL doesn't mean that no data will ever be available,
 *			just that there is none currently.
 */

Nav sounding_get_nav(SoundingStream stream)
{
	Nav	rtn;

	switch(stream->type) {
		case SOUNDING_FILE_HIPS:
			rtn = sounding_hips_get_nav(stream);
			break;
		case SOUNDING_FILE_RAW:
			rtn = sounding_raw_get_nav(stream);
			break;
		case SOUNDING_FILE_GSF:
		case SOUNDING_FILE_OMG:
		case SOUNDING_FILE_NATIVE:
		case SOUNDING_FILE_PLAIN:
			rtn = NULL;	/* These formats can't do this at all */
			break;
		case SOUNDING_FILE_UNKNOWN:
			error_msg(modname, "error: attempt to read navigation"
				" from uninitialised stream.\n");
			rtn = NULL;
			break;
		default:
			error_msg(modname, "error: internal: request for nav from an"
				" unknown sounding file type.\n");
			rtn = NULL;
			break;
	}
	return(rtn);
}

/* Routine:	sounding_get_next
 * Purpose:	Get next bathymetry packet from input stream
 * Inputs:	stream	SoundingStream to read from
 * Outputs:	*plat	Platform orientation information for ping
 *			**data	*data points to data buffer in private store
 *			*n_read	Set to number of soundings read from file
 *			Returns FILE_OK if read was good, otherwise appropriate error code.
 * Comment:	This is the primary read interface for bathymetry from the system.
 *			Because of the structure of the code, this is essentially just a
 *			marshalling yard for the various read routines associated with the
 *			different reader routines.  Its principal purpose is to call the
 *			right routine, and then generate error bounds on the soundings
 *			read using the appropriate error computation routine.
 */

FileError sounding_get_next(SoundingStream stream, Platform *plat,
							Sounding **data, u32 *n_read)
{
	FileError	rtn;
	u32			snd;
	f64			angle;	/* Beam angle for computing range */
	f64			tx_east, tx_north; 	/* Projected location of transducer */
	f64			de, dn;				/* Offset vector for beam solution */
	f64			length;				/* Magnitude of the offset vector */
	
	if (stream->fd != NULL && feof(stream->fd)) return(FILE_EOF);
	switch(stream->type) {
		case SOUNDING_FILE_HIPS:
			rtn = sounding_hips_get_next(stream, plat, stream->current);
			break;
		case SOUNDING_FILE_GSF:
			rtn = sounding_gsf_get_next(stream, plat, stream->current);
			break;
		case SOUNDING_FILE_OMG:
			rtn = sounding_omg_get_next(stream, plat, stream->current);
			break;
		case SOUNDING_FILE_PLAIN:
			rtn = sounding_plain_get_next(stream, stream->current);
			memset(plat, 0, sizeof(Platform));
			plat->surf_sspeed = default_surf_sspeed;
					/* This has to be initialised to avoid problems with
					 * error modelling later in the sequence.
					 */
			break;
		case SOUNDING_FILE_NATIVE:
			rtn = sounding_native_get_next(stream, plat, stream->current);
			break;
		case SOUNDING_FILE_RAW:
			rtn = sounding_raw_get_next_bathy(stream, plat, stream->current);
			break;
		case SOUNDING_FILE_UNKNOWN:
			error_msg(modname, "error: file type not set.\n");
			rtn = FILE_INVALID;
			break;
		default:
			error_msgv(modname, "internal: file type (%d) not known.\n",
						(u32)(stream->type));
			rtn = FILE_INVALID;
			break;
	}	
	if (rtn == FILE_OK) {
		*data = stream->current->beams;
		*n_read = stream->current->n_beams;
		/* We only need to do sounding error attribution on non-native files,
		 * since the import to native file does this as part of the process.
		 *   Modified 2002-02-04 to avoid error attribution if the file is in
		 * GSF format either, since GSF should have errors in, and if not, we
		 * might need more detailed information on the structure of the data
		 * packet being read in order to do the error attribution than we can
		 * transfer to this level.  This is particularly true if the file has
		 * dual-head information, or information from a split-head transducer
		 * (e.g., Elac systems) where the error model is different for the port
		 * and starboard sections due to the different offset and mounting
		 * angles.
		 *	Modified 2002-02-06 to avoid error attribution on HIPS/IO files for
		 * the same reasons as GSF files.
		 */
		if (stream->type != SOUNDING_FILE_NATIVE  &&
			stream->type != SOUNDING_FILE_GSF &&
			stream->type != SOUNDING_FILE_HIPS) {
			/* Compute the range value used in error modelling */
			for (snd = 0; snd < *n_read; ++snd) {
				angle = DEG2RAD(
							device_beam_to_angle(stream->device,
												 plat->surf_sspeed,
												 (*data)[snd].beam_number));
				(*data)[snd].range = (f32)((*data)[snd].depth/cos(angle));

#ifdef __DEBUG__
error_msgv(modname, "debug: beam = %d sspeed = %f angle = %f\n",
	(*data)[snd].beam_number, plat->surf_sspeed, angle);
#endif
			}
			if (!errmod_estimate(stream->errormodel, plat, *data, *n_read)) {
				error_msg(modname, "failed computing error bounds for soundings.\n");
				rtn = FILE_INTERNAL;
			}
			for (snd = 0; snd < *n_read; ++snd)
				(*data)[snd].depth = -(*data)[snd].depth;
		}
		/* Apply depth gate if set by the user */
		if (stream->min_depth < FLT_MAX || stream->max_depth > -FLT_MAX) {
			for (snd = 0; snd < *n_read; ++snd) {
				if ((*data)[snd].depth > stream->min_depth ||
									(*data)[snd].depth < stream->max_depth) {
					/* Note sense of comparisons above --- this is due to the
					 * fact that depths are always negative internally except
					 * between translation and error assignment as above.
					 */
					/* We remove the current point and fix up counters */
					memmove(*data+snd, *data+snd+1,
							sizeof(Sounding)*(*n_read - snd - 1));
					--(*n_read);
					--snd;
				}
			}
		}
		
		/* Apply angle gate if set by the user */
		if (stream->min_angle > -M_PI/2.0 || stream->max_angle < M_PI/2.0) {
			/* Project the platform location into the mapsheet coordinates */
			projection_ll_to_en_deg(stream->proj,
					plat->longitude, plat->latitude, &tx_east, &tx_north);
			for (snd = 0; snd < *n_read; ++snd) {
				/* Compute offset from platform to beam */
				de = tx_east - (*data)[snd].east;
				dn = tx_north - (*data)[snd].north;
				length = sqrt(de*de + dn*dn);
				
				/* Compute angle between sounding geometric ray and vertical */
				angle = atan(length/(*data)[snd].depth);
				
				if (angle < stream->min_angle || angle > stream->max_angle) {
					memmove(*data+snd, *data+snd+1,
							sizeof(Sounding)*(*n_read - snd - 1));
					--(*n_read);
					--snd;
				}
			}
		}
	} else {
		*data = NULL;
		*n_read = 0;
	}
	return(rtn);
}

/* Routine:	sounding_get_next_attitude
 * Purpose:	Extract high resolution attitude information from input stream
 * Inputs:	stream	SoundingStream to operate on
 *			plat	Platform structure to read attitude into
 * Outputs:	*plat is set up.  Returns FILE_OK for success, otherwise an error
 *			message and a suitable FileError return code.
 * Comment:	This routine only operates if the stream is of type
 *			SOUNDING_FILE_RAW, and only then if the stream actually has
 *			high resolution attitude data *and* the user requested that this
 *			be buffered when the sounding stream was configured.  Note that
 *			the return codes and error messages will come from different
 *			modules depending on what the actual error is.
 */

FileError sounding_get_next_attitude(SoundingStream stream, Platform *plat)
{
	FileError	rtn;
	
	if (feof(stream->fd)) return(FILE_EOF);
	switch(stream->type) {
		case SOUNDING_FILE_HIPS:
		case SOUNDING_FILE_GSF:
		case SOUNDING_FILE_OMG:
		case SOUNDING_FILE_PLAIN:
		case SOUNDING_FILE_NATIVE:
			error_msgv(modname, "error: cannot obtain high res. attitude from"
				" data type %d.\n", (u32)(stream->type));
			rtn= FILE_INVALID;
			break;
		case SOUNDING_FILE_RAW:
			rtn = sounding_raw_get_next_attitude(stream, plat);
			break;
		default:
			error_msgv(modname, "internal: file type (%d) not known.\n",
						(u32)(stream->type));
			rtn = FILE_INVALID;
			break;
	}	
	return(rtn);
}

/* Routine:	sounding_get_next_imagery
 * Purpose:	Extract full time-series imagery from input stream
 * Inputs:	stream	Stream to operate on
 *			plat	Platform structure to interpolate attitude into
 * Outputs:	image	Imagery pointer to set to internal store
 *			n_read	Set to number of beams available in internal store
 * Comment:	This routine only operates if the stream is of type
 *			SOUNDING_FILE_RAW, and then only if the stream actually has some
 *			time-series backscatter, *and* the user requested that this be
 *			buffered in the first place.  Note that the return codes and error
 *			messages will come from different modules, depending on what the
 *			error actually is.
 */

FileError sounding_get_next_imagery(SoundingStream stream, Platform *plat,
									Imagery **imagery, u32 *n_read)
{
	FileError	rtn;
	
	if (feof(stream->fd)) return(FILE_EOF);
	switch(stream->type) {
		case SOUNDING_FILE_HIPS:
		case SOUNDING_FILE_GSF:
		case SOUNDING_FILE_OMG:
		case SOUNDING_FILE_PLAIN:
		case SOUNDING_FILE_NATIVE:
			error_msgv(modname, "error: cannot obtain backscatter from"
				" data type %d.\n", (u32)(stream->type));
			rtn= FILE_INVALID;
			break;
		case SOUNDING_FILE_RAW:
			rtn = sounding_raw_get_next_imagery(stream, plat, stream->current);
			break;
		default:
			error_msgv(modname, "internal: file type (%d) not known.\n",
						(u32)(stream->type));
			rtn = FILE_INVALID;
			break;
	}
	if (rtn == FILE_OK) {
		*imagery = stream->current->ibeams;
		*n_read = stream->current->n_ibeams;
	} else {
		*imagery = NULL;
		*n_read = 0;
	}
	return(rtn);
}

/* Routine:	sounding_add_depthgate
 * Purpose:	Add a static depth gate to the SoundingStream algorithm
 * Inputs:	stream	SoundingStream to work from
 *			min		Minimum depth to accept (see comment)
 *			max		Maximum depth to accept (see comment)
 * Outputs:	True if depth gate was added, otherwise False
 * Comment:	Attempts to set the depth gate for accepting soundings.  Recall that
 *			depths are always negative internally, but we specify the depths
 *			here positive --- that is, min < max is required
 */

Bool sounding_add_depthgate(SoundingStream stream, f32 min, f32 max)
{
	if (min >= max) {
		error_msgv(modname, "can't set inconsistent depth gate with (min,max)"
			" = (%f, %f) m.\n", min, max);
		return(False);
	}
	stream->min_depth = -min;
	stream->max_depth = -max;
	return(True);
}

/* Routine:	sounding_add_anglegate
 * Purpose:	Sets a gate on angles acceptable in multibeam data
 * Inputs:	stream	SoundingStream to work from
 *			min		Minimum angle to accept (degrees)
 *			max		Maximum angle to accept (degrees)
 * Outputs:	True if angle gate was set, otherwise False
 * Comment:	This sets up a fixed angular range (note: not beam number range)
 *			which is acceptable for data.  Angles are specified wrt nadir
 *			(i.e., straight down), with port side angles negative.  We restrict
 *			angles -90 <= min < max <= 90.
 */

Bool sounding_add_anglegate(SoundingStream stream, f32 min, f32 max)
{
	if (-90.0 > min || min >= max || max > 90.0) {
		error_msgv(modname, "can't set inconsistent angle gate with"
			" -90.0 deg. < min = %f < max = %f < 90.0 deg.\n", min, max);
		return(False);
	}
	stream->min_angle = (f32)(DEG2RAD(min));
	stream->max_angle = (f32)(DEG2RAD(max));
	return(True);
}

/* Routine:	sounding_get_time
 * Purpose:	Return timestamp on a buffer
 * Inputs:	stream	SoundingStream to operate on
 *			buf		Buffer to access
 * Outputs:	*stamp	Timestamp on buffer
 *			Returns FILE_INVALID if the stream is not a Raw type, or
 *			FILE_NOTINIT if the stream doesn't have the data you request; may
 *			return other FileErrors from the underlying buffer stream (e.g.,
 *			FILE_TRYAGAIN if there is no data in the buffer).
 * Comment: Timestamps are held internally at UTC seconds since 00:00:00 on
 *			01/01/1970, and are at the maximal accuracy of the underlying
 *			file stream (typically milliseconds).
 */

FileError sounding_get_time(SoundingStream stream, SStreamTypes buf, f64p stamp)
{
	if (stream->type != SOUNDING_FILE_RAW) {
		error_msg(modname, "can't get buffer timestamps on non-raw data.\n");
		return(FILE_INVALID);
	}
	if ((stream->data_avail & buf) == 0) {
		error_msgv(modname, "no buffer of type %d available in stream.\n",
			(u32)buf);
		return(FILE_NOTINIT);
	}
	return(sounding_raw_extract_time(stream, buf, stamp));
}

/* Routine:	sounding_new_to_file
 * Purpose:	Generate an output stream for writing in native format
 * Inputs:	*name		Name of the file to open for output
 *			generator	Device being used to generate the data
 *			vessel		Vessel configuration for data platform
 *			proj		Projection used for mapsheet conversion
 * Outputs:	Pointer to output stream, or NULL on failure
 * Comment:	This opens a basic output stream which just encapsulates the data
 *			being passed, including the platform data available at the time.
 */


SoundingStream sounding_new_to_file(const char *name, Device generator,
									Vessel vessel, Projection proj)
{
	Header	rtn;
	u32		maxbeams;
	
	if ((rtn = (Header)calloc(1, sizeof(Hdr))) == NULL) return(NULL);
	if ((rtn->fd = fopen(name, "wb")) == NULL) {
		error_msgv(modname, "error: couldn't open \"%s\" for writing.\n", name);
		sounding_release(rtn);
		return(NULL);
	}
	if ((rtn->current = sounding_new_ping(device_get_nbeams(generator))) == NULL) {
		error_msg(modname, "failed allocating ping buffer.\n");
		sounding_release(rtn);
		return(NULL);
	}
	maxbeams = device_get_nbeams(generator);
	rtn->device = generator;
	if (!sounding_native_put_hdr(rtn, vessel, maxbeams)) {
		error_msg(modname, "error: failed header write.\n");
		sounding_release(rtn);
		return(NULL);
	}
	rtn->type = SOUNDING_FILE_NATIVE;
	rtn->ping_num = -1;
	rtn->proj = proj;
	return(rtn);
}

void sounding_detach_file(SoundingStream stream)
{
	if (stream->fd != NULL) fclose(stream->fd);
	if (stream->release_func != NULL) {
		(*(stream->release_func))(stream->private);
		stream->private = NULL;
		stream->release_func = NULL;
	}
	stream->fd = NULL;
	stream->ping_num = -1;
}

/* Routine:	sounding_attach_file
 * Purpose:	Attach a new file stream to an extant SoundingStream structure
 * Inputs:	stream	Stream to reuse
 *			*name	Name of new file to attach
 *			reqd	Data types required from the file (see comment)
 * Outputs:	True if stream was sucessfully attached, otherwise False
 * Comment:	This detaches the current file from the stream, and attempts to
 *			open and initialise a new one (e.g., after a line break).  The
 *			_reqd_ element is a logical OR of elements from the SStreamTypes
 *			enum, but is ignored unless the stream is of type SOUNDING_FILE_RAW
 *			which is the only one that currently provides more than one data
 *			type.
 */

Bool sounding_attach_file(SoundingStream stream, const char *name, u32 reqd)
{
	Bool	rc;
	int		gsfHandle;

	sounding_detach_file(stream);
	if (stream->type == SOUNDING_FILE_GSF) {
		if (gsfOpen(name, GSF_READONLY, &gsfHandle) < 0) {
			error_msgv(modname, "underlying library failed opening \"%s\": %s\n",
				name, gsfStringError());
			return(False);
		}
	} else if (stream->type == SOUNDING_FILE_HIPS) {
		/* Do nothing ... all work is done by sounding_hips_check_hdr() */
	} else {
		if ((stream->fd = fopen(name, "rb")) == NULL) {
			error_msgv(modname, "couldn't open \"%s\" for reading.\n", name);
			return(False);
		}
	}
	switch(stream->type) {
		case SOUNDING_FILE_HIPS:
			rc = sounding_hips_check_hdr(stream, name);
			break;
		case SOUNDING_FILE_GSF:
			rc = sounding_gsf_check_hdr(stream, gsfHandle);
			break;
		case SOUNDING_FILE_OMG:
			rc = sounding_omg_check_hdr(stream);
			break;
		case SOUNDING_FILE_NATIVE:
			rc = sounding_native_check_hdr(stream);
			break;
		case SOUNDING_FILE_PLAIN:
			rc = sounding_plain_check_hdr(stream);
			break;
		case SOUNDING_FILE_RAW:
			rc = sounding_raw_configure(stream, stream->device, reqd);
			break;
		case SOUNDING_FILE_UNKNOWN:
			error_msg(modname, "file type is not known: cannot attach.\n");
			rc = False;
			break;
		default:
			error_msgv(modname, "internal: file type (%d) not recognised.\n",
					(u32)(stream->type));
			rc = False;
			break;
	}
	return(rc);
}

FileError sounding_put_next(SoundingStream stream, Platform *plat,
							Sounding *data, u32 n_snd)
{
	FileError	rc;
	sPing		ping;
	
	memset(&ping, 0, sizeof(Ping));
	ping.n_beams = n_snd;
	ping.beams = data;
	ping.bathy_pingnum = stream->ping_num;
	ping.file_id = stream->current->file_id;
	ping.buffer_space = n_snd;
	
	switch(stream->type) {
		case SOUNDING_FILE_NATIVE:
			rc = sounding_native_put_next(stream, plat, &ping);
			break;
		case SOUNDING_FILE_HIPS:
		case SOUNDING_FILE_GSF:
		case SOUNDING_FILE_OMG:
		case SOUNDING_FILE_PLAIN:
		case SOUNDING_FILE_RAW:
			error_msgv(modname, "cannot write filetype %d.\n",
						(u32)(stream->type));
			break;
		case SOUNDING_FILE_UNKNOWN:
			error_msg(modname, "file type not set.\n");
			break;
		default:
			error_msgv(modname, "file type %d not recognised.\n",
						(u32)(stream->type));
			break;
	}
	return(rc);
}

Device sounding_get_device(SoundingStream stream)
{
	return(stream->device);
}

Projection sounding_get_projection(SoundingStream stream)
{
	return(stream->proj);
}

s32 sounding_get_pingnum(SoundingStream stream)
{
	return(stream->ping_num);
}

/* Routine:	sounding_flush
 * Purpose:	Flush any buffered data associated with the stream
 * Inputs:	stream	Stream to operate on
 * Outputs:	-
 * Comment:	At present, this only has any effect on raw sounding streams, where
 *			it releases all of the buffered data currently being held.
 */

void sounding_flush(SoundingStream stream)
{
	if (stream->type == SOUNDING_FILE_RAW)
		sounding_raw_flush_all(stream->private, stream->device);
}

/* Routine:	sounding_construct_arc
 * Purpose:	Solves the direct problem in geodesy (locates points relative
 *			to each other)
 * Inputs:	*lon	Longitude of the initial point (see comment)
 *			*lat	Lattitude of the initial point
 *			length	Length of the vector to the next point
 *			azimuth	Azimuth of the next point from the initial point
 * Outputs:	*lon, *lat are updated with the location of the next point
 * Comment:	This code is based on Semme's CGeoReference class, transformed here
 *			into C and encapsulated.  Note that this routine is in the global
 *			namespace, but is only prototyped in sounding_private.h so that
 *			it is only `known' internal to the sounding_*.c routines.
 *			Original comment is:
 *	CGeoReference::ConstructArc solves the direct problem in geodesy, that 
 *	is: given the coordinates of point p1, plus the azimuth and distance 
 *	to a second point -- compute the coordinates of the second point
 *	the solution is iterated untill the change in sigma is less than DEL
 *
 *	Note the ugly coding and ungainly variable names -- due to direct 
 *	translation from Fortran -- You should've seen the original!!
 *
 *	REFERENCE  T. VINCENTY - SURVEY REVIEW, APRIL 1975 
 *	Original Fortran source GLOB by Inge Nesbo, Jan 1976.
 *
 *			Note that the input lattitude, longitude and azimuth must be in
 *			radians, rather than degrees, and that this system only works for
 *			WGS84 due to the embedded constants for mean radius of the earth
 *			eccentricity, etc.
 */

#define DEL 1.0e-12

void sounding_construct_arc(f64p lon, f64p lat, f64 length, f64 azimuth)
{
    f64 sinAzim, cosAzim, sinLat, cosLat, diag, sinU1, cosU1, ac, bc;
    f64 sinSig1, cosSig1, sig, sig1, sAlf, uSq, dSig, dDSig, dSigM, sig0;
    f64 sinSig, cosSig, cosDSig, sinDLat, cosDLat, cSqAlf, c, ddL;
    s16	ik,stop;
	f64 To_lat,To_lon,From_lat,From_lon;
	f64 m_dA,m_dF,m_dB/*,m_dSqE, m_dE --- set but never used */,
		m_dSqE2,m_dE2,bOnA,eSq2;

	m_dA = 6378137.;
	m_dF = 1. / 298.257223563;
	m_dB = m_dA * (1. - m_dF);
/*	   m_dN = ( m_dA - m_dB) / ( m_dA + m_dB);*/
/*	m_dSqE = ( 2 * m_dF - m_dF * m_dF);*/
/*	m_dE = sqrt( m_dSqE);*/
	m_dSqE2 = ( m_dA * m_dA - m_dB * m_dB) / ( m_dB * m_dB);
	m_dE2 = sqrt( m_dSqE2);
/*	    m_strSpheroid = "World Geodetic System 1984";*/
	bOnA = m_dB / m_dA;
	eSq2 = m_dE2 * m_dE2;

	From_lat = *lat;
	From_lon = *lon;
    sinAzim = sin(azimuth);
    cosAzim = cos(azimuth);
    
    sinLat = sin(From_lat) * bOnA;
    cosLat = cos(From_lat);
    
    diag = sqrt( sinLat * sinLat + cosLat * cosLat);
    
    sinU1 = sinLat / diag;
    cosU1 = cosLat / diag;
    
    sinSig1 = sinU1;
    cosSig1 = cosU1 * cosAzim;
    sig1 = atan2( sinSig1, cosSig1);
    sAlf = cosU1 * sinAzim; 
    
    uSq = ( 1. - sAlf * sAlf) * eSq2;
    ac = -768. + uSq * ( 320. - 175. * uSq );
    ac = 1. + uSq * ( 4096. + uSq * ac) / 16384.;
    bc = -128. + uSq * ( 74. - 47. * uSq);
    bc = uSq *(256. + uSq * bc) / 1024.;  
    
    dDSig = 0.;
    sig0 = length / m_dB / ac;
    sig = sig0;
    
    
    /*	Start iteration
     *
     *	NOTE: the strange stop construct is used as I did not want to change
     *	the code too fundamentaly from the fortran code -- the fortran code
     *	used a goto statement to jump out of the !middle! of the 'while' loop
     */

    ik = True;
    stop = False;
    
    while(!stop)
    {
	    if (!ik) stop = True;
	    dSig = dDSig;
	    dSigM = 2. * sig1 + sig;
	    sinSig = sin(sig);
	    cosSig = cos(sig);
	    cosDSig = cos(dSigM);
	    if (ik)	
	    {
		    dDSig = bc * cosDSig * ( -3. + 4. * sinSig * sinSig) *
						(-3. + 4. * cosDSig * cosDSig) / 6.;
		    dDSig = bc * sinSig *
					(cosDSig+bc*(cosSig*(-1.+2.*cosDSig*cosDSig)-dDSig)/4.);
		    sig = sig0 + dDSig;
		    if( fabs(dDSig - dSig ) <= DEL) ik = False;
	    }
    }
    
    /* Compute lattitude */
    
    sinLat = sinU1 * cosSig + cosU1 * sinSig * cosAzim;
    cosLat = sinU1 * sinSig - cosU1 * cosSig * cosAzim;
    cosLat = (1. - m_dF) * sqrt( sAlf * sAlf + cosLat * cosLat);
    To_lat = atan2( sinLat, cosLat);
    
    /* Compute longitude */
    
    sinDLat = sinSig * sinAzim;
    cosDLat = cosU1 * cosSig - sinU1 * sinSig * cosAzim;
    cSqAlf = 1. - sAlf * sAlf;
    c = cSqAlf  * (4. + m_dF * ( 4. -3. * cSqAlf)) * m_dF / 16.;
    
    ddL = cosDSig + c * cosSig * ( -1. + 2. * cosDSig * cosDSig);
    ddL = (1. - c) * m_dF * sAlf * ( sig + c * sinSig * ddL);
    
    To_lon = From_lon + atan2(sinDLat, cosDLat) - ddL;
    
	*lat = To_lat;
	*lon= To_lon;
}

/* Routine:	sounding_locate_beam
 * Purpose:	Determine the location of a beam given transducer position and
 *			offset relative to transducer heading
 * Inputs:	x, y			Offset relative to transducer
 *			heading			Heading CW from N, dec. deg.
 *			t_lon, t_lat	Transducer longitude & lattitude
 *			proj			Projection in use on mapsheet
 * Outputs:	*b_east, *b_north	Beam projected location
 * Comment:	This uses code from Luciano to fix up aximuth from heading and
 *			offset, and code from Semme to compute the position of the beam
 *			using a standard solution to `the direct problem in geodesy' (it
 *			says here).  Consequence: don't have to project beams to compute
 *			the locate of the end (it's all done in geographic coordinates).
 *				This routine is in the global namespace, but is only prototyped
 *			in sounding_private.h, so that (like sounding_construct_arc()) it
 *			is only available to the sounding_*.c files.  Typically, code would
 *			use this as a wrapper around sounding_construct_arc().
 */

void sounding_locate_beam(f32 x, f32 y, f32 heading, f64 t_lon, f64 t_lat,
				 		  Projection proj, f64p b_east, f64p b_north)
{
	f64	azimuth, length;
	
	/* Convert everything into radians, since Semme's software requires it */
	t_lon *= DEG2RAD(1.0); t_lat *= DEG2RAD(1.0);
	heading *= (f32)(DEG2RAD(1.0));
	
	/* If transducer has a valid navigation calculate beam position using
	 * Semme's software
	 */
	if (t_lat != 0.0 && t_lon != 0.0 /*&& x != 0.0*/ && y != 0.0) {
		length = sqrt(x*x  + y*y);
		/* positive x implies negative angle,  positive y adds 90 degrees to
		 * the heading
		 */
		if (y > 0.0)  
			azimuth = (f64)(heading + M_PI_2 - atan(x/fabs(y)));
		else
			azimuth = (f64)(heading - M_PI_2 + atan(x/fabs(y)));
		if (azimuth > 2.0*M_PI )  azimuth -= 2.0*M_PI;
		if (azimuth < 0.0) azimuth += 2.0*M_PI;
		sounding_construct_arc(&t_lon, &t_lat, length, azimuth);
		projection_ll_to_en_rad(proj, t_lon, t_lat, b_east, b_north);
	} else if (t_lat != 0.0 && t_lon != 0.0 && x == 0.0 && y == 0.0) {
		/* The unlikely event that the beam is directly below the tx. */
		projection_ll_to_en_rad(proj, t_lon, t_lat, b_east, b_north);
	} else {
		*b_east = 0.0;
		*b_north = 0.0;
	}
}

/* Routine:	sounding_locate_pt
 * Purpose:	Determine the location of a point given center position and
 *			offset relative to heading
 * Inputs:	x, y			Offset relative to center (m)
 *			heading			Heading CW from N, dec. deg.
 *			t_lon, t_lat	Center longitude & lattitude
 * Outputs:	*p_lon, *b_lat	Point geographic location
 * Comment:	This uses code from Luciano to fix up aximuth from heading and
 *			offset, and code from Semme to compute the position of the beam
 *			using a standard solution to `the direct problem in geodesy' (it
 *			says here).  Consequence: don't have to project beams to compute
 *			the locate of the end (it's all done in geographic coordinates).
 *				This routine is in the global namespace, but is only prototyped
 *			in sounding_private.h, so that (like sounding_construct_arc()) it
 *			is only available to the sounding_*.c files.  Typically, code would
 *			use this as a wrapper around sounding_construct_arc().
 */

void sounding_locate_pt(f32 x, f32 y, f32 heading, f64 t_lon, f64 t_lat,
				 		f64p p_lon, f64p p_lat)
{
	f64	azimuth, length;
	
	/* Convert everything into radians, since Semme's software requires it */
	t_lon *= DEG2RAD(1.0); t_lat *= DEG2RAD(1.0);
	heading *= (f32)(DEG2RAD(1.0));
	
	/* If transducer has a valid navigation calculate beam position using
	 * Semme's software
	 */
	if (t_lat != 0.0 && t_lon != 0.0 && (x != 0.0 || y != 0.0)) {
		length = sqrt(x*x  + y*y);
		/* positive x implies negative angle,  positive y adds 90 degrees to
		 * the heading
		 */
		if (y > 0.0)  
			azimuth = (f64)(heading + M_PI_2 - atan(x/fabs(y)));
		else
			azimuth = (f64)(heading - M_PI_2 + atan(x/fabs(y)));
		if (azimuth > 2.0*M_PI )  azimuth -= 2.0*M_PI;
		if (azimuth < 0.0) azimuth += 2.0*M_PI;
		sounding_construct_arc(&t_lon, &t_lat, length, azimuth);
		*p_lon = RAD2DEG(t_lon);
		*p_lat = RAD2DEG(t_lat);
	} else {
		*p_lon = 0.0;
		*p_lat = 0.0;
	}
}

/* Routine:	sounding_execute_params
 * Purpose:	Execute parameter list for the sounding module
 * Inputs:	*list	ParList to walk down and parse
 * Outputs:	True if parameter reading works OK, otherwise False
 * Comment:	This looks for a default surface sound speed, and then passes the
 *			list on to the sub-modules for further parsing.
 */

typedef enum {
	SND_UNKNOWN = 0,
	SND_SURF_SSPEED
} SndParamEnum;

Bool sounding_execute_params(ParList *list)
{
	ParTable	tab[] = {
		{ "default_surf_sspeed",	SND_SURF_SSPEED },
		{ NULL,						SND_UNKNOWN }
	};
	Bool	rc;
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;

	node = list;
	do {
		node = params_match(node, "sounding", tab, &id, &match);
		switch (id) {
			case SND_UNKNOWN:
				break;
			case SND_SURF_SSPEED:
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
			default:
				error_msgv(modname, "error: unknown return from parameter"
					" matching module (%d).\n", id);
				return(False);
				break;
		}
	} while (node != NULL);
	
	rc = True;
	rc &= sounding_gsf_execute_params(list);
	rc &= sounding_omg_execute_params(list);
	rc &= sounding_raw_execute_params(list);
	rc &= sounding_hips_execute_params(list);
	
	return(rc);
}
