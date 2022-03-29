/*
 * $Id: cube.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:44  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.1.2.1  2002/07/14 02:20:46  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.1  2001/08/21 02:00:07  brc
 * Added headers for CUBE module.
 *
 *
 * File:	cube.h
 * Purpose:	Implement CUBE algorithm for depth estimation on a grid
 * Date:	27 July 2001
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

#ifndef __CUBE_H__
#define __CUBE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "errmod.h"

typedef struct _cube_param *Cube;		/* Algorithm control parameters */

/* Routine:	cube_get_invalid
 * Purpose:	Return value being used to represent invalid data
 * Inputs:	p	Cube parameter structure to work from
 * Outputs:	Returns invalid data marker
 * Comment:	The invalid number is not strictly defined, but is usually something
 *			obvious, like FLT_MAX or -FLT_MAX, or NaN.
 */

extern f32 cube_get_invalid(Cube p);

/* Routine:	cube_write_param
 * Purpose:	Write parameter structure to file
 * Inputs:	param	Pointer to (opaqued) parameter structure
 *			*f		FILE structure to write into
 * Outputs:	True if write was sucessful, otherwise False
 * Comment:	-
 */

extern Bool cube_write_param(Cube param, FILE *f);

/* Routine:	cube_read_param
 * Purpose:	Read parameter structure from file
 * Inputs:	*f	FILE structure to read from
 * Outputs:	Pointer to (opaqued) parameter structure, or NULL on failure.
 * Comment:	This doesn't do any version checking, which may cause problems in
 *			the future.
 */

extern Cube cube_read_param(FILE *f);

/* Routine:	cube_init_param
 * Purpose:	Initialise parameters for the algorithm to their default values
 * Inputs:	order	IHO Survey Order as defined in errmod.h
 *			dn, de	Northing and Easting spacings for sheet
 * Outputs:	Pointer to an opaqued parameter structure for the algorithm
 * Comment:	This initialises the parameter to the currently compiled values.
 *			At present, there is no other way to change the parameters than to
 *			recompile the source.
 */

extern Cube cube_init_param(ErrModIHOOrder order, f64 de, f64 dn);

/* Routine:	cube_release_param
 * Purpose:	Releases all memory associated with the algorithm parameters struct
 * Inputs:	*param	Pointer to the (opaqued) algorithm parameter structure
 * Outputs:	-
 * Comment:	-
 */

extern void cube_release_param(Cube param);

/* Routine:	cube_execute_params
 * Purpose:	Execute parameters list for this sub-module
 * Inputs:	*list	ParList linked list to work through
 * Outputs:	True if the list was parsed properly, otherwise False
 * Comment:	This looks for a whole screed of parameters used to control all
 *			aspects of the CUBE algorithm ... basically the list at the top
 *			of the source file.  All of the parameters are limit checked as
 *			far as possible.
 */

#include "params.h"

extern Bool cube_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
