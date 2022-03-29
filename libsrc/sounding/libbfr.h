/* $Id: libbfr.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1  2002/08/30 21:44:08  dneville
 * Adding some more code needed for the CUBE raw simrad reader
 *
 * Revision 1.3  2000/11/06 04:24:43  brc
 * Added error reporting facility with filter level which can be set globally.
 * This really should be a little more sophisticated so that errors can be
 * redirected somewhere useful, but this'll do for now.
 *
 * Revision 1.2  2000/07/13 18:34:32  roland
 * Something was done to help with using in a C++ environment
 *
 * Revision 1.1.1.1  2000/06/28 18:50:50  brc
 * Compiler for binary file reader code - takes an ASCII description of the
 * data format and generates C-code to read and print it.
 *
 *
 * File:	libbfr.h
 * Purpose:	Provide support routines for bfreader derived binary file readers
 * Date:	28 May 2000
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

#ifndef __LIBBFR_H__
#define __LIBBFR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "stdtypes.h"

#define BF_STRING_WARN_LIMIT	512
#define BF_STRING_TERM_LIMIT	1024

typedef enum {
	BFR_PRINT_IDS = 0,		/* Print only the ID of the block */
	BFR_PRINT_HEADERS,		/* Print IDs and header information */
	BFR_PRINT_BLOCKS,		/* Print contents of all blocks, except arrays */
	BFR_PRINT_TERMARRAYS,	/* Print contents of all blocks, including terminated arrays */
	BFR_PRINT_ARRAYSTART,	/* Print contents of all blocks, and first few elements of 1D arrays */
	BFR_PRINT_ALL			/* Print *everything* [ you have been warned! ] */
} BfrPrintLevel;

typedef enum {
	BFR_FILE_END,			/* EOF while searching for a start-of-dgram */
	BFR_READ_ERROR,			/* Generic read error */
	BFR_NO_HEADER,			/* Didn't find a header block within max re-synch distance */
	BFR_DGRAM_READ_FAIL,	/* Failed to read a datagram (cause unknown) */
	BFR_DGRAM_UNKNOWN,		/* Datagram ID was not recognised */
	BFR_INVALID_TAIL,		/* Tail sequence didn't match or wouln't validate */
	BFR_DGRAM_OK			/* Everything read OK */
} BFRtn;

typedef enum {
	BFR_FILT_DEBUG = 0,		/* Debugging messages */
	BFR_FILT_INFO,			/* An informational message */
	BFR_FILT_WARN,			/* A warning about unusual events */
	BFR_FILT_CRIT			/* A critical message */
} BFFiltLvl;

#define BFR_PRINT_ARRAYLEN	10	/* Print first ten array elements */

extern char *tabs[11];	/* Array for arranging printing in reasonable manner */

/* Routine:	padn
 * Purpose:	Reads bytes from file until length is a multiple of given pad size
 * Inputs:	*f		File to read from
 *			nread	Number of elements read from file since last synchronisation
 *			pad		Size of pad units
 * Outputs:	Returns the total number of bytes read from the file
 * Comment:	This can be used to synchronise to a half-word, word or double-word
 *			boundary, as required.
 */

extern int padn(FILE *f, int nread, int pad);

/* Routine:	ReadTerminated
 * Purpose:	Read a U8 string until termination character is encountered
 * Inputs:	*f		File to read from
 *			term	Terminator to check for
 *			*tot_read	Total number of bytes read from file
 * Outputs:	Returns pointer to string, terminated with 'term' and '\0' or NULL;
 *			also updates the total number of bytes read so that padding works.
 * Comment:	This will issue warnings if the terminator cannot be found within
 *			BF_STRING_WARN_LIMIT characters, and will terminate at BF_STRING_TERM_LIMIT
 *			characters, in order to avoid running to the end of file for a badly
 *			formated string.
 */

extern u8 *ReadTerminated(FILE *f, u8 term, int *tot_read);

/* Routine:	PrintTerminated
 * Purpose:	Print a textual description of a terminated array with length control
 * Inputs:	*string		String to print
 *			*name		Name of the variable being printed
 *			*f			File to write output into
 *			indent		Current indent level
 * Outputs:	-
 * Comment:	This assumes that the string being manipulated is terminated with a zero byte (rather
 *			than whatever terminator it was read from the file with).  We basically just print the
 *			string, attempting to break it at the end of a nominal 80 character line, and assuming
 *			that tabs are 8 characters.
 */

extern void PrintTerminated(u8 *string, char *name, FILE *f, int indent);

/* Routine:	bfrSetFiltLvl
 * Purpose:	Set default message filter level
 * Inputs:	level	Message reporting level to set
 * Outputs:	-
 * Comment:	This sets the message filter, so that only messages of severity
 *			_level_ or higher are reported.  See libbfr.h for definitions.
 */

extern void bfrSetFiltLvl(BFFiltLvl level);

/* Routine:	bfrFiltText
 * Purpose:	Print (or ignore) text message depending on filter levels
 * Inputs:	*f		FILE stream to emit on
 *			lvl		Log level of the message
 *			*fmt	Format string for fprintf()
 *			...		Format parameters for fprintf()
 * Outputs:	Returns number of characters actually put to the stream (may be 0)
 * Comment:	This checks the severity of the text message, and prints it if the
 *			level is greater than or equal to the current filter level.
 */

extern u32 bfrFiltText(FILE *f, BFFiltLvl lvl, char *fmt, ...);

#ifdef __cplusplus
}
#endif
#endif
