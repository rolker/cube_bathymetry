/*
 * $Id: arithgutm.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:13  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.1.2.1  2002/12/15 02:06:40  brc
 * Modified version of arithr4 to allow it to be applied to GUTMs.  This is the
 * version that will probably be maintained from now on.
 *
 *
 * File:	arithgutm.c
 * Purpose:	Sum or difference of two GUTM files
 * Date:	22 November 2002
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
#include <math.h>
#include <float.h>
#include "stdtypes.h"
#include "ccom_general.h"
#include "error.h"
#include "mapsheet.h"
#include "errmod.h"

static char *modname = "arithgutm";
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
	GUTM_1 = 1,
	GUTM_2,
	GUTM_OUT,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("arithgutm V%s [%s] - Sum or difference of two GUTM files.\n",
			p, __DATE__);
	printf("Syntax: arithgutm [opt] <input1><input2><output>\n");
	printf(" Input GUTM files ---------^--------^       ^\n");
	printf(" Output GUTM file --------------------------'\n");
	printf(" Options:\n");
	printf("  -d  Compute difference, rather than sum of inputs.\n");
	printf("  -n  Normalise differences to percentage of depth.\n");
	printf("  -a  Compute absolute differences, rather than signed.\n");
	printf(" Notes:\n");
	printf("  1.  The input GUTMs must be of the same physical dimensions,\n");
	printf("      and be in the same place in the world.\n");
	printf("  2.  The default behaviour of the software is to compute the sum\n");
	printf("      of the two surfaces, unless the -d flag is used.\n");
	free(p);
}

void process_surfaces(GUTM *a, GUTM *b, Bool diff)
{
	u32		pel, npels = a->rows * a->cols;
	f32		invalid;
	
	mapsheet_get_invalid(MAP_DATA_F32, &invalid);
	if (diff) {
		for (pel = 0; pel < npels; ++pel) {
			if (a->data[pel] == invalid || b->data[pel] == invalid)
				a->data[pel] = invalid;
			else
				a->data[pel] -= b->data[pel];
		}
	} else {
		for (pel = 0; pel < npels; ++pel) {
			if (a->data[pel] == invalid || b->data[pel] == invalid)
				a->data[pel] = invalid;
			else
				a->data[pel] += b->data[pel];
		}
	}
}

Bool check_surfaces(GUTM *a, GUTM *b)
{
	if (a->rows != b->rows || a->x0 != b->x0 || a->y0 != b->y0 ||
			a->cell_size != b->cell_size) {
		error_msgv(modname, "error: GUTMs are not compatible.\n");
		return(False);
	}
	return(True);
}

void norm_by_depth(GUTM *diff, GUTM *depth)
{
	u32	pel, npels = diff->rows * diff->cols;
	f32	invalid;

	mapsheet_get_invalid(MAP_DATA_F32, &invalid);
	for (pel = 0; pel < npels; ++pel)
		if (depth->data[pel] != invalid && diff->data[pel] != invalid)
			diff->data[pel] = (f32)(100.0*diff->data[pel]/(-depth->data[pel]));
}

void compute_magnitude(GUTM *surf)
{
	u32	pel, npels = surf->rows * surf->cols;
	f32	invalid;

	mapsheet_get_invalid(MAP_DATA_F32, &invalid);
	for (pel = 0; pel < npels; ++pel)
		if (surf->data[pel] != invalid)
			surf->data[pel] = (f32)fabs(surf->data[pel]);
}

int main(int argc, char **argv)
{
	int		c;
	GUTM	*surf1, *surf2;
	Bool	diff = False, norm_to_depth = False, use_absolutes = False;
	
	opterr = 0;
	while ((c = getopt(argc, argv, "hdna")) != EOF) {
		switch(c) {
			case 'd':
				diff = True;
				break;
			case 'n':
				norm_to_depth = True;
				break;
			case 'a':
				use_absolutes = True;
				break;
			case '?':
				fprintf(stderr, "%s: unknown flag '%c'.\n", modname, optopt);
			case 'h':
				Syntax();
				return(1);
		}
	}
	argc -= optind-1; argv += optind-1;
	if (argc != ARGC_EXPECTED) {
		Syntax();
		return(1);
	}
	if ((surf1 = ccom_gutm_read(argv[GUTM_1])) == NULL ||
		(surf2 = ccom_gutm_read(argv[GUTM_2])) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" and \"%s\" for input.\n",
			argv[GUTM_1], argv[GUTM_2]);
		return(1);
	}
	if (!check_surfaces(surf1, surf2)) {
		error_msg(modname, "error: surfaces are not compatible.\n");
		return(1);
	}
	process_surfaces(surf1, surf2, diff);
	if (norm_to_depth) norm_by_depth(surf1, surf2);
	if (use_absolutes) compute_magnitude(surf1);
	if (!ccom_gutm_write(surf1, argv[GUTM_OUT])) {
		error_msgv(modname, "error: failed to write \"%s\" for output.\n",
			argv[GUTM_OUT]);
		return(1);
	}

	return(0);
}
