/*
 * $Id: makesheets.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.1.2.1  2002/12/15 02:16:17  brc
 * Attempt to compute a set of MapSheets to cover a given area, given an initialisation
 * surface for the area.  This computes the depth bands for the area, given the user's
 * input of allowable MapSheet resolutions, and then computes the bounds for a set of
 * MapSheets to tile the area, and the associated masks for assimilate etc. so that only
 * data in the MapSheet's active area are loaded into it.
 *
 *
 * File:	makesheets.c
 * Purpose:	Generate a set of SDFs and masks corresponding to an initialisation surface
 * Date:	18 August 2002
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2002.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <math.h>
#include "stdtypes.h"
#include "error.h"
#include "mapsheet.h"
#include "ccom_general.h"
#include "projection.h"
#include "stime.h"

static char *modname = "makesheets";
static char *modrev = "$Revision: 20 $";

#undef __DEBUG__

#define DEG2RAD(x) ((x)*M_PI/180.0)

#define DEFAULT_BEAMWIDTH	1.5

#define DEFAULT_GRID_SPACINGS	3
static f32 def_grid_spc[DEFAULT_GRID_SPACINGS] = {
	1.0f, 3.0f, 5.0f
};
#define DEFAULT_MAX_RESOLUTION	250.0

/* This next defines the maximum size of the mapsheet that we'll
 * accept as a single entity once we start building tiles.
 */
#define DEFAULT_TGT_SHEET_SIZE	5000
#define MAX_TGT_SHEET_SIZE		10000

/* This next defines the maximum slop that we allow before we break
 * the whole area into more than one component mapsheet, expressed
 * as a percentage of the maximum sheet size above.  This gives us
 * a little hysteresis in the size, so that even if the whole
 * MapSheet is going to be a little bigger than we might want, we'll
 * still accept it, since going to multiple components is a fairly
 * big expense in terms of the organisation that they require.
 */
#define DEFAULT_TGT_SHEET_SLOP		50.0

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
	INIT_GUTM,
	UTM_ZONE,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("makesheets V%s [%s] - Make one or more SDFs based on a GUTM.\n",
			p, __DATE__);
	printf("Syntax: makesheets [opt] <basedir><initgutm><utm_zone>\n");
	printf(" Base directory for project -^         ^         ^\n");
	printf(" Initialisation GUTM for project ------'         |\n");
	printf(" UTM zone in which to work ----------------------'\n");
	printf(" Options:\n");
	printf("  -s          Do projections in the southern hemisphere.\n");
	printf("  -b <bw>     Carry out depth slice computations assuming\n");
	printf("              a beam of <bw> degrees at nadir (default: %.2f deg.)\n",
			DEFAULT_BEAMWIDTH);
	printf("  -r <radius> Set up a dilation radius on the masks being\n");
	printf("              created of at least <radius> m on the ground.\n");
	printf("              The default is to use one cell of the input\n");
	printf("              initialiser, which is usually of sufficiently\n");
	printf("              low resolution for this to be enough overlap.\n");
	printf("  -m <size>   Prefer component MapSheets to be no bigger than\n");
	printf("              <size> estimation nodes along either axis.  Note\n");
	printf("              that the actual size may vary, since the code\n");
	printf("              prefers to make fewer sheets even if larger.\n");
	printf("  -v <slop>   Specify percentage by which the MapSheet size may\n");
	printf("              be varied in order to reduce the total number of\n");
	printf("              MapSheets being generated.\n");
	printf("  -l <res>[,<res>]\n");
	printf("              Specify resolutions of mapsheets to use (in meters).\n");
	free(p);
}

/* Routine:	compute_thresholds
 * Purpose:	Determine the threshold depth for a transition to a particular
 *			estimation node resolution.
 * Inputs:	beamwidth	Beam width of nadir beams on target sonar
 *			grid_dx		Estimation node resolutions to compute thresholds for
 *			n_dx		Number of resolutions in the list
 * Outputs:	*thresh		Output array of depth thresholds
 * Comment:	The thresholds are computed by determining the depth at which the
 *			beam matches the requested grid resolutions.
 */

void compute_thresholds(f32 beamwidth, f32 *grid_dx, u32 n_dx, f32 *thresh)
{
	u32		n;

	for (n = 0; n < n_dx; ++n)
		thresh[n] = (f32)(0.5*grid_dx[n]/tan(DEG2RAD(beamwidth/2.0)));
#ifdef __DEBUG__
	for (n = 0; n < n_dx; ++n)
		error_msgv(modname, "debug: threshold %d at %f m.\n", n, thresh[n]);
#endif
}

/* Routine:	write_sdf
 * Purpose:	Write an SDF text file to given specification
 * Inputs:	*name		Output filename
 *			*init		Pointer to the GUTM structure for geo-referencing
 *			cen_lon		Longitude for the projection clause
 *			cen_lat		Latitude for the projection clause
 *			spacing		Grid resolution to write into the SDF
 * Outputs:	True if file was written, False otherwise
 * Comment:	-
 */

Bool write_sdf(char *name, GUTM *init, f64 cen_lon, f64 cen_lat, f32 spacing)
{
	FILE	*op;
	f64		x1, y1;

	if ((op = fopen(name, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for SDF output.\n",
			name);
		return(False);
	}
	fprintf(op, "# Automatically generated by %s at %s.\n", modname,
		stime_timestamp());
	fprintf(op, "sheet {\n");
	fprintf(op, "\tprojection {\n");
	fprintf(op, "\t\ttype utm;\n");
	fprintf(op, "\t\torigin (%.6lf, %.6lf) deg;\n", cen_lon, cen_lat);
	fprintf(op, "\t}\n");

	x1 = init->x0 + (init->cols-1)*init->cell_size;
	y1 = init->y0 + (init->rows-1)*init->cell_size;

	fprintf(op, "\tbounds (%.2lf m, %.2lf m, %.2lf m, %.2lf m);\n",
		init->x0, init->y0, x1, y1);
	fprintf(op, "\tspacing (%.2f, %.2f) m;\n", spacing, spacing);
	fprintf(op, "\tbackstore BACKSTORE;\n");
	fprintf(op, "}\n");

	fclose(op);
	return(True);
}

/* Routine:	stencil_mask
 * Purpose:	Stencil in the bounds of an SDF into a layout cartoon
 * Inputs:	*d		Cartoon (low resolution outline) to work on
 *			*base	Base GUTM for geo-referencing of the d[...] workspace
 *			*sdf	GUTM for geo-referencing of the SDF to add as a boundary
 * Outputs:	-
 * Comment:	This just digitises the outline of the SDF into the cartoon.
 */

void stencil_mask(u8 *d, GUTM *base, GUTM *sdf)
{
	f64		base_east, base_north;
	s32		west, east, south, north, row, col;

	base_east = base->x0 + (base->cols-1)*base->cell_size;
	base_north = base->y0 + (base->rows-1)*base->cell_size;

	west = (u32)((sdf->x0 - base->x0)/base->cell_size);
	east = (u32)(((sdf->x0+(sdf->cols-1)*sdf->cell_size) - base->x0)/base->cell_size);
	south = (u32)((base_north - sdf->y0)/base->cell_size);
	north = (u32)((base_north - (sdf->y0+(sdf->rows-1)*sdf->cell_size))/base->cell_size);

	if (west < 0) west = 0; if (west >= (s32)base->cols) west = base->cols-1;
	if (east < 0) east = 0; if (east >= (s32)base->cols) east = base->cols-1;
	if (south < 0) south = 0; if (south >= (s32)base->rows) south = base->rows-1;
	if (north < 0) north = 0; if (north >= (s32)base->rows) north = base->rows-1;

	/* Work the west/east edges */
	for (row = north; row < south; ++row) {
		d[row*base->cols+west] = 255;
		d[row*base->cols+east] = 255;
	}

	/* Work the north/south edges */
	for (col = west; col < east; ++col) {
		d[north*base->cols+col] = 255;
		d[south*base->cols+col] = 255;
	}
}

/* Routine:	write_tfw
 * Purpose:	Write a TIFF world file for given coordinates.
 * Inputs:	*name	File to write
 *			res		Cell resolution of the file
 *			x0, y0	Bottom left corner in ground coordinate units
 * Outputs:	True if everything was written correctly, otherwise False
 * Comment:	-
 */

Bool write_tfw(char *name, f32 res, f32 x0, f32 y0)
{
	FILE	*op;

	if ((op = fopen(name, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for TFW output.\n",
			name);
		return(False);
	}
	fprintf(op, "%lf\n0.0\n0.0\n%lf\n%lf\n%lf\n", res, res, x0, y0);
	fclose(op);
	return(True);
}

/* Routine:	fit_component_mapsheets
 * Purpose:	Work out where to fit a set of smaller mapsheets to cover the whole.
 * Inputs:	*basename		Name to use when generating components
 *			*whole			GUTM specification to use for geo-referencing
 *			tgt_sheet_size	Target size, in estimation nodes, to use for a MapSheet
 *			tgt_sheet_slop	Allowable extent of size variation to reduce the sheet count
 *			cen_lon			Projection longitude for component mapsheets
 *			cen_lat			Projection latitude for component mapsheets
 *			spacing			Resolution of the component mapsheets
 *			*mask			Mask image to base the component tiling on
 * Outputs:	True if all of the components were made and written, else False
 * Comment:	This assumes that the _whole_ GUTM also geo-references the _mask_
 *			array (including the spacing of nodes), so that we can use the _mask_
 *			array to shrink-wrap the MapSheet component tiles to minimise the
 *			wasted nodes which are not being used.
 */

Bool fit_component_mapsheets(char *basename, GUTM *whole, u32 tgt_sheet_size, f32 tgt_sheet_slop,
							 f64 cen_lon, f64 cen_lat, f32 spacing, u8 *mask)
{
	char	*buffer;
	GUTM	comp;
	f64		west, east, south, north, comp_width, comp_height, whole_width, whole_height,
			whole_north, whole_east;
	u32		whole_cols, whole_rows, row, col, comp_cols, comp_rows,
			n_comp_cols, n_comp_rows, mask_row, mask_col, start, end;
	Bool	active, rc;
	u8		*cartoon;
	TIFFFlags	flgs = { False, False, False, False };

	if ((buffer = (char*)malloc(strlen(basename)+100)) == NULL) {
		error_msg(modname, "error: failed to get the buffer for component mapsheet names.\n");
		return(False);
	}
	if ((cartoon = (u8*)malloc(sizeof(u8)*whole->cols*whole->rows)) == NULL) {
		error_msg(modname, "error: failed to get the buffer for component cartoons.\n");
		free(buffer);
		return(False);
	}
	memcpy(cartoon, mask, sizeof(u8)*whole->cols*whole->rows);
	for (row = 0; row < whole->rows; ++row)
		for (col = 0; col < whole->cols; ++col)
			cartoon[row*whole->cols+col] /= 2;	/* Cut white down so that bounds show */
	
	/* Check that the basic MapSheet is big enough to warrant splitting */
	whole_width = whole->cell_size*(whole->cols-1);
	whole_height = whole->cell_size*(whole->rows-1);
	whole_cols = (u32)floor(whole_width/spacing)+1;
	whole_rows = (u32)floor(whole_height/spacing)+1;
	if (whole_cols < (1.0+tgt_sheet_slop/100.0)*tgt_sheet_size &&
		whole_rows < (1.0+tgt_sheet_slop/100.0)*tgt_sheet_size) {
		/* We can just generate one SDF */
		sprintf(buffer, "%s.sdf", basename);
		rc = write_sdf(buffer, whole, cen_lon, cen_lat, spacing);
		free(cartoon);
		free(buffer);
		return(rc);
	}

	/*
	 * Determine the approximate size of the element that we are going to generate.
	 */
	n_comp_cols = (u32)floor(whole_cols/tgt_sheet_size);
	n_comp_rows = (u32)floor(whole_rows/tgt_sheet_size);

	/* Note that because we use floor() here, we might have slightly larger sizes than
	 * we would prefer (due to run-off at the east/north ends).  Hence, we need to check
	 * that we're not going to exceed the hysteresis limit before going on.
	 */
	if (whole_cols/n_comp_cols > (1.0+tgt_sheet_slop)*tgt_sheet_size)
		++n_comp_cols;
	if (whole_rows/n_comp_rows > (1.0+tgt_sheet_slop)*tgt_sheet_size)
		++n_comp_rows;

	comp_cols = (u32)ceil(whole_cols/n_comp_cols)+1;	/* Width in estimation node columns */
	comp_rows = (u32)ceil(whole_rows/n_comp_rows)+1;	/* Height in estimation node rows */
	comp_width = (comp_cols-1) * spacing;				/* Width in meters */
	comp_height = (comp_rows-1) * spacing;				/* Height in meters */

	/*
	 * Run over the grid of tiles, working out the location of each, and then reducing
	 * the bounding box to the minimum active area.
	 */
	whole_north = whole->y0 + whole_height;
	whole_east = whole->x0 + whole_width;
	for (row = 0; row < n_comp_rows; ++row) {
		for (col = 0; col < n_comp_cols; ++col) {
			south = whole->y0 + row*comp_height;
			north = whole->y0 + (row+1)*comp_height;
			if (north > whole_north) north = whole_north;
			west = whole->x0 + col*comp_width;
			east = whole->x0 + (col+1)*comp_width;
			if (east > whole_east) east = whole_east;

			/* Shrink the western edge in to the first active area */
			start = (u32)((whole_north - north)/whole->cell_size);
			end = (u32)((whole_north - south)/whole->cell_size);
			do {
				mask_col = (u32)((west - whole->x0)/whole->cell_size);
				active = False;
				for (mask_row = start; mask_row <= end; ++mask_row)
					if (mask[mask_row*whole->cols+mask_col] == 0) {
						active = True;
						break;
					}
				if (!active) west += spacing;
			} while (!active && west < east);
			
			/* Shrink the eastern edge in to the first active area */
			do {
				mask_col = (u32)((east - whole->x0)/whole->cell_size);
				active = False;
				for (mask_row = start; mask_row <= end; ++mask_row)
					if (mask[mask_row*whole->cols+mask_col] == 0) {
						active = True;
						break;
					}
				if (!active) east -= spacing;
			} while (!active && east > west);

			/* Shrink the southern edge in to the first active area */
			start = (u32)((west - whole->x0)/whole->cell_size);
			end = (u32)((east - whole->x0)/whole->cell_size);
			do {
				mask_row = (u32)((whole_north - south)/whole->cell_size);
				active = False;
				for (mask_col = start; mask_col <= end; ++mask_col)
					if (mask[mask_row*whole->cols+mask_col] == 0) {
						active = True;
						break;
					}
				if (!active) south += spacing;
			} while (!active && south < north);

			/* Shrink the north edge in to the first active area */
			do {
				mask_row = (u32)((whole_north - north)/whole->cell_size);
				active = False;
				for (mask_col = start; mask_col <= end; ++mask_col)
					if (mask[mask_row*whole->cols+mask_col] == 0) {
						active = True;
						break;
					}
				if (!active) north -= spacing;
			} while (!active && north > south);

			/* Finally, check that there is something to write, and make the SDF */
			if (west < east && south < north) {
				comp.x0 = west;
				comp.y0 = south;
				comp.cell_size = spacing;
				comp.cols = (u32)ceil((east - west)/spacing) + 1;
				comp.rows = (u32)ceil((north - south)/spacing) + 1;
				sprintf(buffer, "%s-%d-%d.sdf", basename, row, col);
				if (!write_sdf(buffer, &comp, cen_lon, cen_lat, spacing)) {
					error_msgv(modname, "error: failed writing sdf \"%s\".\n",
						buffer);
					free(cartoon);
					free(buffer);
					return(False);
				}
				/* Add the component to the cartoon */
				stencil_mask(cartoon, whole, &comp);
			}
		}
	}
	sprintf(buffer, "%s_cartoon.tif", basename);
	if (!ccom_write_tiff(NULL, buffer, cartoon, TIFF_U8, whole->rows, whole->cols, &flgs)) {
		error_msgv(modname, "error: failed to write initialisation cartoon as \"%s\".\n", buffer);
		free(buffer);
		free(cartoon);
		return(False);
	}
	sprintf(buffer, "%s_cartoon.tfw", basename);
	if (!write_tfw(buffer, whole->cell_size, (f32)whole->x0, (f32)whole->y0)) {
		error_msgv(modname, "error: failed to write initialisation cartoon TFW as \"%s\".\n", buffer);
		free(buffer);
		free(cartoon);
		return(False);
	}
	free(cartoon);
	free(buffer);
	return(True);
}

f32 *parse_resolutions(char *res_string, u32 *n_res)
{
	f32		*rtn;
	u32		n_found = 0;
	char	*tok, *ptr;
	
error_msgv(modname, "debug: resolutions string = \"%s\".\n", res_string);

	tok = res_string;
	while ((ptr = strchr(tok, ',')) != NULL) {
		++n_found;
		tok = ++ptr;
	}
	if (strlen(tok) > 0) ++n_found;
error_msgv(modname, "debug: found %d resolutions.\n", n_found);
	if ((rtn = (f32*)malloc(sizeof(f32)*n_found)) == NULL) {
		error_msgv(modname, "error: failed to get memory for %d resolutions.\n",
			n_found);
		return(NULL);
	}
	n_found = 0;
	tok = strtok(res_string, ",");
	while (tok != NULL) {
		rtn[n_found] = (f32)atof(tok);
		if (rtn[n_found] < 0.0f || rtn[n_found] > DEFAULT_MAX_RESOLUTION ||
			(n_found > 0 && rtn[n_found] < rtn[n_found-1])) {
			error_msgv(modname, "error: failed in converting element %d"
				" of \"%s\" as a valid resolution.\n", n_found, res_string);
			free(rtn);
			return(NULL);
		}
		++n_found;
		tok = strtok(NULL, ",");
	}
	*n_res = n_found;
	return(rtn);
}

int main(int argc, char **argv)
{
	int		c;
	u32		utm_zone, n_pels, pel, spc, n_used, dilate_size;
	Projection	proj;
	GUTM	*init;
	f32		beamwidth = DEFAULT_BEAMWIDTH, invalid_data;
	u8		*level, *mask, *op_mask;
	f64		x0, y0, x1, y1, cen_x, cen_y, cen_lon, cen_lat = 45.0;
	u32		n_spacings = 0,
			tgt_sheet_size = DEFAULT_TGT_SHEET_SIZE;
	f32		*grid_dx, tgt_sheet_slop = DEFAULT_TGT_SHEET_SLOP;
	f32		*thresholds, dilate_radius = -1.0f;
	char	*buffer, *res_string = NULL;
	TIFFFlags	flags = { False, False, False, False };

	ccom_log_command_line(modname, argc, argv);

	opterr = 0;
	while ((c = getopt(argc, argv, "hsb:r:l:m:v:")) != EOF) {
		switch(c) {
			case 'l':
				res_string = strdup(optarg);
				break;
			case 'm':
				tgt_sheet_size = atoi(optarg);
				if (tgt_sheet_size == 0 || tgt_sheet_size > MAX_TGT_SHEET_SIZE) {
					error_msgv(modname, "error: target sheet size should be in"
						" range (0,%d], not %d.\n", MAX_TGT_SHEET_SIZE, tgt_sheet_size);
					return(1);
				}
				break;
			case 'v':
				tgt_sheet_slop = (f32)atof(optarg);
				if (tgt_sheet_slop <= 0.0f || tgt_sheet_slop >= 100.0f) {
					error_msgv(modname, "error: target sheet size slop should be"
						" in range (0.0, 100.0), not %.2f\n", tgt_sheet_slop);
					return(1);
				}
				break;
			case 'r':
				dilate_radius = (f32)atof(optarg);
				break;
			case 'b':
				beamwidth = (f32)atof(optarg);
				if (beamwidth < 0 || beamwidth > 5.0) {
					error_msgv(modname, "error: beamwidth must be in range (0.0, 5.0]"
						" (not %lf deg.).\n", beamwidth);
					return(1);
				}
				break;
			case 's':
				cen_lat = -45.0;
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

	if (res_string == NULL) {
		/* No resolutions were specified on the command line, so we need to copy in
		 * the default set.
		 */
		if ((grid_dx = (f32*)malloc(sizeof(f32)*DEFAULT_GRID_SPACINGS)) == NULL) {
			error_msg(modname, "error: failed to get memory for the set of default"
				" grid spacings.\n");
			return(1);
		}
		memcpy(grid_dx, def_grid_spc, sizeof(f32)*DEFAULT_GRID_SPACINGS);
		n_spacings = DEFAULT_GRID_SPACINGS;
	} else {
		if ((grid_dx = parse_resolutions(res_string, &n_spacings)) == NULL) {
			error_msgv(modname, "error: could not parse \"%s\" for grid resolutions.\n",
				res_string);
			return(1);
		}
	}
	if ((thresholds = (f32*)malloc(sizeof(f32)*n_spacings)) == NULL) {
		error_msgv(modname, "error: failed to get memory for depth thresholds.\n");
		return(1);
	}

	utm_zone = atoi(argv[UTM_ZONE]);
	cen_lon = -177.0 + (utm_zone-1)*6.0;
	if ((proj = projection_new_utm(cen_lon, cen_lat)) == NULL) {
		error_msgv(modname, "error: can't make projection for UTM zone %d"
				" (projection centered at (%lf, %lf) deg.)\n",
				utm_zone, cen_lon, cen_lat);
		return(1);
	}

	if ((buffer = (char*)malloc(strlen(argv[BASE_DIR])+strlen(argv[INIT_GUTM])+40)) == NULL) {
		error_msg(modname, "error: failed to get buffer space for"
				" for initialisation names.\n");
		return(1);
	}

	sprintf(buffer, "%s/init/%s", argv[BASE_DIR], argv[INIT_GUTM]);
	if ((init = ccom_gutm_read(buffer)) == NULL) {
		error_msgv(modname, "error: cannot open \"%s\" for initialisation.\n", buffer);
		return(1);
	}

	x0 = init->x0; y0 = init->y0;
	x1 = init->x0 + (init->cols-1)*init->cell_size;
	y1 = init->y0 + (init->rows-1)*init->cell_size;

	cen_x = (x1 + x0)/2.0; cen_y = (y1 + y0)/2.0;

#ifdef __DEBUG__
	error_msgv(modname, "debug: cen_x = %lf cen_y = %lf\n", cen_x, cen_y);
#endif

	projection_en_to_ll_deg(proj, cen_x, cen_y, &cen_lon, &cen_lat);
	
	compute_thresholds(beamwidth, grid_dx, n_spacings, thresholds);

	n_pels = init->rows*init->cols;
	if ((level = (u8*)calloc(n_pels, sizeof(u8))) == NULL ||
		(mask = (u8*)calloc(n_pels, sizeof(u8))) == NULL ||
		(op_mask = (u8*)calloc(n_pels, sizeof(u8))) == NULL) {
		error_msg(modname, "error: failed to get buffer for threshold levels and masks.\n");
		return(1);
	}

	/* Compute which depth range the cell falls into */
	mapsheet_get_invalid(MAP_DATA_F32, &invalid_data);
	for (pel = 0; pel < n_pels; ++pel) {
		while (level[pel] < n_spacings-1 && init->data[pel] != invalid_data &&
					-(init->data[pel]) > thresholds[level[pel]])
			++level[pel];
		if (init->data[pel] == invalid_data) level[pel] = n_spacings;
	}

	/* Now, for each cell spacing, generate an SDF file and mask */
	if (dilate_radius < 0.0f)
		dilate_size = 3;	/* Minimum dilation structuring element that does something */
	else {
		dilate_size = (u32)floor((2.0*dilate_radius)/init->cell_size + 0.51);
		if (dilate_size < 3) dilate_size = 3;
	}

	for (spc = 0; spc < n_spacings; ++spc) {
		n_used = 0;
		for (pel = 0; pel < n_pels; ++pel) {
			if (level[pel] == spc) {
				mask[pel] = 1;
				++n_used;
			}
		}
		if (n_used > 0) {
			sprintf(buffer, "%s/sdfs/%dm", argv[BASE_DIR], (u32)grid_dx[spc]);
			/* Now, we need to dilate the mask so that we get some overlap between the
			 * different levels, and hence ensure that there is no gap when the surfaces
			 * are reconstructed as a composite entity.
			 */
			ccom_dilate(mask, init->rows, init->cols, dilate_size, op_mask);
			for (pel = 0; pel < n_pels; ++pel)
				op_mask[pel] = 255 - 255*op_mask[pel];
			sprintf(buffer, "%s/init/%dm.tif", argv[BASE_DIR], (u32)grid_dx[spc]);
			if (!ccom_write_tiff(NULL, buffer, op_mask, TIFF_U8, init->rows, init->cols, &flags)) {
				error_msgv(modname, "error: failed to write mask \"%s\".\n", buffer);
				return(1);
			}
			sprintf(buffer, "%s/sdfs/%dm", argv[BASE_DIR], (u32)grid_dx[spc]);
			if (!fit_component_mapsheets(buffer, init, tgt_sheet_size, tgt_sheet_slop, cen_lon, cen_lat, grid_dx[spc], op_mask)) {
				error_msgv(modname, "error: failed to write SDF file(s) to base \"%s\".\n", buffer);
				return(1);
			}
		}
		memset(mask, 0, sizeof(u8)*n_pels);
	}

	free(mask);
	free(level);
	free(buffer);

	return(0);
}
