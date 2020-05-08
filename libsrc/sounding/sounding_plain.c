/*
 * $Id: sounding_plain.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.3  2001/02/10 19:11:12  brc
 * Added code to fake a sounding detection method (assumed to be amplitude
 * detected in all cases).
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
 * File:	sounding_plain.c
 * Purpose:	Read soundings from plain (CCOM-SIMPLE) file and format for use
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
#include "sounding_plain.h"

static char *modname = "sounding_plain";
static char *modrev = "$Revision: 2 $";

typedef struct {
	u16	file_id;
	u8	beam_number;
	u8	backscatter;
	f64	longitude,
		lattitude;
	f32	depth;
} SSnd;	/* A raw sounding structure - never seen by user code */

#define SIMPLE_MIN_DEPTH	-20000.0
#define SIMPLE_MAX_DEPTH	20000.0

typedef struct {
	u32		buf_sz;
	SSnd	*buffer;
} SBuffer;	/* Private buffer for conversion so we don't have to allocate */

void sounding_plain_release(void *buffer)
{
	SBuffer	*buf = (SBuffer*)buffer;
	if (buf != NULL) {
		free(buf->buffer);
		free(buf);
	}
}

Bool sounding_plain_check_hdr(SoundingStream stream)
{
	SSnd	buffer;
	SBuffer	*store;
	u32		target;
	
	/* There is no header per-se about the simple format, so the best we can
	 * do is read the first element, and see if it makes sense.
	 */
	if (fread(&buffer, sizeof(SSnd), 1, stream->fd) != 1) {
		error_msg(modname, "failed reading first element.\n");
		rewind(stream->fd);
		return(False);
	}
	rewind(stream->fd);
	if (buffer.longitude < -180.0 || buffer.longitude >= 180.0 ||
		buffer.lattitude < -90.0 || buffer.lattitude >= 90.0 ||
		buffer.depth < SIMPLE_MIN_DEPTH || buffer.depth > SIMPLE_MAX_DEPTH) {
		error_msg(modname, "first element looks suspicious --- check file.\n");
		return(False);
	}
	
	/* Well, at least we're sure the first sounding is on the planet somewhere.
	 * We now need to build a conversion buffer.
	 */
	target = sizeof(SSnd)*stream->current->buffer_space;
	if ((store = (SBuffer*)calloc(1, sizeof(SBuffer))) == NULL ||
		(store->buffer = (SSnd*)malloc(target)) == NULL) {
		error_msg(modname, "failed allocating conversion buffer.\n");
		sounding_plain_release(store);
		return(False);
	}
	stream->private = store;
	stream->release_func = sounding_plain_release;
	stream->data_avail = SOUNDING_BATHY;
	return(True);
}

FileError sounding_plain_get_next(SoundingStream stream, Ping ping)
{
	u32		target, snd;
	SBuffer	*store = (SBuffer*)(stream->private);

	/* Simple file format is just a set of SSnd structures, so we read
	 * as many as we have space for Sounding's in the ping.
	 */
	
	if (store == NULL) {
		error_msg(modname, "internal: buffer not allocated!\n");
		return(FILE_NOTINIT);
	}
	target = (ping->buffer_space < store->buf_sz) ?
				ping->buffer_space : store->buf_sz;
	if (fread(store->buffer, sizeof(SSnd), target, stream->fd) != target) {
		if (feof(stream->fd)) {
			error_msg(modname, "end of file.\n");
			return(FILE_EOF);
		} else {
			error_msg(modname, "error reading file.\n");
			return(FILE_IOERR);
		}
	}
	/* Now convert the data to soundings */
	ping->n_beams = target;
	for (snd = 0; snd < target; ++snd) {
		ping->beams[snd].file_id = store->buffer[snd].file_id;
		ping->beams[snd].beam_number = store->buffer[snd].beam_number;
		ping->beams[snd].backscatter = store->buffer[snd].backscatter;
		projection_ll_to_en_deg(stream->proj,
							store->buffer[snd].longitude,
							store->buffer[snd].lattitude,
							&(ping->beams[snd].east),
							&(ping->beams[snd].north));
		ping->beams[snd].depth = store->buffer[snd].depth;
		ping->beams[snd].flags &= ~SOUNDING_DETECT_MTHD_FLAG;
			/* Pretend that all soundings are amplitude detected */
	}
	return(FILE_OK);
}

