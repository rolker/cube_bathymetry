/*
 * $Id: listlines.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.1.2.1  2002/12/15 02:14:47  brc
 * Utility to make a list of lines in the HDCS hierarchy so that the commands
 * that need a list of files as ASCII input can work.  This is a lot easier than
 * trying to generate these with a command line sequence, and a great deal
 * faster too.
 *
 *
 *
 * File:	listlines.c
 * Purpose:	Determine HIPS lines in a directory structure
 * Date:	19 November 2002
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
#include <getopt.h>
#include <time.h>
#include <math.h>

#include "stdtypes.h"
#include "ccom_general.h"
#include "error.h"

static char *modname = "listlines";
static char *modrev = "$Revision: 20 $";

static char *get_rcs_rev(const char *rev)
{
	char *p = strchr(rev, ' ');
	
	if (p != NULL) {
		char *b = strdup(++p);
		if ((p = b) != NULL && (b = strchr(p, ' ')) != NULL) *b = '\0';
	} else p = strdup("1.0");
	return(p);
}

typedef enum {
	BASE_DIR = 1,
	OUTPUT_LINELIST,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("listlines V%s [%s] - Enumerate lines in an HDCS directory.\n",
			p, __DATE__);
	printf("Syntax: linelist [opt] <base_dir><linelist>\n");
	printf(" Directory of HDCS data ----^         ^\n");
	printf(" Output ASCII list of lines ----------'\n");
	printf(" Options:\n");
	printf("  [None]\n");
	printf(" Notes:\n");
	printf("  [None]\n");
}

int main(int argc, char **argv)
{
	int			c;
	FileEntry	*source, *line;
	FILE		*op;
	u32			line_count;

	ccom_log_command_line(modname, argc, argv);

	opterr = 0;
	while ((c = getopt(argc, argv, "h")) != EOF) {
		switch(c) {
			case 'h':
			case '?':
			default:
				fprintf(stderr, "%s: unknown flag '%c'.\n", modname, optopt);
				Syntax();
				return(1);
		}
	}
	argc -= optind-1; argv += optind-1;
	if (argc != ARGC_EXPECTED) {
		Syntax();
		return(1);
	}
	
	if ((source = ccom_enumerate_hips_lines(argv[BASE_DIR])) == NULL) {
		error_msgv(modname, "error: failed to make list of lines in \"%s\".\n",
			argv[BASE_DIR]);
		return(1);
	}
	if ((op = fopen(argv[OUTPUT_LINELIST], "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output of line names.\n",
			argv[OUTPUT_LINELIST]);
		return(1);
	}
	line = source; line_count = 0;
	while (line != NULL) {
		fprintf(op, "%s\n", line->filename);
		++line_count;
		line = line->next;
	}
	fclose(op);
	error_msgv(modname, "info: wrote %d line names to \"%s\".\n",
		line_count, argv[OUTPUT_LINELIST]);

	return(0);
}
