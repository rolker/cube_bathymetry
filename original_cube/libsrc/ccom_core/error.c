/*
 * $Id: error.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:27  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.3  2001/02/10 18:04:03  brc
 * Added initialisation of rtn pointer in lookup code to ensure that there is
 * never a bad return.
 *
 * Revision 1.2  2000/08/24 14:43:35  roland
 * Fixing bugs so it will make on linux
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	error.c
 * Purpose:	Centralised error reporting for modules.
 * Date:	04 July 2000
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
#include <stdarg.h>
#include "stdtypes.h"
#include "error.h"

static char *modname = "error";
static char *modrev = "$Revision: 2 $";

static FILE	*curout = NULL;
static Bool	usrfile = False;

static const char *default_error = "no information found";

static ErrLookup file_errors[] = {
	{ (u32)FILE_OK,			"operation completed successfully" },
	{ (u32)FILE_EOF,		"end of file" },
	{ (u32)FILE_IOERR,		"system IO error" },
	{ (u32)FILE_NOMEM,		"insufficient memory to complete operation" },
	{ (u32)FILE_NOTINIT,	"not initialised" },
	{ (u32)FILE_INVALID,	"invalid parameters for operation" },
	{ (u32)FILE_TRYAGAIN,	"can not complete operation at this time" },
	{ (u32)FILE_INTERNAL,	"an internal error occurred" },
	{ (u32)FILE_OK,			NULL }
};

static ErrLookup *error_tables[] = {
	file_errors,
	NULL
};
	
void error_msg(const char *name, const char *msg)
{
    if (curout == NULL) curout = stderr;
	if (name != NULL)
		fprintf(curout, "%s: %s", name, msg);
	else
		fprintf(curout, "%s", msg);
}

void error_msgv(const char *name, const char *fmt, ...)
{
	va_list	args;
	
    if (curout == NULL) curout = stderr;
	va_start(args, fmt);
	if (name != NULL) fprintf(curout, "%s: ", name);
	vfprintf(curout, fmt, args);
	va_end(args);
}

void error_set_output(const char *name)
{
	if (strcmp(name, "stdout") == 0) {
		if (usrfile) fclose(curout);
		usrfile = False;
		curout = stdout;
	} else if (strcmp(name, "stderr") == 0) {
		if (usrfile) fclose(curout);
		usrfile = False;
		curout = stderr;
	} else {
		if (usrfile) fclose(curout);
		if ((curout = fopen(name, "w")) == NULL) {
			fprintf(stderr, "%s: can't open \"%s\" for error output.\n",
					modname, name);
			curout = stderr;
			usrfile = False;
		} else
			usrfile = True;
	}
}

void error_flush_output(void)
{
    if (curout == NULL) curout = stderr;
	fflush(curout);
}

/* Routine:	error_lookup_one
 * Purpose:	Lookup up an error in a specific table
 * Inputs:	errorcode	Code to look for
 *			*table		Table to search in
 * Outputs:	Pointer to description code, or NULL if none found
 * Comment:	-
 */

static const char *error_lookup_one(u32 errorcode, ErrLookup *table)
{
	while (table->description != NULL) {
		if (table->id == errorcode) return(table->description);
		++table;
	}
	return(NULL);
}

/* Routine:	error_lookup
 * Purpose:	Return description for known errors
 * Inputs:	errorcode	Code to look-up
 *			*table		Pointer to specific table to use, or NULL for internals
 * Outputs:	Pointer to string describing the error, or to a default string if
 *			nothing was found.
 * Comment:	String returned is a static string within the module (and users
 *			should also bear this in mind if they provide their own tables).
 */

const char *error_lookup(u32 errorcode, ErrLookup *table)
{
	const char	*rtn = NULL;
	ErrLookup	**tbl;
	
	if (table != NULL)
		rtn = error_lookup_one(errorcode, table);
	else {
		tbl = error_tables;
		while (*tbl != NULL && rtn == NULL)
			rtn = error_lookup_one(errorcode, *tbl++);
	}
	if (rtn == NULL) rtn = default_error;
	return((const char *)rtn);
}
