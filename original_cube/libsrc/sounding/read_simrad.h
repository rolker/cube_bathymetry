/*
 * $Id: read_simrad.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1  2002/08/30 21:42:58  dneville
 * Adding some more code needed for the CUBE raw simrad reader
 *
 * Revision 1.5  2001/05/14 04:23:28  brc
 * Updated to make modules 'params'-aware.
 *
 * Revision 1.4  2001/02/11 17:56:14  brc
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
 * Revision 1.3  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
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
 * File:	read_simrad.h
 * Purpose:	Types and prototypes to interface to Simrad files
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

#ifndef __READ_SIMRAD_H__
#define __READ_SIMRAD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "error.h"
#define ALLOW_SIMRAD
#include "simrad.h"

/* Routine:	validate_simrad
 * Purpose: Check validity of current EM format header block
 * Inputs:	*hdr	Pointer to header block
 *			maxlen	Maximum expected length for any header block
 * Outputs:	True if block looks OK, False otherwise
 * Comment:	This checks that the STX byte is right, and that the declared size
 *			of block is reasonable.
 */

extern Bool validate_simrad(emhdr_t *d, int maxlen);

/* Routine:	simrad_release
 * Purpose:	Release memory associated with a simrad_t datagram
 * Inputs:	*data	Pointer to data element
 * Outputs:	-
 * Comment:	This assumes that the pointer passed *is* a simrad_t*.  If it isn't
 *			many very bad things may happen ...
 */

extern void simrad_release(void *data);

/* Routine:	simrad_register_releasers
 * Purpose:	Set up pointers to suitable release functions in buffer structures
 * Inputs:	*p	Pointer to RPrivate structure to fill in
 * Outputs:	-
 * Comment:	Note that this only fills in the elements that it knows about,
 *			leaving the rest NULL.  This will cause visible debug warnings
 *			elsewhere, giving a security check.
 */

extern void simrad_register_releasers(RPrivate *p);

/* Routine:	simrad_get_next
 * Purpose:	Read the next datagram out of the raw file, coping with errors and
 *			inserting nav into the buffer provided
 * Inputs:	*ip		File to read from
 *			*param	Parameter structure containing data buffers
 *			nav		Nav sequence to insert packets into
 *			att		Att sequence to insert packets into
 * Outputs:	FILE_OK if next item read, otherwise something appropriate
 * Comment:	This reads the input file until it terminates in an error (or EOF)
 *			with the proviso that the next element read is something that the
 *			user is interested in.  Any packets for which a buffer element is
 *			not available (either because there is no buffer at all, or because
 *			the user has not provided buffer space for a packet which is
 *			handled by the system) are ignored, and any nav packets are
 *			interpreted and inserted into the nav stream directly.
 */


extern FileError simrad_get_next(FILE *ip, RPrivate *param, Nav nav, Att att);

/* Routine:	simrad_translate_bathy
 * Purpose:	Translate Simrad proprietry bathy data format to internal
 * Inputs:	dev		Device used in the translation
 *			proj	Projection in use on the mapsheet
 *			data	Pointer to internal private data structure
 *			orient	Orientation of the survey platform at reception
 *			ping	Internal data structure to fill in
 * Outputs:	FILE_OK if translation went OK, otherwise suitable error
 * Comment:	This translates the data structure into the internal format
 *			(assuming that the transducer locations are set), and then releases
 *			the internal format structure.  Note that error checking is done
 *			for the device and type of packet, etc.
 */

extern FileError simrad_translate_bathy(Device dev, Projection proj, void *data,
								 		Platform *orient, Ping ping);

/* Routine:	simrad_translate_attitude
 * Purpose:	Copy attitude from internal buffer into output structure
 * Inputs:	*data	Data from internal buffer store
 *			*orient	Output platform orientation with low-res attitude/position
 * Outputs:	Updates attitude information in *orient with high-res attitude
 * Comment:	This copies over the positioning information in the Platform
 *			structure from the low-res side, then replaces the attitude info
 *			from the buffered data (which is already stored internally as a
 *			Platform structure in translated format).
 */

extern FileError simrad_translate_attitude(void *data, Platform *orient);
/* Routine:	simrad_translate_imagery
 * Purpose:	Copy imagery data from raw buffer into output structure
 * Inputs:	dev		Device being used to generate the data
 *			proj	Projection parameters for current mapsheet
 *			*data	Pointer to low-level reader data structure
 *			*orient	Pointer to interpolated platform orientation space
 *			*ping	Pointer to current ping workspace
 * Outputs:	FILE_OK if the data is available, is configured, and is compatible
 *			with the current bathy ping stored in the Ping structure.  Otherwise
 *			a suitable error message.
 * Comment:	This breaks the information output of the imagery structure and into
 *			the Imagery structures.  We want to locate the beams in the imagery,
 *			but can't do this without the corresponding depth telegrams (since
 *			the imagery datagram doesn't have location).  Therefore, we need to
 *			have the depth datagram available before getting the imagery worked
 *			out.  Here, we keep the ping numbers up to date in the Ping structure
 *			and check that ping->bathy_pingnum is the same as the incoming
 *			ping number before locating beams.
 */

extern FileError simrad_translate_imagery(Device dev, Projection proj,
										  void *data, Platform *orient,
										  Ping ping);

/* Routine:	read_simrad_execute_params
 * Purpose:	Execute parameters for this sub-sub-module
 * Inputs:	*list	List of ParList structures to work with
 * Outputs:	True if list was parsed correctly, otherwise False
 * Comment:	This looks for the number of resynch events to allow in the input
 *			stream, and a number of parameters to control the attitude
 *			decimation filter algorithm.
 */

#include "params.h"

extern Bool read_simrad_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
