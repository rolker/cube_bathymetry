/*
 * $Id: errmod.c 2 2003-02-03 20:18:41Z brc $
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
 * Revision 1.3  2002/03/14 04:22:36  brc
 * Added routine to allow actual beam angles to be read back.  The error model
 * is the only piece of code which allows the user to get actual beam angles,
 * since it's the only one which integrates the device() module plate-centric
 * angles with the vessel() module static offsets.
 *
 * Revision 1.2  2001/05/14 04:19:32  brc
 * Updated to make modules 'params'-aware.
 *
 * Revision 1.1  2001/02/10 18:02:54  brc
 * Added routines to compute error estimates in various modes.  The errmod.c
 * routines are user-level interface; errmod_iho.c implements the basic `error
 * as a function of depth' idea inherent in IHO V3; errmod_full.c implements
 * the full MBES error model in all details.
 *
 *
 * File:	errmod.c
 * Purpose:	Error modelling for bathymetric data
 * Date:	24 November 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <float.h>

/* Administrative includes from the library as a whole */
#include "stdtypes.h"
#include "error.h"

/* Includes for objects required by this module */
#include "device.h"
#include "vessel.h"
#include "sounding.h"
#include "params.h"

/* Includes for and from this module */
#include "errmod.h"
#include "errmod_iho.h"
#include "errmod_full.h"

static char *modname = "errmod";
static char *modrev = "$Revision: 2 $";

/* ----------- Defines for internal typedefs: virtual functions ------------- */

/* Computes the error model for a given attitude and set of data */
typedef Bool (*ErrFunc)(Device device, Vessel vessel, Platform *plat,
						Sounding *data, u32 nSnds, void *ws);

/* Creates a private workspace for a method, given device and vessel config. */	
typedef void *(*ErrCreate)(Device dev, Vessel ves);

/* Destroys a private workspace */
typedef void (*ErrDestroy)(void *ws);

/* Encapsulates all of the information about the error model */
struct _err_mod {
	Device		device;
	Vessel		vessel;
	ErrFunc		compute;	/* N.B.: current error function */
	ErrDestroy	destructor;
	void		*ws;		/* Workspace for current error function */
}; /* *ErrMod defined in errmod.h */

/* ------------ Virtual tables for the various known algorithms ------------- */

static ErrCreate	constructors[ERRMOD_NMTHDS] = {
						errmod_iho_construct,
						errmod_full_construct
					};
static ErrDestroy	destructors[ERRMOD_NMTHDS] = {
						errmod_iho_destruct,
						errmod_full_destruct
					};
static ErrFunc		compute[ERRMOD_NMTHDS] = {
						errmod_iho_compute,
						errmod_full_compute
					};

/* --------------------------- Implementations ------------------------------ */

/* Routine:	errmod_release
 * Purpose:	Releases all memory associated with the error model
 * Inputs:	model	Model to be released
 * Outputs:	-
 * Comment:	Note that this removes the model's workspace *and* the ErrMod
 *			structure itself, but not the Device or Vessel structures.
 */

void errmod_release(ErrMod model)
{
	if (model == NULL) return;
	(*(model->destructor))(model->ws);
	free(model);
}

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

ErrMod errmod_new(Device device, Vessel vessel, ErrMthd method)
{
	ErrMod	rtn;
	
	if (method < 0 || method >= ERRMOD_NMTHDS) {
		error_msgv(modname, "error model method %d not known.\n",
			(u32)method);
		return(NULL);
	}
	if ((rtn = (ErrMod)calloc(1, sizeof(struct _err_mod))) == NULL) {
		error_msg(modname, "failed allocating error module workspace.\n");
		return(NULL);
	}
	rtn->device = device;
	rtn->vessel = vessel;
	rtn->compute = compute[method];
	rtn->destructor = destructors[method];
	if ((rtn->ws = (*(constructors[method]))(device, vessel)) == NULL) {
		error_msg(modname, "failed to initialise error module.\n");
		errmod_release(rtn);
		return(NULL);
	}
	return(rtn);
}

/* Routine:	errmod_setmethod
 * Purpose:	Sets the computation method for the error model dynamically
 * Inputs:	model	Model to reconfigure
 *			method	Method to use
 * Outputs:	True if reconfiguration worked, else False
 * Comment:	The error model is preserved until the new method is assured, so
 *			the ErrMod structure is still valid even if the change doesn't work.
 */

Bool errmod_setmethod(ErrMod model, ErrMthd method)
{
	void	*ws;
	
	if (method < 0 || method > ERRMOD_NMTHDS) {
		error_msgv(modname, "error model method %d not known.\n", (u32)method);
		return(False);
	}
	if ((ws = (*(constructors[method]))(model->device, model->vessel)) == NULL) {
		error_msg(modname, "warning: error model reconfiguration failed.\n");
		return(False);
	}
	(*(model->destructor))(model->ws);
	model->compute = compute[method];
	model->destructor  = destructors[method];
	model->ws = ws;
	return(True);
}

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

Bool errmod_estimate(ErrMod model, Platform *plat, Sounding *data, u32 nSnds)
{
	return((*(model->compute))(model->device, model->vessel, plat, data, nSnds,
							   model->ws));
}

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

typedef struct {
	f64	snd_fixed;
	f64	snd_pcent;
	f64	dtm_fixed;
	f64	dtm_pcent;
	f64	horizontal;
} OrderErrorInfo;

Bool errmod_get_iho_limits(ErrModIHOOrder order, f64p snd_f, f64p snd_p,
						   f64p dtm_f, f64p dtm_p, f64p horiz)
{
	static OrderErrorInfo	limits[4] = {
		{ 0.2, 0.0075, 0.2, 0.0075,	2.0		},
		{ 0.5, 0.013,  1.0, 0.0026,	5.0		},
		{ 1.0, 0.023,  2.0, 0.05,	20.0	},
		{ 1.0, 0.023,  5.0, 0.05,	150.0	}
	};
	u32			ord = (u32)order;
	
	switch (order) {
		case ERRMOD_IHO_ORDER_1:
		case ERRMOD_IHO_ORDER_2:
		case ERRMOD_IHO_ORDER_3:
		case ERRMOD_IHO_ORDER_4:
			break;
		default:
			error_msgv(modname, "error: IHO survey order (%d) not known.\n",
				ord);
			return(False);
	}
	*snd_f = limits[ord].snd_fixed;
	*snd_p = limits[ord].snd_pcent;
	*dtm_f = limits[ord].dtm_fixed;
	*dtm_p = limits[ord].dtm_pcent;
	*horiz = limits[ord].horizontal;
	return(True);
}

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

f64 errmod_get_beam_angle(ErrMod model, u32 beam)
{
	if (model->compute == errmod_full_compute)
		return(errmod_full_get_beam_angle(model->ws, beam));
	else
		return(DBL_MAX);
}

/* Routine:	errmod_execute_params
 * Purpose:	Execute parameter list in this module and sub-modules
 * Inputs:	*list	List of ParList structures to use
 * Outputs:	True if parameter reading went OK, otherwise False	
 * Comment:	The base module has no user-settable parameters, but the sub-modules
 *			do.  This just passes on the list to them and accumulates the
 *			results.
 */

Bool errmod_execute_params(ParList *list)
{
	Bool	rc = True;
	
	rc &= errmod_full_execute_params(list);
	rc &= errmod_iho_execute_params(list);
	return(rc);
}
