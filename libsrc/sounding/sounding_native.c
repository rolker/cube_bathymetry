/*
 * $Id: sounding_native.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.4  2001/09/23 19:11:57  brc
 * Multiple upgrades to make the system actually do something useful!  Abandoned
 * the internal sounding structure in favour of the one normally used in the
 * rest of the library, which makes the native stream essentially a data dump of
 * memory as another system is translating data.  The stream also has a header
 * of the vessel description used in constructing the stream, and the device so
 * that the file is basically self contained.  It is not the intention that this
 * file format become a generic archive format: it is designed simply to be a
 * temporary holding set so that the library can re-use data without having to
 * translate from raw datagrams each time.
 *
 * Revision 1.3  2001/02/10 19:05:54  brc
 * Modified to accommodate the new vessel descriptions and error modeling
 * requirements.
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
 * File:	sounding_native.c
 * Purpose:	Read soundings from native file and format for use
 * Date:	14 July 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "stdtypes.h"
#include "error.h"
#include "sounding.h"
#include "sounding_private.h"
#include "sounding_native.h"

static char *modname = "sounding_native";
static char *modrev = "$Revision: 2 $";

typedef struct {
	Device		device;
	Vessel		vessel;
	u32			n_buffer;
	Sounding	*buffer;
} NBuffer;

#define NSND_CURRENT_VERSION	1
#define NSND_MAGIC_ID			0x4E534E44
#define DNSN_MAGIC_ID			0x444E534E

typedef struct {
	u32	magic;
	u32	version;
} NHeader;

void sounding_native_release(void *buffer)
{
	NBuffer	*buf = (NBuffer*)buffer;
	
	if (buf != NULL) {
		if (buf->device != NULL) device_release(buf->device);
		if (buf->vessel != NULL) vessel_release(buf->vessel);
		if (buf->buffer != NULL) free(buf->buffer);
		free(buf);
	}
}

Vessel sounding_native_get_vessel(void *buffer)
{
	NBuffer *buf = (NBuffer *)buffer;
	
	return(buf->vessel);
}

Bool sounding_native_check_hdr(SoundingStream stream)
{
	NHeader	hdr;
	NBuffer	*store;
	Device	dev;
	Vessel	vessel;
	u32		target;
	
	if (fread(&hdr, sizeof(NHeader), 1, stream->fd) != 1) {
		error_msg(modname, "error: failed reading header.\n");
		return(False);
	}
	if (hdr.magic != NSND_MAGIC_ID) {
		error_msg(modname, "error: didn't match magic number in header;\n");
		if (hdr.magic == DNSN_MAGIC_ID)
			error_msg(modname, " but did match reverse --- your data was not"
				"written on the same byte architecture.\n");
		else
			error_msg(modname, " are you *sure* this is a NSND native file?\n");
		return(False);
	}
	if (hdr.version != NSND_CURRENT_VERSION) {
		error_msgv(modname,
			"error: cannot read version %d (only current, V%d).\n",
			hdr.version, NSND_CURRENT_VERSION);
		return(False);
	}
	
	/* Attempt to set up device properties for stream */
	if ((dev = device_new_from_file(stream->fd)) == NULL) {
		error_msg(modname,
			"error: failed reading data generator info from file.\n");
		return(False);
	}
	/* Attempt to read vessel configuration parameters from stream */
	if ((vessel = vessel_new_from_file(stream->fd)) == NULL) {
		error_msg(modname, "error: failed reading vessel config. from file.\n");
		device_release(dev);
		return(False);
	}
	
	/* Attempt to allocate space for the private conversion buffer */
	target = sizeof(Sounding)*device_get_nbeams(dev);
	if ((store = (NBuffer*)calloc(1, sizeof(NBuffer))) == NULL ||
		(store->buffer = (Sounding*)malloc(target)) == NULL) {
		error_msg(modname, "error: failed to allocate private workspace.\n");
		sounding_native_release(store);
		return(False);
	}
	store->n_buffer = device_get_nbeams(dev);
	store->vessel = vessel;
	store->device = dev;
	
	stream->private = store;
	stream->release_func = sounding_native_release;
	stream->device = dev;
	stream->data_avail = SOUNDING_BATHY;
	return(True);
}

static Bool sounding_get_native_hdr(FILE *fd, Platform *orient, Ping ping)
{
	if (fread(&(ping->n_beams), sizeof(s32), 1, fd) != 1)
		return(False);
	if (fread(&(ping->file_id), sizeof(u16), 1, fd) != 1)
		return(False);
	if (fread(&(ping->bathy_pingnum), sizeof(s32), 1, fd) != 1)
		return(False);
	if (fread(orient, sizeof(Platform), 1, fd) != 1)
		return(False);
	return(True);
}

FileError sounding_native_get_next(SoundingStream stream, Platform *orient,
								   Ping ping)
{
	u32		snd;
	NBuffer	*store = (NBuffer*)(stream->private);
	f64		east, north;
	
	if (store == NULL) {
		error_msg(modname, "internal: error: private buffer not initialised!\n");
		return(FILE_NOTINIT);
	}
	if (!sounding_get_native_hdr(stream->fd, orient, ping)) return(FILE_IOERR);
	if (ping->n_beams > ping->buffer_space) {
		error_msgv(modname,
				"error: ping has %d beams, ping buffer is only %d long.\n",
				ping->n_beams, ping->buffer_space);
		return(FILE_INVALID);
	}
	if (fread(ping->beams, sizeof(Sounding), ping->n_beams, stream->fd)
														!= ping->n_beams) {
		error_msgv(modname, "error: failed reading ping %d data.\n",
			ping->bathy_pingnum);
		return(FILE_IOERR);
	}
	/* The Sounding structure is recorded directly in the native file format,
	 * which is a little more verbose that we'ld like, but less verbose than
	 * working from raw files.  However, since we don't want to bogart the
	 * projection by mandating that it must be the same as when this file was
	 * constructed, we unproject the sounding resolved locations to lat/lon
	 * before the file is written.  Consequently, we need to re-project into
	 * the new coordinate system here.  This assumes that the system is working
	 * with the same horizontal datum system, but since the library only
	 * supports one (WGS84) anyway, this shouldn't be too wierd.
	 */
	for (snd = 0; snd < ping->n_beams; ++snd) {
		projection_ll_to_en_deg(stream->proj,
								ping->beams[snd].east,
								ping->beams[snd].north,
								&east, &north);
		ping->beams[snd].east = east;
		ping->beams[snd].north = north;
	}
	return(FILE_OK);
}

static Bool sounding_native_write_ping(FILE *fd, Platform *orient, Ping ping)
{
	if (fwrite(&(ping->n_beams), sizeof(u32), 1, fd) != 1)
		return(False);
	if (fwrite(&(ping->file_id), sizeof(u16), 1, fd) != 1)
		return(False);
	if (fwrite(&(ping->bathy_pingnum), sizeof(s32), 1, fd) != 1)
		return(False);
	if (fwrite(orient, sizeof(Platform), 1, fd) != 1)
		return(False);
	return(True);
}

static Bool sounding_native_write_data(SoundingStream stream, Ping ping,
									   NBuffer *store)
{
	u32		snd;
	f64		east, north;

	memcpy(store->buffer, ping->beams, ping->n_beams*sizeof(Sounding));
	for (snd = 0; snd < ping->n_beams; ++snd) {
		projection_en_to_ll_deg(stream->proj,
								store->buffer[snd].east,
								store->buffer[snd].north,
								&east, &north);
		store->buffer[snd].east = east;
		store->buffer[snd].north = north;
	}
	if (fwrite(store->buffer, sizeof(Sounding), ping->n_beams, stream->fd)
															!= ping->n_beams) {
		error_msg(modname, "error: failed writing soundings.\n");
		return(False);
	}
	return(True);
}

FileError sounding_native_put_next(SoundingStream stream, Platform *orient,
								   Ping ping)
{
	NBuffer	*store = (NBuffer*)(stream->private);
	
	if (stream->fd == NULL) {
		error_msg(modname, "error: stream has not been initialised.\n");
		return(FILE_NOTINIT);
	}
	if (ping == NULL || ping->beams == NULL) {
		error_msg(modname, "error: ping has not been initialised.\n");
		return(FILE_NOTINIT);
	}
	if (ping->n_beams == 0) {
		error_msg(modname, "error: ping contains no data (n_beams==0).\n");
		return(FILE_INVALID);
	}
	if (ping->n_beams > store->n_buffer) {
		error_msgv(modname, "error: ping has %d beams, conversion buffer only"
			" has %d.\n", ping->n_beams, store->n_buffer);
		return(FILE_INVALID);
	}
	if (!sounding_native_write_ping(stream->fd, orient, ping)) {
		error_msg(modname, "error: failed writing ping header information.\n");
		return(FILE_IOERR);
	}
	if (!sounding_native_write_data(stream, ping, store)) {
		error_msg(modname, "error: put: error writing soundings.\n");
		return(FILE_IOERR);
	}
	return(FILE_OK);
}

Bool sounding_native_put_hdr(SoundingStream stream, Vessel vessel, u32 maxbeams)
{
	NHeader	hdr = { NSND_MAGIC_ID, NSND_CURRENT_VERSION };
	NBuffer	*store;
	u32		target;
	
	if (fwrite(&hdr, sizeof(NHeader), 1, stream->fd) != 1) {
		error_msg(modname, "error: failed to write header block to file.\n");
		return(False);
	}
	if (!device_write(stream->device, stream->fd)) {
		error_msg(modname, "error: failed writing data generator information\n");
		return(False);
	}
	if (!vessel_write(vessel, stream->fd)) {
		error_msg(modname, "error: failed writing vessel information.\n");
		return(False);
	}
	target = sizeof(Sounding)*maxbeams;
	if ((store = (NBuffer*)calloc(1, sizeof(NBuffer))) == NULL ||
		(store->buffer = (Sounding*)malloc(target)) == NULL) {
		error_msg(modname, "error: failed allocating conversion workspace.\n");
		sounding_native_release(store);
		return(False);
	}
	store->n_buffer = maxbeams;
		/* Note that we leave store->device = store->vessel = NULL so that
		 * releasing the output stream doesn't release them too: we didn't
		 * allocate the memory, so we shouldn't be fiddling with it either;
		 * this has been the cause of more than one dodgy and hard to find
		 * undocumented feature.
		 */
	stream->private = store;
	stream->release_func = sounding_native_release;
	return(True);
}
