/*
 * $Id: sounding_plain.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.2  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	sounding_plain.h
 * Purpose:	Read soundings from native file and format for use
 * Date:	14 July 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __SOUNDING_PLAIN_H__
#define __SOUNDING_PLAIN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "sounding_private.h"

extern Bool sounding_plain_check_hdr(SoundingStream stream);
extern FileError sounding_plain_get_next(SoundingStream stream, Ping ping);

#ifdef __cplusplus
}
#endif

#endif
