/*
 * $Id: omg.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:27  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.11.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.11  2001/12/20 00:41:35  roland
 * Made a few fixes to make it compile under Linux
 *
 * Revision 1.10  2001/09/23 18:56:06  brc
 * Added partial support for rotated mapsheets.  In this case, we compute the
 * new (rotated) bounding box in the unrotated coordinate system, and then build
 * a mapsheet in the unrotated coordinate system.  This doesn't really give us
 * the same thing as the rotated mapsheet since we are looking at different
 * nodes/pixels, etc. but its as close as libccom is going to get :-)  After
 * consultation with JEHC, added an extra pad element into the header of the
 * OMG1 format.  He claims that this is the SGI's default padding, although this
 * doesn't appear to be the case as of 6.5.9m ...  Also made the code swap if
 * the host platform isn't big endian, since OMG1s are always stored in big
 * endian format.  Moved to using mapsheet.c for the invalid data option, and
 * then upgraded the data replacement code to allow a particular value to be
 * replaced with another so that the internal invalid data can be translated to
 * OMG1 invalid data (typically 0.0 for R4s).  Added backwards compatibility
 * call with call-through to maintain current code.
 *
 * Revision 1.9  2001/08/28 16:04:06  brc
 * Added omg_new_from_mapsheet_header() to allow construction of just the
 * geo-referencing parts of the OMGRaster from a mapsheet structure.  The
 * result has no data stored, and any indication of data should not be
 * believed.
 *
 * Revision 1.8  2001/02/10 16:05:37  brc
 * Modified naming on raster checking function to ensure compatibility with
 * the rest of the routines in this module.
 *
 * Revision 1.7  2001/02/10 16:03:22  brc
 * Added code to check for computation compatibility between R4 files before
 * using them.  This checks sizes, projections, etc. as a basic check sequence.
 * Note that this mostly checks the header, and doesn't guarantee that the
 * data types are the same (e.g., to use one for a depth file and another for
 * a mask).
 *
 * Revision 1.6  2000/12/07 22:31:23  brc
 * Added comments to explain the construction of OMG rasters from mapsheets
 * and vice versa a little more clearly.  Also improved the preservation of
 * projection parameters during conversion.
 *
 * Revision 1.5  2000/12/03 20:43:38  brc
 * Multiple modifications to fix errors and improve matching with mapsheets.
 * Turns out that the thing marked `proj_merid' in the OMG header is actually
 * used for the projection scale lattitude in mercator projections, rather
 * than being a meridian (which makes no sense).  This had implications for
 * code which attempts to match up between OMG1 files and mapsheet, so patched
 * the code which determines projections to preserve the projection lattitude
 * on load/save.  Also improved the false origin computation in omg_get_projection()
 * so that the code now approximately hits (0,0) in the bottom left.
 *     Added omg_replace_data() to allow the user level code to change the data
 * surface in the OMGRaster, given that it is in the same projection, location,
 * spacing, etc.  This allows the user to read an OMG1 file for the header
 * location, projection, etc. but then replace the data with a processed result
 * so that exactly matching OMG1s can be computed.
 *
 * Revision 1.4  2000/11/14 22:46:51  brc
 * Improved accuracy on some error reporting to allow for finer debugging.
 *
 * Revision 1.3  2000/08/24 15:11:35  brc
 * Modified numerous files to allow the code to compile cleanly under Linux.
 *
 * Revision 1.2  2000/08/22 16:11:19  brc
 * Modified makefiles to ensure that race condition to include files is resolved
 * by building the include directory as a first pass from the top-level makefile.
 * Also corrected small bug in omg.c using error_msg rather than error_msgv.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:25  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 * Revision 1.3  2000/06/13 20:17:11  brc
 * Added more utility routines to omgif.c/h to allow for more automatic interface
 * at the whole file level.  Also provides the Grid structure to encapsulate what
 * we're doing at the mapsheet level.
 *
 * Revision 1.2  2000/04/22 18:07:38  brc
 * Added routine to read OMG1 header and check for properties, set up projection,
 * etc. since this is used by a number of executables.
 *
 * Revision 1.1  2000/04/22 16:41:11  brc
 * Generation of library to provide general services and interact with the
 * original OMG libraries.
 *
 *
 * File:	omgif.c
 * Purpose:	Interface with OMG files for reading and writing
 * Date:	22 April 2000
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
#include <float.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "stdtypes.h"
#include "projection.h"
#include "omg.h"
#include "ccom_general.h"

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define DEG2RAD(x) ((x)*M_PI/180.0)

static char *modname = "omg";

static char *omg1_header = "OMG1";

typedef enum {
	OMG_CONTAINS_RED	=	1,
	OMG_CONTAINS_GRN	=	2,
	OMG_CONTAINS_BLU	=	3,
	OMG_CONTAINS_GRY	=	4,
	OMG_CONTAINS_UNKNOWN
} OMGContents;

typedef enum {
	OMG_PROJECTION_NONE		=	0,	/* Projection not relevant/known */
	OMG_PROJECTION_MERCATOR	=	1,	/* Standard Mercator */
	OMG_PROJECTION_UTM		=	2,	/* Universal Transverse Mercator */
	OMG_PROJECTION_LATLON	=	3,	/* Unprojected: minutes of arc */
	OMG_PROJECTION_ROTUTM	=	4,	/* Rotated UTM */
	OMG_PROJECTION_ROTMERC	=	5,	/* Rotated Mercator Projection */
	OMG_PROJECTION_UNKNOWN
} OMGProjection;

typedef enum {
	OMG_ELLIPSOID_WGS84		=	1
} OMGEllipsoid;

typedef enum {
	OMG_HEMISPHERE_NORTH	=	0,
	OMG_HEMISPHERE_SOUTH	=	1,
	OMG_HEMISPHERE_UNKNOWN
} OMGHemisphere;

/* Structure header for OMG1 style files.  A great deal of this is irrelevant,
 * and is rarely used.
 */

typedef struct _omg_hdr {
	char    header_name[4]	/*="OMG1"*/;
	u32		dx;		/* Width in pixels */
	u32		dy;		/* Height in pixels */
	u32		pad;	/* Pad to 64-byte boundary before next section */
	f64		xmin;	/* (4) Bounds in file coordinate system (note that the	*/
	f64		xmax;	/* interpretation of these depends on the projection	*/
	f64		ymin;	/* style used (if any) --- see _projection_)			*/
	f64		ymax;
	f64		xstep;		/* (2) Spacings in column/row directions (meters if */
	f64		ystep;		/* the data has been projected, or minutes of arc)	*/
	u8		data_type;  /* Code for data width --- see omg.h 				*/
	u8		rgb_bw; 	/* Code for interpretation --- see OMGContents enum */
	u8  	projection; /* Code for projection --- see OMGProjection enum 	*/
	u8   	ellipsoid;	/* Code for ellipsoid --- see OMGEllipsoid enum		*/
	f32   	proj_merid;	/* Projection latitude (Mercator only)				*/
			/* (3) Sun angle and vertical exageration for U8 sun-shaded ims	*/
	f32		sun_elev, sun_azi, sun_exag;
			/* (3) View angle and vertical exageration (? may not be used?)	*/
	f32   	view_elev, view_azi, view_exag;
			/* (2) Maximum and minimum value of data for rescaling			*/
	f32		max_value, min_value;
			/* (2) User defined labels (? may not be used?)					*/
	char	label_1[72], label_2[72];
			/* Hemisphere of image data --- see OMGHemisphere enum			*/
	char	hemisphere;
			/* Padding to bring size to 1024 bytes							*/
	char	unass2, unass3, unass4, unassigned[4];
			/* Colour map for pseudo-colour images							*/
	char	rgb_cmap[768];
} RasterHdr;

typedef struct _omg_raster {
	OMGType			type;	/* Data size contained within the file */
	RasterHdr		*hdr;	/* Original header information for reference */
	void			*data;	/* Data loaded from file */
} Raster /*, *OMGRaster */;

static OMGRaster omg_new_empty(void)
{
	OMGRaster	rtn;

	if ((rtn = (OMGRaster)calloc(1, sizeof(Raster))) == NULL ||
		(rtn->hdr = (RasterHdr *)calloc(1, sizeof(RasterHdr))) == NULL) {
		free(rtn);
		return(NULL);
	}
	memcpy(rtn->hdr->header_name, omg1_header, 4);
	return(rtn);
}

void omg_release(OMGRaster raster)
{
	if (raster != NULL) {
		free(raster->hdr);
		free(raster->data);
		free(raster);
	}
}

OMGType omg_get_data_type(OMGRaster raster)
{
	return(raster->type);
}

void omg_get_sizes(OMGRaster raster, u32p width, u32p height)
{
	*width = raster->hdr->dx;
	*height = raster->hdr->dy;
}

Projection omg_get_projection(OMGRaster r)
{
	Projection	rtn;
	f64			east, north;
	
	switch(r->hdr->projection) {
		case OMG_PROJECTION_UTM:
			/* In UTM projection, r->hdr->proj_merid is the true projection
			 * meridian used to determine the UTM zone.  However the projection
			 * latitude is unknown (and not really used in true UTM).  We
			 * fake it here as the midpoint of the target area.
			 */
			rtn = projection_new_utm(r->hdr->proj_merid,
									 (r->hdr->ymin+r->hdr->ymax)/2.0);
			break;
		case OMG_PROJECTION_MERCATOR:
			/* Preserve projection latitude from OMG, but reset longitude */
			rtn = projection_new_mercator(
						(r->hdr->xmin+r->hdr->xmax)/2.0,
						r->hdr->proj_merid,	/* Actually latitude */
						0.0, 0.0);
			/* Project bottom left to get approximate false easting/northing */
			projection_ll_to_en_deg(rtn, r->hdr->xmin, r->hdr->ymin,
									&east, &north);
			projection_release(rtn);
			/* Then reconstruct with appropriate offsets */
			rtn = projection_new_mercator(
						(r->hdr->xmin+r->hdr->xmax)/2.0,
						r->hdr->proj_merid,
						-east, -north);
			break;
		default:
			error_msgv(modname, "projection type %d not allowed.\n",
						(u32)(r->hdr->projection));
			return(NULL);
	}
	return(rtn);
}

void *omg_get_data(OMGRaster r)
{
	return(r->data);
}

void omg_get_spacings(OMGRaster r, f64p dx, f64p dy)
{
	*dx = r->hdr->xstep;
	*dy = r->hdr->ystep;
}

void omg_get_bounds(OMGRaster r, f64p left, f64p bottom, f64p right, f64p top)
{
	*left = r->hdr->xmin;
	*right = r->hdr->xmax;
	*bottom = r->hdr->ymin;
	*top = r->hdr->ymax;
}

Bool omg_bounds_projected(OMGRaster r)
{
	Bool	rtn;
	
	switch(r->hdr->projection) {
		case OMG_PROJECTION_NONE:		rtn = False; break;
		case OMG_PROJECTION_UTM:		rtn = True; break;
		case OMG_PROJECTION_MERCATOR:	rtn = False; break;
		case OMG_PROJECTION_LATLON:		rtn = False; break;
		default:
			error_msgv(modname, "internal: projection type %d unknown.\n",
				(u32)(r->hdr->projection));
			return(False);
	}
	return(rtn);
}

OMGRaster omg_new_from_mapsheet_header(MapSheet sheet)
{
	OMGRaster	rtn;
	RasterHdr	*hdr;
	Projection	proj;
	ProjType	ptype;
	f64			cen_lon, cen_lat, x0, y0;
	
	if ((rtn = omg_new_empty()) == NULL) return(NULL);
	hdr = rtn->hdr;
	
	/* Set up everything that can be done without reference to projection */
	mapsheet_get_width_height(sheet, &(hdr->dx), &(hdr->dy));
	mapsheet_get_bl(sheet, &(hdr->xmin), &(hdr->ymin));
	mapsheet_get_tr(sheet, &(hdr->xmax), &(hdr->ymax));
	mapsheet_get_spacing(sheet, &(hdr->xstep), &(hdr->ystep));
	rtn->type = OMG1_UNKNOWN;
	hdr->data_type = OMG1_UNKNOWN;
	hdr->rgb_bw = OMG_CONTAINS_GRY;
	
	/* Deal with projection-specific items */
	proj = mapsheet_get_projection(sheet);
	switch(projection_get_ellipsoid(proj)) {
		case PROJECTION_WGS84:
			hdr->ellipsoid = OMG_ELLIPSOID_WGS84;
			break;
		default:
			error_msgv(modname, "internal: ellipsoid type (%d) unknown.\n",
						(u32)projection_get_ellipsoid(proj));
			free(rtn);
			return(NULL);
	}
	ptype = projection_get_type(proj);
	projection_get_params(proj, &cen_lon, &cen_lat, &x0, &y0);
	switch(ptype) {
		case PROJECTION_UTM:
			hdr->projection = OMG_PROJECTION_UTM;
			/* In UTM projection mode, the proj_merid is used to determine
			 * the UTM zone.  As long as it's roughly in the right area, and
			 * we're not working on the very edge of a projection zone, then
			 * we'll still get the same projection parameters.  In this case,
			 * we fake it with the central longitude used in the Projection,
			 * which should be close enough for Government Work (TM).
			 */
			hdr->proj_merid = (f32)cen_lon;
			break;
		case PROJECTION_MERCATOR:
			hdr->projection = OMG_PROJECTION_MERCATOR;
			/* In Mercator projection mode, the proj_merid is actually the
			 * projection latitude for true scale.  The simplest solution is
			 * to have this set true at the center of the area of interest
			 * (or at least what the Projection was initialised with, which
			 * might not be that location for special purposes --- e.g., for
			 * comparisson of the surface with another mapsheet in a particular
			 * projection mode).
			 */
			hdr->proj_merid = (f32)cen_lat;
			/* The mercator projection has bounds in lat/lon rather than m */
			projection_en_to_ll_deg(proj, hdr->xmin, hdr->ymin, &x0, &y0);
			hdr->xmin = x0; hdr->ymin = y0;
			projection_en_to_ll_deg(proj, hdr->xmax, hdr->ymax, &x0, &y0);
			hdr->xmax = x0; hdr->ymax = y0;
			break;
		case PROJECTION_POLAR_STEREO:
			error_msg(modname, "polar stereographic not supported by OMG1.\n");
			return(NULL);
			break;
		default:
			error_msgv(modname, "internal: projection %d unknown.\n",
						(u32)ptype);
			return(NULL);
			break;
	}
	if (cen_lat >= 0.0)
		hdr->hemisphere = OMG_HEMISPHERE_NORTH;
	else
		hdr->hemisphere = OMG_HEMISPHERE_SOUTH;
	
	rtn->data = NULL;
	return(rtn);
}

OMGRaster omg_new_from_mapsheet(MapSheet sheet, MapSheetElem data)
{
	OMGRaster	rtn;
	RasterHdr	*hdr;
	Projection	proj;
	ProjType	ptype;
	f64			cen_lon, cen_lat, x0, y0;
	MapSheetDataType	dtype;
	void		*out_buf;
	u32			space_rqd;
	
	if ((rtn = omg_new_empty()) == NULL) return(NULL);
	hdr = rtn->hdr;
	
	/* Set up everything that can be done without reference to projection */
	mapsheet_get_width_height(sheet, &(hdr->dx), &(hdr->dy));
	mapsheet_get_bl(sheet, &(hdr->xmin), &(hdr->ymin));
	mapsheet_get_tr(sheet, &(hdr->xmax), &(hdr->ymax));
	mapsheet_get_spacing(sheet, &(hdr->xstep), &(hdr->ystep));
	dtype = mapsheet_get_data_type(sheet, data);
	switch(dtype) {
		case MAP_DATA_U8:
			rtn->type = OMG1_U8;
			hdr->data_type = OMG1_U8;
			space_rqd = hdr->dx * hdr->dy;
			break;
		case MAP_DATA_U16:
			rtn->type = OMG1_U16;
			hdr->data_type = OMG1_U16;
			space_rqd = hdr->dx * hdr->dy * 2;
			break;
		case MAP_DATA_U32:
			rtn->type = OMG1_U16;
			hdr->data_type = OMG1_U16;
			space_rqd = hdr->dx * hdr->dy * 2;
			break;
		case MAP_DATA_F32:
			rtn->type = OMG1_F32;
			hdr->data_type = OMG1_F32;
			space_rqd = hdr->dx * hdr->dy * 4;
			break;
		case MAP_DATA_F64:
			rtn->type = OMG1_F32;
			hdr->data_type = OMG1_F32;
			space_rqd = hdr->dx * hdr->dy * 4;
			break;
		default:
			error_msgv(modname, "internal: data type %d unknown.\n",
				(int)dtype);
			free(rtn);
			return(NULL);
	}
	hdr->rgb_bw = OMG_CONTAINS_GRY;
	
	/* Deal with projection-specific items */
	proj = mapsheet_get_projection(sheet);
	switch(projection_get_ellipsoid(proj)) {
		case PROJECTION_WGS84:
			hdr->ellipsoid = OMG_ELLIPSOID_WGS84;
			break;
		default:
			error_msgv(modname, "internal: ellipsoid type (%d) unknown.\n",
						(u32)projection_get_ellipsoid(proj));
			free(rtn);
			return(NULL);
	}
	ptype = projection_get_type(proj);
	projection_get_params(proj, &cen_lon, &cen_lat, &x0, &y0);
	switch(ptype) {
		case PROJECTION_UTM:
			hdr->projection = OMG_PROJECTION_UTM;
			/* In UTM projection mode, the proj_merid is used to determine
			 * the UTM zone.  As long as it's roughly in the right area, and
			 * we're not working on the very edge of a projection zone, then
			 * we'll still get the same projection parameters.  In this case,
			 * we fake it with the central longitude used in the Projection,
			 * which should be close enough for Government Work (TM).
			 */
			hdr->proj_merid = (f32)cen_lon;
			break;
		case PROJECTION_MERCATOR:
			hdr->projection = OMG_PROJECTION_MERCATOR;
			/* In Mercator projection mode, the proj_merid is actually the
			 * projection latitude for true scale.  The simplest solution is
			 * to have this set true at the center of the area of interest
			 * (or at least what the Projection was initialised with, which
			 * might not be that location for special purposes --- e.g., for
			 * comparisson of the surface with another mapsheet in a particular
			 * projection mode).
			 */
			hdr->proj_merid = (f32)cen_lat;
			/* The mercator projection has bounds in lat/lon rather than m */
			projection_en_to_ll_deg(proj, hdr->xmin, hdr->ymin, &x0, &y0);
			hdr->xmin = x0; hdr->ymin = y0;
			projection_en_to_ll_deg(proj, hdr->xmax, hdr->ymax, &x0, &y0);
			hdr->xmax = x0; hdr->ymax = y0;
			break;
		case PROJECTION_POLAR_STEREO:
			error_msg(modname, "polar stereographic not supported by OMG1.\n");
			return(NULL);
			break;
		default:
			error_msgv(modname, "internal: projection %d unknown.\n",
						(u32)ptype);
			return(NULL);
			break;
	}
	if (cen_lat >= 0.0)
		hdr->hemisphere = OMG_HEMISPHERE_NORTH;
	else
		hdr->hemisphere = OMG_HEMISPHERE_SOUTH;
	
	/* Now, get data from the map-sheet and convert to internal format */
	if ((out_buf = mapsheet_get_data(sheet, data)) == NULL) {
		error_msg(modname, "failed to get map-sheet data.\n");
		free(rtn->hdr); free(rtn);
		return(NULL);
	}
	if ((rtn->data = malloc(space_rqd)) == NULL) {
		error_msgv(modname, "failed to get output space (%d bytes).\n",
					space_rqd);
		free(rtn->hdr); free(rtn);
		return(NULL);
	}
	switch(dtype) {
		case MAP_DATA_U8:
			{
				u32	pel, npels = hdr->dx * hdr->dy;
				u8	*data, no_data;
				
				memcpy(rtn->data, out_buf, space_rqd);
				mapsheet_get_invalid(MAP_DATA_U8, &no_data);
				data = (u8*)(rtn->data);
				for (pel = 0; pel < npels; ++pel)
					if (data[pel] == no_data) data[pel] = 0;
			}
			break;
		case MAP_DATA_U16:
			{
				u32	pel, npels = hdr->dx * hdr->dy;
				u16	*data, no_data;
				
				memcpy(rtn->data, out_buf, space_rqd);
				mapsheet_get_invalid(MAP_DATA_U16, &no_data);
				data = (u16*)(rtn->data);
				for (pel = 0; pel < npels; ++pel)
					if (data[pel] == no_data) data[pel] = 0;
			}
			break;
		case MAP_DATA_F32:
			{
				u32	pel, npels = hdr->dx * hdr->dy;
				f32	*data, no_data;
				
				memcpy(rtn->data, out_buf, space_rqd);
				mapsheet_get_invalid(MAP_DATA_F32, &no_data);
				data = (f32*)(rtn->data);
				for (pel = 0; pel < npels; ++pel)
					if (data[pel] == no_data) data[pel] = 0.f;
			}
			break;
		case MAP_DATA_U32:
			{
				u32		pel, npels = hdr->dx * hdr->dy, no_data;
				u16p	out = (u16p)(rtn->data);
				u32p	in = (u32p)out_buf;
				
				mapsheet_get_invalid(MAP_DATA_U32, &no_data);
				for (pel = 0; pel < npels; ++pel)
					if (in[pel] == no_data)
						out[pel] = 0;
					else
						out[pel] = in[pel] >> 16;
			}
			break;
		case MAP_DATA_F64:
			{
				u32		pel, npels = hdr->dx * hdr->dy;
				f32p	out = (f32p)(rtn->data);
				f64p	in = (f64p)out_buf;
				f64		no_data;
				
				mapsheet_get_invalid(MAP_DATA_F64, &no_data);
				for (pel = 0; pel < npels; ++pel)
					if (in[pel] == no_data)
						out[pel] = 0.0f;
					else
						out[pel] = (f32)(in[pel]);
			}
			break;
		default:
			error_msg(modname,
				"internal: map data type not known during copy!\n");
			free(rtn->hdr); free(rtn->data); free(rtn);
			return(NULL);
	}
	free(out_buf);

	return(rtn);
}

OMGRaster omg_new_from_file(const char *name, Bool header_only)
{
	FILE		*f;
	OMGRaster	rtn;
	u32			space_rqd, pel, npels;
	f64			center_x, center_y, alpha, phi, lat, lon, angle;
	f32			length, width, height;
	CcomEndian	endian;
	
	if ((rtn = omg_new_empty()) == NULL) {
		error_msg(modname, "could not get blank memory.\n");
		return(NULL);
	}
	if ((f = fopen(name, "rb")) == NULL) {
		error_msgv(modname, "could not open \"%s\" for reading.\n", name);
		return(NULL);
	}
	if (fread(rtn->hdr, sizeof(RasterHdr), 1, f) != 1) {
		error_msgv(modname, "failed reading file \"%s\" header.\n", name);
		omg_release(rtn);
		fclose(f);
		return(NULL);
	}
	if ((endian = ccom_native_endian()) != ENDIAN_BIG) {
		/* Headers are always written out in big endian format, so we have to
		 * swap for little endian machines */
		ccom_swap_4(&(rtn->hdr->dx));
		ccom_swap_4(&(rtn->hdr->dy));
		ccom_swap_8(&(rtn->hdr->xmin));
		ccom_swap_8(&(rtn->hdr->xmax));
		ccom_swap_8(&(rtn->hdr->ymin));
		ccom_swap_8(&(rtn->hdr->ymax));
		ccom_swap_8(&(rtn->hdr->xstep));
		ccom_swap_8(&(rtn->hdr->ystep));
		ccom_swap_4(&(rtn->hdr->proj_merid));
		ccom_swap_4(&(rtn->hdr->sun_elev));
		ccom_swap_4(&(rtn->hdr->sun_azi));
		ccom_swap_4(&(rtn->hdr->sun_exag));
		ccom_swap_4(&(rtn->hdr->view_elev));
		ccom_swap_4(&(rtn->hdr->view_azi));
		ccom_swap_4(&(rtn->hdr->view_exag));
		ccom_swap_4(&(rtn->hdr->max_value));
		ccom_swap_4(&(rtn->hdr->min_value));
	}
	switch(rtn->hdr->data_type) {
		case OMG1_NONE:
			rtn->type = OMG1_UNKNOWN;
			space_rqd = 0;
			break;
		case OMG1_U8:
			rtn->type = OMG1_U8;
			space_rqd = 1;
			break;
		case OMG1_U16:
			rtn->type = OMG1_U16;
			space_rqd = 2;
			break;
		case OMG1_F32:
			rtn->type = OMG1_F32;
			space_rqd = 4;
			break;
		default:
			error_msgv(modname, "OMG data type %d unknown.\n",
						(u32)(rtn->hdr->data_type));
			omg_release(rtn);
			fclose(f);
			return(NULL);
	}
	
	switch(rtn->hdr->projection) {
		case OMG_PROJECTION_UTM:
		case OMG_PROJECTION_MERCATOR:
			break;
		case OMG_PROJECTION_ROTUTM:
		case OMG_PROJECTION_ROTMERC:

			/* For rotated mapsheets, the (xmin,ymin) are actually the center
			 * of the mapsheet, rather than the minimum, and the (xmax,ymax)
			 * are in fact the rotation angle, and unused.  We assume that the
			 * xmax is the angle in degrees, but this has *never* been tested
			 * where the angle is not zero, and hence we may have a small
			 * untested feature ...
			 */
			
			center_x = rtn->hdr->xmin;
			center_y = rtn->hdr->ymin;
			error_msgv(modname, "debug: center = (%.1lf, %.1lf)\n",
				center_x, center_y);
			
			alpha = DEG2RAD(rtn->hdr->xmax);
			error_msgv(modname, "debug: angle = %.1f deg.\n", rtn->hdr->xmax);

			if (rtn->hdr->projection == OMG_PROJECTION_ROTMERC) {
				alpha = -alpha;	/* Convert to heading, and wrap */
				while (alpha < 0.0) alpha += 2.0*M_PI;
				while (alpha > 2.0*M_PI) alpha -= 2.0*M_PI;
			}
			
			/* We need to convert these into ortho mapsheets, and issue a
			 * suitable warning.
			 */
			 error_msgv(modname, "warning: rotated OMG1, converting to ortho,"
				" but pixels will be different (angle %.1f deg.).\n",
				rtn->hdr->xmax);
			
			width = (f32)(rtn->hdr->dx * rtn->hdr->xstep);
			height = (f32)(rtn->hdr->dy * rtn->hdr->ystep);
			
			error_msgv(modname, "debug: (dx,dy) = (%d, %d) pels, and"
				" (w,h) = (%.1f, %.1f), (xstep,ystep) = (%.1f, %.1f)m.\n",
				rtn->hdr->dx, rtn->hdr->dy, width, height,
				rtn->hdr->xstep, rtn->hdr->ystep);
			
			rtn->hdr->xmin = FLT_MAX; rtn->hdr->ymin = FLT_MAX;
			rtn->hdr->xmax = -FLT_MAX; rtn->hdr->ymax = -FLT_MAX;
			
			phi = atan2(height, width);
			length = (f32)sqrt(height*height + width*width);
			
			if (rtn->hdr->projection == OMG_PROJECTION_ROTMERC)
				sounding_locate_pt(width/2.0f, height/2.0f, (f32)alpha,
							center_x, center_y, &lon, &lat);
			else {
				angle = alpha + phi;
				lon = center_x + (length/2.0)*cos(angle);
				lat = center_y + (length/2.0)*sin(angle);
			}
			
			rtn->hdr->xmin = MIN(rtn->hdr->xmin, lon);
			rtn->hdr->ymin = MIN(rtn->hdr->ymin, lat);
			rtn->hdr->xmax = MAX(rtn->hdr->xmax, lon);
			rtn->hdr->ymax = MAX(rtn->hdr->ymax, lat);
			
			if (rtn->hdr->projection == OMG_PROJECTION_ROTMERC)
				sounding_locate_pt(width/2.0f, -height/2.0f, (f32)alpha,
							center_x, center_y, &lon, &lat);
			else {
				angle = alpha - phi;
				lon = center_x + (length/2.0)*cos(angle);
				lat = center_y + (length/2.0)*sin(angle);
			}				
			
			rtn->hdr->xmin = MIN(rtn->hdr->xmin, lon);
			rtn->hdr->ymin = MIN(rtn->hdr->ymin, lat);
			rtn->hdr->xmax = MAX(rtn->hdr->xmax, lon);
			rtn->hdr->ymax = MAX(rtn->hdr->ymax, lat);
			
			if (rtn->hdr->projection == OMG_PROJECTION_ROTMERC)
				sounding_locate_pt(-width/2.0f, height/2.0f, (f32)alpha,
							center_x, center_y, &lon, &lat);
			else {
				angle = alpha + M_PI + phi;
				lon = center_x + (length/2.0)*cos(angle);
				lat = center_y + (length/2.0)*sin(angle);
			}
			
			rtn->hdr->xmin = MIN(rtn->hdr->xmin, lon);
			rtn->hdr->ymin = MIN(rtn->hdr->ymin, lat);
			rtn->hdr->xmax = MAX(rtn->hdr->xmax, lon);
			rtn->hdr->ymax = MAX(rtn->hdr->ymax, lat);
			
			if (rtn->hdr->projection == OMG_PROJECTION_ROTMERC)
				sounding_locate_pt(-width/2.0f, height/2.0f, (f32)alpha,
							center_x, center_y, &lon, &lat);
			else {
				angle = alpha + M_PI - phi;
				lon = center_x + (length/2.0)*cos(angle);
				lat = center_y + (length/2.0)*sin(angle);
			}
			
			rtn->hdr->xmin = MIN(rtn->hdr->xmin, lon);
			rtn->hdr->ymin = MIN(rtn->hdr->ymin, lat);
			rtn->hdr->xmax = MAX(rtn->hdr->xmax, lon);
			rtn->hdr->ymax = MAX(rtn->hdr->ymax, lat);
			
			/* Finally, we convert the area into new widths */
			if (rtn->hdr->projection == OMG_PROJECTION_ROTUTM) {
				rtn->hdr->dx = (u32)((rtn->hdr->xmax - rtn->hdr->xmin)/
									rtn->hdr->xstep);
				rtn->hdr->dy = (u32)((rtn->hdr->ymax - rtn->hdr->ymin)/
									rtn->hdr->ystep);
			} else {
				/* In mercator mode, we've been working with the bounds in
				 * geographic coordinates, and hence we have to project the
				 * bounds and recompute the sizes.  We have to assume that the
				 * bounds are straight in the projected coordinate system,
				 * even though we know that they are not.
				 */
				 f64		min_x, min_y, max_x, max_y;
				 Projection	proj;
				 
				 if ((proj = projection_new_mercator(center_x, center_y, 0, 0))
				 		== NULL) {
					error_msgv(modname, "error: failed to construct bounds"
						" reprojection operator.\n");
					omg_release(rtn);
					fclose(f);
					return(NULL);
				}
				projection_ll_to_en_deg(proj, rtn->hdr->xmin, rtn->hdr->ymin,
										&min_x, &min_y);
				projection_ll_to_en_deg(proj, rtn->hdr->xmax, rtn->hdr->ymax,
										&max_x, &max_y);
				rtn->hdr->dx = (u32)((max_x - min_x)/rtn->hdr->xstep);
				rtn->hdr->dy = (u32)((max_y - min_y)/rtn->hdr->ystep);
			}
			
			if (rtn->hdr->projection == OMG_PROJECTION_ROTUTM)
				rtn->hdr->projection = OMG_PROJECTION_UTM;
			else {
				rtn->hdr->projection = OMG_PROJECTION_MERCATOR;
				/* We also have to unproject the coordinates, since mercator
				 * R4s should have bounds in lat/lon
				 */
			}
			
			error_msgv(modname, "info: OMG1 unrotated to (w,h)=(%d,%d), "
				"corners (%.1lf, %.1lf)->(%.1lf, %.1lf).\n",
				rtn->hdr->dx, rtn->hdr->dy, rtn->hdr->xmin, rtn->hdr->ymin,
				rtn->hdr->xmax, rtn->hdr->ymax);
			break;
		default:
			/* This is either not projected, projection unknown, or something
			 * wierd unknown to the rest of the library.
			 */
			error_msgv(modname, "internal: error: projection %d cannot be"
				" converted to internal representation.\n");
			omg_release(rtn);
			fclose(f);
			return(NULL);
	}
	
	if (header_only) {
		fclose(f);
		return(rtn);
	}
	
	space_rqd *= rtn->hdr->dx * rtn->hdr->dy;
	if ((rtn->data = malloc(space_rqd)) == NULL) {
		error_msgv(modname, "failed to allocate input space (%d bytes).\n",
			space_rqd);
		omg_release(rtn);
		fclose(f);
		return(NULL);
	}
	if (fread(rtn->data, space_rqd, 1, f) != 1) {
		error_msgv(modname, "failed reading file \"%s\" data.\n", name);
		omg_release(rtn);
		fclose(f);
		return(NULL);
	}
	npels = rtn->hdr->dx * rtn->hdr->dy;
	if (endian != ENDIAN_BIG) {
		if (rtn->type == OMG1_U16) {
			u16	*d = (u16*)(rtn->data);
			for (pel = 0; pel < npels; ++pel)
				ccom_swap_2(d+pel);
		} else if (rtn->type == OMG1_F32) {
			f32	*d = (f32*)(rtn->data);
			for (pel = 0; pel < npels; ++pel)
				ccom_swap_4(d+pel);
		}
		/* Don't need to swap for U8 */
	}
	if (rtn->type == OMG1_U8) {
		u8	invalid;
		u8	*data = ((u8*)(rtn->data));

		mapsheet_get_invalid(MAP_DATA_U8, &invalid);
		for (pel = 0; pel < npels; ++pel)
			if (data[pel] == 0) data[pel] = invalid;
	} else if (rtn->type == OMG1_U16) {
		u16	invalid, *data = ((u16*)(rtn->data));

		mapsheet_get_invalid(MAP_DATA_U16, &invalid);
		for (pel = 0; pel < npels; ++pel)
			if (data[pel] == 0) data[pel] = invalid;
	} else if (rtn->type == OMG1_F32) {
		f32 invalid, *data = ((f32*)(rtn->data));

		mapsheet_get_invalid(MAP_DATA_F32, &invalid);
		for (pel = 0; pel < npels; ++pel)
			if (data[pel] == 0.0f) data[pel] = invalid;
	} else {
		error_msgv(modname, "error: internal: unknown data type not trapped (%d).\n",
			rtn->type);
		omg_release(rtn);
		return(NULL);
	}

	return(rtn);
}

/* Routine:	omg_write
 * Purpose:	Write and OMG1file
 * Inputs:	*name	Name of the file to write
 *			raster	OMGRaster type structure
 * Outputs:	Success as a Bool
 * Comment:	This simply dumps the file, header first then data, assuming that
 *			the size in the header corresponds to the size of the data array.
 *			Expect very bad things to happen if this isn't the case ...
 */

Bool omg_write(const char *name, OMGRaster raster)
{
	u32		data_size, npels, pel;
	FILE	*f;
	RasterHdr	dummy_hdr;
	CcomEndian	endian;
	
	switch(raster->type) {
		case OMG1_U8:	data_size = 1; break;
		case OMG1_U16:	data_size = 2; break;
		case OMG1_F32:	data_size = 4; break;
		default:
			error_msgv(modname, "OMG1 type (%d) unknown.\n",
				(u32)(raster->type));
			return(False);
	}
	npels = raster->hdr->dx * raster->hdr->dy;
	
	memcpy(&dummy_hdr, raster->hdr, sizeof(RasterHdr));
	if ((endian = ccom_native_endian()) != ENDIAN_BIG) {
		/* OMG1/R4 are always written out big endian */
		ccom_swap_4(&(dummy_hdr.dx));
		ccom_swap_4(&(dummy_hdr.dy));
		ccom_swap_8(&(dummy_hdr.xmin));
		ccom_swap_8(&(dummy_hdr.xmax));
		ccom_swap_8(&(dummy_hdr.ymin));
		ccom_swap_8(&(dummy_hdr.ymax));
		ccom_swap_8(&(dummy_hdr.xstep));
		ccom_swap_8(&(dummy_hdr.ystep));
		ccom_swap_4(&(dummy_hdr.proj_merid));
		ccom_swap_4(&(dummy_hdr.sun_elev));
		ccom_swap_4(&(dummy_hdr.sun_azi));
		ccom_swap_4(&(dummy_hdr.sun_exag));
		ccom_swap_4(&(dummy_hdr.view_elev));
		ccom_swap_4(&(dummy_hdr.view_azi));
		ccom_swap_4(&(dummy_hdr.view_exag));
		ccom_swap_4(&(dummy_hdr.max_value));
		ccom_swap_4(&(dummy_hdr.min_value));
	}


	if ((f = fopen(name, "wb")) == NULL) {
		error_msgv(modname, "failed opening \"%s\" for output.\n", name);
		return(False);
	}
	if (fwrite(&dummy_hdr, sizeof(RasterHdr), 1, f) != 1) {
		error_msgv(modname, "failed writing \"%s\" header.\n", name);
		fclose(f);
		return(False);
	}

	npels = raster->hdr->dx * raster->hdr->dy;
	if (raster->type == OMG1_U8) {
		u8	invalid, *data = ((u8*)(raster->data));

		mapsheet_get_invalid(MAP_DATA_U8, &invalid);
		for (pel = 0; pel < npels; ++pel)
			if (data[pel] == invalid) data[pel] = 0;
	} else if (raster->type == OMG1_U16) {
		u16	invalid, *data = ((u16*)(raster->data));

		mapsheet_get_invalid(MAP_DATA_U16, &invalid);
		for (pel = 0; pel < npels; ++pel)
			if (data[pel] == invalid) data[pel] = 0;
	} else if (raster->type == OMG1_F32) {
		f32	invalid, *data = ((f32*)(raster->data));

		mapsheet_get_invalid(MAP_DATA_F32, &invalid);
		for (pel = 0; pel < npels; ++pel)
			if (data[pel] == invalid) data[pel] = 0.0f;
	} else {
		error_msgv(modname, "error: internal: unknown data type not trapped (%d).\n",
			raster->type);
		fclose(f);
		return(False);
	}

	if (endian == ENDIAN_BIG) {
		if (fwrite(raster->data, data_size, npels, f) != npels) {
			error_msgv(modname, "failed writing \"%s\" data.\n", name);
			fclose(f);
			return(False);
		}
	} else {
		/* We need to do this the hard way.  For anything other than
		 * OMG1/U8, we need to swap each element and then write it,
		 * praying that the ANSI C buffering system won't make this
		 * too painfully slow ...
		 */
		if (raster->type == OMG1_U8) {
			if (fwrite(raster->data, data_size, npels, f) != npels) {
				error_msgv(modname, "error: failed writing \"%s\" data.\n",
					name);
				fclose(f);
				return(False);
			}
		} else if (raster->type == OMG1_U16) {
			u16	swap;
			for (pel = 0; pel < npels; ++pel) {
				swap = *((u16*)raster->data+pel);
				ccom_swap_2(&swap);
				if (fwrite(&swap, 2, 1, f) != 1) {
					error_msgv(modname, "error: failed writing \"%s\""
						" pel %d on swap.\n", name, pel);
					fclose(f);
					return(False);
				}
			}
		} else {
			f32	swap;
			for (pel = 0; pel < npels; ++pel) {
				swap = *((f32*)raster->data+pel);
				ccom_swap_4(&swap);
				if (fwrite(&swap, 4, 1, f) != 1) {
					error_msgv(modname, "error: failed writing \"%s\""
						" pel %d on swap.\n", name, pel);
					fclose(f);
					return(False);
				}
			}
		}
	}
	fclose(f);
	return(True);
}

/* Routine:	omg_replace_data_filt
 * Purpose:	Replace data into an OMGRaster structure
 * Inputs:	raster		Raster to work on
 *			type		Type of data being added
 *			rows, cols	Size of data being passed
 *			*data		Pointer to the data to copy
 *			*null_data	Pointer to a value to use for 'No Data' when translating
 *						input into buffer, or NULL if no translation is to be
 *						done.
 *			op_null		Replacement value for null data in input, if filtering.
 * Outputs:	Returns True if data was reorganised, otherwise False
 * Comment:	This attempts to copy the new data into a structure, then releases
 *			the old data and replaces it with the new buffer, updating the OMG
 *			header structure in the process.  This means that a great deal of
 *			extra memory is going to be required, which may cause problems in
 *			some cases.  Note that this routine assumes that the data being
 *			passed is the same resolution and size as the raster already
 *			existing (but is possibly of a different type).  The code here
 *			checks the size being provided against the extant size, failing
 *			if they are different.
 *				In the case that the input data has 'No Data' values, we have
 *			to translate into something understood by other users of OMG1/R4,
 *			typically 0.0 for floats.  The *null_data pointer provides the
 *			recognition string to this, and op_null the replacement value.
 */

Bool omg_replace_data_filt(OMGRaster raster, OMGType type, u32 rows, u32 cols,
					  	   void *data, void *null_data, void *op_null)
{
	u32		data_size, target;
	void	*buffer;
	
	if (raster->hdr->dx != cols || raster->hdr->dy != rows) {
		error_msgv(modname, "error: size for replace_data (%dx%d) is not the"
			" same as that in the raster (%dx%d).\n", rows, cols,
			raster->hdr->dx, raster->hdr->dy);
		return(False);
	}
	switch(type) {
		case OMG1_U8:	data_size = 1; break;
		case OMG1_U16:	data_size = 2; break;
		case OMG1_F32:	data_size = 4; break;
		default:
			error_msgv(modname, "internal: OMG1 type (%d) unknown.\n",
				(u32)type);
			return(False);
	}
	target = data_size * rows * cols;
	if ((buffer = malloc(target)) == NULL) {
		error_msgv(modname, "failed to allocate copy buffer for add_data (%d "
			"bytes).\n", target);
		return(False);
	}
	memcpy(buffer, data, target);
	if (raster->data != NULL) free(raster->data);
	raster->data = buffer;
	raster->type = type;
	raster->hdr->data_type = (u8)type;
	
	/* Input filtering.  If 'No Data' values are present, then we need to
	 * replace them with the *op_null value.
	 */
	if (null_data != NULL) {
		u32	npels = raster->hdr->dx * raster->hdr->dy, pel;
		switch(type) {
			case OMG1_U8:
				{
					u8	*ptr = (u8*)(raster->data);
					u8	test_pt = *((u8*)null_data), rep_pt = *((u8*)op_null);
					for (pel = 0; pel < npels; ++pel)
						if (*ptr == test_pt)
							*ptr++ = rep_pt;
						else
							++ptr;
				}
				break;
			case OMG1_U16:
				{
					u16	*ptr = (u16*)(raster->data);
					u16	test_pt = *((u16*)null_data), rep_pt = *((u16*)op_null);
					for (pel = 0; pel < npels; ++pel)
						if (*ptr == test_pt)
							*ptr++ = rep_pt;
						else
							++ptr;
				}
				break;
			case OMG1_F32:
				{
					f32	*ptr = (f32*)(raster->data);
					f32	test_pt = *((f32*)null_data), rep_pt = *((f32*)op_null);
					for (pel = 0; pel < npels; ++pel)
						if (*ptr == test_pt)
							*ptr++ = rep_pt;
						else
							++ptr;
				}
				break;
			default:
				error_msgv(modname, "internal: OMG1 type (%d) unknown.\n",
					(u32)type);
				return(False);
		}
	}
	return(True);
}

/* Routine:	omg_replace_data
 * Purpose:	Replace data in an OMG1 structure.
 * Inputs:	(see omg_replace_data_filt())
 * Outputs:	(see omg_replace_data_filt())
 * Comment:	This is a call-through to omg_replace_data_filt() without the
 *			filtering --- it just does a direct replacement of the data in the
 *			OMGRaster structure.
 */

Bool omg_replace_data(OMGRaster raster, OMGType type, u32 rows, u32 cols,
					  void *data)
{
	return(omg_replace_data_filt(raster, type, rows, cols, data, NULL, NULL));
}

/* Routine:	omg_check_surfaces
 * Purpose:	Check that the two surfaces specified are suitable for use
 * Inputs:	*ras1, *ras2	Surfaces to check
 * Outputs:	True if the surfaces are OK, otherwise False.
 * Comment:	This routine checks that the surfaces are of the same physical
 *			dimensions, that they are in the same projection, and of the same
 *			area, that they are floating point surfaces, and that the grid
 *			cells are square in the projected coordinate system.  Otherwise,
 *			an error message indicating the problem is printed and False is
 *			returned.
 */

Bool omg_check_surfaces(OMGRaster ras1, OMGRaster ras2)
{
	f64	dx1, dx2, dy1, dy2;				/* Cell spacings */
	u32	w1, w2, h1, h2;					/* Width and Height */
	f64	l1, l2, r1, r2, t1, t2, b1, b2;	/* Bounds */
	Projection	p1, p2;
	
	omg_get_sizes(ras1, &w1, &h1); omg_get_sizes(ras2, &w2, &h2);
	omg_get_spacings(ras1, &dx1, &dy1); omg_get_spacings(ras2, &dx2, &dy2);
	omg_get_bounds(ras1, &l1, &b1, &r1, &t1);
	omg_get_bounds(ras2, &l2, &b2, &r2, &t2);
	p1 = omg_get_projection(ras1); p2 = omg_get_projection(ras2);
	
	if (projection_get_type(p1) != projection_get_type(p2)) {
		error_msg(modname, "raster 1 and raster 2 have different projection"
			" systems.\n");
		return(False);
	}
	if (projection_get_ellipsoid(p1) != projection_get_ellipsoid(p2)) {
		error_msg(modname, "raster 1 and raster 2 are projected onto different"
			" ellipsoids.\n");
		return(False);
	}
	if (l1 != l2 || r1 != r2 || t1 != t2 || b1 != b2) {
		error_msgv(modname, "raster 1 location (%g, %g)->(%g, %g) is not"
			" the same as raster 2 location (%g, %g)->(%g, %g).\n",
			l1, b1, r1, t1, l2, b2, r2, t2);
		return(False);
	}
	if (w1 != w2 || h1 != h2) {
		error_msgv(modname, "raster 1 size (%dx%d) is not the same as"
			" raster 2 size (%dx%d).\n", h1, w1, h2, w2);
		return(False);
	}
	if (dx1 != dx2 || dy1 != dy2) {
		error_msg(modname, "raster files are inconsistent (projections,"
			" ellipsoids, bounds and dimensions match, but spacings don't).\n");
		return(False);
	}
	if (dx1 != dy1) {
		error_msgv(modname, "raster cells (%g, %g) are not square in the "
			"projected coordinate system.\n", dx1, dy1);
		return(False);
	}
	return(True);
}
