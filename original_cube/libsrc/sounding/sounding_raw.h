/*
 * $Id: sounding_raw.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.7.2.1  2002/07/14 02:20:37  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
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
 * Revision 1.5  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
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
 * File:	sounding_raw.h
 * Purpose:	Interface with raw sonar reading processes.
 * Date:
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

#ifndef __SOUNDING_RAW_H__
#define __SOUNDING_RAW_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "ccom_general.h"
#include "nav.h"
#include "att.h"
#include "sounding.h"
#include "error.h"
#include "device.h"

typedef enum {
	RAW_BATHY = 0,		/* Buffer up bathymetry packets */
	RAW_IMAGERY,		/* Buffer up backscatter or sidescan packets */
	RAW_ATTITUDE,		/* Buffer up towfish/ship attitude packets */
	RAW_NBUFS
} RBufKn;

typedef void (*RawRel)(void *);	/* Release function for the buffer */

typedef struct {
	u32	quantum;	/* Number of slots in the buffer */
	u32	minimum;	/* Number of slots which must be available before read */
	f64	*epoch;		/* Timestamp on the corresponding datagram */
	u32	n_used;		/* Number of buffer slots currently used (aka insert pt) */
	u32	n_space;	/* Number of buffer slots available */
	void **dgram;	/* Pointers to datagrams in causal order */
	RawRel	release;	/* Pointer to function to call to release slots */
} RBuffer;

typedef struct {
	Nav		nav;		/* Navigation information being accumulated */
	Att		att;		/* Attitude information being accumulated */
	Filt	*attfir;	/* Attitude decimation FIR filter parameters */
	RBuffer	*data;		/* Pointer to known buffers */
} RPrivate;

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

extern Bool sounding_raw_configure(SoundingStream s, Device dev, u32 rqd);

/* Routine:	sounding_raw_flush_all
 * Purpose:	Flush all existing buffers
 * Inputs:	*param	Pointer to private data structure
 *			dev		Device used in generating buffer data.
 * Outputs:	-
 * Comment:	This just runs through all buffers and frees them.
 */

extern void sounding_raw_flush_all(void *param, Device dev);

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

extern FileError sounding_raw_get_next_bathy(SoundingStream stream,
											 Platform *orient, Ping ping);
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

extern FileError sounding_raw_get_next_attitude(SoundingStream stream,
											 Platform *orient);

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

extern FileError sounding_raw_get_next_imagery(SoundingStream stream,
											   Platform *orient, Ping ping);

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

extern FileError sounding_raw_extract_time(SoundingStream stream,
										   SStreamTypes buf, f64 *stamp);

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

extern void sounding_raw_discard_n(RPrivate *p, RBufKn buf, u32 n);

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

extern Nav sounding_raw_get_nav(SoundingStream stream);

/* Routine:	sounding_raw_execute_params
 * Purpose:	Execute parameters for this sub-module, and its children
 * Inputs:	*list	ParList to walk and interpret
 * Outputs:	True if parameters list was interpreted correctly, otherwise False
 * Comment:	This looks for the buffer safety factor (i.e., scale on minimum
 *			declared number of buffer slots required by the raw readers), and
 *			then passes the list to all known specialist raw readers.
 */

#include "params.h"

extern Bool sounding_raw_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
