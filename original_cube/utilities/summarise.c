/*
 * $Id: summarise.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.1.2.1  2002/12/15 02:18:05  brc
 * A utility to extract a number of surfaces from a MapSheet simultaneously,
 * and write them all into a directory with appropriate suffixes.
 *
 *
 * ------------------ log from sheet2gutm until clone --------------------------
 *
 * Revision 1.4.2.1  2002/07/14 02:20:48  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.4  2002/05/10 22:21:27  brc
 * Added facility to mask out the edges of a GUTM being created.  This is done
 * by thresholding the sheet, then eroding the edges using the code in
 * ccom_general.c.  The resulting grid is used as a mask for the GUTM.  This is
 * typically used because the multibeam survey leaves ragged edges where there
 * are badly resolved beams dominating the solution due to low data density at
 * the survey edge.  Also added a new feature to select the surface based on
 * a single parameter flag, rather than having a flag for each surface.  This
 * is mainly to do with having enough letters in the alphabet ... :-)
 *
 * Revision 1.3  2002/03/14 04:51:59  brc
 * Added facility to force purging of queues (mapsheet finalisation) before
 * readback, and to de-spike data.
 *
 * Revision 1.2  2001/12/07 21:30:46  brc
 * Added facility to sub-set the GUTM before writing, which can significantly
 * speed up performance of visualisation.  Also added experimental facility to
 * dump hypotheses to disc from the mapsheet.  This also takes the sub-setting
 * components into account.
 *
 * Revision 1.1  2001/10/06 16:24:26  brc
 * Added sheet2gutm.c to write sheet information into GeoZui3D format using
 * the code in ccom_general.c, and updated makefile to suit.
 *
 *
 * -------------------- log from sheet2tif.c until clone -----------------------
 *
 * Revision 1.8  2001/09/23 19:27:10  brc
 * Added the hypothesis count surface as a readback option.
 *
 * Revision 1.7  2001/08/28 15:57:58  brc
 * Modified code to call parameter reset sequence on mapsheet after the sheet
 * is loaded, iff there is a parameter file specified on the command line.  This
 * makes sure that we do over-ride the parameters in the mapsheet (e.g., to
 * set disambiguation method).
 *
 * Revision 1.6  2001/08/21 01:35:00  brc
 * Added facility to load a parameter file so that the multiple hypothesis
 * disambiguation method can be chosen.
 *
 * Revision 1.5  2001/05/16 21:13:10  brc
 * Added float.h for DBL_MAX, FLT_MAX, etc. under Linux
 *
 * Revision 1.4  2001/05/13 02:37:29  brc
 * Added facility to write to 8bpp images rather than just 16bpp.  This makes
 * transfer to other systems/viewers easier.
 *
 * Revision 1.3  2000/09/24 01:21:33  brc
 * Modifications to deal with new call sequence with sounding interface.
 *
 * Revision 1.2  2000/08/24 15:11:35  brc
 * Modified numerous files to allow the code to compile cleanly under Linux.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	summarise.c
 * Purpose:	Convert mapsheets to GUTMs output with scaling and NaN.
 * Date:	4 September 2002
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
#include <limits.h>
#include <math.h>
#ifndef WIN32
	#include <sys/fcntl.h>
#endif
#include <getopt.h>
#include <float.h>
#include "stdtypes.h"
#include "mapsheet.h"
#include "error.h"
#include "ccom_general.h"
#include "params.h"
#include "stime.h"

static char *modname = "summarise";
static char *modrev = "$Revision: 20 $";

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define DEG2RAD(x) ((x)*M_PI/180.0)
#define RAD2DEG(x) ((x)*180.0/M_PI)

/* Default neighbourhood width for despiking.  Note that this has to be an odd
 * number of pels^H^H^H^Hnodes.
 */
#define DEFAULT_NHOOD_WIDTH	3
#define MIN_NHOOD_WIDTH		3
#define MAX_NHOOD_WIDTH		15

static Bool verbose = False;	/* Easier to get to than passing to routines */

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
	INPUT_SHEET = 1,
	OP_DIRECTORY,
	OP_PREFIX,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("summarise V%s [%s] - Convert MapSheet to GUTMs.\n",
			p, __DATE__);
	printf("Syntax: summarise [opt] <input><output><prefix>\n");
	printf(" Input mapsheet ------------^      ^       ^\n");
	printf(" Output directory -----------------'       |\n");
	printf(" Prefix for output GUTMs ------------------'\n");
	printf(" Options:\n");
	printf("  -v         Be verbose about clipping and scaling.\n");
	printf("  -p         Purge data queues before generating surfaces.\n");
	printf("  -r <mslp>  Run de-spiking algorithm on depth before generating output,\n");
	printf("             using <mslp> as maximum allowable slope (degrees).\n");
	printf("  -w <width> Width of de-spiking neighbourhood in nodes.\n");
	printf("  -l         Limit de-spiking algorithm to remove only deep spikes.\n");
	printf("  -f params  Specify parameters file for library.\n");
	printf("  -m <width> Mask edges in MapSheet to <width> m using an opening\n");
	printf("             algorithm.\n");
	printf(" Notes:\n");
	printf("  1. Not all depth estimation algorithms can provide all surfaces.\n");
	printf("  2. Purging the data queues is something that you shouldn't be\n");
	printf("     doing unless you know what you're doing.\n");
	free(p);
}

Bool despike_data(f32 *data, u32 rows, u32 cols, f32 max_angle, f32 spacing,
				  u32 nhood_width, Bool deep_only)
{
	DespikeStat	s;
	
	if (verbose)
		error_msgv(modname, "info: starting to de-spike data at %s.\n",
			stime_timestamp());
	
	if (!ccom_despike(data, rows, cols, max_angle, spacing, nhood_width, deep_only, &s)) {
		error_msg(modname, "error: failed to despike data.\n");
		return(False);
	}
	
	if (verbose) {
		error_msgv(modname, "info: finished de-spiking data at %s.\n",
			stime_timestamp());
		error_msgv(modname, "info: summary: %d nodes total, %d unused (%d used, "
			"%.2f%%).\n", s.n_points, s.undefined_points, s.n_used_points,
			s.pcnt_defined);
		error_msgv(modname, "info: summary: %d isolated points set to INVALID.\n",
			s.isolated_points);
		error_msgv(modname, "info: summary: %d spikes total (%.2f%% of used pts),"
			" %d shoal (%.2f%%), %d deep (%.2f%%).\n",
			s.n_spikes_removed, s.pcnt_spikes,
			s.shoal_spikes, s.pcnt_shoal, s.deep_spikes, s.pcnt_deep);
	}
	return(True);
}

Bool mask_edges(f32 *data, MapDataBuffer *buffer, u32 rows, u32 cols, f32 spacing, f32 mask_width)
{
	u8	*thresh, *mask;
	u32	size, pel, npels = rows*cols;
	f32	invalid_f32;
	u8	invalid_u8;
	u16	invalid_u16;
	u32	invalid_u32;
	TIFFFlags	flg = { False, False, False, False };
	
	size = (u32)((mask_width+0.5f)/spacing);
	if ((size%2)==0) ++size;
	
	if ((thresh = ccom_threshold(data, rows, cols, True, 0.0f, NULL)) == NULL) {
		error_msg(modname, "error: failed to compute thresholded data set.\n");
		return(False);
	}

#ifdef __DEBUG__
	ccom_write_tiff(NULL, "thresholded.tif", thresh, TIFF_U8, rows, cols, &flg);
#endif

	/* Do erosion followed by dilation at the same size (a.k.a. 'an opening') so
	 * that we remove all small points, but don't move the edges of the area too
	 * much (and don't open up massive holes in the data if there are small gaps).
	 */
	if ((mask = ccom_erode(thresh, rows, cols, size, NULL)) == NULL) {
		error_msg(modname, "error: failed to compute eroded data set.\n");
		free(thresh);
		return(False);
	}
	if (ccom_dilate(mask, rows, cols, size, thresh) == NULL) {
		error_msg(modname, "error: failed to compute dilated data set.\n");
		free(mask); free(thresh);
		return(False);
	}
	free(mask); mask = thresh;

#ifdef __DEBUG__
	ccom_write_tiff(NULL, "mask.tif", mask, TIFF_U8, rows, cols, &flg);
#endif

	mapsheet_get_invalid(MAP_DATA_U8, &invalid_u8);
	mapsheet_get_invalid(MAP_DATA_U16, &invalid_u16);
	mapsheet_get_invalid(MAP_DATA_U32, &invalid_u32);
	mapsheet_get_invalid(MAP_DATA_F32, &invalid_f32);
	
	for (pel = 0; pel < npels; ++pel)
		if (mask[pel] == 0) {
			if (buffer->depth != NULL) buffer->depth[pel] = invalid_f32;
			if (buffer->uncrt != NULL) buffer->uncrt[pel] = invalid_f32;
			if (buffer->hypstr != NULL) buffer->hypstr[pel] = invalid_f32;
			if (buffer->sdev != NULL) buffer->sdev[pel] = invalid_f32;
			if (buffer->nhypo != NULL) buffer->nhypo[pel] = invalid_u8;
			if (buffer->nhits != NULL) buffer->nhits[pel]= invalid_u32;
			if (buffer->backscatter != NULL) buffer->backscatter[pel] = invalid_u16;
		}
	
	free(mask);
	return(True);
}

Bool write_data(char *directory, char *prefix, char *name, void *data, DataType dtype,
				u32 height, u32 width, f64 x0, f64 y0, f64 spacing)
{
	char	*buffer;
	u32		buf_len = strlen(directory)+strlen(prefix)+strlen(name)+8;
	Bool	rc;

	if (data == NULL) return(True);	/* Ignore any data that doesn't exist */

	if ((buffer = (char*)malloc(sizeof(char)*buf_len)) == NULL) {
		error_msgv(modname, "error: failed to get buffer for name construction (%d bytes).\n",
			buf_len*sizeof(char));
		return(False);
	}
	sprintf(buffer, "%s/%s_%s.gutm", directory, prefix, name);
	if (!(rc = ccom_write_gutm(buffer, data, dtype, height, width, (f32)spacing, x0, y0))) {
		error_msgv(modname, "error: failed to write \"%s\" as \"%s\".\n",
			name, buffer);
	}
	free(buffer);
	return(rc);
}

Bool write_density(char *directory, char *prefix, char *name, u32 *nhits,
				   u32 height, u32 width, f64 x0, f64 y0, f64 spacing)
{
	f32		*density, cell_area = (f32)(spacing*spacing);
	u32		pel, npels = height*width;
	Bool	rc;

	if ((density = (f32*)malloc(sizeof(f32)*npels)) == NULL) {
		error_msgv(modname, "error: can't get memory for density buffer (%d pels).\n",
			npels);
		return(False);
	}
	for (pel = 0; pel < npels; ++pel)
		density[pel] = nhits[pel]/cell_area;
	rc = write_data(directory, prefix, name, density, DATA_F32, height, width, x0, y0, spacing);
	free(density);
	return(rc);
}

Bool write_coverage(char *directory, char *prefix, char *name, f32 *depth,
					u32 *nhits, u32 height, u32 width, f64 x0, f64 y0, f64 spacing)
{
	f32		*coverage, cell_area = (f32)(spacing*spacing), target_area;
	u32		pel, npels = height*width;
	Bool	rc;

	if ((coverage = (f32*)malloc(sizeof(f32)*npels)) == NULL) {
		error_msgv(modname, "error: can't get memory for coverage buffer (%d pels).\n",
			npels);
		return(False);
	}
	for (pel = 0; pel < npels; ++pel) {
		/* Specs & Deliverables suggests one sounding for 5.0m + 5% of depth cells is
		 * an adequate level of coverage.  We estimate how close we are to this by
		 * multiplying the sounding density by this target area.
		 */
		target_area = (f32)(5.0 + 0.05*depth[pel]);
		target_area *= target_area;
		coverage[pel] = (nhits[pel]/cell_area)*target_area;
	}
	rc = write_data(directory, prefix, name, coverage, DATA_F32, height, width, x0, y0, spacing);
	free(coverage);
	return(rc);
}

int main(int argc, char **argv)
{
	int				c;
	Bool			params_reset = False, deep_only = False, make_density = False,
					make_coverage = False;
	ParList			*parlist = NULL;
	MapSheet		sheet;
	MapDataBuffer	*buffer;
	u32				width, height;
	f64				west, north, de, dn;
	f32				mask_width = -1.0f;
	Bool			purge_tiles = False;
	f64				max_angle = -1.0;
	u32				nhood_width = DEFAULT_NHOOD_WIDTH;
	
	ccom_log_command_line(modname, argc, argv);
	
	opterr = 0;
	while ((c = getopt(argc, argv, "hvf:pr:w:m:ldc")) != EOF) {
		switch(c) {
			case 'd':
				make_density = True;
				break;
			case 'c':
				make_coverage = True;
				break;
			case 'l':
				deep_only = True;
				break;
			case 'm':
				mask_width = (f32)atof(optarg);
				if (mask_width < 0.0f) {
					error_msgv(modname, "error: mask widths must be +ve.\n");
					return(1);
				}
				break;
			case 'w':
				nhood_width = atoi(optarg);
				if ((nhood_width%2) == 0) {
					error_msgv(modname, "error: de-spiking neighbourhood width"
						" must be an odd number of nodes (not %d).\n",
						nhood_width);
					return(1);
				}
				if (nhood_width < MIN_NHOOD_WIDTH || nhood_width > MAX_NHOOD_WIDTH) {
					error_msgv(modname, "error: de-spiking neighbourhood width"
						" must be in range [%d, %d] (not %d).\n",
						MIN_NHOOD_WIDTH, MAX_NHOOD_WIDTH, nhood_width);
					return(1);
				}
				break;
			case 'r':
				max_angle = atof(optarg);
				if (max_angle < 0.0 || max_angle >= 90.0) {
					error_msgv(modname, "error: maximum slope angle must be"
						" in range [0.0, 90.0).\n");
					return(1);
				}
				break;
			case 'p':
				purge_tiles = True;
				break;
			case 'f':
				if (!params_new_from_file(optarg, &parlist)) {
					error_msgv(modname, "error: failed reading \"%s\" for"
						" parameters.\n", optarg);
					return(1);
				}
				if (!params_execute(parlist)) {
					error_msg(modname, "error: failed executing parameter"
						" list.\n");
					return(1);
				}
				params_reset = True;
				break;
			case 'v':
				verbose = True;
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
	if ((sheet = mapsheet_load_sheet(argv[INPUT_SHEET])) == NULL) {
		error_msgv(modname, "failed to load \"%s\" as mapsheet.\n",
			argv[INPUT_SHEET]);
		return(1);
	}
	if (purge_tiles) mapsheet_finalise(sheet);
	if (params_reset)
		if (!mapsheet_reset_params(sheet)) {
			error_msg(modname, "error: failed to reset parameters to command"
				" line supplied file set.\n");
			return(1);
		}
	if ((buffer = mapsheet_get_all_data(sheet)) == NULL) {
		error_msgv(modname, "error: failed to get data from MapSheet \"%s\".\n",
			argv[INPUT_SHEET]);
		return(1);
	}
	mapsheet_get_width_height(sheet, &width, &height);
	if (verbose)
		error_msgv(modname, "sheet dimensions %d rows, %d cols.\n",
					height, width);
	mapsheet_get_spacing(sheet, &de, &dn);
	mapsheet_get_tl(sheet, &west, &north);
	if (de != dn) {
		error_msgv(modname, "error: GUTM only supports aspect ratio 1:1 (and"
			" you have spacings (de, dn) = (%lf, %lf) m).\n", de, dn);
		return(1);
	}
	
	if (max_angle >= 0.0 && buffer->depth != NULL)
		despike_data(buffer->depth, height, width, (f32)max_angle, (f32)de, nhood_width, deep_only);
	
	if (mask_width > 0.0 && buffer->depth != NULL)
		mask_edges(buffer->depth, buffer, height, width, (f32)de, mask_width);

	if (!write_data(argv[OP_DIRECTORY], argv[OP_PREFIX], "depth", buffer->depth, DATA_F32,
					height, width, west, north, de) ||
		!write_data(argv[OP_DIRECTORY], argv[OP_PREFIX], "uncrt", buffer->uncrt, DATA_F32,
					height, width, west, north, de) ||
		!write_data(argv[OP_DIRECTORY], argv[OP_PREFIX], "hypstr", buffer->hypstr, DATA_F32,
					height, width, west, north, de) ||
		!write_data(argv[OP_DIRECTORY], argv[OP_PREFIX], "nhypo", buffer->nhypo, DATA_U8,
					height, width, west, north, de) ||
		!write_data(argv[OP_DIRECTORY], argv[OP_PREFIX], "nhits", buffer->nhits, DATA_U32,
					height, width, west, north, de) ||
		!write_data(argv[OP_DIRECTORY], argv[OP_PREFIX], "bsctr", buffer->backscatter, DATA_U16,
					height, width, west, north, de) ||
		!write_data(argv[OP_DIRECTORY], argv[OP_PREFIX], "sdev", buffer->sdev, DATA_F32,
					height, width, west, north, de)) {
			error_msg(modname, "error: failed to write one or more components of the data set"
				" from \"%s\" as indicated above.\n");
			return(1);
	}
	if (make_density && buffer->nhits != NULL) {
		write_density(argv[OP_DIRECTORY], argv[OP_PREFIX], "dnsty", buffer->nhits,
			height, width, west, north, de);
	}
	if (make_coverage && buffer->nhits != NULL && buffer->depth != NULL) {
		write_coverage(argv[OP_DIRECTORY], argv[OP_PREFIX], "covr", buffer->depth,
			buffer->nhits, height, width, west, north, de);
	}
	mapsheet_release(sheet);
	mapsheet_release_databuffer(buffer);
	free(buffer);
	return(0);
}
