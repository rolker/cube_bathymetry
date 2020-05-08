/*
 * $Id: initsheet.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.3.2.4  2003/02/12 02:58:56  brc
 * Added facility to specify a geo-referencing GUTM for the mask
 * image directly.  This allows the mask to be properly referenced
 * even if it wasn't constructed from the surface that we are now
 * using for initialisation.  This can occur if the initialisation surface
 * has to be built incrementally due to, e.g., there being no extant
 * survey or chart in the area, or the surface being refined as the
 * survey progresses.
 *
 * Revision 1.3.2.3  2002/12/15 01:57:53  brc
 * Added facility to scale masks to the appropriate location and resolution
 * before using them.  This ensures that we can use lower resolutions masks
 * for high resolution MapSheet construction.  Also corrected scaling bug
 * the was using 95% confidence, rather than converting to variance before
 * using the data for initialisation.
 *
 * Revision 1.3.2.2  2002/08/15 18:17:16  brc
 * Removed internal GUTM routines in favour of the ones in
 * ccom_general.c, and updated the GUTM writer to use the
 * correct specifications for the coordinates so that the product
 * GUTMs don't go wandering all over the place!
 *
 * Revision 1.3.2.1  2002/08/15 18:08:19  brc
 * Updates to make the windows port compile and operate on Unix, and to make
 * sure that blendsurfaces is added to the project.
 *
 * Revision 1.3  2002/05/10 22:16:28  brc
 * Multiple modifications based on new experience with the way that MapSheets
 * are likely to be used, and the Snow Passage verification project.
 * 1.  Initialisation is now done based on GUTMs alone, rather than using R4s.
 * There is just a little too much confusion over the georeferencing of R4s to
 * be certain what is going on.  Although GUTMs are more verbose, they are much
 * better defined.  This resulted in a lot of general GUTM code being built in
 * this file, rather than being put into the rest of the library where it
 * should be (probably as a GUTM module?)  This is unsatisfactory, but is being
 * retained for the present since a code release push is required.
 * 2. Added the facility to initialise the uncertainty component of a MapSheet
 * based on another GUTM, or based on a composite GUTM (with the newer
 * USE_ALTERNATE_DATA clause and two columns of data).
 * 3. Added the facility to write out the initialisation surface, so that the
 * user doesn't have to immediately turn around and sheet2gutm the new
 * MapSheet.  This can also make re-initialisation easier after the first
 * pass, since the data will already have been interpolated and integrated (q.v.)
 * 4. Added the facility to include another GUTM for secondary initialisation.
 * This is used where high resolution data is available in most of the area
 * (e.g., a multibeam survey), but lower resolution (or less accurate) data is
 * available elsewhere (e.g., a singlebeam survey which was subsequently
 * TINed).  The primary surface is used whenever possible, and the secondary
 * surface everywhere the primary is not defined.
 * 5. Added the facility to specify the algorithm being used in the MapSheet.
 * 6. Added the facility to specify the order of survey being used.
 *
 * Revision 1.2  2001/05/15 01:18:08  brc
 * Modifications to make libccom compile cleanly under Linux.
 *
 * Revision 1.1  2001/04/10 23:31:21  brc
 * Initialisation code using TIFF files to set up a mapsheet structure for
 * further update.
 *
 *
 * File:	initsheet.c
 * Purpose:	Initialise a mapsheet from R4 plus uncertainty
 * Date:	21 March 2001
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
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
#include "ccom_general.h"
#include "error.h"
#include "mapsheet.h"
#include "omg.h"
#include "stime.h"

static char *modname = "initsheet";
static char *modrev = "$Revision: 20 $";

#define DEFAULT_IHO_SURVEY_ORDER ERRMOD_IHO_ORDER_1
#define C95_SCALE	1.96

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
	INITIALISER,
	OUTPUT_SHEET,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("initsheet V%s [%s] - Initialise a mapsheet from GUTM plus "
		   "optional uncertainty.\n", p, __DATE__);
	printf("Syntax: initsheet [opt] <mapin><gutm><mapout>\n");
	printf(" Input mapsheet (ASCII) ---^     ^       ^\n");
	printf(" Input GUTM depths/opt. unct. ---'       |\n");
	printf(" Output mapsheet ------------------------'\n");
	printf(" Options:\n");
	printf("  -f <config>    Configure library using <config>.\n");
	printf("  -a <alg>       Set depth estimation algorithm to be <alg>:\n");
	printf("                    mean   Binned trimmed mean estimation.\n");
	printf("                    median Binned median estimation.\n");
	printf("                    nodal  Pre-cube nodal estimation.\n");
	printf("                    cube   HyperCUBE (default).\n");
	printf("  -u <unct>      Specify uncertainty to use if the GUTM on the\n");
	printf("                 command line doesn't support it as part of the\n");
	printf("                 data format via USE_ALTERNATE_DATA.\n");
	printf("  -p             Interpret <unct> as a percentage of depth (in\n");
	printf("                 range (0.0,100.0)) rather than as a fixed 95%%\n");
	printf("                 confidence interval in meters.\n");
	printf("  -m <mask.tif>  Use TIFF file to mask out areas not to be\n");
	printf("                 updated with new data.\n");
	printf("  -g <gutm>      Use <gutm> as geo-referencing for <mask.tif>\n");
	printf("                 (Default is to use the geo-referencing from the\n");
	printf("                 primary initialisation surface GUTM.)\n");
	printf("  -i order       Set IHO survey order for estimation accuracy "
						  "(default: %d).\n", 1+(u32)DEFAULT_IHO_SURVEY_ORDER);
	printf("  -s <gutm>      Use <gutm> to provide data where the command\n");
	printf("                 line initialisation GUTM doesn't.\n");
	printf("  -w <init>      Write final initialisation surface to <init>.\n");
	printf(" Notes:\n");
	printf("  1. The mask image must be a U8 TIFF with white (255)\n");
	printf("     indicating areas where no update should be done.  Other\n");
	printf("     areas are initialised with uncertainties as specified on\n");
	printf("     command line.  Note that the data may also have 'holes'\n");
	printf("     (typically set to 0.0 m) which imply that no data is\n");
	printf("     available to initialise the sheet.  Combining these two\n");
	printf("     conditions allows for areas of the sheet to be over-ridden\n");
	printf("     by the user, or fixed at a particular depth, etc.\n");
	printf("  2. The GUTMs supplied can be simple depths, or can specify the\n");
	printf("     uncertainty required on a node-by-node basis through the\n");
	printf("     USE_ALTERNATE_DATA modification in the GUTM specification.\n");
	printf("     Note that if no alternative data is provided, then the user\n");
	printf("     must supply an initialisation uncertainty via the -u flag.\n");
	printf("     If an uncertainty surface is supplied, it should be the 95%%\n");
	printf("     CI in meters, rather than variance or std. deviation.\n");
	printf("  3. The uncertainty specified is, by default, a fixed 95%% CI\n");
	printf("     in meters (irrespective of depth).  Using the -p flag sets\n");
	printf("     the code to use <unct> as a percentage of the initialisation\n");
	printf("     depth coming from the command line GUTM.\n");
	printf("  4. The code here will use bilinear interpolation to re-sample\n");
	printf("     the data surfaces to the sample spacing of the mapsheet,\n");
	printf("     and/or the correct resolution.  Note that in the case where\n");
	printf("     the uncertainty is specified in the GUTM, uncertainty will\n");
	printf("     also be interpolated, which might not be the correct thing\n");
	printf("     to do under all circumstances.  In this case, an external\n");
	printf("     solution might be a better choice.\n");
	free(p);
}

/* Routine:	gutm_interpolate
 * Purpose:	Interpolate GUTM data using bilinear interpolation
 * Inputs:	sheet	MapSheet (target for interpolation)
 *			*gutm	Pointer to a GUTM structure
 * Outputs:	Pointer to interpolated GUTM on success, or NULL otherwise
 * Comment:	This interpolates the data present in the GUTM raster to the right
 *			size for use in initialisation of the mapsheet.
 */

GUTM *gutm_interpolate(MapSheet sheet, GUTM *gutm)
{
	f64		in_west, in_east, in_north, in_south;
	f64		out_west, out_east, out_north, out_south;
	f64		in_dn, in_de, out_de, out_dn;
	f64		e, n, delta_x, delta_y;
	s32		c, r;
	u32		row, col, cols, rows, n_valid = 0, n_outside = 0;
	f32		invalid_data;
	GUTM	*rtn;
	Bool	alt_data = False;
	
	error_msgv(modname, "info: starting GUTM interpolation at %s.\n",
		stime_timestamp());
	
	/* Calibrate from MapSheet */
	mapsheet_get_width_height(sheet, &cols, &rows);
	mapsheet_get_spacing(sheet, &out_de, &out_dn);
	mapsheet_get_bl(sheet, &out_west, &out_south);
	mapsheet_get_tr(sheet, &out_east, &out_north);
	
	in_dn = in_de = gutm->cell_size; /* GUTM only supports square pixels */
	in_west = gutm->x0;
	in_east = in_west + (gutm->cols-1)*in_de;
	in_south = gutm->y0;
	in_north = in_south + (gutm->rows-1)*in_dn;

	if (gutm->altdata != NULL) alt_data = True;
	
	error_msgv(modname, "info: interpolating from (%dx%d) -> (%dx%d), from"
		" %.1f m grid to %.1f m grid.\n", gutm->rows, gutm->cols, rows, cols,
		in_de, out_de);

	if ((rtn = ccom_gutm_new(cols, rows, (f32)out_de, out_west, out_south, gutm->z0,
														alt_data)) == NULL) {
		error_msgv(modname, "error: failed to allocate buffer for new GUTM"
			" (%dx%d cells, size %.2f m).\n", cols, rows, out_de);
		return(NULL);
	}
	
	mapsheet_get_invalid(MAP_DATA_F32, &invalid_data);
	for (row = 0; row < rows; ++row) {
		n = out_north - row*out_dn;
		for (col = 0; col < cols; ++col) {
			e = out_west + col * out_de;
			if (e > in_west && e < in_east && n > in_south && n < in_north) {
				c = (s32)floor((e - in_west)/in_de);
				r = (s32)floor((in_north - n)/in_dn);
				/* Check that all corner points are available for interp */
				if (gutm->data[r*gutm->cols + c] == invalid_data ||
					gutm->data[r*gutm->cols + c + 1] == invalid_data ||
					gutm->data[(r+1)*gutm->cols + c] == invalid_data ||
					gutm->data[(r+1)*gutm->cols + c + 1] == invalid_data) {
					rtn->data[row*cols + col] = invalid_data;
					if (alt_data)
						rtn->altdata[row*cols + col] = invalid_data;
				} else {
					delta_x = (e - (in_west + in_de*c))/in_de;
					delta_y = (n - (in_north - (r+1)*in_dn))/in_dn;
					rtn->data[row*cols + col] =
						(f32)(gutm->data[r*gutm->cols+c]*(1.0-delta_x)*delta_y +
						gutm->data[r*gutm->cols+c+1]*delta_x*delta_y +
						gutm->data[(r+1)*gutm->cols+c]*(1.0-delta_x)*(1.0-delta_y) +
						gutm->data[(r+1)*gutm->cols+c+1]*delta_x*(1.0-delta_y));
					if (alt_data)
						rtn->altdata[row*cols + col] =
							(f32)(gutm->altdata[r*gutm->cols+c]*(1.0-delta_x)*delta_y +
							gutm->altdata[r*gutm->cols+c+1]*delta_x*delta_y +
							gutm->altdata[(r+1)*gutm->cols+c]*(1.0-delta_x)*(1.0-delta_y) +
							gutm->altdata[(r+1)*gutm->cols+c+1]*delta_x*(1.0-delta_y));
					++n_valid;
				}
			} else {
				++n_outside;
				rtn->data[row*cols + col] = invalid_data;
				if (alt_data)
					rtn->altdata[row*cols + col] = invalid_data;
			}
		}
	}
	
	error_msgv(modname, "info: done with surface interpolation at %s.\n",
		stime_timestamp());
	error_msgv(modname, "debug: interp: set %d of output total %d pels,"
		" with %d points outside of input initialisation grid.\n",
		n_valid, rtn->rows*rtn->cols, n_outside);

	return(rtn);
}

/* Routine:	construct_unct
 * Purpose:	Construct an uncertainty surface
 * Inputs:	*data	GUTM surface being built
 *			unct	Uncertainty (fixed, 1 s.d. or % of depth)
 *			upcnt	Flag: True => unct is % of depth
 * Outputs:	Returns True on success, or False.
 * Comment:	This constructs the corresponding uncertainty surface for the
 *			depth surface provided.  If the upcnt flag is set, then the value
 *			in _unct_ is the percentage of depth to set; otherwise it is a
 *			fixed uncertainty in meters at 1 s.d.
 */

Bool construct_unct(GUTM *data, f32 unct, Bool upcnt)
{
	u32	pel, npels = data->rows*data->cols, nset = 0;
	f32	scale, invalid_data, var;
	
	if (unct < 0.0f) {
		error_msg(modname, "error: trying to build an uncertainty surface"
			" without specifying an uncertainty!\n");
		return(False);
	}
	if (data->altdata == NULL &&
				(data->altdata = (f32*)malloc(sizeof(f32)*npels)) == NULL) {
		error_msgv(modname, "error: failed getting memory for uncertainty"
			" surface construction (%dx%dx%d bytes).\n",
			data->rows, data->cols, sizeof(f32));
		return(False);
	}
	mapsheet_get_invalid(MAP_DATA_F32, &invalid_data);
	if (upcnt) {
		scale = (f32)((unct/(1.96*100.0))*(unct/(1.96*100.0)));
		for (pel = 0; pel < npels; ++pel)
			if (data->data[pel] != invalid_data) {
				++nset;
				data->altdata[pel] = data->data[pel]*data->data[pel]*scale;
			} else
				data->altdata[pel] = invalid_data;
	} else {
		var = unct*unct;
		for (pel = 0; pel < npels; ++pel)
			if (data->data[pel] != invalid_data) {
				++nset;
				data->altdata[pel] = var;
			} else data->altdata[pel] = invalid_data;
	}
	error_msgv(modname, "debug: unct: set %d elements of %d (%.2lf %%).\n",
		nset, npels, 100.0*((f32)nset/npels));
	return(True);
}

/* Routine:	combine_surfaces
 * Purpose:	Combine the low resolution and high resolution surfaces
 * Inputs:	*primary	Default surface to use
 *			*secondary	Fill-in surface where primary is not defined
 * Outputs:	Pointer to combined surface, or NULL on failure.
 * Comment:	The typical mode for initialisation is that we have some low-res
 *			data around the edges of the survey (typically TINed single-beam
 *			soundings and shore-line) and some high-res data in the center
 *			(typically multibeam).  Therefore, we want to supersede the high-res
 *			where we have it, and keep the low-res elsewhere.
 */

GUTM *combine_surfaces(GUTM *primary, GUTM *secondary)
{
	f32		invalid_data;
	u32		pel, npels = primary->rows*primary->cols;
	GUTM	*rtn;
	
	if (primary->rows != secondary->rows ||
		primary->cols != secondary->cols) {
		error_msgv(modname, "error: primary size (%d,%d), secondary size"
			" (%d,%d).\n", primary->rows, primary->cols, secondary->rows,
			secondary->cols);
		return(NULL);
	}
	if (primary->altdata == NULL || secondary->altdata == NULL) {
		error_msg(modname, "error: primary and/or secondary data for"
			" combination surface do not have alternate data.\n");
		return(NULL);
	}
	
	if ((rtn = ccom_gutm_clone(primary)) == NULL) {
		error_msgv(modname, "error: failed to allocate combined surface"
			" buffer (%dx%dx2x%d bytes).\n", primary->rows, primary->cols,
			sizeof(f32));
		return(NULL);
	}
	
	mapsheet_get_invalid(MAP_DATA_F32, &invalid_data);
	for (pel = 0; pel < npels; ++pel) {
		if (primary->data[pel] != invalid_data) {
			rtn->data[pel] = primary->data[pel];
			rtn->altdata[pel] = primary->altdata[pel];
		} else {
			rtn->data[pel] = secondary->data[pel];
			rtn->altdata[pel] = secondary->altdata[pel];
		}
	}
	return(rtn);
}

/* Routine:	construct_surface
 * Purpose:	Interpolate primary and secondary data into a common initialisation
 *			surface
 * Inputs:	sheet	MapSheet to use for geo-referencing and size of output
 *			*pri	Name of the primary use GUTM
 *			*sec	Name of the secondary GUTM (or NULL).
 *			unct	Uncertainty for the surface if none specified in GUTM
 *			upcnt	Flag: True=> _unct_ is a percentage of depth.
 *			*georef	Location for georeferencing information from the primary
 *					initialisation GUTM (or NULL if not required).
 * Outputs:	Pointer to initialisation surface GUTM on success, or NULL
 * Comment:	This essentially just sequences the calls required to make the
 *			composite surface --- memory management, etc.  If sec == NULL,
 *			only the high resolution surface is loaded and interpolated.
 */

GUTM *construct_surface(MapSheet sheet, char *pri, char *sec,
						f32 unct, Bool upcnt, GUTM *georef)
{
	u32		sheet_cols, sheet_rows, pel, n_pels;
	GUTM	*pri_base, *sec_base, *primary, *secondary, *rtn;
		
	error_msgv(modname, "info: start constructing composite"
		" interpolation surface at %s.\n", stime_timestamp());

	mapsheet_get_width_height(sheet, &sheet_cols, &sheet_rows);

	/* Generate GUTM for primary initialisation depths */
	if ((pri_base = ccom_gutm_read(pri)) == NULL) {
		error_msgv(modname, "error: failed loading \"%s\" from file.\n", pri);
		return(NULL);
	}
	if (pri_base->altdata != NULL) {
		/* The input uncertainty is 95% CI, so we have to re-scale to variance before
		 * using it in action.
		 */
		n_pels = pri_base->rows * pri_base->cols;
		for (pel = 0; pel < n_pels; ++pel)
			pri_base->altdata[pel] = (f32)((pri_base->altdata[pel]/C95_SCALE)*
										(pri_base->altdata[pel]/C95_SCALE));
	}

	if (georef != NULL) {
		memcpy(georef, pri_base, sizeof(GUTM));
		georef->data = NULL;	/* We set data pointers to NULL because the
								 * data is going away soon --- only the georef
								 * information is valuable after interpolation.
								 */
		georef->altdata = NULL;
	}

	if ((primary = gutm_interpolate(sheet, pri_base)) == NULL) {
		error_msgv(modname, "error: can't interpolate depth surface"
			" initialiser (%dx%d) to size of mapsheet (%dx%d).\n",
			pri_base->rows, pri_base->cols, sheet_rows, sheet_cols);
		ccom_gutm_release(pri_base);
		return(NULL);
	}
	ccom_gutm_release(pri_base);
	
	if (primary->altdata == NULL && !construct_unct(primary, unct, upcnt)) {
		error_msg(modname, "error: can't construct primary uncertainty surface.\n");
		ccom_gutm_release(primary);
		return(NULL);
	}
	if (sec != NULL) {
		if ((sec_base = ccom_gutm_read(sec)) == NULL) {
			error_msgv(modname, "error: failed to read secondary \"%s\" into "
				"buffer.\n", sec);
			ccom_gutm_release(primary);
			return(NULL);
		}
		
		if ((secondary = gutm_interpolate(sheet, sec_base)) == NULL) {
			error_msg(modname, "error: failed to interpolate secondary GUTM.\n");
			ccom_gutm_release(primary);
			ccom_gutm_release(sec_base);
			return(NULL);
		}
		ccom_gutm_release(sec_base);

		if (secondary->altdata == NULL &&
									!construct_unct(secondary, unct, upcnt)) {
			error_msg(modname, "error: can't construct secondary uncertainty"
				" surface.\n");
			ccom_gutm_release(primary);
			ccom_gutm_release(secondary);
			return(NULL);
		}
		
		if ((rtn = combine_surfaces(primary, secondary)) == NULL) {
			error_msg(modname, "error: could not construct combination surface"
				" from components.\n");
			ccom_gutm_release(primary);
			ccom_gutm_release(secondary);
			return(NULL);
		}
		ccom_gutm_release(primary);
		ccom_gutm_release(secondary);
	} else
		rtn = primary;
		
	error_msgv(modname, "info: end constructing composite interpolation "
		"surface at %s.\n", stime_timestamp());

	return(rtn);
}

/* Routine:	scale_mask
 * Purpose:	Scale up a mask at arbitrary size to the right size for initialisation
 * Inputs:	*base		Pointer to the base mask
 *			rows, cols	Size of the base mask
 *			*georef		Pointer to geo-referencing information for the base mask
 *			sheet		MapSheet we're heading towards, for calibration
 * Outputs:	Returns a pointer to the newly scale mask, or NULL on failure.
 * Comment:	Note that no interpolation is done, since the mask is only
 *			valid for particular values (0 and 255 in this case).  We also
 *			assume that the mask has the same geo-referencing information as
 *			the primary initialisation grid (since the raw TIFF has none of its own).
 *			This isn't too serious in its limitations, since the masks have to
 *			be generated from something, and the only realistic source is the
 *			primary initialisation GUTM...
 */

u8 *scale_mask(u8 *base, u32 rows, u32 cols, GUTM *georef, MapSheet sheet)
{
	f64		x_sht, y_sht, dx, dy, mask_width, mask_height;
	f64		sheet_left, sheet_top, sheet_de, sheet_dn;
	u32		c_msk, r_msk, c_sht, r_sht;
	u32		sheet_rows, sheet_cols;
	u8		*rtn;

	mask_width = (georef->cols-1)*georef->cell_size;
	mask_height = (georef->rows-1)*georef->cell_size;

	mapsheet_get_width_height(sheet, &sheet_cols, &sheet_rows);
	mapsheet_get_tl(sheet, &sheet_left, &sheet_top);
	mapsheet_get_spacing(sheet, &sheet_de, &sheet_dn);

	if ((rtn = (u8*)malloc(sizeof(u8)*sheet_cols*sheet_rows)) == NULL) {
		error_msgv(modname, "error: failed to get mask scale buffer (%dx%d pels).\n",
			sheet_rows, sheet_cols);
		return(NULL);
	}

	for (r_sht = 0; r_sht < sheet_rows; ++r_sht)
		for (c_sht = 0; c_sht < sheet_cols; ++c_sht) {
			x_sht = sheet_left + c_sht*sheet_de;
			y_sht = sheet_top - r_sht*sheet_dn;
			dx = x_sht - georef->x0;
			dy = georef->y0 + mask_height - y_sht;
			if (dx < 0.0 || dy < 0.0 || dx > mask_width || dy > mask_height) {
				/* The point from the mapsheet is outside of the zone of
				 * definition of the initialisation mask.  We fake 'not defined'
				 * as 'let the node be updated, since we don't know better.'
				 */
				rtn[r_sht*sheet_cols+c_sht] = 0;
			} else {
				c_msk = (u32)floor(dx/georef->cell_size);
				r_msk = (u32)floor(dy/georef->cell_size);
				rtn[r_sht*sheet_cols+c_sht] = base[r_msk*cols + c_msk];
			}
		}
	return(rtn);
}

int main(int argc, char **argv)
{
	int			c;
	MapSheet	sheet;
	void		*mask = NULL;
	u32			sheet_rows, sheet_cols, mask_rows, mask_cols;
	f32			unct_scale = -1.0;
	char		*mask_name = NULL, *sec_init = NULL, *out_gutm = NULL,
				*mask_georef_name = NULL;
	GUTM		*initsurf, georef;
	TIFFType	ttype;
	Bool		unct_pcnt = False;
	ParList		*parlist = NULL;
	MapSheetDepthAlg	alg = MAP_DEPTH_HYPERCUBE;
	ErrModIHOOrder	order = DEFAULT_IHO_SURVEY_ORDER;
	
	ccom_log_command_line(modname, argc, argv);

	opterr = 0;
	while ((c = getopt(argc, argv, "hm:pf:a:i:s:u:w:g:")) != EOF) {
		switch(c) {
			case 'g':
				mask_georef_name = strdup(optarg);
				break;
			case 'w':
				out_gutm = strdup(optarg);
				break;
			case 'u':
				unct_scale = (f32)atof(optarg);
				if (unct_scale < 0.0) {
					error_msgv(modname, "error: uncertainty must be positive"
						" (not %f).\n", unct_scale);
					return(1);
				}
				break;
			case 's':
				sec_init = strdup(optarg);
				break;
			case 'i':
				switch (atoi(optarg)) {
					case 1: order = ERRMOD_IHO_ORDER_1; break;
					case 2: order = ERRMOD_IHO_ORDER_2; break;
					case 3: order = ERRMOD_IHO_ORDER_3; break;
					case 4: order = ERRMOD_IHO_ORDER_4; break;
					default:
						error_msg(modname, "error: IHO survey orders are"
							" [1,4].\n");
						return(1);
				}
				break;
			case 'a':
				if (strcmp(optarg, "mean") == 0)
					alg = MAP_DEPTH_BINED_MEAN;
				else if (strcmp(optarg, "median") == 0)
					alg = MAP_DEPTH_BINED_MEDIAN;
				else if (strcmp(optarg, "nodal") == 0)
					alg = MAP_DEPTH_NODAL;
				else if (strcmp(optarg, "cube") == 0)
					alg = MAP_DEPTH_HYPERCUBE;
				else {
					error_msgv(modname, "error: algorithm \"%s\" not known.\n",
						optarg);
					return(1);
				}
				break;
			case 'p':
				unct_pcnt = True;
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
				break;
			case 'm':
				mask_name = strdup(optarg);
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
	
	if ((sheet = mapsheet_new_from_ascii(argv[INPUT_SHEET])) == NULL) {
		error_msgv(modname, "error: failed to make blank mapsheet from"
			" \"%s\".\n", argv[INPUT_SHEET]);
		return(1);
	}
	mapsheet_get_width_height(sheet, &sheet_cols, &sheet_rows);
	
	if (unct_scale >= 0.0) {
		if (unct_pcnt) {
			if (unct_scale <= 0.0 || unct_scale >= 100.0) {
				error_msgv(modname, "error: percentage uncertainty must be in "
					"the range (0.0, 100.0) (not %f).\n", unct_scale);
				return(1);
			}
		} else unct_scale /= 1.96f;
	}

	if (mask_georef_name != NULL && mask_name == NULL) {
		error_msgv(modname, "error: must have a mask specified if a mask"
			" geo-referencing GUTM is also specified.\n");
		return(1);
	}

	if ((initsurf = construct_surface(sheet, argv[INITIALISER], sec_init,
						   unct_scale, unct_pcnt, &georef)) == NULL) {
		error_msg(modname, "error: failed to construct initialisation surface"
			" from components.\n");
		return(1);
	}
	if (out_gutm != NULL) {
		if (!ccom_write_gutm(out_gutm, initsurf->data, DATA_F32,
				initsurf->rows, initsurf->cols, initsurf->cell_size,
				initsurf->x0, initsurf->y0 - initsurf->cell_size*(initsurf->rows-1))) {
			error_msgv(modname, "error: could not write initialisation surface"
				" GUTM to \"%s\".\n", out_gutm);
			return(1);
		}
	}
	
	/* Read and generate the mask TIFF image, if present */
	if (mask_name != NULL) {
		if ((mask = ccom_read_tiff(mask_name, &ttype,
											&mask_rows, &mask_cols)) == NULL) {
			error_msgv(modname, "error: failed reading \"%s\" for sheet mask.\n",
				mask_name);
			return(1);
		}
		if (ttype != TIFF_U8) {
			error_msgv(modname, "error: mask TIFF is not compatible with mapsheet "
				" initialiser (TIFF: %d x %d, elements, not U8).\n",
				mask_rows, mask_cols);
			return(1);
		}
		if (mask_georef_name != NULL) {
			GUTM	*ext_geo;	/* Temporary holder while we do the read */
			Bool	full_grid, alt_data;
			FILE	*mask_geo_f;

			if ((mask_geo_f = fopen(mask_georef_name, "r")) == NULL) {
				error_msgv(modname, "error: failed to open \"%s\" for mask georeferencing.\n",
					mask_georef_name);
				return(1);
			}
			if ((ext_geo = ccom_gutm_read_header(mask_geo_f, &full_grid, &alt_data)) == NULL) {
				error_msgv(modname, "error: failed to read georeferencing from GUTM \"%s\".\n",
					mask_georef_name);
				return(1);
			}
			fclose(mask_geo_f);
			memcpy(&georef, ext_geo, sizeof(GUTM));
			ccom_gutm_release(ext_geo);
		}	
		if (mask_rows != sheet_rows || mask_cols != sheet_cols) {
			u8 *tmp_mask;

			if ((tmp_mask = scale_mask(mask, mask_rows, mask_cols, &georef, sheet)) == NULL) {
				error_msgv(modname, "error: failed to interpolate mask from (%d, %d) pels"
					" to match MapSheet.\n", mask_rows, mask_cols);
				return(1);
			}
			free(mask);
			mask = tmp_mask;
		}
	}
	
	/* Construct new mapsheet, then initialise */
	if (!mapsheet_add_depth_unct(sheet, alg, order, initsurf->data,
								 initsurf->altdata, mask,
								 sheet_cols, sheet_rows)) {
		error_msg(modname, "error: failed initialising mapsheet depths.\n");
		return(1);
	}
	if (!mapsheet_save_sheet(sheet, argv[OUTPUT_SHEET])) {
		error_msgv(modname, "error: failed writing \"%s\" as output.\n",
			argv[OUTPUT_SHEET]);
		return(1);
	}
	
	return(0);
}
