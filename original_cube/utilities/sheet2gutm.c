/*
 * $Id: sheet2gutm.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.4.2.2  2002/12/15 02:02:13  brc
 * Added check for despiking to ensure that only deep spikes are removed
 * (optionally).  Made the subset option absolute location, rather than relative
 * location with respect to the SW corner of the grid.  Added another dilation
 * to the edge-nibbler code in an attempt to improve read-back (but it's still a
 * little wierd ... and basically non-functional.)
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
 * File:	sheet2gutm.c
 * Purpose:	Convert mapsheets to GUTM output with scaling and NaN.
 * Date:	3 October 2001
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

static char *modname = "sheet2gutm";
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
	OUTPUT_GUTM,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("sheet2gutm V%s [%s] - Convert mapsheets to GUTM output.\n",
			p, __DATE__);
	printf("Syntax: sheet2gutm [opt] <input><output>\n");
	printf(" Input mapsheet ------------^       ^\n");
	printf(" Output GUTM data grid -------------'\n");
	printf(" Options:\n");
	printf("  -d         Extract and save depth surface (default).\n");
	printf("  -u         Extract and save depth uncertainty surface.\n");
	printf("  -n         Extract and save number of hypotheses (if possible).\n");
	printf("  -e <type>  Extract <type> where <type> is:\n");
	printf("               depth   Depth surface (default)\n");
	printf("               uncrt   Uncertainty surface\n");
	printf("               nhypo   Number of hypotheses (if possible)\n");
	printf("               hypstr  Hypothesis strength ratio (if possible)\n");
	printf("  -v         Be verbose about clipping and scaling.\n");
	printf("  -p         Purge data queues before generating surfaces.\n");
	printf("  -r <mslp>  Run de-spiking algorithm before generating output,\n");
	printf("             using <mslp> as maximum allowable slope (degrees).\n");
	printf("  -w <width> Width of de-spiking neighbourhood in nodes.\n");
	printf("  -l         Limit de-spiking to only remove deep spikes.\n");
	printf("  -f params  Specify parameters file for library.\n");
	printf("  -s e,n,w,h Specify a sub-set of data to be extracted.\n");
	printf("  -g file    Dump hypotheses from MapSheet as GDP file in <file>.\n");
	printf("  -m <width> Mask edges in mapsheet to <width> m using an erosion\n");
	printf("             algorithm.\n");
	printf(" Notes:\n");
	printf("  1. Not all depth estimation algorithms can provide all surfaces.\n");
	printf("  2. The extraction sub-set is in meters, UTM; (e,n) is center of section.\n");
	printf("  3. Sub-set parameters apply to hypothesis dump as well as GUTM.\n");
	printf("  4. Sub-setting is done to the nearest estimation node.\n");
	printf("  5. Purging the data queues is something that you shouldn't be\n");
	printf("     doing unless you know what you're doing.\n");
	free(p);
}

void subset_data(void *data, MapSheetDataType type,
				 u32 *n_east, u32 *n_north,
				 f64 *west, f64 *north, f64 de, f64 dn,
				 f64 cen_e, f64 cen_n, f64 width, f64 height)
{
	s32		r0, r1, c0, c1, rows, cols, row;
	u8		*base = data, *src;
	u32		d_size, copy_size, src_step;
	
	c0 = (s32)floor((cen_e - *west - width/2)/de + 0.5);
	c0 = MAX(0, c0);
	c1 = (s32)floor((cen_e - *west + width/2)/de + 0.5);
	c1 = MIN((s32)(*n_east) - 1, c1);
	r0 = (s32)floor((*north - (cen_n + height/2))/dn + 0.5);
	r0 = MAX(0, r0);
	r1 = (s32)floor((*north - (cen_n - height/2))/dn + 0.5);
	r1 = MIN((s32)(*n_north) - 1, r1);	/* Not strictly necessary, but ... */
	
	rows = (r1 - r0) + 1; cols = (c1 - c0) + 1;
		
	if (rows == (s32)(*n_north) && cols == (s32)(*n_east)) return;
	
	switch(type) {
		case MAP_DATA_U8:	d_size = 1; break;
		case MAP_DATA_U16:	d_size = 2; break;
		case MAP_DATA_U32:
		case MAP_DATA_F32:	d_size = 4; break;
		case MAP_DATA_F64:	d_size = 8; break;
		default:
			error_msgv(modname, "error: data type (%d) not known in subset.\n",
				(u32)type);
			return;
	}
	src = base + d_size * r0 * (*n_east);
	copy_size = d_size * cols;
	src_step = d_size * (*n_east);
	for (row = r0; row <= r1; ++row, base += copy_size, src += src_step)
		memcpy(base, src + c0 * d_size, copy_size);

	*north -= r0 * dn;
	*west += c0 * de;
	*n_north = rows;
	*n_east = cols;
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

Bool mask_edges(f32 *data, u32 rows, u32 cols, f32 spacing, f32 mask_width)
{
	u8	*thresh, *mask;
	u32	size, pel, npels = rows*cols;
	f32	invalid_data;
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

	mapsheet_get_invalid(MAP_DATA_F32, &invalid_data);
	
	for (pel = 0; pel < npels; ++pel)
		if (mask[pel] == 0) data[pel] = invalid_data;
	
	free(mask);
	return(True);
}

int main(int argc, char **argv)
{
	int				c;
	Bool			params_reset = False;
	ParList			*parlist = NULL;
	MapSheet		sheet;
	MapSheetElem	type = MAP_DEPTH_ESTIMATE;
	MapSheetDataType	dtype;
	DataType		optype;
	void			*data;
	u32				width, height;
	f64				west, north, de, dn;
	f64				subset_cen_e, subset_cen_n, subset_width, subset_height;	/* Sub-set parameters */
	f32				mask_width = -1.0f;
	Bool			subset = False, purge_tiles = False, deep_only = False;
	char			*hypo_gdp = NULL;
	f64				max_angle = -1.0;
	u32				nhood_width = DEFAULT_NHOOD_WIDTH;
	
	ccom_log_command_line(modname, argc, argv);

	opterr = 0;
	while ((c = getopt(argc, argv, "hdunvf:s:g:pr:w:e:m:l")) != EOF) {
		switch(c) {
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
			case 'e':
				if (strcmp(optarg, "depth") == 0)
					type = MAP_DEPTH_ESTIMATE;
				else if (strcmp(optarg, "uncrt") == 0)
					type = MAP_DEPTH_UNCERTAINTY;
				else if (strcmp(optarg, "nhypo") == 0)
					type = MAP_DEPTH_HYPOTHESES;
				else if (strcmp(optarg, "hypstr") == 0)
					type = MAP_HYPOTHESIS_STRENGTH;
				else {
					error_msgv(modname, "error: data type \"%s\" not known.\n",
						optarg);
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
			case 'g':
				hypo_gdp = strdup(optarg);
				break;
			case 's':
				if (sscanf(optarg, "%lf,%lf,%lf,%lf",
						&subset_cen_e, &subset_cen_n, &subset_width, &subset_height) != 4) {
					error_msgv(modname, "error: failed to parse \"%s\" for"	
						" subset string.\n", optarg);
					return(1);
				}
				subset = True;
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
			case 'd':
				type = MAP_DEPTH_ESTIMATE;
				break;
			case 'u':
				type = MAP_DEPTH_UNCERTAINTY;
				break;
			case 'n':
				type = MAP_DEPTH_HYPOTHESES;
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
	if ((dtype = mapsheet_get_data_type(sheet, type)) == MAP_DATA_ERROR) {
		error_msg(modname, "failed getting data size information.\n");
		return(1);
	}
	if ((data = mapsheet_get_data(sheet, type)) == NULL) {
		error_msg(modname, "failed getting data from mapsheet.\n");
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
	
	if (subset)
		subset_data(data, dtype, &width, &height, &west, &north, de, dn,
			subset_cen_e, subset_cen_n, subset_width, subset_height);

	switch(dtype) {
		case MAP_DATA_U8:	optype = DATA_U8; break;
		case MAP_DATA_U16:	optype = DATA_U16; break;
		case MAP_DATA_U32:	optype = DATA_U32; break;
		case MAP_DATA_F32:	optype = DATA_F32; break;
		case MAP_DATA_F64:	optype = DATA_F64; break;
		default:
			error_msgv(modname, "internal: data type %d unknown.\n",
				(u32)dtype);
			return(1);
	}
	if (mask_width > 0.0 && dtype == MAP_DATA_F32)
		mask_edges((f32*)data, height, width, (f32)de, mask_width);

	if (max_angle >= 0.0 && dtype == MAP_DATA_F32)
		despike_data((f32*)data, height, width, (f32)max_angle, (f32)de, nhood_width, deep_only);
	
	if (!ccom_write_gutm(argv[OUTPUT_GUTM], data, optype, height, width,
			(f32)de, west, north)) {
		error_msgv(modname, "error: failed writing \"%s\".\n",
				   argv[OUTPUT_GUTM]);
		return(1);
	}
	
	if (hypo_gdp != NULL) {
		MapBounds	*bptr = NULL;
		MapBounds	bnds;
		
		if (subset) {
			bnds.west = subset_cen_e - subset_width/2;
			bnds.south = subset_cen_n - subset_height/2;
			bnds.east = bnds.west + subset_width;
			bnds.north = bnds.south + subset_height;
			bptr = &bnds;
		}
		if (!mapsheet_dump_hypo(sheet, hypo_gdp, bptr)) {
			error_msgv(modname, "error: failed writing hypotheses to \"%s\".\n",
				hypo_gdp);
			return(1);
		}
	}
	mapsheet_release(sheet);
	return(0);
}
