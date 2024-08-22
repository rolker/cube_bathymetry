/*
 * $Id: errmod_iho.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:51  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.2.2.1  2002/07/14 02:20:46  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.2  2001/05/14 04:20:24  brc
 * Updated to make modules 'params'-aware, and updated to V4 of S.44 model,
 * so that the survey order can be specified, and the depth components are
 * computed based on the table in errmod.c and depth.
 *
 * Revision 1.1  2001/02/10 18:02:54  brc
 * Added routines to compute error estimates in various modes.  The errmod.c
 * routines are user-level interface; errmod_iho.c implements the basic `error
 * as a function of depth' idea inherent in IHO V3; errmod_full.c implements
 * the full MBES error model in all details.
 *
 *
 * File:	errmod_iho.c
 * Purpose:	Compute errors for soundings based on IHO V4
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "stdtypes.h"
#include "error.h"

#include "device.h"
#include "vessel.h"
#include "sounding.h"
#include "params.h"

#include "errmod.h"
#include "errmod_iho.h"

static char *modname = "errmod_iho";
static char *modrev = "$Revision: 2 $";

#undef __DEBUG__

#define DEFAULT_IHO_ORDER	ERRMOD_IHO_ORDER_1

typedef struct _err_iho {
	ErrModIHOOrder	order;		/* Order of the survey being used */
	f32		fixed_vertical;		/* Fixed fraction of vertical error */
	f32		var_vertical;		/* Variance part of vertical error */
	f32		horizontal;			/* Horizontal part of error, 2d rms */
} ErrIHO;

static ErrIHO default_params;
static Bool default_set = False;

/* Routine:	errmod_iho_make_default
 * Purpose:	Generate the default set of parameters if not already made
 * Inputs:	order	Survey order to make as default
 * Outputs:	-
 * Comment:	This just copies the default parameters into the static parameter
 *			structure used to initialise ones sent to the outside world.
 */

void errmod_iho_make_default(ErrModIHOOrder order)
{
	f64		vert_fx, vert_var, dtm_fix, dtm_var, hor;
	
	if (default_set) return;
	default_params.order = order;
	errmod_get_iho_limits(order, &vert_fx, &vert_var, &dtm_fix, &dtm_var, &hor);
	default_params.fixed_vertical = (f32)(vert_fx * vert_fx);
	default_params.var_vertical = (f32)(vert_var * vert_var);
	default_params.horizontal = (f32)((hor / 1.96) * (hor / 1.96));
	default_set = True;
}

/* Routine:	errmod_iho_construct
 * Purpose:	Generate internal workspace for the IHO based computation engine
 * Inputs:	device	Device in use for making soundings
 *			vessel	Vessel in which device is installed
 * Outputs:	Pointer to workspace (cast to void *) or NULL on error
 * Comment:	-
 */

void *errmod_iho_construct(Device device, Vessel vessel)
{
	ErrIHO	*rtn;
	
	if ((rtn = (ErrIHO*)calloc(1, sizeof(ErrIHO))) == NULL) {
		error_msg(modname, "failed to allocate IHO error workspace.\n");
		return(NULL);
	}
	errmod_iho_make_default(DEFAULT_IHO_ORDER);
	memcpy(rtn, &default_params, sizeof(ErrIHO));
	return((void*)rtn);
}

/* Routine:	errmod_iho_destruct
 * Purpose:	Release memory associated with IHO based computation engine
 * Inputs:	*ws	Pointer to workspace
 * Outputs:	-
 * Comment:	-
 */

void errmod_iho_destruct(void *ws)
{
	ErrIHO	*works = (ErrIHO*)ws;
	free(works);
}

/* Routine:	errmod_iho_compute
 * Purpose:	Carry out computations for IHO based error model
 * Inputs:	plat	Orientation and location of observation platform
 *			data	Soundings structure to be filled in
 *			nSnds	Number of soundings in observation set
 *			*ws		Pointer to private workspace
 * Outputs:	True if computations succeeded, otherwise False
 * Comment:	At present, this always returns True.
 */

Bool errmod_iho_compute(Device device, Vessel vessel, Platform *plat,
						Sounding *data, u32 nSnds, void *ws)
{
	u32	s;
	f32	vert_fx = ((ErrIHO*)ws)->fixed_vertical,
		vert_var = ((ErrIHO*)ws)->var_vertical,
		horiz = ((ErrIHO*)ws)->horizontal;
	
	for (s = 0; s < nSnds; ++s) {
		/* Vertical error */
		data[s].dz = (f32)sqrt(vert_fx + vert_var*data[s].depth*data[s].depth);
		data[s].dz /= 1.96f;			/* Convert 95% CI to standard deviation */
		data[s].dz *= data[s].dz;	/* And convert to variances */
		
		/* Horizontal error */
		data[s].dr = horiz;
	}
	return(True);
}

/* Routine:	errmod_iho_execute_params
 * Purpose:	Execute a parameter list for the IHO based error module
 * Inputs:	*list	List of parameter definitions to work through
 * Outputs:	True if parameters were executed OK, otherwise False
 * Comment:	This accepts either 'iho_order' [1,4] or 'fixed_vert', 'var_vert',
 *			'horizontal' to set things specifically.  It is a mistake to have
 *			both specified.
 */

typedef enum {
	ERRIHO_UNKNOWN = 0,
	ERRIHO_ORDER,
	ERRIHO_VFIXED,
	ERRIHO_VVAR,
	ERRIHO_HORIZ
} ErrIHOPar;

Bool errmod_iho_execute_params(ParList *list)
{
	ParTable	tab[] = {
		{ "order",	ERRIHO_ORDER	},
		{ "vert_fixed",	ERRIHO_VFIXED	},
		{ "vert_var",	ERRIHO_VVAR		},
		{ "horizontal",	ERRIHO_HORIZ	},
		{ NULL,			ERRIHO_UNKNOWN	}
	};
	ParList	*node, *match;
	u32		id, order = ERRMOD_IHO_ORDER_UNKNOWN;
	f32		vert_fx = -1.f, vert_var = -1.f, horiz = -1.f;
	
	node = list;
	do {
		node = params_match(node, "errmod.iho", tab, &id, &match);
		switch (id) {
			case ERRIHO_UNKNOWN:
				/* Matched nothing ... but that may just mean that there's
				 * nothing there for this module.
				 */
				break;
			case ERRIHO_ORDER:
				order = atoi(match->data);
				if (order < 1 || order > 4) {
					error_msgv(modname, "error: IHO survey order should be in"
						" range [1,4] (not %d).\n", order);
					return(False);
				}
				--order;	/* Enum starts at zero */
				match->used = True;
				break;
			case ERRIHO_VFIXED:
				vert_fx = (f32)params_translate_length(match->data);
				if (vert_fx < 0.f) {
					error_msgv(modname, "error: fixed vertical error component"
						" must be positive (not %f).\n", vert_fx);
					return(False);
				}
				match->used = True;
				break;
			case ERRIHO_VVAR:
				vert_var = (f32)atof(match->data);
				if (vert_var < 0.f) {
					error_msgv(modname, "error: variable vertical error "
						"component must be positive (not %f).\n", vert_var);
					return(False);
				}
				match->used = True;
				break;
			case ERRIHO_HORIZ:
				horiz = (f32)params_translate_length(match->data);
				if (horiz < 0.f) {
					error_msgv(modname, "error: horizontal error "
						"component must be positive (not %f).\n", horiz);
					return(False);
				}
				match->used = True;
				break;			
			default:
				error_msgv(modname, "error: unknown return from parameter"
					" matching module (%d).\n", id);
				return(False);
				break;
		}
	} while (node != NULL);
	
	/* Make sure that some default parameter setup has been done */
	errmod_iho_make_default(DEFAULT_IHO_ORDER);
	
	/* If the order has been set, interpret this as the base order that the
	 * user wants, and set up the default variables to that spec.
	 */
	if (order != ERRMOD_IHO_ORDER_UNKNOWN) {
		default_set = False;	/* Reset interlock */
		errmod_iho_make_default((ErrModIHOOrder)order);
#ifdef __DEBUG__
error_msgv(modname, "debug: setting IHO survey order to %d.\n", (u32)order);
#endif
	}
	/* If any of the specific variables have been set, then add them to the
	 * base order definition
	 */
	if (vert_fx >= 0.f) {
		default_params.fixed_vertical = vert_fx * vert_fx;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting fixed vertical error to %f.\n", vert_fx);
#endif
	}
	if (vert_var >= 0.f) {
		default_params.var_vertical = vert_var * vert_var;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting variable vertical error to %f.\n", vert_var);
#endif
	}
	if (horiz >= 0.f) {
		default_params.horizontal = (f32)((horiz/1.96) * (horiz/1.96));
#ifdef __DEBUG__
error_msgv(modname, "debug: setting horizontal error to %f.\n", horiz);
#endif
	}
	return(True);

}
