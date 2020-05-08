/*
 * $Id: sounding_omg.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.4  2001/05/14 04:23:28  brc
 * Updated to make modules 'params'-aware.
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
 * File:	sounding_omg.h
 * Purpose:	Read soundings from OMG/HDCS file and format for use
 * Date:	14 July 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __SOUNDING_OMG_H__
#define __SOUNDING_OMG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "sounding_private.h"

extern Bool sounding_omg_check_hdr(SoundingStream stream);
extern FileError sounding_omg_get_next(SoundingStream stream, Platform *orient,
									   Ping ping);

/* Routine:	sounding_omg_execute_params
 * Purpose:	Execute parameters for this sub-module
 * Inputs:	*list	ParList of parameters to work down.
 * Outputs:	True if the list of parameters was read correctly, otherwise False
 * Comment:	This looks for default surface speed, default mean column speed,
 *			default vessel speed (none of which are correctly maintained in the
 *			OMG/HDCS files) and a flag to indicate whether to honour OMG/HDCS
 *			edit flags or not.
 */

#include "params.h"

extern Bool sounding_omg_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
