%{
/*
 * $Id: mapsheet_par.y 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:00  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.3.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.3  2001/09/23 20:37:32  brc
 * Added optional backing store clause and recognition token, and upgraded the
 * mapsheet construction code to use the calls with backing store specification.
 *
 * Revision 1.2  2001/08/20 22:38:56  brc
 * Made allocator static so that it doesn't pollute the namespace
 * without due cause.  This shouldn't be a problem now that the mapsheet
 * ASCII reader has moved into the library where it should have been all
 * along...
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
 * File:	mapsheet_par.y
 * Purpose:	YACC source for mapsheet descriptions.
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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "stdtypes.h"
#include "mapsheet.h"
#include "projection.h"
#include "error.h"

#include "mapsheet_private_parser.h"

#define DEG2MET(x) ((x)*60.0*1.0*1852.0)
				/* Converts degrees of arc to meters at equator, approx. */

static const char *modname = "mapsheet_par";

extern int maplex(void);

static SheetList *alloc_sheetlist(void);
extern SheetList *sheets;
void maperror(char *msg);

%}

%union {
	s32			ival;
	f64			fval;
	char		*sval;
	SheetList	*shval;
	SheetDef	sdval;
	SheetVec	vecval;
	Projection	prval;
	Number		numval;
	NumVec		nvecval;
}

%token METERS KILOMETERS DEGREES MINUTES RADIANS PROJECTION SHEET
%token OBRACE CBRACE OBRACKET CBRACKET LISTSEP
%token EOS SPACING LOCATION BOUNDS TYPE ORIGIN FALSE_ORIGIN BACKSTORE
%token <fval> FLOAT
%token <sval> IDENTIFIER
%token <sval> STRING

%type <shval>	file sheet;
%type <prval>	projection;
%type <sdval>	sheetdef;
%type <sval>	typespec;
%type <vecval>	origspec;
%type <vecval>	optfalseo;
%type <vecval>	unit;
%type <sdval>	dimspec;
%type <vecval>	spacespec;
%type <sval>	obackstore;
%type <numval>	num;
%type <nvecval>	numvec;

%%

file:	sheet	{ $1->next = sheets; sheets = $1; }
	|	file sheet { $2->next = sheets; sheets = $2; }
	;

sheet:	SHEET OBRACE projection sheetdef CBRACE
	{
		if (($$ = alloc_sheetlist()) == NULL) {
			maperror("cannot build sheet description structure");
			return(-1);
		}
		if ($4.center.units == Sheet_Meters) {
			/* Attempt to build mapsheet with projected center */
#ifdef __DEBUG__
			error_msgv(modname, "making mapsheet by projected center, "
				"(w,h)=(%lf, %lf) m, deltas = (%lf, %lf) m, center = (%lf, %lf) m\n",
				$4.sizes.x, $4.sizes.y, $4.spacing.x, $4.spacing.y,
				$4.center.x, $4.center.y);
#endif
			$$->sheet = mapsheet_new_by_proj_center_backed(
							$3,
							$4.sizes.x, $4.sizes.y,
							$4.spacing.x, $4.spacing.y,
							$4.center.x, $4.center.y,
							$4.backing_store);
		} else {
			/* Attempt to build mapsheet with lon/lat center */
#ifdef __DEBUG__
			error_msgv(modname, "making mapsheet by geographic center, "
				"(w,h)=(%lf, %lf) m, deltas = (%lf, %lf) m, center = (%lf, %lf) deg\n",
				$4.sizes.x, $4.sizes.y, $4.spacing.x, $4.spacing.y,
				$4.center.x, $4.center.y);
#endif
			$$->sheet = mapsheet_new_by_center_backed(
							$3,
							$4.sizes.x, $4.sizes.y,
							$4.spacing.x, $4.spacing.y,
							$4.center.x, $4.center.y,
							$4.backing_store);
		}
		if ($$->sheet == NULL) {
			maperror("cannot build mapsheet");
			return(-1);
		}
	}
	;

projection:	PROJECTION OBRACE typespec origspec optfalseo CBRACE
	{
#ifdef __DEBUG__
		error_msgv(modname, "projection = %s, o=(%lf,%lf) fo=(%lf,%lf)\n", $3,
			$4.x, $4.y, $5.x, $5.y);
#endif
		if (strcmp($3, "utm") == 0) {
			if ($5.x != 0.0 || $5.y != 0.0)
				maperror("UTM has no user-defined false origin (ignoring)");
			$$ = projection_new_utm($4.x, $4.y);
		} else if (strcmp($3, "mercator") == 0) {
			$$ = projection_new_mercator($4.x, $4.y, $5.x, $5.y);
		} else if (strcmp($3, "polarstereo") == 0) {
			$$ = projection_new_polar_stereo($4.x, $4.y, $5.x, $5.y);
		} else {
			maperror("projection type not recognised");
			return(-1);
		}
		if ($$ == NULL) {
			maperror("cannot create projection method");
			return(-1);
		}
	}
	;

typespec:	TYPE IDENTIFIER EOS
	{
		$$ = $2;
	}
	;

origspec:	ORIGIN OBRACKET FLOAT LISTSEP FLOAT CBRACKET unit EOS
	{
		if ($7.units != Sheet_Degrees) {
			maperror("origin can only be specified in angular units");
			return(-1);
		}
		$$.units = $7.units;
		$$.x = $3 * $7.x; $$.y = $5 * $7.y;
	}
	;

optfalseo:	{ memset(&($$), 0, sizeof(SheetVec)); }
	|	FALSE_ORIGIN OBRACKET FLOAT LISTSEP FLOAT CBRACKET unit EOS
		{
			if ($7.units != Sheet_Meters) {
				maperror("false origin can only be specified in linear units");
				return(-1);
			}
			$$.units = $7.units;
			$$.x = $3 * $7.x; $$.y = $5 * $7.y;
		}
	;

sheetdef:	dimspec spacespec obackstore
	{
		$$ = $1;
		$$.spacing = $2;
		$$.backing_store = $3;
	}
	;

dimspec:
		LOCATION numvec EOS
		{
			if ($2.vec[0].units != $2.vec[1].units) {
				maperror("center locations must have the same units");
				return(-1);
			}
			$$.center.units = $2.vec[0].units;
			$$.center.x = $2.vec[0].value; $$.center.y = $2.vec[1].value;
			$$.sizes.units = Sheet_Meters;
			if ($2.vec[2].units == Sheet_Degrees) {
				maperror("warning: converting degrees to meters at equator");
				$$.sizes.x = DEG2MET($2.vec[2].value);
			} else
				$$.sizes.x = $2.vec[2].value;
			if ($2.vec[3].units == Sheet_Degrees) {
				maperror("warning: converting degrees to meters at equator");
				$$.sizes.y = DEG2MET($2.vec[3].value);
			} else
				$$.sizes.y = $2.vec[3].value;
		}
	|	BOUNDS numvec EOS
		{
			if ($2.vec[0].units != $2.vec[1].units ||
				$2.vec[2].units != $2.vec[3].units ||
				$2.vec[1].units != $2.vec[2].units) {
				maperror("bounds must all be in the same units");
				return(-1);
			}
			$$.center.units = $2.vec[0].units;
			$$.center.x = ($2.vec[0].value+$2.vec[2].value)/2.0;
			$$.center.y = ($2.vec[1].value+$2.vec[3].value)/2.0;
			$$.sizes.units = Sheet_Meters;
			if ($2.vec[0].units == Sheet_Degrees) {
				maperror("warning: converting degrees to meters at equator");
				$$.sizes.x = DEG2MET($2.vec[2].value-$2.vec[0].value);
				$$.sizes.y = DEG2MET($2.vec[3].value-$2.vec[1].value);
			} else {
				$$.sizes.x = $2.vec[2].value - $2.vec[0].value;
				$$.sizes.y = $2.vec[3].value - $2.vec[1].value;
			}
		}
	;

numvec:
	OBRACKET num LISTSEP num LISTSEP num LISTSEP num CBRACKET
	{
		$$.vec[0] = $2;
		$$.vec[1] = $4;
		$$.vec[2] = $6;
		$$.vec[3] = $8;
	}
	;

num:
	FLOAT unit
	{
		$$.units = $2.units;
		$$.value = $1 * $2.x;
	}
	;

spacespec:
	SPACING OBRACKET FLOAT LISTSEP FLOAT CBRACKET unit EOS
	{
		$$.units = Sheet_Meters;
		$$.x = $3 * $7.x; $$.y = $5 * $7.y;
		
		if ($7.units != Sheet_Meters) {
			maperror("warning: converting degrees to meters at equator");
			$$.x *= DEG2MET(1.0);
			$$.y *= DEG2MET(1.0);
		}
	}
	;

unit:	METERS { $$.units = Sheet_Meters; $$.x = $$.y = 1.0; }
	| KILOMETERS { $$.units = Sheet_Meters; $$.x = $$.y = 1000.0; }
	| DEGREES { $$.units = Sheet_Degrees; $$.x = $$.y = 1.0; }
	| MINUTES { $$.units = Sheet_Degrees; $$.x = $$.y = 1/60.0; }
	| RADIANS { $$.units = Sheet_Degrees; $$.x = $$.y = M_PI/180.0; }
	;

obackstore:	{ $$ = NULL; }
	| BACKSTORE STRING EOS { $$ = $2; }
	;
%%

int 		maplinenum = 1;
SheetList	*sheets = NULL;

void maperror(char *msg)
{
	error_msgv(modname, "%s at line %d.\n", msg, maplinenum);
}

static SheetList *alloc_sheetlist(void)
{
	SheetList	*rtn;
	
	if ((rtn = (SheetList*)calloc(1, sizeof(SheetList))) == NULL) {
		maperror("out of memory for sheet structure");
		return(NULL);
	}
	return(rtn);
}
