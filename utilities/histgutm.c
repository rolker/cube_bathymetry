/*
 * $Id: histgutm.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.1.2.1  2002/12/15 02:13:12  brc
 * Histgrams of GUTMs, rather than R4s.
 *
 *
 * File:	histgutm.c
 * Purpose:	Generate binned histograms of GUTM data
 * Date:	25 November 2002
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2002.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <math.h>
#include <getopt.h>
#include <float.h>
#include "stdtypes.h"
#include "ccom_general.h"
#include "error.h"
#include "mapsheet.h"

static char *modname = "histgutm";
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
	INPUT_GUTM = 1,
	OUTPUT_PMF,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("histgutm V%s [%s] - Generate binned histograms of GUTM data.\n",
			p, __DATE__);
	printf("Syntax: histgutm [opt] <gutm><pmf>\n");
	printf(" Input GUTM -------------^    ^\n");
	printf(" Output ASCII pmf est. -------'\n");
	printf(" Options:\n");
	printf("  -w <width>  Set histogram bin width for data.\n");
	free(p);
}

Bool alloc_space(u32 nslots, f32p *ordinate, f32p *pmf)
{
	*ordinate = *pmf = NULL;
	if ((*ordinate = (f32p)malloc(sizeof(f32)*nslots)) == NULL ||
		(*pmf = (f32p)calloc(nslots, sizeof(f32))) == NULL) {
		error_msgv(modname, "failed to allocate 2x%d f32 slots for pmf.\n",
			nslots);
		free(*ordinate); free(*pmf);
		return(False);
	}
	return(True);
}

Bool hist(GUTM *data, f32 width, f32p *ordinate, f32p *pmf, u32p slots)
{
	u32	ord, pel, nused = 0, npels = data->rows * data->cols;
	f32	min, max, offset, scale, invalid;
	
	mapsheet_get_invalid(MAP_DATA_F32, &invalid);
	/* Determine min/max */
	min = FLT_MAX; max = -FLT_MAX;
	error_msg(modname, "info: finding min/max ... "); error_flush_output();
	for (pel = 0; pel < npels; ++pel) {
		if (data->data[pel] == invalid) continue;
		min = (data->data[pel] < min) ? data->data[pel] : min;
		max = (data->data[pel] > max) ? data->data[pel] : max;
	}
	error_msgv(NULL, "min = %f max = %f.\n", min, max);
	
	if (width <= 0.0) {
		/* Set for 2^16 slots */
		*slots = 1<<16;
	} else {
		/* Set slots according to width and min/max */
		*slots = (u32)((max-min)/width);
	}
	offset = min;
	scale = *slots/(max-min);

	if (!alloc_space(*slots, ordinate, pmf)) {
		error_msg(modname, "error: failed allocating memory for PMF.\n");
		return(False);
	}
	/* Set up ordinates */
	for (ord = 0; ord < *slots; ++ord)
		(*ordinate)[ord] = (ord+0.5f)/scale + offset;
	/* Accumulate histogram */
	for (pel = 0; pel < npels; ++pel) {
		if (data->data[pel] == invalid) continue;
		ord = (u32)(scale*(data->data[pel]-offset));
		if (ord >= *slots) continue;
		(*pmf)[ord] += 1.0f;
		++nused;
	}
	/* Normalise histogram */
	for (ord = 0; ord < *slots; ++ord)
		(*pmf)[ord] /= nused/scale;

	return(True);
}

int main(int argc, char **argv)
{
	int			c;
	FILE		*fout;
	GUTM		*input;
	u32			slot, slots;
	f32			*pmf, *ordinate, bin_width = -1.0f;
	Bool		rc = False;
	
	opterr = 0;
	while ((c = getopt(argc, argv, "hw:")) != EOF) {
		switch(c) {
			case 'w':
				bin_width = (f32)atof(optarg);
				if (bin_width <= 0.0f) {
					error_msg(modname, "bin_width must be > 0.0.\n");
					return(1);
				}
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
	if ((input = ccom_gutm_read(argv[INPUT_GUTM])) == NULL) {
		error_msgv(modname, "failed to load \"%s\" as GUTM.\n",
			argv[INPUT_GUTM]);
		return(1);
	}

	if (!hist(input, bin_width, &ordinate, &pmf, &slots)) {
		error_msgv(modname, "error: failed to generate PMF.\n");
		return(1);
	}

	if ((fout = fopen(argv[OUTPUT_PMF], "w")) == NULL) {
		error_msgv(modname, "could not open \"%s\" for output.\n",
				argv[OUTPUT_PMF]);
		return(1);
	}
	for (slot = 0; slot < slots; ++slot)
		fprintf(fout, "%f %f\n", ordinate[slot], pmf[slot]);
	fclose(fout);
	
	return(0);
}
