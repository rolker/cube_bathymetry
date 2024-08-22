/* $Id: libbfr.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1  2002/08/30 21:44:08  dneville
 * Adding some more code needed for the CUBE raw simrad reader
 *
 * Revision 1.5  2000/11/06 04:24:43  brc
 * Added error reporting facility with filter level which can be set globally.
 * This really should be a little more sophisticated so that errors can be
 * redirected somewhere useful, but this'll do for now.
 *
 * Revision 1.4  2000/10/01 23:32:39  brc
 * Fixed bug in ReadTerminated() which resulted in wierdness if realloc() moved
 * the final string when it was resized for return to caller.
 *
 * Revision 1.3  2000/07/11 15:42:38  brc
 * Corrected bug in padn() which caused it to return the *total* number of
 * bytes read, rather than the number read in padn().  Exterior interface says
 * it should be the latter, leading to the number of bytes reported being
 * roughly doubled in the read_*() routines.
 *
 * Revision 1.2  2000/07/11 01:53:49  brc
 * Modified padn() routine so that odd pad lengths are interpreted as `pad to
 * nearest odd length' --- i.e., no pad if we've already read an odd number of
 * bytes, or read one more if current read is even.  This gets around some
 * Simrad wierdnesses with padding on end of image data datagrams when the
 * number of data-points is even.  In their definition `pad to even' means
 * `pad to odd' since the tail is an odd number of bytes ...
 *
 * Revision 1.1.1.1  2000/06/28 18:50:50  brc
 * Compiler for binary file reader code - takes an ASCII description of the
 * data format and generates C-code to read and print it.
 *
 *
 * File:	libbfr.c
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdarg.h>
#include "libbfr.h"

static char *modname = "libbfr";

/* Define size of terminated string buffer expansion quantum */
#define BF_STRING_QUANTUM	256

char *tabs[11] = {
	"", "\t", "\t\t", "\t\t\t", "\t\t\t\t", "\t\t\t\t\t", "\t\t\t\t\t\t",
	"\t\t\t\t\t\t\t", "\t\t\t\t\t\t\t\t", "\t\t\t\t\t\t\t\t\t",
	"\t\t\t\t\t\t\t\t\t\t"
};

static BFFiltLvl filt_level = BFR_FILT_DEBUG; /* Report everything by default */

/* Routine:	padn
 * Purpose:	Reads bytes from file until length is a multiple of given pad size
 * Inputs:	*f		File to read from
 *			nread	Number of elements read from file since last synchronisation
 *			pad		Size of pad units
 * Outputs:	Returns the total number of bytes read from the file in this routine
 * Comment:	This can be used to synchronise to a half-word, word or double-word
 *			boundary, as required.
 */

int padn(FILE *f, int nread, int pad)
{
	u32		read_here = 0;
	
	if (pad % 2 == 0) {
		while (nread % pad != 0) {
			fgetc(f);
			++nread; ++read_here;
		}
	} else if (nread % 2 == 0) {
		fgetc(f);
		++nread; ++read_here;
	}
	return(read_here);
}

/* Routine:	ReadTerminated
 * Purpose:	Read a U8 string until termination character is encountered
 * Inputs:	*f			File to read from
 *			term		Terminator to check for
 *			*tot_read	Total number of bytes read
 * Outputs:	Returns pointer to string, terminated with 'term' and '\0' or NULL; also updates
 *			the count of bytes read from the file so that padding works.
 * Comment:	This will issue warnings if the terminator cannot be found within
 *			BF_STRING_WARN_LIMIT characters, and will terminate at BF_STRING_TERM_LIMIT
 *			characters, in order to avoid running to the end of file for a badly
 *			formated string.
 */

u8 *ReadTerminated(FILE *f, u8 term, int *tot_read)
{
	u8	*string = NULL, *ext;
	int	string_len = 0, index = 0, c, target;

	while ((c = fgetc(f)) != EOF && c != term) {
		if (index >= string_len) {
			/* Extend string by one quantum */
			target = string_len + BF_STRING_QUANTUM;
			if ((ext = (u8*)realloc(string, target)) == NULL) {
				fprintf(stderr, "%s: failed getting memory for string.\n",
					modname);
				free(string);
				return(NULL);
			}
			string = ext;
			string_len = target;
		}
		string[index++] = c;
		if (index == BF_STRING_WARN_LIMIT) {
			fprintf(stderr, "%s: warning: string exceeding %d characters.\n",
				modname, BF_STRING_WARN_LIMIT);
		} else if (index >= BF_STRING_TERM_LIMIT) {
			fprintf(stderr, "%s: error: string exceeding %d characters.  Terminating.\n",
				modname, BF_STRING_TERM_LIMIT);
			break;
		}
	}
	if (string == NULL) return(NULL);
	/* Finally, resize string for termination, and terminate. */
	if ((ext = (u8*)realloc(string, index+1)) == NULL) {
		fprintf(stderr, "%s: could not resize string for termination.\n", modname);
		free(string);
		return(NULL);
	}
	string = ext;	/* Reassign just in case realloc() moved the string */
	string[index++] = '\0';
	*tot_read += index;
		/* N.B.: *not* index-1 in order to account for terminator which isn't retained */
	return(string);
}

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

void PrintTerminated(u8 *string, char *name, FILE *f, int indent)
{
	int		line_indent,	/* Indent after the header has been printed (for aligning subsequent lines) */
			line_len,
			to_print = strlen((char*)string);
	
	fprintf(f, "%s%s =\t", tabs[indent], name);
	line_indent = indent + 1 + (strlen(name)+2)/8;
	line_len = 80 - (8*line_indent + 3);
	fprintf(f, "\"%-.*s\"\n", line_len, string);
	to_print -= line_len;
	string += line_len;
	while (to_print > 0) {
		fprintf(f, "%s\"%-.*s\"\n", tabs[line_indent], line_len, string);
		to_print -= line_len;
		string += line_len;
	}
}

/* Routine:	bfrSetFiltLvl
 * Purpose:	Set default message filter level
 * Inputs:	level	Message reporting level to set
 * Outputs:	-
 * Comment:	This sets the message filter, so that only messages of severity
 *			_level_ or higher are reported.  See libbfr.h for definitions.
 */

void bfrSetFiltLvl(BFFiltLvl level)
{
	filt_level = level;
}

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

u32 bfrFiltText(FILE *f, BFFiltLvl lvl, char *fmt, ...)
{
	va_list	args;
	int		nbytes;
	
	if ((u32)lvl < (u32)filt_level) return(0);
	
	va_start(args, fmt);
	nbytes = vfprintf(f, fmt, args);
	va_end(args);
	
	return(nbytes);
}
