/*
 * $Id: error.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:27  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.2  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	error.h
 * Purpose:	Central error reporting with redirection.
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

#ifndef __ERROR_H__
#define __ERROR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"

typedef enum {
	FILE_OK = 0,	/* No error */
	FILE_EOF,		/* End of file reached */
	FILE_IOERR,		/* File IO error */
	FILE_NOMEM,		/* No memory for operation */
	FILE_NOTINIT,	/* Buffers or system not initialised */
	FILE_INVALID,	/* Operation or parameters invalid for request */
	FILE_TRYAGAIN,	/* Can't complete at current time */
	FILE_INTERNAL	/* An internal error occured */
} FileError;

typedef struct {
	u32			id;
	const char	*description;
} ErrLookup;

extern void error_msg(const char *name, const char *msg);
extern void error_msgv(const char *name, const char *fmt, ...);
extern void error_set_output(const char *name);
extern void error_flush_output(void);

/* N.B.: table == NULL => search internal table(s) */
extern const char *error_lookup(u32 errorcode, ErrLookup *table);

#ifdef __cplusplus
}
#endif

#endif
