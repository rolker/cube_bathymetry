/*
 * $Id: params.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:27  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.7.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.7  2002/03/14 04:04:22  brc
 * Added interpolation module to initialisation sequence.
 *
 * Revision 1.6  2001/09/23 18:58:00  brc
 * Added call to support specification of memory sizes in parameter files with
 * suitable semi-official multipliers (k, M, G, T) with their correct binary
 * values (i.e., multiples of 2^10 rather than 1000).
 *
 * Revision 1.5  2001/08/21 00:10:30  brc
 * Added cube() initialisation sequence to call table.
 *
 * Revision 1.4  2001/05/15 01:18:08  brc
 * Modifications to make libccom compile cleanly under Linux.
 *
 * Revision 1.3  2001/05/14 20:46:35  brc
 * Added mapsheet module to call list.
 *
 * Revision 1.2  2001/05/14 04:16:27  brc
 * Added translation routines to map from lengths, durations and rates into
 * uniform MKS units with unit translation and metric multipliers.
 *
 * Revision 1.1  2001/05/13 02:42:15  brc
 * Added facilities to work with a generic parameter file, reading a hierarchically
 * constructed list of modules with string recognition constants for their parameters.
 * The module also has a generic executor so that a command-line user program can
 * have a parameter list passed around all of the modules in the library that
 * are 'params' aware.
 *
 *
 * File:	params.c
 * Purpose:	Parameter reader and parser support code
 * Date:	11 May 2001
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
#include <ctype.h>
#include <limits.h>
#include <float.h>
#include <math.h>
#include "stdtypes.h"
#include "error.h"
#include "ccom_general.h"
#include "params.h"
#include "att.h"
#include "errmod.h"
#include "nav.h"
#include "stime.h"
#include "tides.h"
#include "vessel.h"
#include "sounding.h"
#include "mapsheet.h"
#include "cube.h"
#include "interp.h"

static char *modname = "params";
static char *modrev = "$Revision: 2 $";

static ParExecutor call_table[] = {
	interp_execute_params,
	att_execute_params,
	errmod_execute_params,
	nav_execute_params,
	stime_execute_params,
	tide_execute_params,
	vessel_execute_params,
	sounding_execute_params,
	mapsheet_execute_params,
	cube_execute_params,
	NULL
};

extern ParList *parser_list;
extern FILE *parin;

/* Routine:	params_search_table
 * Purpose:	Search a ParTable for the given tag
 * Inputs:	*tab	Table to search
 *			*name	Final element to search for in table
 * Outputs:	tag of the element matched in the table, or 0 if not found.
 * Comment:	This is simple linear search, and hence may not be massively
 *			efficient for long parameter lists.
 */

static u32 params_search_table(ParTable *tab, const char *name)
{
	u32		elem;
	
	elem = 0;
	while (tab[elem].name != NULL) {
		if (strcmp(name, tab[elem].name) == 0)
			return(tab[elem].tag);
		++elem;
	}
	return(0);
}

/* Routine:	params_get_tail
 * Purpose:	Extract tail element of a composite string
 * Inputs:	*name	Composite string to work with
 * Outputs:	Returns pointer to last element, or name if no separators are found
 * Comment:	-
 */

static char *params_get_tail(char *name)
{
	char	*rtn;
	
	if ((rtn = strrchr(name, '.')) == NULL) return(name);
	return(rtn+1);
}

/* Routine:	params_match
 * Purpose:	Match the first element in the linked list which has the right
 *			prefix for the calling module
 * Inputs:	*root	Start of linked list to work with
 *			*prefix	Module prefix to match at start of tags
 *			*table	Table of elements to match for this module
 * Outputs:	Returns a pointer to the next element of the linked list to search
 *			from for further matches.
 *			*rtn	Tag of element that was matched
 *			*match	ParList element that was matched
 * Comment:	This provides the main user interface to the parameter matching
 *			algorithm.  This code runs down the list of parameters, finding the
 *			first one which matches the user prefix, and returning a pointer to
 *			the next section of the list to search, the element matched (if any)
 *			and the table element match (if any).  When the list of parameters
 *			is exhausted, the code returns NULL.  Note that the code may return
 *			NULL *and* have a matched tag, since the pointer returned is for
 *			the *next* list element to search.
 */

ParList *params_match(ParList *root, const char *prefix, ParTable *table,
					  u32 *rtn, ParList **match)
{
	u32		prefix_len = strlen(prefix), tag;
	
	*rtn = 0;
	*match = NULL;	/* Failsafe initialisation */
	
	while (root != NULL) {
		if (strncmp(prefix, root->name, prefix_len) == 0) {
			/* Matched prefix, check that remaining component is a leaf */
			if (strchr(root->name+prefix_len+1, '.') == NULL) {
				/* Search supplied table for a match */
				if ((tag = params_search_table(table,
										params_get_tail(root->name))) == 0) {
					/* Prefix matches, but no match in table --- problem */
					error_msgv(modname, "warning: \"%s\" was not matched in the"
						" \"%s\" module parameter list.\n", root->name, prefix);
				} else {
					*rtn = tag;
					*match = root;
					return(root->next);
				}
			}
		}
		root = root->next;
	}
	return(NULL);
}

/* Routine:	params_execute
 * Purpose:	Executive code for all of the parameter modules indicated in the
 *			header: supply parameters to all code.
 * Inputs:	*list	Root of the ParList linked list to use for parameters
 * Outputs:	True if all of the parameter in the list were marked `used' by the
 *			various bits of code; otherwise False.
 * Comment:	The error check makes sure that everything that the user specified
 *			in the parameter file was used.  This also makes sure that if the
 *			user mis-typed something, it gets caught.
 *				This module uses the table at the top of the params.c file to
 *			determine which modules in the library are `params' aware.  That is,
 *			which should be offered the list, and which routine in the module
 *			to call to handle it.  It is up to the callees what they do with the
 *			parameter list, although of course, it would be wierd if they didn't
 *			use params_match() to work through the list.  It is up to the
 *			callee code to pass the list to any sub-modules if required.
 */

Bool params_execute(ParList *list)
{
	u32		elem;
	Bool	rtn = True;
	
	elem = 0;
	while (call_table[elem] != NULL) {
		rtn &= (*call_table[elem])(list);
		++elem;
	}
	while (list != NULL) {
		if (list->used == False) {
			error_msgv(modname, "warning: parameter string \"%s\" was not"
				" claimed by any registered module.\n", list->name);
			rtn = False;
		}
		list = list->next;
	}
	return(rtn);
}

/* Routine:	params_reverse_list
 * Purpose:	Apply the patented ultra-cool Reverse-o-Matic list reverser
 * Inputs:	*start	Head of singly linked list to reverse
 * Outputs:	Returns pointer to new head of linked list
 * Comment:	This reverses the order of the linked list passed using an
 *			algorithm so cunning it could be voted Emeritus Professor of
 *			Cunning at Oxford University.
 */

ParList *params_reverse_list(ParList *start)
{
    ParList *root = NULL, *next;

    while (start != NULL) {
        next = start->next; /* Record where we're going after this */
        start->next = root;
        root = start;       /* Append node to new list */
        start = next;       /* Repoint to next item in previous list */
    }
    return(root);
}

/* Routine:	params_new_from_file
 * Purpose:	Read a parameter list from ASCII file
 * Inputs:	*name	Name of the file to read
 * Outputs:	Returns True if file read OK, otherwise False.
 *			**list	Set to point to linked list of parameter pairs, or NULL
 *					if none read.
 * Comment:	A file with no parameter descriptions is valid (but wierd).
 */

int parparse(void *);

Bool params_new_from_file(const char *name, ParList **list)
{
	if ((parin = fopen(name, "r")) == NULL) {
		error_msgv(modname, "failed to open \"%s\" for parameter list.\n",
			name);
		return(False);
	}
	if (parparse(NULL) < 0) {
		error_msgv(modname, "failed parsing \"%s\" for parameters.\n", name);
		return(False);
	}
	*list = params_reverse_list(parser_list);
	parser_list = NULL;
	return(True);
}

/* Routine:	params_release
 * Purpose:	Release dynamic memory associated with the parameter list
 * Inputs:	*list	List to free
 * Outputs:	-
 * Comment:	-
 */

void params_release(ParList *list)
{
	ParList	*next;
	
	while (list != NULL) {
		next = list->next;
		free(list->name);
		free(list);
		list = next;
	}
}

/* Routine:	params_translate_length
 * Purpose:	Translate a number + units into a usable length measurement
 * Inputs:	*string	String to work with
 * Outputs:	Returns length in meters, or DBL_MAX if conversion fails.
 * Comment:	This code understands a number of different units, which are:
 *				'm'		MKS standard meters
 *				'ft'	Imperial feet (1 ft = 12 in = 0.3048m exactly)
 *				'in'	Imperial inches (1 in = 0.0254m exactly)
 *				'nm'	Nautical miles (1 nm = 1852m)
 *			and where appropriate, MKS scales ('u' = 0.000001, 'm' = 0.001,
 *			'c' = 0.01, 'd' = 0.1, 'h' = 100, 'k' = 1000).  Scales are applied
 *			to the number, converted as a double precision float before the
 *			value is returns, so that the value is always in base unit MKS.
 */

f64 params_translate_length(char *string)
{
	f64		rtn;
	char	*tail, *scale;
	
	if (string == NULL || strlen(string) == 0)
		return(DBL_MAX);
	
	rtn = strtod(string, &tail);
	if (tail == string) {
		error_msgv(modname, "cannot extract a number from \"%s\".\n", string);
		return(DBL_MAX);
	}
	/* Now check whether there is anything left to convert into a unit */
	while (isspace(*tail)) ++tail;
	if (strlen(tail) >= 1) {
		/* Something there --- check known units */
		if (strstr(tail, "nm") != NULL) {
			/* Units in Nautical Miles with no scale conversion (needs to check
			 * for this first to make sure that we don't try to match this as
			 * nano-meters!)
			 */
			rtn *= 1852.0;
		} else if (strstr(tail, "ft") != NULL) {
			/* Units in Imperial feet with no scale conversion */
			rtn *= 0.3048;
		} else if (strstr(tail, "in") != NULL) {
			/* Units in Imperial inches with no scale conversion */
			rtn *= 0.0254;
		} else if ((scale = strrchr(tail, 'm')) != NULL) {
			/* MKS Units with scale conversions */
			switch(*--scale) {
				case 'u':	rtn *= 1e-6; break;
				case 'm':	rtn *= 1e-3; break;
				case 'c':	rtn *= 1e-2; break;
				case 'd':	rtn *= 1e-1; break;
				case 'h':	rtn *= 1e02; break;
				case 'k':	rtn *= 1e03; break;
				default:
					if (!isspace(*scale) && !isdigit(*scale)) {
						error_msgv(modname, "error: unrecognised scale for"
							" length conversion (%c).\n", *scale);
						return(DBL_MAX);
					}
					break;
			}
		} else {
			/* Unknown units? */
			error_msg(modname, "unknown units for length conversion.\n");
			return(DBL_MAX);
		}
	}
	return(rtn);
}

/* Routine:	params_translate_duration
 * Purpose:	Translate a number into units of seconds, with optional scaling
 * Inputs:	*string	String to work from
 * Outputs:	Returns converted string as seconds, or DBL_MAX for failure
 * Comment:	This applies suitable conversions to the numerical part of the
 *			string, and then looks for a units string in the remainder.  The
 *			code recognises common MKS multipliers ('u' = 1e-6, 'm' = '1e-3',
 *			'c' = 1e-2) when the units are seconds ('s'), and also recognises
 *			'min' as minutes, and 'hr' as hours.  Other units of time, and
 *			composites (1 hr 10 min) are not allowed.
 */

f64 params_translate_duration(char *string)
{
	f64		rtn;
	char	*tail, *scale;
	
	if (string == NULL || strlen(string) == 0)
		return(DBL_MAX);
	
	rtn = strtod(string, &tail);
	if (tail == string) {
		error_msgv(modname, "cannot extract a number from \"%s\".\n", string);
		return(DBL_MAX);
	}
	/* Now check whether there is anything left to convert into a unit */
	while (isspace(*tail)) ++tail;
	if (strlen(tail) >= 1) {
		/* Something there --- check known units */
		if (strstr(tail, "hr") != NULL) {
			/* Units in hours */
			rtn *= 60.0 * 60.0;
		} else if (strstr(tail, "mn") != NULL) {
			/* Units in minutes */
			rtn *= 60.0;
		} else if ((scale = strstr(tail, "s")) != NULL) {
			/* MKS Units with scale conversions */
			switch(*--scale) {
				case 'u':	rtn *= 1e-6; break;
				case 'm':	rtn *= 1e-3; break;
				case 'c':	rtn *= 1e-2; break;
				default:
					if (!isspace(*scale) && !isdigit(*scale)) {
						error_msgv(modname, "error: unrecognised scale for"
							" durations (%c).\n", *scale);
						return(DBL_MAX);
					}
					break;
			}
		} else {
			/* Unknown units? */
			error_msg(modname, "unknown units for duration conversion.\n");
			return(DBL_MAX);
		}
	}
	return(rtn);
}

/* Routine:	params_translate_rate
 * Purpose:	Translate a number into units of Hz, including MKS scales.
 * Inputs:	*string	String to work from
 * Outputs:	Returns converted rate in Hz, or DBL_MAX on failure.
 * Comment:	This looks for the units 'Hz', and then applies any scale factors
 *			if knows about ('k' = 1000, 'M' = 1e6) before returning the
 *			converted rate.
 */

f64 params_translate_rate(char *string)
{
	f64		rtn;
	char	*tail, *scale;
	
	if (string == NULL || strlen(string) == 0)
		return(DBL_MAX);
	
	rtn = strtod(string, &tail);
	if (tail == string) {
		error_msgv(modname, "cannot extract a number from \"%s\".\n", string);
		return(DBL_MAX);
	}
	/* Now check whether there is anything left to convert into a unit */
	while (isspace(*tail)) ++tail;
	if (strlen(tail) >= 1) {
		/* Something there --- check known units */
		if ((scale = strstr(tail, "Hz")) != NULL) {
			/* OK - units.  Check for multipliers */
			switch(*--scale) {
				case 'k':
					rtn *= 1000.0;
					break;
				case 'M':
					rtn *= 1.0e6;
					break;
				default:
					if (!isspace(*scale) && !isdigit(*scale)) {
						error_msgv(modname, "error: unrecognised scale for"
							" rate conversion (%c).\n", *scale);
						return(DBL_MAX);
					}
					break;
			}
		} else {
			/* Unknown units? */
			error_msg(modname, "unknown units for duration conversion.\n");
			return(DBL_MAX);
		}
	}
	return(rtn);

}

/* Routine:	params_translate_memory
 * Purpose:	Translate a size in memory units into bytes, including customary
 *			units for powers of two.
 * Inputs:	*string	String to work from
 * Outputs:	Returns converted size in bytes, or INT_MAX on failure
 * Comment:	This looks for the units 'b' == bytes, and then applys any scale
 *			factors it knows about ('k' = 2^10, 'M' = 2^20, 'G' = 2^30, 'T' =
 *			2^40) before returning the size.  Note that since the return value
 *			is only a 32-bit unsigned int, anything over about 4G would overflow
 *			and will be rejected.  The conversion is done internally in floating
 *			point, so fractions of a unit can be specified (e.g., '2.5 Mb'), but
 *			the value is rounded down to an integer number of bytes just before
 *			being returned to the user.
 */

u32 params_translate_memory(char *string)
{
	f64		rtn;
	char	*tail, *scale;
	
	if (string == NULL || strlen(string) == 0)
		return(INT_MAX);
	
	rtn = strtod(string, &tail);
	if (tail == string) {
		error_msgv(modname, "cannot extract a number from \"%s\".\n", string);
		return(INT_MAX);
	}

	/* Now check whether there is anything left to convert into a unit */
	while (isspace(*tail)) ++tail;
	if (strlen(tail) >= 1) {
		/* Something there --- check known units */
		if ((scale = strstr(tail, "b")) != NULL) {
			/* OK - units.  Check for multipliers */
			switch(*--scale) {
				case 'k':
					rtn *= 1024.0;
					break;
				case 'M':
					rtn *= 1024.0*1024.0;
					break;
				case 'G':
					rtn *= 1024.0*1024.0*1024.0;
					break;
				case 'T':
					rtn *= 1024.0*1024.0*1024.0*1024.0;
					break;
				default:
					if (!isspace(*scale) && !isdigit(*scale)) {
						error_msgv(modname, "error: unrecognised scale for"
							" rate conversion (%c).\n", *scale);
						return(INT_MAX);
					}
					break;
			}
		} else {
			/* Unknown units? */
			error_msg(modname, "unknown units for duration conversion.\n");
			return(INT_MAX);
		}
	}
	if (rtn < 0.0 || rtn >= INT_MAX) {
		error_msgv(modname, "error: size is out of representable range"
			"[0, %d).\n", INT_MAX);
		return(INT_MAX);
	}
	return((u32)floor(rtn));

}
