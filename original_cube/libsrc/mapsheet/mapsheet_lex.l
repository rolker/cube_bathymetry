%{
/*
 * $Id: mapsheet_lex.l 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:43  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:00  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.2.2.2  2002/12/15 01:42:14  brc
 * Added underbars in filenames.
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
 * Revision 1.2  2001/09/20 18:47:26  brc
 * Added 'backstore' primitive and STRING element to support the filename that
 * this implies.  Changed to 'map' prefix rather than 'yy' for safety.
 *
 * Revision 1.1  2001/08/11 00:02:20  brc
 * Added mapsheet parser to core code, rather than having it hidden in the
 * utilities section.  This also means that the interface is nicely hidden, and
 * that the user just sees mapsheet_new_from_ascii().
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	mapsheet_lex.l
 * Purpose:	Lexer for simple description of mapsheets
 * Date:	18 July 2000
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
#include "mapsheet_private_parser.h"
#include "mapsheet_par.h"

extern int maplinenum;

int mapwrap(void)
{
	return(1);
}

%}

%%

km			{ return(KILOMETERS); }
m			{ return(METERS); }
deg			{ return(DEGREES); }
min			{ return(MINUTES); }
rad			{ return(RADIANS); }
projection	{ return(PROJECTION); }
sheet		{ return(SHEET); }
\{			{ return(OBRACE); }
\}			{ return(CBRACE); }
\(			{ return(OBRACKET); }
\)			{ return(CBRACKET); }
\,			{ return(LISTSEP); }
;			{ return(EOS); }
spacing		{ return(SPACING); }
location	{ return(LOCATION); }
bounds		{ return(BOUNDS); }
type		{ return(TYPE); }
origin		{ return(ORIGIN); }
falseorg	{ return(FALSE_ORIGIN); }
backstore	{ return(BACKSTORE); }
[-+]?(([0-9]*(\.[0-9]+)?)([eE][-+]?[0-9]+)?) {
		maplval.fval = atof(maptext); return(FLOAT);
		}
[a-zA-Z][a-zA-Z_0-9]*	{
	if ((maplval.sval = strdup(maptext)) == NULL) {
			fprintf(stderr, "lexer: out of memory getting IDENTIFIER space.\n");
			exit(1);
		}
		return(IDENTIFIER);
	}
[a-zA-Z\/][a-zA-Z0-9:_\-\/\.]*	{
		if ((maplval.sval = strdup(maptext)) == NULL) {
			fprintf(stderr, "lexer: out of memory getting STRING space.\n");
			exit(1);
		}
#ifdef __DEBUG__
		fprintf(stderr, "debug: returning STRING \"%s\".\n", maplval.sval);
#endif
		return(STRING);
	}
#.*$		{ /* Ignore comments (# to EOL) */ }
\n			{ ++maplinenum; }
[\t ]+		{ /* Ignore whitespace */ }

%%
