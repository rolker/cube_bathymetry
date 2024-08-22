%{
/*
 * $Id: params_lex.l 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:29:27  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.2.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.2  2001/05/14 04:17:48  brc
 * Added TIMESTAMP to the recognition strings so that it doesn't get mis-
 * interpreted as a number with wierdness.
 *
 * Revision 1.1  2001/05/13 02:42:15  brc
 * Added facilities to work with a generic parameter file, reading a hierarchically
 * constructed list of modules with string recognition constants for their parameters.
 * The module also has a generic executor so that a command-line user program can
 * have a parameter list passed around all of the modules in the library that
 * are 'params' aware.
 *
 *
 * File:	params_lex.l
 * Purpose:	Lexer for generic parameter description file format
 * Date:	12 May 2001
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

#include <string.h>
#include <stdlib.h>
#include "stdtypes.h"
#include "params.h"
#include "params_parse.h"

extern int parlinenum;

#undef __DEBUG__

%}

%%

\{	{ return(OBRACE); }
\}	{ return(CBRACE); }
;	{ return(EOS); }

[a-zA-Z][a-zA-Z_0-9]*	{
		if ((parlval.sval = strdup(partext)) == NULL) {
			fprintf(stderr, "lexer: out of memory getting IDENTIFIER space.\n");
			exit(1);
		}
#ifdef __DEBUG__
		fprintf(stderr, "debug: returning IDENTIFIER \"%s\".\n", parlval.sval);
#endif
		return(IDENTIFIER);
	}

[a-zA-Z\/][a-zA-Z0-9\-\/\.]*	{
		if ((parlval.sval = strdup(partext)) == NULL) {
			fprintf(stderr, "lexer: out of memory getting STRING space.\n");
			exit(1);
		}
#ifdef __DEBUG__
		fprintf(stderr, "debug: returning STRING \"%s\".\n", parlval.sval);
#endif
		return(STRING);
	}

[0-9]{2,4}[\/][0-9\/:]*	{
		if ((parlval.sval = strdup(partext)) == NULL) {
			fprintf(stderr, "lexer: out of memory getting TIMESTAMP space.\n");
			exit(1);
		}
#ifdef __DEBUG__
		fprintf(stderr, "debug: returning TIMESTAMP \"%s\".\n", parlval.sval);
#endif
		return(TIMESTAMP);
	}

[-+]?(([0-9]*(\.[0-9]+)?)([eE][-+]?[0-9]+)?) {
			if ((parlval.sval = strdup(partext)) == NULL) {
				fprintf(stderr, "lexer: out of memory getting NUMBER space.\n");
				exit(1);
			}
#ifdef __DEBUG__
			fprintf(stderr, "debug: returning NUMBER \"%s\".\n", parlval.sval);
#endif
			return(NUMBER);
		}

#.*$		{ /* Ignore comments (# to EOL) */ }
\n			{ ++parlinenum; }
[\t ]+		{ /* Ignore whitespace */ }

%%

static int parwrap(void)
{
	return(1);
}
