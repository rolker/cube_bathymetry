/*
 * $Id: sounding_raw.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.8.2.1  2002/07/14 02:20:37  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.8  2001/12/07 21:24:05  brc
 * Added case for Reson 8125, and fixed bug with reporting devices when the
 * device type can't be matched.
 *
 * Revision 1.7  2001/05/14 04:23:28  brc
 * Updated to make modules 'params'-aware.
 *
 * Revision 1.6  2001/02/11 17:56:14  brc
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
 * Revision 1.5  2001/02/10 19:15:51  brc
 * Fixed memory leak in release code, added code to deal with speed over ground
 * interpolation, and improved error reporting when the buffer priming fails.
 *
 * Revision 1.4  2000/09/29 20:33:23  brc
 * Moved filter design from sounding code to general (since it can be used for
 * more than one purpose), and updated sounding code to use it.  Changed filter
 * to windowed ideal filter time domain design for simplicity.
 *
 * Revision 1.3  2000/09/25 20:19:05  brc
 * Major modification to raw file reading structure, and corresponding changes
 * to the remainder of the library to deal with the changes that this caused
 * at the sounding stream level.
 *
 * Revision 1.2  2000/09/07 21:13:43  brc
 * Removed .install*: mistake in original import.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	sounding_raw.c
 * Purpose:	Provide interface between user and sonars
 * Date:	25 July 2000
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
#include "stdtypes.h"
#include "error.h"
#include "device.h"
#include "nav.h"
#include "att.h"
#include "sounding.h"
#include "sounding_private.h"
#include "sounding_raw.h"
#include "read_simrad.h"
#include "params.h"

#undef __DEBUG__

static char *modname = "sounding_raw";
static char *modrev = "$Revision: 2 $";

#define MIN_BUF_FAC		1.0
#define MAX_BUF_FAC	   10.0
#define BUF_SAFETY_FAC	3.0	/* Safety factor -- buffer exageration */
static f32 buf_safety_fac = BUF_SAFETY_FAC;

void sounding_raw_release(void *param)
{
	u32			buf, elem;
	RPrivate	*p = (RPrivate*)param;

	if (p->nav != NULL) nav_release(p->nav);
	if (p->att != NULL) att_release(p->att);
	if (p->attfir != NULL) ccom_release_fir(p->attfir);
	if (p->data != NULL) {
		for (buf = 0; buf < RAW_NBUFS; ++buf) {
			free(p->data[buf].epoch);
			if (p->data[buf].n_used != 0) {
				if (p->data[buf].release == NULL)
					error_msgv(modname, "warning: %d elements still full in "
					"buffer and no release function registered."
					"  This will result in a memory leak.\n",
					p->data[buf].n_used);
				else {
					for (elem = 0; elem < p->data[buf].n_used; ++elem)
						(*(p->data[buf].release))(p->data[buf].dgram[elem]);
				}
			}
			free(p->data[buf].dgram);
		}
		free(p->data);
	}
	free(p);
}

/* Routine:	sounding_raw_check_space
 * Purpose:	Check that there is space in the input buffers for at least one of
 *			everything
 * Inputs:	*p	Pointer to RPrivate parameter structure
 * Outputs:	True if next read can be accomodated, otherwise False
 * Comment:	This checks that there is at least one space in each of the active
 *			buffers, and returns True if so.  If any of the buffers are full,
 *			False is returned.  This can be used to ensure that a read is not
 *			going to fail to find buffer space, irrespective of what the
 *			datagram turns out to be.
 */

static Bool sounding_raw_check_space(RPrivate *p)
{
	u32	buf;
	
	for (buf = 0; buf < RAW_NBUFS; ++buf)
		if (p->data[buf].dgram != NULL &&	/* i.e., limit to active buffers */
		   (p->data[buf].n_used+p->data[buf].minimum >= p->data[buf].n_space))
				return(False);
	return(True);
}

/* Routine:	sounding_raw_get_next_x
 * Purpose:	Read the next datagram sequence from file
 * Inputs:	*f	FILE to read from
 *			dev	Device in the file (error checking)
 *			buf	Data type of interest
 *			*p	Pointer to private parameters structure for the stream
 * Outputs:	FILE_OK on read and fill buffers, otherwise a suitable error
 * Comment:	This fills the buffers for the entire set of known types, until
 *			the requested buffer is ready to generate data, or one of the other
 *			buffers fills to capacity and will need to be emptied before the
 *			file can be read again.  This guarantees that the user can read the
 *			data of interest, but exposes the code to a number of failure modes
 *			which are difficult to indicate to the user.  Currently, the code
 *			simply returns FILE_TRYAGAIN to indicate that there wasn't an error
 *			per se., just that there is a temporary problem.
 */

static FileError sounding_raw_get_next_x(FILE *f, Device dev, RBufKn buf,
										 RPrivate *p)
{
	f64		nav_first, nav_last, att_first, att_last, last;
	Bool	abort = False;
	FileError	rc = FILE_OK;
	
	if (p->data[buf].dgram == NULL) {
		error_msgv(modname,
			"internal: call for buffer %d, but not initialised.\n", (u32)buf);
		return(FILE_INTERNAL);
	}
	nav_get_bound_times(p->nav, &nav_first, &nav_last);
	att_get_bound_times(p->att, &att_first, &att_last);
	last = (nav_last < att_last) ? nav_last : att_last;
	
	while ((p->data[buf].n_used == 0 || p->data[buf].epoch[0] > last)
																	&& !abort){
		/* First check if there is space to do the read */
		if (!sounding_raw_check_space(p)) {
			rc = FILE_TRYAGAIN;
				/* i.e., no space, but still haven't got what we need */
			abort = True;
			continue;
		}
		/* Next call the requisite driver routine per device type */
		switch(device_get_device(dev)) {
			case DEVICE_EM300:
			case DEVICE_EM1000:
			case DEVICE_EM1002:
			case DEVICE_EM3000:
			case DEVICE_EM3000D:
				rc = simrad_get_next(f, p, p->nav, p->att);
				if (rc != FILE_OK) abort = True;
				break;
			case DEVICE_SB8101:
			case DEVICE_SB9001:
			case DEVICE_SB9003:
			case DEVICE_KLEIN595:
			case DEVICE_KLEIN2000:
			case DEVICE_KLEIN3000:
			case DEVICE_KLEIN5000:
				error_msgv(modname,
					"internal: device %d not yet implemented.\n", (u32)dev);
				rc = FILE_INTERNAL;
				abort = True;
				break;
			case DEVICE_UNKNOWN:
				error_msg(modname, "internal: device not set in raw get.\n");
				rc = FILE_INVALID;
				abort = True;
				break;
			default:
				error_msgv(modname, "internal: device %d not known.\n",
							(u32)device_get_device(dev));
				rc = FILE_INTERNAL;
				abort = True;
				break;
		}
		/* Finally, update our knowledge of nav timestamp bounds */
		nav_get_bound_times(p->nav, &nav_first, &nav_last);
		att_get_bound_times(p->att, &att_first, &att_last);
		last = (nav_last < att_last) ? nav_last : att_last;
	}
	return(rc);	/* Indicate buffer is ready to go (or not) */
}

/* Routine:	sounding_raw_discard_n
 * Purpose:	Discard the first n elements of a buffer
 * Inputs:	*p	Pointer to RPrivate structure
 *			buf	Which buffer to discard from
 *			n	Number of elements to discard
 * Outputs:	-
 * Comment:	This call fails quietly if the buffer doesn't exist, or there is
 *			less data in the buffer than the caller wants us to discard.  Note
 *			that this quietude also means that if the buffer doesn't have a
 *			release function specified, the pointers will be removed causing
 *			a (possibly significant) memory leak.
 */

void sounding_raw_discard_n(RPrivate *p, RBufKn buf, u32 n)
{
	u32 elem;

#ifdef __DEBUG__
printf("%s: debug: discarding %d from buf %d.\n",
	modname, n, (u32)buf);
#endif

	if (p->data[buf].epoch == NULL) return;
	if (p->data[buf].n_used < n) n = p->data[buf].n_used;
	if (p->data[buf].release != NULL)
		for (elem = 0; elem < n; ++elem)
			(*(p->data[buf].release))(p->data[buf].dgram[elem]);
	memmove(p->data[buf].epoch, p->data[buf].epoch+n,
			sizeof(f64)*(p->data[buf].n_used-n));
	memmove(p->data[buf].dgram, p->data[buf].dgram+n,
			sizeof(void*)*(p->data[buf].n_used-n));
	p->data[buf].n_used -= n;
}

/* Routine:	sounding_raw_pop_buf_x
 * Purpose:	Extract the first item from a specified buffer and return
 * Inputs:	buf	Buffer to pop from
 *			dev	Device associated with the stream
 *			*p	Pointer to raw stream private parameter structure
 * Outputs:	*orient	Pointer to Platform structure for orientation information
 *			**data	Pointer to the datagram for interpretation
 * Comment:	This pops the top element off the specified buffer, checking that
 *			there is data, and that the nav is valid (both return FILE_INVALID).
 *			Note that this doesn't make any interpretation of the data; that's
 *			up to the calling routine, and the raw stream handler.
 */

static FileError sounding_raw_pop_buf_x(RBufKn buf, Device dev, RPrivate *p,
								 		Platform *orient, void **data)
{
	f64		nav_first, nav_last, att_first, att_last, first, last, sog;
	
	if (p->data[buf].n_used == 0) {
		error_msgv(modname, "internal: no data in buffer %d to return!\n",
			(u32)buf);
		return(FILE_INVALID);
	}
	nav_get_bound_times(p->nav, &nav_first, &nav_last);
	att_get_bound_times(p->att, &att_first, &att_last);
	first = (att_first > nav_first) ? att_first : nav_first;
	last = (att_last < nav_last) ? att_last : nav_last;
	
	if (p->data[buf].epoch[0] < first || p->data[buf].epoch[0] > last) {
		error_msgv(modname, "internal: no nav (epoch %lf, first %lf, last %lf).\n",
			p->data[buf].epoch[0], first, last);
		return(FILE_INVALID);
	}
	nav_interp_v(p->nav, p->data[buf].epoch[0], &(orient->longitude),
				 &(orient->latitude), &sog);
	orient->vessel_speed = (f32)sog;
	att_interp(p->att, p->data[buf].epoch[0], &(orient->roll), &(orient->pitch),
			   &(orient->heading), &(orient->heave));
	orient->timestamp = p->data[buf].epoch[0];
	*data = p->data[buf].dgram[0];
	memmove(p->data[buf].epoch, p->data[buf].epoch+1,
			sizeof(f64)*(p->data[buf].n_used-1));
	memmove(p->data[buf].dgram, p->data[buf].dgram+1,
			sizeof(void*)*(p->data[buf].n_used-1));
	--p->data[buf].n_used;
	return(FILE_OK);
}

/* Routine:	sounding_raw_get_next_bathy
 * Purpose:	Get the next bathymetry packet in translated format
 * Inputs:	stream	Stream to read data on
 *			orient	Platform structure to accept platform orientation
 *			ping	Ping structure to hold data on conversion
 * Outputs:	FILE_OK for sucess, otherwise a suitable error
 * Comment:	This gets the next bathy packet from the input stream, and
 *			translates it into internal format.  Note that this routine may
 *			return FILE_TRYAGAIN to indicate that it cannot read the bathy data
 *			because one of the other buffers associated with the stream is full
 *			of data that the user hasn't read, and the nav information isn't
 *			sufficient to interpolate the position of any bathy data which is
 *			present.  In this case, the reader can't get any more data from the
 *			input stream because the next packet might be for one of the buffers
 *			which is full, and would result in a dropped packet.  This doesn't
 *			mean that there's an error, just that nothing else can be read
 *			until you either empty or flush the other buffers.
 */

FileError sounding_raw_get_next_bathy(SoundingStream stream, Platform *orient,
									  Ping ping)
{
	FileError	rc;
	RPrivate	*p = (RPrivate*)(stream->private);
	void		*data;
	
	if (p == NULL || stream->type != SOUNDING_FILE_RAW) {
		error_msg(modname,
			"internal: stream private buffer not initialised!\n");
		return(FILE_NOTINIT);
	}
	
	if ((rc = sounding_raw_get_next_x(stream->fd, stream->device,
												RAW_BATHY, p)) != FILE_OK) {
		if (rc == FILE_EOF || rc == FILE_TRYAGAIN) return(rc);
		error_msgv(modname, "failed reading bathy packet: %s.\n",
			error_lookup(rc, NULL));
		return(rc);
	}
	if ((rc = sounding_raw_pop_buf_x(RAW_BATHY, stream->device, p,
				orient, &data)) != FILE_OK) {
		error_msgv(modname, "failed extracting data from buffer: %s.\n",
			error_lookup(rc, NULL));
		return(rc);
	}
	switch(device_get_device(stream->device)) {
		case DEVICE_EM300:
		case DEVICE_EM1000:
		case DEVICE_EM1002:
		case DEVICE_EM3000:
		case DEVICE_EM3000D:
			rc = simrad_translate_bathy(stream->device, stream->proj, data,
										orient, ping);
			break;
		case DEVICE_SB8101:
		case DEVICE_SB9001:
		case DEVICE_SB9003:
		case DEVICE_KLEIN595:
		case DEVICE_KLEIN2000:
		case DEVICE_KLEIN3000:
		case DEVICE_KLEIN5000:
			error_msgv(modname,
				"internal: device %d not yet implemented.\n",
					(u32)device_get_device(stream->device));
			rc = FILE_INTERNAL;
			break;
		case DEVICE_UNKNOWN:
			error_msg(modname, "internal: device not set in raw get.\n");
			rc = FILE_INVALID;
			break;
		default:
			error_msgv(modname, "internal: device %d not known.\n",
						(u32)device_get_device(stream->device));
			rc = FILE_INTERNAL;
			break;
	}
	return(rc);
}

/* Routine:	sounding_raw_get_next_attitude
 * Purpose:	Get the next attitude packet in translated format
 * Inputs:	stream	Stream to read data on
 *			orient	Platform structure to accept platform orientation
 * Outputs:	FILE_OK for sucess, otherwise a suitable error
 * Comment:	This gets the next attitude packet from the input stream, and has
 *			the underlying reader module convert it into external format.
 *				Note that this routine may return FILE_TRYAGAIN to indicate
 *			that it cannot read the bathy data because one of the other buffers
 *			associated with the stream is full of data that the user hasn't
 *			read, and the nav information isn't sufficient to interpolate the
 *			position of any attitude data which is present.  In this case, the
 *			reader can't get any more data from the input stream because the
 *			next packet might be for one of the buffers which is full, and
 *			would result in a dropped packet.  This doesn't mean that there's
 *			an error, just that nothing else can be read until you either empty
 *			or flush the other buffers.
 */

FileError sounding_raw_get_next_attitude(SoundingStream stream, Platform *orient)
{
	FileError	rc;
	RPrivate	*p = (RPrivate*)(stream->private);
	void		*data;
	
	if (p == NULL || stream->type != SOUNDING_FILE_RAW) {
		error_msg(modname,
			"internal: stream private buffer not initialised!\n");
		return(FILE_NOTINIT);
	}
	
	if ((rc = sounding_raw_get_next_x(stream->fd, stream->device,
												RAW_ATTITUDE, p)) != FILE_OK) {
		if (rc == FILE_EOF || rc == FILE_TRYAGAIN) return(rc);
		error_msgv(modname, "failed reading attitude packet: %s.\n",
			error_lookup(rc, NULL));
		return(rc);
	}
	if ((rc = sounding_raw_pop_buf_x(RAW_ATTITUDE, stream->device, p,
				orient, &data)) != FILE_OK) {
		error_msgv(modname, "failed extracting data from buffer: %s.\n",
			error_lookup(rc, NULL));
		return(rc);
	}
	switch(device_get_device(stream->device)) {
		case DEVICE_EM300:
		case DEVICE_EM1000:
		case DEVICE_EM1002:
		case DEVICE_EM3000:
		case DEVICE_EM3000D:
			rc = simrad_translate_attitude(data, orient);
			break;
		case DEVICE_SB8101:
		case DEVICE_SB9001:
		case DEVICE_SB9003:
		case DEVICE_KLEIN595:
		case DEVICE_KLEIN2000:
		case DEVICE_KLEIN3000:
		case DEVICE_KLEIN5000:
			error_msgv(modname,
				"internal: device %d not yet implemented.\n",
					(u32)device_get_device(stream->device));
			rc = FILE_INTERNAL;
			break;
		case DEVICE_UNKNOWN:
			error_msg(modname, "internal: device not set in raw get.\n");
			rc = FILE_INVALID;
			break;
		default:
			error_msgv(modname, "internal: device %d not known.\n",
						(u32)device_get_device(stream->device));
			rc = FILE_INTERNAL;
			break;
	}
	return(rc);
}

/* Routine:	sounding_raw_get_next_imagery
 * Purpose:	Read and translate the next imagery packet from the stream buffers
 * Inputs:	stream	Stream to operate on
 *			*orient	Location to write interpolated platform information into
 *			ping	Pointer to a ping storage location
 * Outputs:	FILE_OK on success, suitable error value otherwise
 * Comment:	This gets the next imagery packet from the input stream, and
 *			translates it into internal format.  Note that this routine may
 *			return FILE_TRYAGAIN to indicate that it cannot read the imagery data
 *			because one of the other buffers associated with the stream is full
 *			of data that the user hasn't read, and the nav information isn't
 *			sufficient to interpolate the position of any imagery data which is
 *			present.  In this case, the reader can't get any more data from the
 *			input stream because the next packet might be for one of the buffers
 *			which is full, and would result in a dropped packet.  This doesn't
 *			mean that there's an error, just that nothing else can be read
 *			until you either empty or flush the other buffers.
 */

FileError sounding_raw_get_next_imagery(SoundingStream stream,
										Platform *orient, Ping ping)
{
	FileError	rc;
	RPrivate	*p = (RPrivate*)(stream->private);
	void		*data;
	
	if (p == NULL || stream->type != SOUNDING_FILE_RAW) {
		error_msg(modname,
			"internal: stream private buffer not initialised!\n");
		return(FILE_NOTINIT);
	}
	
	if ((rc = sounding_raw_get_next_x(stream->fd, stream->device,
												RAW_IMAGERY, p)) != FILE_OK) {
		if (rc == FILE_EOF || rc == FILE_TRYAGAIN) return(rc);
		error_msgv(modname, "failed reading imagery packet: %s.\n",
			error_lookup(rc, NULL));
		return(rc);
	}
	if ((rc = sounding_raw_pop_buf_x(RAW_IMAGERY, stream->device, p,
				orient, &data)) != FILE_OK) {
		error_msgv(modname, "failed extracting data from buffer: %s.\n",
			error_lookup(rc, NULL));
		return(rc);
	}
	switch(device_get_device(stream->device)) {
		case DEVICE_EM300:
		case DEVICE_EM1000:
		case DEVICE_EM1002:
		case DEVICE_EM3000:
		case DEVICE_EM3000D:
			rc = simrad_translate_imagery(stream->device, stream->proj, data,
										  orient, ping);
			break;
		case DEVICE_SB8101:
		case DEVICE_SB9001:
		case DEVICE_SB9003:
		case DEVICE_KLEIN595:
		case DEVICE_KLEIN2000:
		case DEVICE_KLEIN3000:
		case DEVICE_KLEIN5000:
			error_msgv(modname,
				"internal: device %d not yet implemented.\n",
					(u32)device_get_device(stream->device));
			rc = FILE_INTERNAL;
			break;
		case DEVICE_UNKNOWN:
			error_msg(modname, "internal: device not set in raw get.\n");
			rc = FILE_INVALID;
			break;
		default:
			error_msgv(modname, "internal: device %d not known.\n",
						(u32)device_get_device(stream->device));
			rc = FILE_INTERNAL;
			break;
	}
	return(rc);
}

/* Routine:	sounding_raw_get_nav
 * Purpose:	Return all available navigation information
 * Inputs:	stream	SoundingStream to work from
 * Outputs:	Returns pointer to Nav structure, or NULL.
 * Comment:	This clones the nav that currently exists, and then returns a
 *			pointer to the clone.  Consequently, it is up to the user to
 *			ensure that this memory is released when done.  Note that
 *			this routine returns the navigation present at the time of the
 *			call, not necessarily all of the nav data in the raw file.
 *			Therefore, a return of NULL might just mean 'you haven't read
 *			any yet', not 'there is no nav'.
 */

Nav sounding_raw_get_nav(SoundingStream stream)
{
	RPrivate	*par = (RPrivate*)(stream->private);
	u32			point, n_points;
	Nav			rtn;
	f64			epoch, lon, lat, sog;

	if ((rtn = nav_new()) == NULL) {
		error_msgv(modname, "error: can't get buffer to clone nav data.\n");
		return(NULL);
	}
	n_points = nav_get_npoints(par->nav);
	for (point = 0; point < n_points; ++point) {
		if (!nav_get_point_n_v(par->nav, point, &epoch, &lon, &lat, &sog)) {
			error_msgv(modname, "error: failed getting point %d from nav"
				" buffer.\n", point);
			nav_release(rtn);
			return(NULL);
		}
		if (!nav_insert_v(rtn, epoch, lon, lat, sog)) {
			error_msgv(modname, "error: failed putting point %d into nav"
				" buffer clone.\n", point);
			nav_release(rtn);
			return(NULL);
		}
	}
	return(rtn);
}

/* Routine:	sounding_raw_init_param
 * Purpose:	Initialise a private parameter structure for the Raw interface
 * Inputs:	-
 * Outputs:	Pointer to parameter set, or NULL
 * Comment:	Note that this does not allocate the actual buffer space used in
 *			dealing with the various streams of information coming from the
 *			raw datagram stream, since it does not know at this point what
 *			device is going to be used in the stream (and hence what the
 *			capabilities are).  The buffers are initialised NULL, which the
 *			RAW interface units should know as `user is not interested in this
 *			data stream' and hence will ignore if no further action is taken.
 */

static RPrivate *sounding_raw_init_param(void)
{
	RPrivate	*rtn;
	
	if ((rtn = (RPrivate*)calloc(1, sizeof(RPrivate))) == NULL) {
		error_msg(modname, "failed allocating private workspace.\n");
		return(NULL);
	}
	if ((rtn->nav = nav_new()) == NULL) {
		error_msg(modname, "failed allocating nav structure.\n");
		sounding_raw_release(rtn);
		return(NULL);
	}
	if ((rtn->att = att_new()) == NULL) {
		error_msg(modname, "failed allocating att structure.\n");
		return(NULL);
	}
	if ((rtn->data = (RBuffer*)calloc(RAW_NBUFS, sizeof(RBuffer))) == NULL) {
		error_msgv(modname, "failed to allocate %d buffers.\n", RAW_NBUFS);
		sounding_raw_release(rtn);
		return(NULL);
	}
	return(rtn);
}

/* Routine:	sounding_raw_alloc_buf
 * Purpose:	Allocate buffer space for a particular buffer
 * Inputs:	*p		Pointer to RPrivate structure to use
 *			buf		Buffer to use
 *			slots	Number of slots to allocate in the buffer
 *			minimum	Minimum number of slots to have in buffer before allowing
 *					a read operation to take place.
 * Outputs:	True if allocation went OK, otherwise False
 * Comment:	This does not check whether the buffer is currently allocated or
 *			not, and hence could cause memory leaks under some circumstances.
 *				Note that the minimum number of slots required before allowing
 *			a read operation must be less than the number of slots which are
 *			being allocated (otherwise a read will never be allowed!) and may
 *			be more than one, if the underlying reader can break more than one
 *			cannonical component out of a single datagram (e.g., aggregated
 *			attitude and heading packets in EM series echosounders).
 */

static Bool sounding_raw_alloc_buf(RPrivate *p, RBufKn buf, u32 slots,
								   u32 minimum)
{
	if ((p->data[buf].epoch = (f64p)calloc(slots, sizeof(f64))) == NULL ||
		(p->data[buf].dgram = (void**)calloc(slots, sizeof(void*))) == NULL) {
		error_msgv(modname, "failed allocating %d slots in buffer.\n",
			slots);
		return(False);
	}
	p->data[buf].quantum = slots;
	p->data[buf].minimum = minimum;
	p->data[buf].n_used = 0;
	p->data[buf].n_space = slots;
	return(True);
}

/* Routine:	sounding_raw_configure
 * Purpose:	Configure stream buffers for device and required data
 * Inputs:	*s	Stream to configure
 *			dev	Device assumed to be feeding the stream
 *			rqd	Data required by the user-level process (LOR of SStreamTypes)
 * Outputs:	True if buffers were set up correctly, otherwise False
 * Comment:	This checks that the device specification and the required data
 *			are compatible, and allocates the required buffers according to the
 *			ping rates of the sonar, assuming that we'll get 1Hz nav strings in
 *			the data stream.  This routine assumes that the 
 */

Bool sounding_raw_configure(SoundingStream s, Device dev, u32 rqd)
{
	Bool		rc = True;
	FileError	read_rc;
	u32			props = device_get_properties(dev);
	RPrivate	*p;
	u32			rates[RAW_NBUFS], density[RAW_NBUFS];
	
	if (s->type != SOUNDING_FILE_RAW) {
		error_msg(modname, "internal: stream is not a raw device!\n");
		return(False);
	}
	if ((p = sounding_raw_init_param()) == NULL) return(False);
	s->private = (void*)p;
	s->release_func = sounding_raw_release;
	s->device = dev;

	/* Find the number of data elements which can arrive per data packet */
	rates[RAW_BATHY] = device_get_rate(dev, DEVICE_BATHY_RATE);
	rates[RAW_ATTITUDE] = device_get_rate(dev, DEVICE_ATTITUDE_RATE);
	rates[RAW_IMAGERY] = device_get_rate(dev, DEVICE_IMAGERY_RATE);
	/* Find data density (aka number of elements per data packet */
	density[RAW_BATHY] = device_get_density(dev, DEVICE_BATHY_DENSITY);
	density[RAW_ATTITUDE] = device_get_density(dev, DEVICE_ATTITUDE_DENSITY);
	density[RAW_IMAGERY] = device_get_density(dev, DEVICE_IMAGERY_DENSITY);

	if ((rqd & SOUNDING_BATHY) != 0) {
		if ((props & DEVICE_HAS_BATHY) == 0) {
			error_msgv(modname, "device %s does not provide bathy.\n",
				device_get_name(dev));
			return(False);
		}

#ifdef __DEBUG__
printf("%s: debug: allocating %d slots for bathy buffer (density %d).\n",
	modname, (u32)(rates[RAW_BATHY]*buf_safety_fac), density[RAW_BATHY]);
#endif

		if (!sounding_raw_alloc_buf(p, RAW_BATHY,
						(u32)(rates[RAW_BATHY]*buf_safety_fac), density[RAW_BATHY])) {
			error_msgv(modname, "failed allocating %d slots for bathy.\n",
				rates[RAW_BATHY]);
			return(False);
		}
		s->data_avail |= SOUNDING_BATHY;
	}
	if (rc && (rqd & SOUNDING_ATTITUDE) != 0) {
		if ((props & DEVICE_HAS_ATTITUDE) == 0) {
			error_msgv(modname, "device %s does not provide bathy.\n",
				device_get_name(dev));
			return(False);
		}

#ifdef __DEBUG__
printf("%s: debug: allocating %d slots for attitude buffer (density %d).\n",
	modname, (u32)(rates[RAW_ATTITUDE]*buf_safety_fac), density[RAW_ATTITUDE]);
#endif

		if (!sounding_raw_alloc_buf(p, RAW_ATTITUDE,
				(u32)(rates[RAW_ATTITUDE]*buf_safety_fac), density[RAW_ATTITUDE])) {
			error_msgv(modname, "failed allocating %d slots for bathy.\n",
				rates[RAW_ATTITUDE]);
			return(False);
		}
		s->data_avail |= SOUNDING_ATTITUDE;
	}
	if (rc && (rqd & SOUNDING_IMAGERY) != 0) {
		if ((props & DEVICE_HAS_IMAGERY) == 0) {
			error_msgv(modname, "device %s does not provide bathy.\n",
				device_get_name(dev));
			return(False);
		}
		
#ifdef __DEBUG__
printf("%s: debug: allocating %d slots for imagery buffer (density %d).\n",
	modname, (u32)(rates[RAW_IMAGERY]*buf_safety_fac), density[RAW_IMAGERY]);
#endif

		if (!sounding_raw_alloc_buf(p, RAW_IMAGERY,
					(u32)(rates[RAW_IMAGERY]*buf_safety_fac), density[RAW_IMAGERY])) {
			error_msgv(modname, "failed allocating %d slots for bathy.\n",
				rates[RAW_IMAGERY]);
			return(False);
		}
		s->data_avail |= SOUNDING_IMAGERY;
	}
	/* Next call the requisite driver routine per device type in order to
	 * prime the buffer with the first chunk of data (or any sort) */
	switch(device_get_device(dev)) {
		case DEVICE_EM300:
		case DEVICE_EM1000:
		case DEVICE_EM1002:
		case DEVICE_EM3000:
		case DEVICE_EM3000D:
			simrad_register_releasers(p);
			read_rc = simrad_get_next(s->fd, p, p->nav, p->att);
			if (read_rc != FILE_OK) {
				error_msgv(modname, "failed priming buffer (%s).\n",
					error_lookup(read_rc, NULL));
				rc = False;
			}
			break;
		case DEVICE_SB8101:
		case DEVICE_SB9001:
		case DEVICE_SB9003:
		case DEVICE_SB8125:
		case DEVICE_KLEIN595:
		case DEVICE_KLEIN2000:
		case DEVICE_KLEIN3000:
		case DEVICE_KLEIN5000:
			error_msgv(modname,
				"internal: device %d not yet implemented.\n",
				(u32)device_get_device(dev));
			rc = False;
			break;
		default:
			error_msgv(modname, "error: device %d not known.\n",
				(u32)device_get_device(dev));
			rc = False;
			break;
	}
	return(rc);
}

/* Routine:	sounding_raw_flush_x
 * Purpose:	Flush a particular buffer
 * Inputs:	*p	Pointer to RPrivate structure
 *			buf	Which buffer to flush
 			dev	Device used to generate the data
 * Outputs:	-
 * Comment:	Attempting to flush a non-initialised buffer is quietly ignored,
 *			but attempting to flush a non-existant buffer is reported as an
 *			internal error.
 */

static void sounding_raw_flush_x(RPrivate *p, RBufKn buf, Device dev)
{
	u32	elem;
	if (buf >= RAW_NBUFS) {
		error_msgv(modname,
			"internal: attempt to flush buffer %d (max is %d).\n",
			buf, RAW_NBUFS);
		return;
	}
	if (p->data[buf].dgram == NULL) return;
	if (p->data[buf].release == NULL) {
		error_msgv(modname, "internal: attempt to flush buffer where no"
			" release function exists.\n");
		return;
	}
	for (elem = 0; elem < p->data[buf].n_used; ++elem)
		(*(p->data[buf].release))(p->data[buf].dgram+elem);
	p->data[buf].n_used = 0;
}

/* Routine:	sounding_raw_flush_all
 * Purpose:	Flush all existing buffers
 * Inputs:	*param	Pointer to private data structure
 *			dev		Device used in generating buffer data.
 * Outputs:	-
 * Comment:	This just runs through all buffers and frees them.
 */

void sounding_raw_flush_all(void *param, Device dev)
{
	u32	buf;
	RPrivate	*p = (RPrivate*)param;
	
	for (buf = 0; buf < RAW_NBUFS; ++buf)
		if (p->data[buf].epoch != NULL)
			sounding_raw_flush_x(p, (RBufKn)buf, dev);
}

/* Routine:	sounding_raw_extract_time
 * Purpose:	External interface for buffer timestamp extraction.
 * Inputs:	*stream	Sounding stream to operate on
 *			buf		A SStreamTypes type of buffer to check
 * Outputs:	*stamp	Filled with the timestamp of the earliest element in the
 *					buffer, if possible
 *			Returns
 *				FILE_OK 		stamp if valid
 *				FILE_TRYAGAIN	No data in buffer to return stamp for
 * Comment:	This extracts the first timestamp in the specified buffer, and
 *			returns it.  Timestamps are seconds since epoch 00:00:00: 01/01/1970
 *			and are stored as double precision floating point at the highest
 *			resolution available to underlying file-format (typically ms).
 */

FileError sounding_raw_extract_time(SoundingStream stream, SStreamTypes buf,
									f64 *stamp)
{
	RPrivate	*p = (RPrivate*)(stream->private);
	RBufKn		b;
	
	switch(buf) {
		case SOUNDING_BATHY:	b = RAW_BATHY; break;
		case SOUNDING_ATTITUDE:	b = RAW_ATTITUDE; break;
		case SOUNDING_IMAGERY:	b = RAW_IMAGERY; break;
		default:
			error_msgv(modname, "internal: etime: unknown buffer type %d.\n",
				(u32)buf);
			return(FILE_INTERNAL);
	}
	if (p->data[b].n_used == 0) return(FILE_TRYAGAIN);
	*stamp = p->data[b].epoch[0];
	return(FILE_OK);
}

/* Routine:	sounding_raw_execute_params
 * Purpose:	Execute parameters for this sub-module, and its children
 * Inputs:	*list	ParList to walk and interpret
 * Outputs:	True if parameters list was interpreted correctly, otherwise False
 * Comment:	This looks for the buffer safety factor (i.e., scale on minimum
 *			declared number of buffer slots required by the raw readers), and
 *			then passes the list to all known specialist raw readers.
 */

typedef enum {
	SND_RAW_UNKNOWN = 0,
	SND_RAW_BUF_EXAG
} SndRawParamEnum;

Bool sounding_raw_execute_params(ParList *list)
{
	ParTable tab[] = {
		{ "buffer_exageration",	SND_RAW_BUF_EXAG	},
		{ NULL,					SND_RAW_UNKNOWN		}
	};
	Bool	rc;
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;

	node = list;
	do {
		node = params_match(node, "sounding.raw", tab, &id, &match);
		switch (id) {
			case SND_RAW_UNKNOWN:
				break;
			case SND_RAW_BUF_EXAG:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_BUF_FAC || dummy_float > MAX_BUF_FAC) {
					error_msgv(modname, "error: buffer exageration factor"
						" must be in the range [%.1f, %.1f] (not %.1f).\n",
						MIN_BUF_FAC, MAX_BUF_FAC, dummy_float);
					return(False);
				}
				buf_safety_fac = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting raw buffer exageration factor to %f (%s).\n",
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
	rc &= read_simrad_execute_params(list);

	return(rc);
}
