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
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
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
