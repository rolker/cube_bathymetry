%{
/*
 * $Id: params_parse.y 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:29:35  dneville
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
 * Revision 1.2  2001/05/14 04:18:18  brc
 * Added TIMESTAMP terminal and added to data non-terminal as another possible.
 *
 * Revision 1.1  2001/05/13 02:42:15  brc
 * Added facilities to work with a generic parameter file, reading a hierarchically
 * constructed list of modules with string recognition constants for their parameters.
 * The module also has a generic executor so that a command-line user program can
 * have a parameter list passed around all of the modules in the library that
 * are 'params' aware.
 *
 *
 * File:	param_parse.y
 * Purpose:	Bison source for parameter file parser
 * Date:	11 May 2001
 * Notes:	This has to be compiled with bison -p par so that the externals are
 *			set to parparse(), parerror(), etc.
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
#include "stdtypes.h"
#include "params.h"
#include "error.h"

static char *modname = "param_parse";

extern int parlex(void);

int 	parlinenum = 1;
ParList	*parser_list = NULL;

void parerror(const char *msg)
{
	error_msgv(modname, "%s at line %d.\n", msg, parlinenum);
}

Bool par_encapsulate_list(char *id, ParList *subset)
{
	ParList	*node = subset;
	char	*buffer;
	u32		id_len = strlen(id), reqd;
	
	while (node != NULL) {
		reqd = id_len + 1 /* . */ + strlen(node->name) + 1 /* \0 */;
		if ((buffer = (char*)malloc(reqd)) == NULL) {
			parerror("out of memory while encapsulating parameter subset list");
			return(False);
		}
		sprintf(buffer, "%s.%s", id, node->name);
		free(node->name);
		node->name = buffer;
		node = node->next;
	}
	return(True);
}

void par_append_list(ParList *base, ParList *ext)
{
	ParList	*node = base;
	
	if (ext == NULL) return;	/* No list to append! */
	if (base == NULL) return;	/* Nothing in base list */
	
	while (node->next != NULL) node = node->next;
	node->next = ext;
}

ParList *par_make_param(char *id, char *data)
{
	ParList	*rtn;
	
	if ((rtn = (ParList*)calloc(1, sizeof(ParList))) == NULL) return(NULL);
	rtn->name = id;
	rtn->data = data;
	rtn->used = False;
	rtn->next = NULL;
	return(rtn);
}

char *par_make_comp_num(char *num, char *unit)
{
	char	*rtn;
	u32		reqd;
	
	if (unit == NULL) return(num);
	reqd = strlen(num) + 1 /* ' ' */ + strlen(unit) + 1 /* \0 */;
	if ((rtn = (char*)malloc(reqd)) == NULL) return(NULL);
	sprintf(rtn, "%s %s", num, unit);
	free(num);
	free(unit);
	return(rtn);
}
	
%}

%union {
	char	*sval;
	ParList	*pval;
}

%token OBRACE CBRACE EOS
%token <sval> IDENTIFIER
%token <sval> STRING
%token <sval> NUMBER
%token <sval> TIMESTAMP

%type <pval>	file;
%type <pval>	paramset;
%type <pval>	paramlist;
%type <pval>	param;
%type <sval>	data;
%type <sval>	optunit;

%%

file:		paramset {
				parser_list = $1;
			}
	|		file paramset {
				par_append_list($2, parser_list);
				parser_list = $2;
			}
	;

paramset:	IDENTIFIER OBRACE paramlist CBRACE {
				if (!par_encapsulate_list($1, $3)) return(-1);
				$$ = $3;
			}
		;

paramlist:	{
				$$ = NULL;
			}
		|	paramlist paramset {
				par_append_list($2, $1);
				$$ = $2;
			}
		|	paramlist param {
				$2->next = $1;
				$$ = $2;
			}
		;

param:		IDENTIFIER data EOS {
				if (($$ = par_make_param($1, $2)) == NULL) {
					parerror("out of memory making parameter list");
					return(-1);
				}
			}
	;

data:		STRING {
				$$ = $1;
			}
	|		IDENTIFIER {
				$$ = $1;
			}
	|		TIMESTAMP {
				$$ = $1;
			}
	|		NUMBER optunit {
				if (($$ = par_make_comp_num($1, $2)) == NULL) {
					parerror("out of memory making data element");
					return(-1);
				}
			}
	;

optunit:	{
				$$ = NULL;
			}
	|		IDENTIFIER {
				$$ = $1;
			}
	;
