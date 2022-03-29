/*
 * $Id: params.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:27  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.3  2001/09/23 18:58:00  brc
 * Added call to support specification of memory sizes in parameter files with
 * suitable semi-official multipliers (k, M, G, T) with their correct binary
 * values (i.e., multiples of 2^10 rather than 1000).
 *
 * Revision 1.2  2001/05/14 04:16:49  brc
 * Added prototypes for the translation modules.
 *
 * Revision 1.1  2001/05/13 02:42:15  brc
 * Added facilities to work with a generic parameter file, reading a hierarchically
 * constructed list of modules with string recognition constants for their parameters.
 * The module also has a generic executor so that a command-line user program can
 * have a parameter list passed around all of the modules in the library that
 * are 'params' aware.
 *
 *
 * File:	params.h
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

#ifndef __PARAMS_H__
#define __PARAMS_H__

#include <stdio.h>
#include "stdtypes.h"

/*
 * Type used for parameter list structure.  This is used to hold the data as
 * they are parsed out of the parameter file and are passed around the modules
 * of code.  The user code should indicate which parameters have been used by
 * setting the _used_ tag.  If, after all of the modules have been polled, there
 * are any elements not marked as having been used, an error is issued by the
 * parameter executive.
 *
 */

typedef struct _par_list {
	char				*name;
	char				*data;
	Bool				used;
	struct _par_list	*next;
} ParList, *pParList;

/*
 * Type used to store look-up information for the executive to process.  The
 * modules that want to use params module services must package their lookup
 * list in this form, terminated with an element with name == NULL and tag == 0.
 * The search code here returns only the _tag_ element.
 *
 */

typedef struct {
	char	*name;
	u32		tag;
} ParTable;

/*
 * Function prototype for parameter file execution routine in other modules.
 *
 */

typedef Bool (*ParExecutor)(ParList *list);

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

extern ParList *params_match(ParList *root, const char *prefix, ParTable *table,
					  		 u32 *rtn, ParList **match);

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

extern Bool params_execute(ParList *list);

/* Routine:	params_new_from_file
 * Purpose:	Read a parameter list from ASCII file
 * Inputs:	*name	Name of the file to read
 * Outputs:	Returns True if file read OK, otherwise False.
 *			**list	Set to point to linked list of parameter pairs, or NULL
 *					if none read.
 * Comment:	A file with no parameter descriptions is valid (but wierd).
 */

extern Bool params_new_from_file(const char *name, ParList **list);

/* Routine:	params_release
 * Purpose:	Release dynamic memory associated with the parameter list
 * Inputs:	*list	List to free
 * Outputs:	-
 * Comment:	-
 */

extern void params_release(ParList *list);

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

extern f64 params_translate_length(char *string);

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

extern f64 params_translate_duration(char *string);

/* Routine:	params_translate_rate
 * Purpose:	Translate a number into units of Hz, including MKS scales.
 * Inputs:	*string	String to work from
 * Outputs:	Returns converted rate in Hz, or DBL_MAX on failure.
 * Comment:	This looks for the units 'Hz', and then applies any scale factors
 *			if knows about ('k' = 1000, 'M' = 1e6) before returning the
 *			converted rate.
 */

extern f64 params_translate_rate(char *string);

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

extern u32 params_translate_memory(char *string);

#endif
