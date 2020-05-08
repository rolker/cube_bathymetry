/*
 * $Id: sounding_gsf.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.2  2001/05/14 04:23:28  brc
 * Updated to make modules 'params'-aware.
 *
 * Revision 1.1  2001/04/10 23:22:10  brc
 * Added to implement GSF file format read-in and reformatting.
 *
 *
 * File:	sounding_gsf.h
 * Purpose:	Read and parse GSF files (with the aid of a safety net)
 * Date:	4 Mar 2001
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __SOUNDING_GSF_H__
#define __SOUNDING_GSF_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "sounding_private.h"


/* Routine:	sounding_gsf_check_hdr
 * Purpose:	Checks file header and stores reference pointer
 * Inputs:	stream		Pointer to the SoundingStream being configured
 *			gsfHandle	Handle returned by GSFlib for the file
 * Outputs:	True if the setup was completed, otherwise False
 * Comment:	The only potential for failure here is if the buffer to store the
 *			gsfHandle pointer runs out of memory.
 */

extern Bool sounding_gsf_check_hdr(SoundingStream stream, int gsfHandle);

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

extern FileError sounding_gsf_get_next(SoundingStream stream, Platform *orient,
									   Ping ping);

/* Routine:	sounding_gsf_execute_params
 * Purpose:	Execute parameters for this sub-module
 * Inputs:	*list	ParList to parse parameters from
 * Outputs:	True if parameter list was executed correctly, otherwise False
 * Comment:	This looks only for the surface sound speed to set in input packets
 *			if there is not sufficient information in the packet to work it out.
 */

#include "params.h"

extern Bool sounding_gsf_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
