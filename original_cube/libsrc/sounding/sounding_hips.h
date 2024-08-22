/*
 * $Id: sounding_hips.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.1.2.1  2002/07/14 02:20:37  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.1  2002/03/14 04:13:51  brc
 * Modules used to read CARIS/HIPS(HDCS) data via. the hipsio library.
 *
 *
 * File:	sounding_hips.h
 * Purpose:	Read soundings from HIPS/HDCS files
 * Date:	2002-02-05
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

#ifndef __SOUNDING_HIPS_H__
#define __SOUNDING_HIPS_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "ccom_general.h"
#include "sounding.h"
#include "sounding_private.h"
#include "nav.h"
#ifdef WIN32
#include "HIPSio/procssdDepths.h"
#else
#include "procssdDepths.h"
#endif

typedef struct {		/* Broken-out specification for a HIPS project line */
	char	*prefix;	/* Whatever's in the filename after PVDL is taken out */
	char	*project;
	char	*vessel;
	char	*day;
	char	*line;
} HIPS_Spec;

extern void sounding_hips_release(void *buffer);

/* Routine:	sounding_hips_check_hdr
 * Purpose:	Checks file header and stores reference pointer
 * Inputs:	stream		Pointer to the SoundingStream being configured
 *			filename	Filename to open for input.
 * Outputs:	True if the setup was completed, otherwise False
 * Comment:	This routine reads the navigation and attitude data for the line,
 *			and then stuffs them into LUTs for further work while reading
 *			profiles.  Hence, can fail if memory fails, etc.  Note that the
 *			filename may be given absolute or relative to the HDCS directory,
 *			but that only the last four components are extracted for use in
 *			choosing the data: '<arbitrary-foo>/project/vessel/day/line' is
 *			the expected form, although 'project/vessel/day/line' is also
 *			allowed.  In any case, absolute paths are ignored, since the HIPS
 *			library always works relative to HDCS_DATA_PATH.
 */

extern Bool sounding_hips_check_hdr(SoundingStream stream, const char *filename);

/* Routine:	sounding_hips_get_next
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

extern FileError sounding_hips_get_next(SoundingStream stream, Platform *orient,
										Ping ping);
/* Routine:	sounding_hips_get_nav
 * Purpose:	Read back all of the navigational information for a line
 * Inputs:	stream	SoundingStream to read from
 * Outputs:	Pointer to Nav structure, or NULL.
 * Comment:	This returns a new buffer filled with all of the nav data
 *			from the line.  This is allocated here, so the user is
 *			responsible for removing the information in it when done.
 */

extern Nav sounding_hips_get_nav(SoundingStream stream);

/* Routine:	sounding_hips_execute_params
 * Purpose:	Execute parameters for this sub-module
 * Inputs:	*list	ParList to parse parameters from
 * Outputs:	True if parameter list was executed correctly, otherwise False
 * Comment:	This looks only for the surface sound speed to set in input packets
 *			if there is not sufficient information in the packet to work it out.
 */

#include "params.h"

extern Bool sounding_hips_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
