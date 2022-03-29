/*
 * $Id: errmod_iho.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:51  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.3.2.1  2002/07/14 02:20:46  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.3  2001/05/14 04:20:24  brc
 * Updated to make modules 'params'-aware, and updated to V4 of S.44 model,
 * so that the survey order can be specified, and the depth components are
 * computed based on the table in errmod.c and depth.
 *
 * Revision 1.2  2001/02/10 19:22:52  brc
 * C++-ified the header files to provide correct namespace conventions when
 * used with C++ compilers.
 *
 * Revision 1.1  2001/02/10 18:02:54  brc
 * Added routines to compute error estimates in various modes.  The errmod.c
 * routines are user-level interface; errmod_iho.c implements the basic `error
 * as a function of depth' idea inherent in IHO V3; errmod_full.c implements
 * the full MBES error model in all details.
 *
 *
 * File:	errmod_iho.h
 * Purpose:	Compute errors for bathy based on IHO V4
 * Date:	24 November 2000
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

#ifndef __ERRMOD_IHO_H__
#define __ERRMOD_IHO_H__

#include "stdtypes.h"
#include "errmod.h"
#include "device.h"
#include "vessel.h"
#include "sounding.h"
#include "params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Routine:	errmod_iho_construct
 * Purpose:	Generate internal workspace for the IHO based computation engine
 * Inputs:	device	Device in use for making soundings
 *			vessel	Vessel in which device is installed
 * Outputs:	Pointer to workspace (cast to void *) or NULL on error
 * Comment:	-
 */

extern void *errmod_iho_construct(Device device, Vessel vessel);

/* Routine:	errmod_iho_destruct
 * Purpose:	Release memory associated with IHO based computation engine
 * Inputs:	*ws	Pointer to workspace
 * Outputs:	-
 * Comment:	-
 */

extern void errmod_iho_destruct(void *ws);

/* Routine:	errmod_iho_compute
 * Purpose:	Carry out computations for IHO based error model
 * Inputs:	plat	Orientation and location of observation platform
 *			data	Soundings structure to be filled in
 *			nSnds	Number of soundings in observation set
 *			*ws		Pointer to private workspace
 * Outputs:	True if computations succeeded, otherwise False
 * Comment:	At present, this always returns True.
 */

extern Bool errmod_iho_compute(Device device, Vessel vessel, Platform *plat,
							   Sounding *data, u32 nSnds, void *ws);

/* Routine:	errmod_iho_execute_params
 * Purpose:	Execute a parameter list for the IHO based error module
 * Inputs:	*list	List of parameter definitions to work through
 * Outputs:	True if parameters were executed OK, otherwise False
 * Comment:	This accepts either 'iho_order' [1,4] or 'fixed_vert', 'var_vert',
 *			'horizontal' to set things specifically.  It is a mistake to have
 *			both specified.
 */

extern Bool errmod_iho_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
