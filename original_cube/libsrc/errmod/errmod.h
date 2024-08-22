/*
 * $Id: errmod.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:51  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.4  2002/03/14 04:22:36  brc
 * Added routine to allow actual beam angles to be read back.  The error model
 * is the only piece of code which allows the user to get actual beam angles,
 * since it's the only one which integrates the device() module plate-centric
 * angles with the vessel() module static offsets.
 *
 * Revision 1.3  2001/05/14 21:55:56  brc
 * Added enum for IHO survey orders, and prototypes for the code to extract
 * the IHO parameters, and execute a parameter list in the module.
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
 * File:	errmod.h
 * Purpose:	Error Modelling for Bathymetric Data
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

#ifndef __ERRMOD_H__
#define __ERRMOD_H__

#include "stdtypes.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ERRMOD_IHO = 0,	/* Compute errors by simple IHO f(z) method */
	ERRMOD_FULL,	/* Full MBES error model for every sounding in every ping */
	ERRMOD_NMTHDS
} ErrMthd;			/* Method used to compute and attribute error bounds */

#include "device.h"
#include "vessel.h"
#include "sounding.h"
#include "params.h"

typedef enum {
	ERRMOD_IHO_ORDER_1 = 0,	/* Critical channels & harbours with hard bottoms */
	ERRMOD_IHO_ORDER_2,		/* Nav channels, harbours and approaches */
	ERRMOD_IHO_ORDER_3,		/* Coastal areas not O1/O2 up to 200m depth */
	ERRMOD_IHO_ORDER_4,		/* Everything else */
	ERRMOD_IHO_ORDER_UNKNOWN
} ErrModIHOOrder;

typedef struct _err_mod *ErrMod;	/* Opaque type for the error model in use */

/* Routine:	errmod_release
 * Purpose:	Releases all memory associated with the error model
 * Inputs:	model	Model to be released
 * Outputs:	-
 * Comment:	Note that this removes the model's workspace *and* the ErrMod
 *			structure itself, but not the Device or Vessel structures.
 */

extern void errmod_release(ErrMod model);

/* Routine:	errmod_new
 * Purpose:	Initialises a base error model structure
 * Inputs:	dev		Device structure for the sonar in use
 *			vessel	Vessel configuration structure for the mounting system
 *			method	Enum for which computational method to use
 * Outputs:	Pointer to ErrMod structure, or NULL on failure
 * Comment:	This simply calls the appropriate construction functions.  Note that
 *			the error module copies the device and vessel *pointers*, and does
 *			not copy the information pointed to.  This allows it to reconfigure
 *			the error algorithm on the fly if required, but also means that
 *			the device & vessel configs. should not be released before the
 *			ErrMod structure is.
 */

extern ErrMod errmod_new(Device device, Vessel vessel, ErrMthd method);

/* Routine:	errmod_setmethod
 * Purpose:	Sets the computation method for the error model dynamically
 * Inputs:	model	Model to reconfigure
 *			method	Method to use
 * Outputs:	True if reconfiguration worked, else False
 * Comment:	The error model is preserved until the new method is assured, so
 *			the ErrMod structure is still valid even if the change doesn't work.
 */

extern Bool errmod_setmethod(ErrMod model, ErrMthd method);

/* Routine:	errmod_estimate
 * Purpose:	Run the error model on a particular instance of data
 * Inputs:	model	Model to use
 *			plat	Observation platform orientation
 *			data	Soundings gathered in observation
 *			nSnds	Number of soundings gathered
 * Outputs:	True if model was computed and errors attributed; else False
 * Comment:	This simply calls through to the appropriate algorithm, checking
 *			return codes as it goes.
 */

extern Bool errmod_estimate(ErrMod model, Platform *plat, Sounding *data,
							u32 nSnds);

/* Routine:	errmod_get_iho_limits
 * Purpose:	Return parameters for IHO survey order specified
 * Inputs:	order	Order of survey to work with
 * Outputs:	*snd_f	Fixed portion of sounding error (m)
 *			*snd_p	Variable portion (% of depth)
 *			*dtm_f	Fixed portion of DTM (Bathymetric Model) error (m)
 *			*dtm_p	Variable portion (% of depth)
 *			*horiz	Horizontal 95% CI (m)
 *			Returns True if data is available, else False.
 * Comment:	This acts as a simple opaque database for the IHO S-44 4ed survey
 *			limits (Table 1, p.5), as ammended.  The idea here is that the
 *			acceptable error for individual soundings is given as a fixed depth
 *			plus a percentage of the depth indicated by the device (combined
 *			using a quadratic summation since they're variances).  The same
 *			applies for the derived DTMs, with different parameters.  This
 *			allows code to work out acceptable limits for data given the order
 *			of the survey.
 *				The acceptable error is given by:
 *					s = sqrt(snd_f*snd_f + snd_p*snd_p*z*z)
 *			where z is indicated (reduced) depth and s is 95% confidence limit
 *			for the sounding (resp. for DTM with dtm_p and dtm_f).
 */

extern Bool errmod_get_iho_limits(ErrModIHOOrder order, f64p snd_f, f64p snd_p,
						   f64p dtm_f, f64p dtm_p, f64p horiz);

/* Routine:	errmod_get_beam_angle
 * Purpose:	Return angle of the specified beam w.r.t. vertical
 * Inputs:	model	Error model structure to use
 *			beam	Beam required
 * Outputs:	Returns beam angle w.r.t. vertical (-ve to port) of beam, or
 *			DBL_MAX if information is not available.
 * Comment:	This only returns useful information if the model is doing the
 *			full error computation, and otherwise returns only DBL_MAX.  Note
 *			that the internal construction of beam angles is in radians, rather
 *			than degrees, and no convertion is done here.  Hence, all angles
 *			are reported in radians w.r.t. nadir so that beams to port of
 *			nadir are -ve and beams to stbd are +ve.  No correction for 
 *			alignment angles or measured roll are added.
 */

extern f64 errmod_get_beam_angle(ErrMod model, u32 beam);

/* Routine:	errmod_execute_params
 * Purpose:	Execute parameter list in this module and sub-modules
 * Inputs:	*list	List of ParList structures to use
 * Outputs:	True if parameter reading went OK, otherwise False	
 * Comment:	The base module has no user-settable parameters, but the sub-modules
 *			do.  This just passes on the list to them and accumulates the
 *			results.
 */

extern Bool errmod_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
