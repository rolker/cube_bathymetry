/*
 * $Id: errmod_full.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:51  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.4  2002/03/14 04:24:21  brc
 * Removed specification of static angle, since this is now defined in the
 * vessel description on a per-head basis (as it always should have been).
 * Fixed bug in use of static roll offsets which resulted from a confusion over
 * whether degrees or radians were being used.
 *
 * Revision 1.3  2001/05/14 04:19:32  brc
 * Updated to make modules 'params'-aware.
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
 * File:	errmod_full.h
 * Purpose:	Compute errors for bathy based on full V3
 * Date:	24 November 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __ERRMOD_FULL_H__
#define __ERRMOD_FULL_H__

#include "stdtypes.h"
#include "errmod.h"
#include "device.h"
#include "vessel.h"
#include "sounding.h"
#include "params.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Routine:	errmod_full_construct
 * Purpose:	Generate internal workspace for the full MBES computation engine
 * Inputs:	device	Device in use for making soundings
 *			vessel	Vessel in which device is installed
 * Outputs:	Pointer to workspace (cast to void *) or NULL on error
 * Comment:	-
 */

extern void *errmod_full_construct(Device device, Vessel vessel);

/* Routine:	errmod_full_destruct
 * Purpose:	Release memory associated with full MBES computation engine
 * Inputs:	*ws	Pointer to workspace
 * Outputs:	-
 * Comment:	-
 */

extern void errmod_full_destruct(void *ws);

/* Routine:	errmod_full_compute
 * Purpose:	Carry out computations for full MBES error model
 * Inputs:	device	Device being used to generate the soundings
 *			vessel	Vessel configuration parameters for platform
 *			plat	Orientation and location of observation platform
 *			data	Soundings structure to be filled in
 *			nSnds	Number of soundings in observation set
 *			*ws		Pointer to private workspace
 * Outputs:	True if computations succeeded, otherwise False
 * Comment:	At present, this always returns True.
 */

extern Bool errmod_full_compute(Device device, Vessel vessel, Platform *plat,
						 		Sounding *data, u32 nSnds, void *ws);

/* Routine:	errmod_full_get_beam_angle
 * Purpose:	Return beam number -> angle mapping used in error modules
 * Inputs:	*ws		Workspace pointer
 *			beam	Beam number to return
 * Outputs:	Returns angle w.r.t. vertical (0 == straight down, -ve to port)
 * Comment:	This returns the angle computed by the error model with static
 *			roll taken into account, so that the angle returned is w.r.t
 *			vertical, rather than w.r.t. transducer face as it is with the
 *			device_beam_to_angle() call.
 */

extern f64 errmod_full_get_beam_angle(void *ws, u32 beam);

/* Routine:	errmod_full_execute_params
 * Purpose:	Execute a list of parameters for this sub-module
 * Inputs:	*list	List of parameters to work with
 * Outputs:	True if list parsed OK, otherwise False.
 * Comment:	Currently only looks for default sound speed
 */

extern Bool errmod_full_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
