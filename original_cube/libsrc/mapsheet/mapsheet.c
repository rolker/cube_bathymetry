/*
 * $Id: mapsheet.c 25 2006-01-20 16:24:40Z brc $
 * $Log$
 * Revision 1.3  2006/01/20 16:24:40  brc
 * Added sentinel values to mapsheet_save_sheet_v201() to ensure that the
 * memory for filename is only free()d if it's used.
 *
 * Revision 1.2  2006/01/19 16:02:38  brc
 * Fixed memory leak in mapsheet_make_directory(), and updated
 * mapsheet_release_data() and mapsheet_delete_depth_surface() to
 * support fix to mapsheet_cube_release(); all reported by Ding Zheng at
 * Reson bv.
 *
 * Revision 1.1.1.1  2003/02/03 20:18:43  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2.4.3  2003/01/31 21:47:13  dneville
 * (BRC)  Added code for nomination, reset of nomination, extraction and
 * removal of hypotheses for HyperCUBE surfaces.  It is difficult to expose these
 * at this level because the calls have to go through a number of layers to make
 * it to the data, leading to inefficiency.
 *
 * Revision 1.2.4.2  2003/01/28 15:33:43  dneville
 * (BRC) Added mapsheet_new_direct() and _backed() to allow direct
 * specification of the number of elements in the sheet, with the northwest
 * corner used as a georef point.  This makes it easier to match other people's
 * setups.
 *
 * Revision 1.2.4.1  2003/01/28 14:30:00  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.16.2.2  2002/12/15 01:34:42  brc
 * Extended maximum MapSheet size to 20000 nodes.  Extended multiple data-surface
 * readback to the user level (support for summarise, making it much faster to read back
 * multiple surfaces since the disam. engine only needs to run once).  Added a call-through
 * to allow interactive requests for the MapSheet to minimise its memory usage.  This can
 * be an important step when the code runs ofr a significant length of time.
 *
 * Revision 1.16.2.1  2002/07/14 02:20:46  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.16  2002/06/16 02:34:23  brc
 * Added checks on width and height of mapsheets being constructed to make
 * sure that the user doesn't pass any negative values (which lead to *very*
 * big sheets once they get cast to u32!).
 *
 * Revision 1.15  2002/05/10 21:03:16  brc
 * Multiple modifications as a part of the verification project.
 * 1. Added code to extrac the hypothesis strength ratio through the usual
 * channels.
 * 2. Added code to compute the spike angle along the same lines as the
 * despiker in ccom_general.c.  This is used as part of the AOI generation code
 * (q.v.) as a measure of the 'spikiness' of the data in the AOI --- the more
 * spikes it finds of significant angle, the more likely it is that CUBE
 * didn't get the reconstruction right.
 * 3. Added mapsheet_analyse_sheet() to generate AOIs from the mapsheet, and
 * compute a linked list of enumerated 'areas of interest'.  This code also
 * computes the basic statistics that describe the AOI, but does not compute
 * and measure of 'significance' for the AOI (left to external code).
 * 4. Added mapsheet_add_depth_fixunct() and mapsheet_add_depth_unct() to drive
 * the new full uncertainty initialisation process used when building the
 * predicted depth surface in HyperCUBE grids.  Note that currently the full
 * uncertainty construction only works for HyperCUBE and it isn't likely to
 * be uprated for anything else RSN.
 *
 * Revision 1.14  2002/03/14 04:31:24  brc
 * Added some basic modifications for port to Win32.  Fixed bug in directory
 * construction code which resulted in a heap corruption (end-member overwrite).
 * Added a little more error reporting to mapsheet read and added information
 * on the depth algorithm being read from a mapsheet so that the data reader
 * can determine what is going on (caused bugs with mean/median surfaces).
 * Fixed bug in backing store management which caused heap errors since the
 * string used to hold the location of a backing store specified in the mapsheet
 * ASCII description wasn't heap allocated, but allocated as a local automatic
 * in the mapsheet parser.
 *
 * Revision 1.13  2001/12/20 00:41:35  roland
 * Made a few fixes to make it compile under Linux
 *
 * Revision 1.12  2001/12/07 20:59:17  brc
 * Fixed bug with buffer over-run in directory name construction.  Added the
 * user-level call to dump hypotheses to a GDP (not recommended for most users
 * though, since this can be enormous).  Changed the current file version to
 * 2.0.1 to support the use of f64's in headers, rather than f32's for the
 * mapsheet bounds.  This ensures that we have sufficient dynamic range for
 * UTM coordinates with small node spacing (e.g., 0.5m).  This does not implement
 * a completely new file format, just a switch at the header reading stage,
 * plus some patching to ensure that we use the same header structure for V1/2.0.0
 * that we always did, but then convert to the current internal version when
 * a sheet is loaded.  This *should* make things seamless. :-)
 *
 * Revision 1.11  2001/09/20 18:19:42  brc
 * Added V2 mapsheets to support HyperCUBE's backing store tile caching by
 * implementing the mapsheet as a directory, with each of the components as a
 * separate file.  Added invalid data point database so that other modules can
 * find out what we use to mark 'no data'.  Added hypothesis surface as a
 * read-back component tied into HyperCUBE (mapsheet_cube.c) and the nodal
 * surface (mapsheet_nodal.c) and integrated into sizing calls.  Integrated
 * HyperCUBE (mapsheet_cube.c) as an estimation method.  Modified read-back
 * routines so that users can supply their own data buffers for speed.  Modified
 * construction routines so that users can supply a location for a backing store,
 * and to make one automatically if they don't (callthroughs to new routines
 * from the old names to ensure backwards compatability).  Added routines to
 * manipulate names for backing store construction so that names are consistent
 * throughout the sub-modules.
 *
 * Revision 1.10  2001/08/28 16:01:13  brc
 * Added facility to allow the mapsheet parameters to be reset to those
 * currently in force.  This is required if the parameters on a mapsheet need
 * to be changed after a save/load cycle (otherwise you're stuck with whatever
 * is in the sheet, since the parameter construction is not run on load).  There
 * are very few checks that the new parameters currently in place make sense
 * w.r.t. the sheet (e.g., median queue lengths, estimation modes), so users
 * should be wary of indiscriminate use of this method.
 *
 * Revision 1.9  2001/08/21 01:45:42  brc
 * Added facility to count the number of soundings actually used from each swath
 * to update the data in the mapsheet region.  This can be used, among other things,
 * to determine which data files in a set are actually used in updating the data.
 *
 * Revision 1.8  2001/08/11 00:02:20  brc
 * Added mapsheet parser to core code, rather than having it hidden in the
 * utilities section.  This also means that the interface is nicely hidden, and
 * that the user just sees mapsheet_new_from_ascii().
 *
 * Revision 1.7  2001/05/16 21:15:20  brc
 * Added debuging for center of sheet value to ensure that it was being
 * computed correctly under Linux
 *
 * Revision 1.6  2001/05/15 01:49:12  brc
 * Turned off debugging code.
 *
 * Revision 1.5  2001/05/14 21:04:59  brc
 * Added code to make the system 'params'-aware (i.e., provide routine for the
 * parameters executive to call).  Made static parameters variables so that they
 * can be modified, and integrated modifications into the code.  Added error
 * checking for return values from the various depth extraction routines.
 * Added code to support making a summary of the contents of a mapsheet by
 * making a summary directory, and then walking all known components of the
 * mapsheet and calling their extraction code.  Data are written as TIFs.
 * Added facility to record the IHO survey order associated with the survey so
 * that client modules can use this to determine error limits if required.
 *
 * Revision 1.4  2001/04/10 22:52:53  brc
 * Re-organised the output and input serialisers so that they have better
 * encapsulated code.  Added standard deviation retrieval for sample
 * statistics in mapsheet_nodal.c.  Added interface for initialisation of the
 * mapsheet_nodal.c structures using a TIFF image.
 *
 * Revision 1.3  2000/12/03 19:47:35  brc
 * Multiple modifications in support of debugging.  Made mapsheet_contains()
 * a global to support tests in user level code (i.e., to make sure that a
 * mapsheet loaded from file actually has the attributes required by the code).
 * Added mapsheet_finalise() to support the mapsheet_nodal.c concept of buffer
 * flushing before readback of the derived surfaces.  Added mapsheet_count_hits()
 * to allow densities to be evaluated on a binned grid surface.  Corrected errors
 * in mapsheet_new_by_bounds(), mapsheet_new_by_proj_bounds() and
 * mapsheet_new_from_omg() to ensure that the projected bounds and offsets now
 * do actually correspond to the required specifications (debuging for AGU was the
 * first time they had actually been used!).
 *
 * Revision 1.2  2000/09/07 21:11:17  brc
 * Modified mapsheet code to allow the bin depth (i.e., number of soundings
 * held in a bin before replacement starts to occur) to be specified by the
 * user.  This allows us to deal with slightly larger areas by limiting the
 * depth in any one bin.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:25  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	mapsheet.c
 * Purpose:	Functions to manipulate the map-sheet datastructure.
 * Date:	04 July 2000
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
#include <sys/types.h>
#include <sys/stat.h>
#ifdef WIN32
	#include <direct.h>
	#include <io.h>
	#define S_ISDIR(x) (((x) & _S_IFDIR) != 0)
#else
	#include <sys/dir.h>
#endif
#include <fcntl.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include <errno.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include "stdtypes.h"
#include "error.h"
#include "ccom_general.h"
#include "projection.h"
#include "mapsheet.h"
#include "mapsheet_private.h"	/* Private types - internal representation */
#include "mapsheet_private_parser.h"
#include "mapsheet_par.h"
#include "mapsheet_mean.h"		/* Mean binning algorithm for depth */
#include "mapsheet_median.h"	/* Median binning algorithm for depth */
#include "mapsheet_nodal.h"		/* Kalman filter algorithm for depth */
#include "mapsheet_cube.h"		/* CUBE in a modular for with super-grids */
#include "errmod.h"
#include "params.h"

#undef __DEBUG__

static char *modname = "mapsheet";
static char *modrev = "$Revision: 25 $";

/* Interface variables for the mapsheet ASCII parser sub-module */
extern FILE 		*mapin;
extern SheetList	*sheets;

/* --------------------------------------------------------------------------
 *                            Private Types
 * -------------------------------------------------------------------------- */

#define MAPSHEET_ID 			0x4D534854L		/* aka 'MSHT' */
#define MAPSHEET_VERSION_1		1U				/* Version 1 definitions */
#define MAPSHEET_VERSION_2		200U			/* Version 2.0.0 definitions */
#define MAPSHEET_VERSION_201	201U			/* Version 2.0.1 definitions */
#define MAPSHEET_CUR_VERSION	MAPSHEET_VERSION_201	/* Version on write */
#define MAPSHEET_FILE_ID		"id"			/* Name to look for in V>2 */

/* Customise the backup temporary directory location and directory separator
 * depending on operating system (we assume either *nix or Win32 only).  Note
 * that this backing store location is only use if:
 *	(a) The mapsheet doesn't specify a backing store, and
 *	(b)	The LIBCCOMTMP environment variable doesn't exist.
 */

#ifdef WIN32
	#define DEFAULT_BASE_BACKING_STORE	"C:\\Temp"
	#define DIR_SEP						'\\'
#else
	#define DEFAULT_BASE_BACKING_STORE	"/usr/tmp"
	#define DIR_SEP						'/'
#endif

#define DEFAULT_MAX_HEIGHT		10000	/* Maximum size of mapsheet in pels */
#define MIN_MAX_HEIGHT			1
#define MAX_MAX_HEIGHT			20000

#define DEFAULT_MAX_WIDTH		10000
#define MIN_MAX_WIDTH			1
#define MAX_MAX_WIDTH			20000

static u32 mapsheet_max_width = DEFAULT_MAX_WIDTH;
static u32 mapsheet_max_height = DEFAULT_MAX_HEIGHT;

#define DEG2RAD(x) ((x)*M_PI/180.0)
#define RAD2DEG(x) ((x)*180.0/M_PI)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

/* A Pre-header for the mapsheet file */
typedef struct {
	u32	magic_num;
	u32	version;
} MapPreHeader;

/* --------------------------------------------------------------------------
 *                            Access Methods
 * -------------------------------------------------------------------------- */

Projection mapsheet_get_projection(MapSheet sheet)
{
	return(sheet->hdr->projection);
}

void mapsheet_get_center(MapSheet sheet, f64p east, f64p north)
{
	*east = sheet->hdr->easting;
	*north = sheet->hdr->northing;
}

u32 mapsheet_get_width(MapSheet sheet)
{
	return(sheet->hdr->n_east);
}

u32 mapsheet_get_height(MapSheet sheet)
{
	return(sheet->hdr->n_north);
}

void mapsheet_get_width_height(MapSheet sheet, u32p width, u32p height)
{
	*width = sheet->hdr->n_east;
	*height = sheet->hdr->n_north;
}

/* Routine:	mapsheet_get_tile_width_height
 * Purpose:	Returns the width/height of tiles (if any) in the mapsheet
 * Inputs:	sheet	MapSheet to work on
 * Outputs:	Returns size of the mapsheet if there are no tiles, or the size
 *			of individual tiles if there are (see comment).
 * Comment:	Note that a MapSheet may be tiled in one component and not in
 *			another.  However, currently the only tiled surface is HyperCUBE,
 *			so other MapSheets will return simply width/height.  Note that all
 *			tiled systems may not have square tiles, and might not have the
 *			same size tiles on the right hand side or bottom row of tiles (so
 *			that they match up with the actual size of the MapSheet).  The value
 *			returned here is the size of the main sequence tiles.
 */

void mapsheet_get_tile_width_height(MapSheet sheet, u32p width, u32p height)
{
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_HYPERCUBE:
			mapsheet_cube_get_tilesize(sheet->grid->hcube_grid, width, height);
			break;
		default:
			mapsheet_get_width_height(sheet, width, height);
			break;
	}
}

void mapsheet_get_spacing(MapSheet sheet, f64p east, f64p north)
{
	*east = sheet->hdr->east_sz;
	*north = sheet->hdr->north_sz;
}

/* Routine:	mapsheet_set_caching
 * Purpose:	Set type of data to cache in estimation module
 * Inputs:	sheet	MapSheet to work from
 *			cache	Element to cache
 * Outputs:	-
 * Comment:	This calls through to the underlying modules, passing on the
 *			caching request.  Modules are free to ignore such requests,
 *			particularly if they don't support the data type requested.
 *			Currently, only the HyperCUBE algorithm (and the underlying CUBE
 *			module) support such caching.
 */

void mapsheet_set_caching(MapSheet sheet, MapCache cache)
{
	if (sheet->hdr->depth == MAP_DEPTH_HYPERCUBE)
		mapsheet_cube_set_caching(sheet->grid->hcube_grid, cache);
}

/* Routine:	mapsheet_get_caching
 * Purpose:	Return the type of data being cached
 * Inputs:	sheet	MapSheet to work from
 * Outputs:	Returns a MapCache enum to indicate the type of data being
 *			cached.
 * Comment:	This calls through to the HyperCUBE module, currently the only
 *			estimation module capable of caching.  If it isn't being used in
 *			the mapsheet, MAP_CACHE_NONE is returned.
 */

MapCache mapsheet_get_caching(MapSheet sheet)
{
	if (sheet->hdr->depth == MAP_DEPTH_HYPERCUBE)
		return(mapsheet_cube_get_caching(sheet->grid->hcube_grid));
	else
		return(MAP_CACHE_NONE);
}

/* Routine:	mapsheet_get_invalid
 * Purpose:	Return value used to mark invalid data
 * Inputs:	type	MapSheetDataType to provide information for
 * Outputs:	*val	Set to invalid data value
 * Comment:	Note that *val must point to a value of the correct type for
 *			correct interpretation, and at least to one of the same size so
 *			that this doesn't stamp somewhere is shouldn't.
 */

void mapsheet_get_invalid(MapSheetDataType type, void *val)
{
	switch(type) {
		case MAP_DATA_U8:
			*((u8*)val) = 255;
			break;
		case MAP_DATA_U16:
			*((u16*)val) = 65535;
			break;
		case MAP_DATA_U32:
			*((u32*)val) = INT_MAX;
			break;
		case MAP_DATA_F32:
			*((f32*)val) = FLT_MAX;
			break;
		case MAP_DATA_F64:
			*((f64*)val) = DBL_MAX;
			break;
		default:
			error_msgv(modname, "internal: error: unknown data type (%d) in"
				" call to mapsheet_get_invalid().\n", (u32)type);
			break;
	}
}

Bool mapsheet_contains(MapSheet sheet, MapSheetElem data)
{
	switch(data) {
		case MAP_DEPTH_UNCERTAINTY:
			if (sheet->hdr->depth == MAP_DEPTH_NODAL ||
				sheet->hdr->depth == MAP_DEPTH_HYPERCUBE) return(True);
			break;
		case MAP_DEPTH_ESTIMATE:
			if (sheet->hdr->depth != MAP_DEPTH_NONE) return(True);
			break;
		case MAP_DEPTH_HYPOTHESES:
			if (sheet->hdr->depth == MAP_DEPTH_NODAL ||
				sheet->hdr->depth == MAP_DEPTH_HYPERCUBE) return(True);
			break;
		case MAP_NODE_HITS:
			if (sheet->grid->hits_grid != NULL) return(True);
			break;
		case MAP_BACKSCATTER:
			if (sheet->grid->refl_grid != NULL) return(True);
			break;
		case MAP_DEPTH_SDEV:
			if (sheet->hdr->depth == MAP_DEPTH_NODAL) return(True);
			break;
		case MAP_HYPOTHESIS_STRENGTH:
			if (sheet->hdr->depth == MAP_DEPTH_HYPERCUBE) return(True);
			break;
	}
	return(False);
}

u32 mapsheet_get_data_size(MapSheet sheet, MapSheetElem data)
{
	u32	size;
	
	if (!mapsheet_contains(sheet, data)) {
		error_msgv(modname, "map-sheet does not contain type %d.\n",
			(u32)data);
		return(0);
	}
	
	switch(data) {
		case MAP_DEPTH_ESTIMATE:	size = sizeof(f32); break;
		case MAP_DEPTH_UNCERTAINTY:	size = sizeof(f32); break;
		case MAP_DEPTH_HYPOTHESES:	size = sizeof(u8);	break;
		case MAP_NODE_HITS:			size = sizeof(u32); break;
		case MAP_BACKSCATTER:		size = sizeof(u16); break;
		case MAP_DEPTH_SDEV:		size = sizeof(f32); break;
		case MAP_HYPOTHESIS_STRENGTH:	size = sizeof(f32); break;
		default:
			error_msgv(modname, "internal: data element %d not known.\n",
						(u32)data);
			return(0);
	}
	return(size);
}

MapSheetDataType mapsheet_get_data_type(MapSheet sheet, MapSheetElem data)
{
	MapSheetDataType	type;
	
	if (!mapsheet_contains(sheet, data)) {
		error_msgv(modname, "map-sheet does not contain type %d.\n",
			(u32)data);
		return(MAP_DATA_ERROR);
	}
	
	switch(data) {
		case MAP_DEPTH_ESTIMATE:	type = MAP_DATA_F32; break;
		case MAP_DEPTH_UNCERTAINTY:	type = MAP_DATA_F32; break;
		case MAP_DEPTH_HYPOTHESES:	type = MAP_DATA_U8;	 break;
		case MAP_NODE_HITS:			type = MAP_DATA_U32; break;
		case MAP_BACKSCATTER:		type = MAP_DATA_U16; break;
		case MAP_DEPTH_SDEV:		type = MAP_DATA_F32; break;
		case MAP_HYPOTHESIS_STRENGTH:	type = MAP_DATA_F32; break;
		default:
			error_msgv(modname, "internal: data element %d not known.\n",
						(u32)data);
			return(MAP_DATA_ERROR);
	}
	return(type);
}

static void *mapsheet_get_depth(MapSheet sheet, u32 width, u32 height,
				void *user_data)
{
	f32			*rtn;
	u32			size = width*height*sizeof(f32);
	
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_NODAL:
			if (sheet->grid->depth_grid == NULL) return(NULL);
			break;
		case MAP_DEPTH_HYPERCUBE:
			if (sheet->grid->hcube_grid == NULL) return(NULL);
			break;
		case MAP_DEPTH_BINED_MEAN:
		case MAP_DEPTH_BINED_MEDIAN:
			if (sheet->grid->bin_grid == NULL) return(NULL);
			break;
		default:
			error_msgv(modname, "internal: depth algorithm %d unknown.\n",
				(u32)(sheet->hdr->depth));
			return(NULL);
	}
	
	if (user_data == NULL) {
		if ((rtn = (f32p)malloc(size)) == NULL) {
			error_msgv(modname, "failed allocating %d bytes for depth.\n", size);
			return(NULL);
		}
	} else
		rtn = (f32p)user_data;
	
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_NODAL:
			if (!mapsheet_nodal_get_depth(sheet, sheet->grid->depth_grid,
									 		sheet->param->depth_param, rtn)) {
				error_msg(modname, "failed extracting depth surface.\n");
				if (user_data == NULL) { free(rtn); rtn = NULL; }
			}
			break;
		case MAP_DEPTH_HYPERCUBE:
			if (!mapsheet_cube_get_depth(sheet, sheet->grid->hcube_grid,
											sheet->param->hcube_param, rtn)) {
				error_msg(modname, "error: failed extracting depth surface.\n");
				if (user_data == NULL) { free(rtn); rtn = NULL; }
			}
			break;
		case MAP_DEPTH_BINED_MEAN:
			mapsheet_bin_by_mean(sheet->grid->bin_grid, width, height,
								sheet->param->bin_param, rtn);
			break;
		case MAP_DEPTH_BINED_MEDIAN:
			mapsheet_bin_by_median(sheet->grid->bin_grid, width, height,
								sheet->param->bin_param, rtn);
			break;
		default:
			error_msg(modname, "internal: mapsheet_get_depth() default.\n");
			if (user_data == NULL) { free(rtn); rtn = NULL; }
			break;
	}
	
	return((void*)rtn);
}

static void *mapsheet_get_depth_uncertainty(MapSheet sheet, u32 width,
											u32 height, void *user_data)
{
	f32			*rtn;
	u32			size = width*height*sizeof(f32);
	
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_NODAL:
			if (sheet->grid->depth_grid == NULL) return(NULL);
			break;
		case MAP_DEPTH_HYPERCUBE:
			if (sheet->grid->hcube_grid == NULL) return(NULL);
			break;
		case MAP_DEPTH_BINED_MEAN:
		case MAP_DEPTH_BINED_MEDIAN:
			return(NULL); /* For the time being, these cannot estimate the
						   * uncertainty in their depths.
						   */
		default:
			error_msgv(modname, "internal: depth algorithm %d unknown.\n",
				(u32)(sheet->hdr->depth));
			return(NULL);
	}
	
	if (user_data == NULL) {
		if ((rtn = (f32p)malloc(size)) == NULL) {
			error_msgv(modname, "failed allocating %d bytes for depth.\n", size);
			return(NULL);
		}
	} else
		rtn = (f32p)user_data;
	
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_NODAL:
			if (!mapsheet_nodal_get_uncertainty(sheet, sheet->grid->depth_grid,
										   	sheet->param->depth_param, rtn)) {
				error_msg(modname, "failed extracting uncertainty surface.\n");
				if (user_data == NULL) { free(rtn); rtn = NULL; }
			}
			break;
		case MAP_DEPTH_HYPERCUBE:
			if (!mapsheet_cube_get_uncertainty(sheet, sheet->grid->hcube_grid,
											sheet->param->hcube_param, rtn)) {
				error_msg(modname, "error: failed extracting uncertainty"
							" surface.\n");
				if (user_data == NULL) { free(rtn); rtn = NULL; }
			}
			break;
		default:
			error_msg(modname, "internal: mapsheet_get_depth_uncertainty()"
				" default.\n");
			if (user_data == NULL) { free(rtn); rtn = NULL; }
			break;
	}
	
	return((void*)rtn);
}

static void *mapsheet_get_hypotheses(MapSheet sheet, u32 width, u32 height,
									void *user_data)
{
	u8			*rtn;
	u32			size = width*height*sizeof(u8);
	
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_NODAL:
			if (sheet->grid->depth_grid == NULL) return(NULL);
			break;
		case MAP_DEPTH_HYPERCUBE:
			if (sheet->grid->hcube_grid == NULL) return(NULL);
			break;
		case MAP_DEPTH_BINED_MEAN:
		case MAP_DEPTH_BINED_MEDIAN:
			return(NULL); /* For the time being, these cannot estimate
						   * multiple hypotheses (and never will)
						   */
		default:
			error_msgv(modname, "internal: depth algorithm %d unknown.\n",
				(u32)(sheet->hdr->depth));
			return(NULL);
	}
	
	if (user_data == NULL) {
		if ((rtn = (u8p)malloc(size)) == NULL) {
			error_msgv(modname, "failed allocating %d bytes for hypotheses.\n", size);
			return(NULL);
		}
	} else
		rtn = (u8p)user_data;
	
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_NODAL:
			if (!mapsheet_nodal_get_hypo(sheet, rtn)) {
				error_msg(modname, "failed extracting hypothesis surface.\n");
				if (user_data == NULL) { free(rtn); rtn = NULL; }
			}
			break;
		case MAP_DEPTH_HYPERCUBE:
			if (!mapsheet_cube_get_hypo(sheet, sheet->grid->hcube_grid,
											sheet->param->hcube_param, rtn)) {
				error_msg(modname, "error: failed extracting uncertainty"
							" surface.\n");
				if (user_data == NULL) { free(rtn); rtn = NULL; }
			}
			break;
		default:
			error_msg(modname, "internal: mapsheet_get_hypotheses()"
				" default.\n");
			if (user_data == NULL) { free(rtn); rtn = NULL; }
			break;
	}
	
	return((void*)rtn);
}

static void *mapsheet_get_hypo_strength(MapSheet sheet, u32 width, u32 height,
										void *user_data)
{
	f32			*rtn;
	u32			size = width*height*sizeof(f32);
	
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_HYPERCUBE:
			if (sheet->grid->hcube_grid == NULL) return(NULL);
			break;
		case MAP_DEPTH_NODAL:
		case MAP_DEPTH_BINED_MEAN:
		case MAP_DEPTH_BINED_MEDIAN:
			return(NULL); /* For the time being, these cannot estimate
						   * hypothesis strength (and never will)
						   */
		default:
			error_msgv(modname, "internal: depth algorithm %d unknown.\n",
				(u32)(sheet->hdr->depth));
			return(NULL);
	}
	
	if (user_data == NULL) {
		if ((rtn = (f32p)malloc(size)) == NULL) {
			error_msgv(modname, "failed allocating %d bytes for hypothesis"
				"strengths.\n", size);
			return(NULL);
		}
	} else
		rtn = (f32p)user_data;
	
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_HYPERCUBE:
			if (!mapsheet_cube_get_hypo_strength(sheet, sheet->grid->hcube_grid,
											sheet->param->hcube_param, rtn)) {
				error_msg(modname, "error: failed extracting uncertainty"
							" surface.\n");
				if (user_data == NULL) { free(rtn); rtn = NULL; }
			}
			break;
		default:
			error_msg(modname, "internal: mapsheet_get_hypo_strength()"
				" default.\n");
			if (user_data == NULL) { free(rtn); rtn = NULL; }
			break;
	}
	
	return((void*)rtn);
}

static void *mapsheet_get_depth_sdev(MapSheet sheet, u32 width, u32 height,
									 void *user_data)
{
	f32			*rtn;
	u32			size = width*height*sizeof(f32);
	
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_NODAL:
			if (sheet->grid->depth_grid == NULL) return(NULL);
			break;
		case MAP_DEPTH_HYPERCUBE:	/* CUBE doesn't track std. dev. */
		case MAP_DEPTH_BINED_MEAN:
		case MAP_DEPTH_BINED_MEDIAN:
			return(NULL); /* For the time being, these cannot estimate the
						   * std. dev. in their depths.
						   */
		default:
			error_msgv(modname, "internal: depth algorithm %d unknown.\n",
				(u32)(sheet->hdr->depth));
			return(NULL);
	}
	
	if (user_data == NULL) {
		if ((rtn = (f32p)malloc(size)) == NULL) {
			error_msgv(modname, "failed allocating %d bytes for depth.\n", size);
			return(NULL);
		}
	} else
		rtn = (f32p)user_data;
	
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_NODAL:
			mapsheet_nodal_get_sdev(sheet, sheet->grid->depth_grid,
										   sheet->param->depth_param, rtn);
			break;
		default:
			error_msg(modname, "internal: mapsheet_get_depth_sdev() default.\n");
			if (user_data == NULL) { free(rtn); rtn = NULL; }
			break;
	}
	
	return((void*)rtn);
}

void *mapsheet_get_data_user(MapSheet sheet, MapSheetElem data, void *user_data)
{
	void	*rtn;
	u32		width = sheet->hdr->n_east, height = sheet->hdr->n_north, size;
	
	switch(data) {
		case MAP_DEPTH_ESTIMATE:
			rtn = mapsheet_get_depth(sheet, width, height, user_data);
			break;
		case MAP_DEPTH_UNCERTAINTY:
			rtn = mapsheet_get_depth_uncertainty(sheet, width, height, user_data);
			break;
		case MAP_DEPTH_HYPOTHESES:
			rtn = mapsheet_get_hypotheses(sheet, width, height, user_data);
			break;
		case MAP_HYPOTHESIS_STRENGTH:
			rtn = mapsheet_get_hypo_strength(sheet, width, height, user_data);
			break;
		case MAP_NODE_HITS:
			if (sheet->grid->hits_grid != NULL) {
				if (user_data == NULL) {
					size = width*height*sizeof(u32);
					if ((rtn = malloc(size)) == NULL)
						error_msgv(modname, "failed to allocate hit count "
							"space (%d bytes).\n", size);
				} else
					rtn = (u32*)user_data;
				memcpy(rtn, sheet->grid->hits_grid[0], size);
			}
			break;
		case MAP_BACKSCATTER:
			if (sheet->grid->refl_grid != NULL) {
				if (user_data == NULL) {
					size = width*height*sizeof(u16);
					if ((rtn = malloc(size)) == NULL)
						error_msgv(modname, "failed to allocate backscatter "
							"space (%d bytes).\n", size);
				} else
					rtn = (u16*)user_data;
				memcpy(rtn, sheet->grid->refl_grid[0], size);
			}
			break;
		case MAP_DEPTH_SDEV:
			rtn = mapsheet_get_depth_sdev(sheet, width, height, user_data);
			break;
		default:
			error_msgv(modname, "internal: data method %d not known.\n",
				(u32)data);
			return(NULL);
	}
	if (rtn == NULL)
		error_msgv(modname, "map-sheet does not contain data (type %d).\n",
					(u32)data);
	return(rtn);
}

void *mapsheet_get_data(MapSheet sheet, MapSheetElem data)
{
	return(mapsheet_get_data_user(sheet, data, NULL));
}

/* Routine: mapsheet_release_databuffer
 * Purpose:	Release all memory currently associated with the MapDataBuffer
 * Inputs:	*buffer	Pointer to buffer on which to work
 * Outputs:	-
 * Comment:	Note that this does not de-allocate the buffer structure itself,
 *			should it be dynamically allocated.
 */

void mapsheet_release_databuffer(MapDataBuffer *buffer)
{
	if (buffer != NULL) {
		free(buffer->depth);
		free(buffer->uncrt);
		free(buffer->hypstr);
		free(buffer->nhypo);
		free(buffer->nhits);
		free(buffer->sdev);
		free(buffer->backscatter);
	}
}

/* Routine:	mapsheet_get_mult_data
 * Purpose:	An internal routine to allow the HyperCUBE surface to be read back
 * Inputs:	sheet	MapSheet to work from
 *			buffer	MapDataBuffer to work into
 * Outputs:	True if successful, otherwise False
 * Comment:	This is basically a way to have all of the data malloc()ed in one place,
 *			before calling mapsheet_cube().
 */

static Bool mapsheet_get_mult_data(MapSheet sheet, MapDataBuffer *buffer)
{
	u32		width = sheet->hdr->n_east, height = sheet->hdr->n_north;

	if ((buffer->depth = (f32*)malloc(sizeof(f32)*width*height)) == NULL ||
		(buffer->uncrt = (f32*)malloc(sizeof(f32)*width*height)) == NULL ||
		(buffer->nhypo = (u8*)malloc(sizeof(u8)*width*height)) == NULL ||
		(buffer->hypstr = (f32*)malloc(sizeof(f32)*width*height)) == NULL) {
		error_msgv(modname, "error: failed to get data buffers for HyperCUBE mass"
			" extraction code (4x%d bytes).\n", sizeof(f32)*width*height);
		return(False);
	}
	if (!mapsheet_cube_get_data(sheet, sheet->grid->hcube_grid, sheet->param->hcube_param,
								buffer->depth, buffer->uncrt, buffer->hypstr)) {
		error_msg(modname, "error: failed to extract data from HyperCUBE.\n");
		return(False);
	}
	if (!mapsheet_cube_get_hypo(sheet, sheet->grid->hcube_grid, sheet->param->hcube_param,
								buffer->nhypo)) {
		error_msgv(modname, "error: failed to extract hypothesis count data from HyperCUBE.\n");
		return(False);
	}
	return(True);
}

/* Routine:	mapsheet_get_all_data
 * Purpose:	Extract all of the data available from a particular MapSheet as efficiently
 *			as possible.
 * Inputs:	sheet	MapSheet to work from
 * Outputs:	Pointer to MapDataBuffers on success, or NULL on failure.
 * Comment:	It is sometimes possible to be more efficient in extracting data by asking
 *			for a chunk of it at once, rather than doing each element one at a time
 *			(e.g., with HyperCUBE, it is faster to get depth, uncrt & hypstr all at
 *			one time because you only have to run the disambiguation engine once).
 *				This routine attempts to take account of these requirements, and if you
 *			want 'all' that's in the MapSheet, this is the best way to get it out.  On
 *			return, the MapDataBuffers structure is set up so that all pointers which
 *			are valid are set non-NULL.  However, this is no guarantee that the data
 *			buffers to which they point are all of the same size (since some data may
 *			be u8, some u16, and some f32, for example).  Users should call
 *			mapsheet_get_data_size() to determine the correct size before using the
 *			pointers.
 */

MapDataBuffer *mapsheet_get_all_data(MapSheet sheet)
{
	MapDataBuffer	*rtn;

	if ((rtn = (MapDataBuffer*)calloc(1, sizeof(MapDataBuffer))) == NULL) {
		error_msgv(modname, "error: failed to allocate memory for a MapDataBuffer"
			" structure (%d bytes).\n", sizeof(MapDataBuffer));
		return(NULL);
	}
	switch (sheet->hdr->depth) {
		case MAP_DEPTH_BINED_MEAN:
		case MAP_DEPTH_BINED_MEDIAN:
			rtn->depth = mapsheet_get_data_user(sheet, MAP_DEPTH_ESTIMATE, NULL);
			break;
		case MAP_DEPTH_HYPERCUBE:
			if (!mapsheet_get_mult_data(sheet, rtn)) {
				error_msg(modname, "error: failed to get multiple data chunks for HyperCUBE"
					" surface.\n");
				mapsheet_release_databuffer(rtn);
				free(rtn);
				return(NULL);
			}
			break;
		case MAP_DEPTH_NODAL:
			rtn->depth = mapsheet_get_data_user(sheet, MAP_DEPTH_ESTIMATE, NULL);
			rtn->uncrt = mapsheet_get_data_user(sheet, MAP_DEPTH_UNCERTAINTY, NULL);
			rtn->sdev = mapsheet_get_data_user(sheet, MAP_DEPTH_SDEV, NULL);
			break;
		default:
			/* Note that having no depth algorithm is not necessarily a problem,
			 * since you can have a sheet with nothing else in it ...
			 */
			break;
	}
	if (mapsheet_contains(sheet, MAP_NODE_HITS))
		rtn->nhits = mapsheet_get_data_user(sheet, MAP_NODE_HITS, NULL);
	if (mapsheet_contains(sheet, MAP_BACKSCATTER))
		rtn->backscatter = mapsheet_get_data_user(sheet, MAP_BACKSCATTER, NULL);
	return(rtn);
}

/* Routine:	mapsheet_dump_hypo
 * Purpose:	Dump GDP file of hypotheses (if available) to file
 * Inputs:	sheet	MapSheet to work with
 *			*name	Name of file to write
 *			*bnds	MapBounds to sub-set, or NULL if none.
 * Outputs:	True if write worked, or False.
 * Comment:	Note that this is designed almost entirely for debugging purposes,
 *			and in general should not be used by normal code.  This routine only
 *			works if there is a HyperCUBE surface attached to the mapsheet, and
 *			only writes GDP files (GeoZui3D), and not anything else.  Use with
 *			caution.  Better yet, don't use at all ...
 */

Bool mapsheet_dump_hypo(MapSheet sheet, char *name, MapBounds *bnds)
{
	if (sheet->hdr->depth != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: cannot dump hypotheses unless there is an"
			" active HyperCUBE depth estimation surface associated with the"
			" current mapsheet.\n");
		return(False);
	}
	return(mapsheet_cube_dump_hypo(sheet, sheet->grid->hcube_grid,
			sheet->param->hcube_param, name, bnds));
}

/* Routine: mapsheet_get_hypo_by_node
 * Purpose:	Extract all of the hypotheses for a single node, given the node's
 *			offset from the origin of the MapSheet
 * Inputs:	sheet		MapSheet structure to work with
 *			col, row	CubeNode to extract
 * Outputs:	Returns pointer to a HypoArray on success, otherwise NULL
 * Comment:	This extracts all of the hypotheses associated with a single node.
 *			Note that if there are no hypotheses, a valid return still occurs,
 *			but with the number of hypotheses set to zero.  This call only works
 *			if there is a HyperCUBE surface attached to the MapSheet.
 */

HypoArray *mapsheet_get_hypo_by_node(MapSheet sheet, u32 col, u32 row)
{
	if (sheet->hdr->depth != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: cannot get hypothesis unless there is an"
			" active HyperCUBE depth estimation surface associated with the"
			" current mapsheet.\n");
		return(NULL);
	}
	return(
		mapsheet_cube_get_hypo_by_node(
			sheet, sheet->grid->hcube_grid, sheet->param->hcube_param, col, row
		)
	);
}

/* Routine: mapsheet_get_hypo_by_location
 * Purpose:	Extract all of the hypotheses for a single node, given the node's
 *			offset from the origin of the MapSheet
 * Inputs:	sheet	MapSheet structure to work with
 *			x, y	CubeNode to extract (see comment)
 * Outputs:	Returns pointer to a HypoArray on success, otherwise NULL
 * Comment:	This extracts all of the hypotheses associated with a single node.
 *			Note that if there are no hypotheses, a valid return still occurs,
 *			but with the number of hypotheses set to zero.  This call only works
 *			if there is a HyperCUBE surface attached to the MapSheet.  The (x,y)
 *			location is in absolute projected coordinates, and the code returns
 *			the CubeNode closest to it in Euclidean distance.  Locations outside
 *			the bounding box of the MapSheet are immediately truncated.
 */

HypoArray *mapsheet_get_hypo_by_location(MapSheet sheet, f64 x, f64 y)
{
	s32	row, col;

	if (sheet->hdr->depth != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: cannot get hypothesis unless there is an"
			" active HyperCUBE depth estimation surface associated with the"
			" current mapsheet.\n");
		return(NULL);
	}
	if (x < sheet->hdr->w_bound ||
		x > sheet->hdr->w_bound+(sheet->hdr->n_east-1)*sheet->hdr->east_sz ||
		y > sheet->hdr->n_bound ||
		y < sheet->hdr->n_bound-(sheet->hdr->n_north-1)*sheet->hdr->north_sz) {
		return(NULL);
	}
	row = (u32)((sheet->hdr->n_bound + sheet->hdr->north_sz/2.0 - y)/sheet->hdr->north_sz + 0.5);
	col = (u32)((x - sheet->hdr->w_bound - sheet->hdr->east_sz/2.0)/sheet->hdr->east_sz + 0.5);
	return(
		mapsheet_cube_get_hypo_by_node(
			sheet, sheet->grid->hcube_grid, sheet->param->hcube_param, col, row
		)
	);
}

/* Routine: mapsheet_get_hypo_area
 * Purpose:	Extract a set of hypotheses for a subset (or all) of the MapSheet
 * Inputs:	sheet	MapSheet structure to analyse
 *			*bnds	MapBounds structure for the subset to report
 * Outputs:	Returns a pointer to the Hypotheses array on success, otherwise NULL
 * Comment:	This extracts all of the hypotheses present in the MapSheet for the
 *			area given, and re-orders them as a plain grid.  Additional georef
 *			information is added so that the Hypotheses structure can stand on
 *			its own.  Obviously, this call will fail if there isn't a HyperCUBE
 *			surface attached to the MapSheet.
 */

Hypotheses *mapsheet_get_hypo_area(MapSheet sheet, MapBounds *bnds)
{
	return(NULL);	/* Currently undefined */
}

/* Routine: mapsheet_nominate_hypo_by_node
 * Purpose:	Choose an hypothesis at a single node, given the node's
 *			offset from the origin of the MapSheet
 * Inputs:	sheet		MapSheet structure to work with
 *			col, row	CubeNode to work on
 *			depth		Depth of the hypothesis to nominate
 * Outputs:	Returns True of success, otherwise False
 * Comment:	This nominates a particular hypothesis for reconstruction, over-
 *			riding the automatic choice.  This only works if there is an active
 *			HyperCUBE surface attached to the MapSheet.  Note that the row/col
 *			are specified from the NW corner of the MapSheet, row-major and
 *			north to south.
 */

Bool mapsheet_nominate_hypo_by_node(MapSheet sheet, u32 col, u32 row, f32 depth)
{
	if (sheet->hdr->depth != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: cannot get hypothesis unless there is an"
			" active HyperCUBE depth estimation surface associated with the"
			" current mapsheet.\n");
		return(False);
	}
	return(
		mapsheet_cube_nominate_hypo_by_node(
			sheet, sheet->grid->hcube_grid, sheet->param->hcube_param, col, row, depth
		)
	);
}

/* Routine: mapsheet_nominate_hypo_by_location
 * Purpose:	Choose a hypothesis at a single node, given the node's
 *			offset from the origin of the MapSheet
 * Inputs:	sheet	MapSheet structure to work with
 *			x, y	CubeNode to extract (see comment)
 *			depth	Depth of the hypothesis to nominate
 * Outputs:	Returns True on success, otherwise False
 * Comment:	This nominates a particular hypothesis for reconstruction, over-riding
 *			the automatic choice.  This call only works if there is a HyperCUBE
 *			surface attached to the MapSheet.  The (x,y) location is in absolute
 *			projected coordinates, and the code returns the CubeNode closest to
 *			it in Euclidean distance.  Locations outside the bounding box of the
 *			MapSheet are immediately truncated.
 */

Bool mapsheet_nominate_hypo_by_location(MapSheet sheet, f64 x, f64 y, f32 depth)
{
	s32	row, col;

	if (sheet->hdr->depth != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: cannot get hypothesis unless there is an"
			" active HyperCUBE depth estimation surface associated with the"
			" current mapsheet.\n");
		return(False);
	}
	if (x < sheet->hdr->w_bound ||
		x > sheet->hdr->w_bound+(sheet->hdr->n_east-1)*sheet->hdr->east_sz ||
		y > sheet->hdr->n_bound ||
		y < sheet->hdr->n_bound-(sheet->hdr->n_north-1)*sheet->hdr->north_sz) {
		return(False);
	}
	row = (u32)((sheet->hdr->n_bound + sheet->hdr->north_sz/2.0 - y)/sheet->hdr->north_sz + 0.5);
	col = (u32)((x - sheet->hdr->w_bound - sheet->hdr->east_sz/2.0)/sheet->hdr->east_sz + 0.5);
	return(
		mapsheet_cube_nominate_hypo_by_node(
			sheet, sheet->grid->hcube_grid, sheet->param->hcube_param, col, row, depth
		)
	);
}

/* Routine: mapsheet_unnominate_hypo_by_node
 * Purpose:	Resets any nomination, allowing the CubeNode to go back to algorithm
 *			control of disambiguation, given the node's offset from the origin of
 *			the MapSheet
 * Inputs:	sheet		MapSheet structure to work with
 *			col, row	CubeNode to work on
 * Outputs:	Returns True of success, otherwise False
 * Comment:	This removes any previous nomination from a node, returning it to
 *			algorithm control of disambiguation.  This only works if there is an active
 *			HyperCUBE surface attached to the MapSheet.  Note that the row/col
 *			are specified from the NW corner of the MapSheet, row-major and
 *			north to south.
 */

Bool mapsheet_unnominate_hypo_by_node(MapSheet sheet, u32 col, u32 row)
{
	if (sheet->hdr->depth != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: cannot get hypothesis unless there is an"
			" active HyperCUBE depth estimation surface associated with the"
			" current mapsheet.\n");
		return(False);
	}
	return(
		mapsheet_cube_unnominate_hypo_by_node(
			sheet, sheet->grid->hcube_grid, sheet->param->hcube_param, col, row
		)
	);
}

/* Routine: mapsheet_unnominate_hypo_by_location
 * Purpose:	Resets any nomination, allowing the CubeNode to go back to algorithm
 *			control of disambiguation, given the node's offset from the origin of
 *			the MapSheet
 * Inputs:	sheet	MapSheet structure to work with
 *			x, y	CubeNode to extract (see comment)
 * Outputs:	Returns True on success, otherwise False
 * Comment:	This removes any previous nomination from a node, returning it to
 *			algorithm control of disambiguation.  This call only works if there is
 *			a HyperCUBE surface attached to the MapSheet.  The (x,y) location is
 *			in absolute projected coordinates, and the code returns the CubeNode
 *			closest to it in Euclidean distance.  Locations outside the bounding
 *			box of the MapSheet are immediately truncated.
 */

Bool mapsheet_unnominate_hypo_by_location(MapSheet sheet, f64 x, f64 y)
{
	s32	row, col;

	if (sheet->hdr->depth != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: cannot get hypothesis unless there is an"
			" active HyperCUBE depth estimation surface associated with the"
			" current mapsheet.\n");
		return(False);
	}
	if (x < sheet->hdr->w_bound ||
		x > sheet->hdr->w_bound+(sheet->hdr->n_east-1)*sheet->hdr->east_sz ||
		y > sheet->hdr->n_bound ||
		y < sheet->hdr->n_bound-(sheet->hdr->n_north-1)*sheet->hdr->north_sz) {
		return(False);
	}
	row = (u32)((sheet->hdr->n_bound + sheet->hdr->north_sz/2.0 - y)/sheet->hdr->north_sz + 0.5);
	col = (u32)((x - sheet->hdr->w_bound - sheet->hdr->east_sz/2.0)/sheet->hdr->east_sz + 0.5);
	return(
		mapsheet_cube_unnominate_hypo_by_node(
			sheet, sheet->grid->hcube_grid, sheet->param->hcube_param, col, row
		)
	);
}

/* Routine:	mapsheet_match_hypothesis
 * Purpose:	Determine which hypothesis in a HypoArray matches a given depth
 * Inputs:	depth	Depth at which to match
 *			*array	HypoArray to work from
 * Outputs:	Number of hypothesis matching (see comment), or -1 if none
 * Comment:	The concept of 'match' here is fairly strict: unless you have a
 *			depth from a hypothesis, don't expect to match it in a list
 *			associated with the node.  Technically, the code here checks for
 *			a depth agreement withing 0.001m.
 */

s32 mapsheet_match_hypothesis(f32 depth, HypoArray *array)
{
	u32	hypo;

	for (hypo = 0; hypo < array->n_hypos; ++hypo)
		if (fabs(depth - array->array[hypo].z) < 0.001) return(hypo);
	return(-1);
}

/* Routine: mapsheet_remove_hypo_by_node
 * Purpose:	Remove an hypothesis at a single node, given the node's
 *			offset from the origin of the MapSheet
 * Inputs:	sheet		MapSheet structure to work with
 *			col, row	CubeNode to work on
 *			depth		Depth of the hypothesis to nominate
 * Outputs:	Returns True of success, otherwise False
 * Comment:	This removes a particular hypothesis for reconstruction.  This only
 *			works if there is an active HyperCUBE surface attached to the MapSheet.
 *			Note that the row/col are specified from the NW corner of the MapSheet,
 *			row-major and north to south.
 */

Bool mapsheet_remove_hypo_by_node(MapSheet sheet, u32 col, u32 row, f32 depth)
{
	if (sheet->hdr->depth != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: cannot get hypothesis unless there is an"
			" active HyperCUBE depth estimation surface associated with the"
			" current mapsheet.\n");
		return(False);
	}
	return(
		mapsheet_cube_remove_hypo_by_node(
			sheet, sheet->grid->hcube_grid, sheet->param->hcube_param, col, row, depth
		)
	);
}

/* Routine: mapsheet_remove_hypo_by_location
 * Purpose:	Remove a hypothesis at a single node, given the node's
 *			offset from the origin of the MapSheet
 * Inputs:	sheet	MapSheet structure to work with
 *			x, y	CubeNode to extract (see comment)
 *			depth	Depth of the hypothesis to nominate
 * Outputs:	Returns True on success, otherwise False
 * Comment:	This removes a particular hypothesis for reconstruction.  This call
 *			only works if there is a HyperCUBE surface attached to the MapSheet.
 *			The (x,y) location is in absolute projected coordinates, and the code
 *			returns the CubeNode closest to it in Euclidean distance.  Locations
 *			outside the bounding box of the MapSheet are immediately truncated.
 */

Bool mapsheet_remove_hypo_by_location(MapSheet sheet, f64 x, f64 y, f32 depth)
{
	s32	row, col;

	if (sheet->hdr->depth != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: cannot get hypothesis unless there is an"
			" active HyperCUBE depth estimation surface associated with the"
			" current mapsheet.\n");
		return(False);
	}
	if (x < sheet->hdr->w_bound ||
		x > sheet->hdr->w_bound+(sheet->hdr->n_east-1)*sheet->hdr->east_sz ||
		y > sheet->hdr->n_bound ||
		y < sheet->hdr->n_bound-(sheet->hdr->n_north-1)*sheet->hdr->north_sz) {
		return(False);
	}
	row = (u32)((sheet->hdr->n_bound + sheet->hdr->north_sz/2.0 - y)/sheet->hdr->north_sz + 0.5);
	col = (u32)((x - sheet->hdr->w_bound - sheet->hdr->east_sz/2.0)/sheet->hdr->east_sz + 0.5);
	return(
		mapsheet_cube_remove_hypo_by_node(
			sheet, sheet->grid->hcube_grid, sheet->param->hcube_param, col, row, depth
		)
	);
}

/* Routine:	mapsheet_release_aois
 * Purpose:	Release memory associated with an AOI list
 * Inputs:	*root	Head of the linked list to free
 * Outputs:	-
 * Comment:	-
 */

void mapsheet_release_aois(AOI *root)
{
	AOI *next;
	
	while (root != NULL) {
		next = root->next;
		free(root);
		root = next;
	}
}

/* Routine:	mapsheet_compute_spikeangle
 * Purpose:	Compute the angle between a spike and the surrounding seafloor
 * Inputs:	sheet		MapSheet to work with
 *			*data		Surface to compute on (typically depth)
 *			row, col	Location on surface to compute
 *			size		Size of the analysis region for background estimation
 * Outputs:	Returns angle between data[row][col] and the surrounding region
 * Comment:	The routine is not intended to compute real angles, simply to give
 *			a metric to describe the 'spike-ness' of the data.  In essence, it
 *			is similar to the NASA/JPL despiker in that it computes a metric
 *			for the data (in this case, mean data value), leaving out the
 *			point of interest, and then a difference between the surface and
 *			the point of interest.  In this case, we compute the angle that the
 *			point of interest would make with a flat surface at the mean depth
 *			and one sample spacing from the 2D location of the node.  In this
 *			way, we have what amounts to an estimate of local slope angle.
 */

f32 mapsheet_compute_spikeangle(MapSheet sheet, f32 *data, u32 row, u32 col,
								u32 size)
{
	s32	r, c, rows = sheet->hdr->n_north, cols = sheet->hdr->n_east;
	f32	mean, invalid_data;
	u32	n_neighbours;
	
	mapsheet_get_invalid(MAP_DATA_F32, &invalid_data);
	mean = 0.0f; n_neighbours = 0;
	for (r = row-size; r <= (s32)(row+size); ++r) {
		if (r < 0 || r >= rows) continue;
		for (c = (s32)col-size; c <= (s32)(col+size); ++c) {
			if (c < 0 || c >= cols ||
					(c == (s32)col && r == (s32)row) || data[r*cols+c] == invalid_data)
				continue;
			mean += data[r*cols + c];
			++n_neighbours;
		}
	}
	if (n_neighbours == 0) return((f32)(M_PI_2));
	mean /= n_neighbours;
	return((f32)atan(fabs((data[row*cols+col] - mean)/sheet->hdr->east_sz)));
}

/* Routine:	mapsheet_analyse_sheet
 * Purpose:	Analyse the depth surface currently in the MapSheet for AOIs
 * Inputs:	sheet	MapSheet structure to analyse
 * Outputs:	Returns pointer to linked list of AOI structures if successful, or
 *			NULL otherwise.
 *			*depth	Pointer to space for depth surface, or NULL if not required
 *			*unct	Pointer to space for uncertainty surface, or NULL
 *			*hypstr	Pointer to space for hypothesis strength surface, or NULL
 *			*nhyp	Pointer to space for #hypo surface, or NULL
 * Comment:	This routine is only supported for HyperCUBE surfaces, and returns
 *			with failure immediately if the correct depth surface is not
 *			available.
 *				The routine computes the areas of interest using blob analysis
 *			on a suitably thresholded hypothesis count surface, and then fills
 *			in the AOI information from the various components of the HyperCUBE
 *			depth database.  Since we need to extract all of the surfaces to do
 *			the computations, the pointers in the function call allow the user
 *			to avoid further disam. calls to mapsheet_get_data() and just
 *			output the data to disk for study.  If any of the pointers are set
 *			to NULL, the corresponding surface buffer is released; otherwise,
 *			it is the caller's responsibility to release the memory when it is
 *			no longer required.
 */

AOI *mapsheet_analyse_sheet(MapSheet sheet, f32 context_size,
							f32 *depth, f32 *unct, f32 *hypstr, u8 *nhyp)
{
	f32			*idepth = NULL, *iunct = NULL, *ihypstr = NULL, no_data_f32;
	u8			*inhyp = NULL;
	u32			row, col, rows, cols, pel, npels, dilation_size, n_elem;
	u8			*blobs = NULL, *dil_buffer = NULL, no_data_u8;
	BlobBBox	*boxes, *box_ptr;
	AOI			*rtn = NULL, *tmp;
	
	if (sheet->hdr->depth != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: analysis of depth surfaces is only"
			" supported for HyperCUBE surfaces.\n");
		return(NULL);
	}
	
	rows = sheet->hdr->n_north; cols = sheet->hdr->n_east;
	npels = rows*cols;
	
	if ((depth == NULL && (idepth = (f32*)malloc(sizeof(f32)*npels)) == NULL) ||
		(unct == NULL && (iunct = (f32*)malloc(sizeof(f32)*npels)) == NULL) ||
		(hypstr == NULL && (ihypstr = (f32*)malloc(sizeof(f32)*npels)) == NULL) ||
		(nhyp == NULL && (inhyp = (u8*)malloc(sizeof(u8)*npels)) == NULL) ||
		(blobs = (u8*)malloc(sizeof(u8)*npels)) == NULL) {
		error_msgv(modname, "error: failed allocating buffers for analysis"
			" (sheet is %d x %d nodes).\n",
			sheet->hdr->n_north, sheet->hdr->n_east);
		free(idepth); free(iunct); free(ihypstr); free(inhyp); free(blobs);
		return(NULL);
	}
	if (idepth != NULL) depth = idepth;
	if (iunct != NULL) unct = iunct;
	if (ihypstr != NULL) hypstr = ihypstr;
	if (inhyp != NULL) nhyp = inhyp;
		/* Note that this sequence means that if external buffers are made
		 * available, we don't reallocate.  Hence, we can safely free() all of
		 * the internal buffer pointers, since the NULL initialisation above
		 * means that they are either valid, or NULL and hence free() works in
		 * all cases.  This simplifies close-out of the routine.
		 */
	
	/* Stage 1: Get hypothesis count surface, and threshold */
	if (!mapsheet_cube_get_hypo(sheet, sheet->grid->hcube_grid,
			sheet->param->hcube_param, nhyp)) {
		error_msg(modname, "error: failed to extract count of hypotheses for"
			" analysis.\n");
		free(idepth); free(iunct); free(ihypstr); free(inhyp); free(blobs);
		return(NULL);
	}
	mapsheet_get_invalid(MAP_DATA_U8, &no_data_u8);
	for (pel = 0; pel < npels; ++pel)
		if (nhyp[pel] > 1 && nhyp[pel] != no_data_u8)
			blobs[pel] = 1;
		else
			blobs[pel] = 0;
		
	/* Stage 2: Dilate the binarised hypothesis surface so that we agglomerate
	 * areas with small disconnects between them, and hence report them as a
	 * single area of interest (in most cases, they will be manifestations of
	 * the same data problem).
	 */
	dilation_size = (u32)((context_size+0.5f)/sheet->hdr->east_sz);
	if ((dilation_size%2)==0) ++dilation_size;
	if ((dil_buffer =
				ccom_dilate(blobs, rows, cols, dilation_size, NULL)) == NULL) {
		error_msg(modname, "error: failed to get blob dilation buffer.\n");
		free(idepth); free(iunct); free(ihypstr); free(inhyp); free(blobs);
		return(NULL);
	}
	free(blobs);
	
	/* Stage 3: Blob detection on the dilated thresholded hypothesis counts */
	if ((boxes = ccom_blob_detect(dil_buffer, rows, cols)) == NULL) {
		error_msg(modname, "warning: no areas with multiple hypotheses found"
			" in surface --- suspicious!\n");
		free(idepth); free(iunct); free(ihypstr); free(inhyp); free(dil_buffer);
		return(NULL);
	}
	
	/* Stage 4: Get the real data surfaces --- all at one time --- and then
	 * compute statistics for the blobs identified above from them.
	 */
	if (!mapsheet_cube_get_data(sheet, sheet->grid->hcube_grid,
			sheet->param->hcube_param, depth, unct, hypstr)) {
		error_msg(modname, "error: failed to extract composite surfaces for"
			" analysis.\n");
		free(idepth); free(iunct); free(ihypstr); free(inhyp); free(dil_buffer);
		return(NULL);
	}
	mapsheet_get_invalid(MAP_DATA_F32, &no_data_f32);
	box_ptr = boxes;
	while (box_ptr != NULL) {
		if ((tmp = (AOI*)calloc(1, sizeof(AOI))) == NULL) {
			error_msg(modname, "error: failed allocating space for an"
				" AOI structure.\n");
			free(idepth); free(iunct); free(ihypstr); free(inhyp);
			free(dil_buffer);
			mapsheet_release_aois(rtn);
			return(NULL);
		}
		tmp->x0 = sheet->hdr->w_bound +
					sheet->hdr->east_sz*(box_ptr->min_x + box_ptr->max_x)/2.0;
		tmp->y0 = sheet->hdr->n_bound -
					sheet->hdr->north_sz*(box_ptr->min_y + box_ptr->max_y)/2.0;
		tmp->width = (box_ptr->max_x - box_ptr->min_x)*sheet->hdr->east_sz;
		tmp->height = (box_ptr->max_y - box_ptr->min_y)*sheet->hdr->north_sz;
		
		tmp->shoalest_depth = -FLT_MAX;
		tmp->hypstr_focus = 0.0;
		tmp->mean_depth = tmp->mean_unct =
				tmp->mean_nhyp = tmp->mean_hypstr = 0.0;
		n_elem = 0;
		for (row = box_ptr->min_y; row <= box_ptr->max_y; ++row) {
			for (col = box_ptr->min_x; col <= box_ptr->max_x; ++col) {
				if (depth[row*cols + col] == no_data_f32 ||
												dil_buffer[row*cols + col] == 0)
					continue;
				tmp->shoalest_depth = MAX(tmp->shoalest_depth,
														depth[row*cols+col]);
				tmp->mean_depth += depth[row*cols + col];
				tmp->mean_unct += unct[row*cols + col];
				tmp->mean_nhyp += nhyp[row*cols + col];
				tmp->mean_hypstr += hypstr[row*cols + col];
				tmp->mean_angle +=
					mapsheet_compute_spikeangle(sheet, depth, row, col, 1);
				++n_elem;
				if (hypstr[row*cols + col] > tmp->hypstr_focus) {
					tmp->hypstr_focus = hypstr[row*cols + col];
					tmp->nhyp_focus = nhyp[row*cols + col];
					tmp->x_focus = sheet->hdr->w_bound +
										col*sheet->hdr->east_sz;
					tmp->y_focus = sheet->hdr->n_bound -
										row*sheet->hdr->north_sz;
					tmp->depth_focus = depth[row*cols + col];
				}
			}
		}
		tmp->mean_depth /= n_elem;
		tmp->mean_unct /= n_elem;
		tmp->mean_nhyp /= n_elem;
		tmp->mean_hypstr /= n_elem;
		tmp->mean_angle /= n_elem;
		tmp->mean_angle = (f32)(RAD2DEG(tmp->mean_angle));
		
		tmp->next = rtn;
		rtn = tmp;
		box_ptr = box_ptr->next;
	}
	ccom_release_bloblist(boxes);
	
	free(dil_buffer);
	free(idepth); free(iunct); free(ihypstr); free(inhyp);
	return(rtn);
}

/* Routine:	mapsheet_make_directory
 * Purpose:	Make a directory to specification, including path if required
 * Inputs:	*dir	Directory name to construct
 * Outputs:	True if made sucessfully, otherwise False
 * Comment:	This parses the passed string, testing each node as it does, until
 *			it either hits a failure, or encounters the first node that does
 *			not exist.  It then parses the remainder of the string, making
 *			directories as it goes (similar for mkdir -p).
 */

Bool mapsheet_make_directory(char *dir)
{
	char		*buffer, *start, *end;
	u32			len, copied;
	struct stat	sbuf;
	Bool		done;
	
	len = strlen(dir);
	if ((buffer = (char*)malloc(len+1)) == NULL) {
		error_msgv(modname, "error: failed to allocate directory pathname"
			" buffer (%d bytes).\n", len);
		return(False);
	}
	
	/* Scan path checking that everything is a directory.  Stop at first non-
	 * existant element.
	 */
	copied = 0; start = dir; done = False;
	while (!done && copied < len) {
		end = strchr(start, DIR_SEP);
		if (end == NULL)
			copied = len;
		else {
			copied = end - dir + 1;
			start = end + 1;
		}
		strncpy(buffer, dir, copied);
		buffer[copied] = '\0';
		if (stat(buffer, &sbuf) < 0) {
			if (errno == ENOENT) {
				done = True;
				copied = 0;
			} else {
				error_msg(modname, "error: fatal error in scanning directory"
					" pathname.\n");
				free(buffer);
				return(False);
			}
		} else {
			if (!S_ISDIR(sbuf.st_mode)) {
				error_msg(modname, "error: directory pathname contains extant"
					" non-directory component.\n");
				free(buffer);
				return(False);
			}
		}
	}
	
	if (copied == len) {
		free(buffer);	/* Fix leak reported by Ding Zheng at Reson bv */
		return(True);	/* Here, we've used all of the directory name, and it
						 * kept matching as directories, right to the end.  Thus
						 * the target must exist and we don't need to do anything.
						 */
	}
	
	/* Construct path element by element if possible */
	done = False; copied = 0;
	while (!done && copied < len) {
		end = strchr(start, DIR_SEP);
		if (end == NULL)
			copied = len;
		else {
			copied = end - dir + 1;
			start = end+1;
		}
		strncpy(buffer, dir, copied);
		buffer[copied] = '\0';
#ifdef WIN32
		if (_mkdir(buffer) < 0) {
#else
		if (mkdir(buffer, 0755) < 0) {
#endif
			error_msgv(modname, "error: failed generating directory path \"%s\".\n", buffer);
			free(buffer);
			return(False);
		}
	}
	free(buffer);
	return(True);
}

/* Routine:	mapsheet_remap_f64
 * Purpose:	Convert F64 to F32 in-place
 * Inputs:	*data	Pointer to buffer
 *			npels	Number of pels total in the data
 * Outputs:	-
 * Comment:	Note that this code only works because by the nature of the types
 *			defined here, we can guarantee that the input data is twice the
 *			size of the output data.  Thus, we can work two pointers through
 *			the data without them ever colliding.  Be vewy vewy caweful changing
 *			anything hewe.
 */

static void mapsheet_remap_f64(void *data, u32 npels)
{
	f64	*input = (f64*)data;
	f32	*output = (f32*)data;
	
	while (npels-- > 0)
		*output++ = (f32)(*input++);
}

/* Routine:	mapsheet_remap_u32
 * Purpose:	Convert U32 to U16 with dynamic range maintenance
 * Inputs:	*data	Pointer to data buffer
 *			npels	Number of pels total in the data
 * Outputs:	*type	Set to TIFF type to write
 * Comment:	Note that this code only works because by the nature of the types
 *			defined, we can guarantee that the input data is twice the size of
 *			the output data.  Comments per mapsheet_remap_f64 on modifications.
 *				Because we are limited with dynamic range in the U16 portion
 *			of the data, we attempt to ensure that as much of the dynamic range
 *			of the data is preserved as possible.  We compute the maximum and
 *			minimum values present.  If the maximum will fit into an 8bpp
 *			or 16bpp image, we set type==TIFF_U8/U16 and cast directly.  If
 *			this doesn't work, we then see if the whole range will fit into an
 *			8bpp or 16bpp image with a single shift in offset and convert
 *			appropriately.  Finally, we compute an offset and scale to fit the
 *			data into 16bpp TIFF and convert.
 */

typedef enum {
	REMAP_CLEAN,	/* Converted directly with no shift or scale */
	REMAP_SHIFTED,	/* Shifted but not scaled */
	REMAP_SCALED	/* Shifted and scaled */
} Remaps;

static Remaps mapsheet_remap_u32(u32 *data, u32 npels, TIFFType *type)
{
	u32		max = 0, min = INT_MAX, pel, range;
	f32		scale;
	u8		*ptr8 = (u8*)data;
	u16		*ptr16 = (u16*)data;
	
	for (pel = 0; pel < npels; ++pel) {
		max = MAX(max, data[pel]);
		min = MIN(min, data[pel]);
	}
	
	/* Plan A: See if the data can be fit into 8bpp or 16bpp with no conversion
	 */
	if (max < 255) {
		for (pel = 0; pel < npels; ++pel)
			*ptr8++ = (u8)(*data++);
		*type = TIFF_U8;
		return(REMAP_CLEAN);
	} else if (max < 65535) {
		for (pel = 0; pel < npels; ++pel)
			*ptr16++ = (u16)(*data++);
		*type = TIFF_U16;
		return(REMAP_CLEAN);
	}
	
	/* Plan B: see if the dynamic range can be accommodated with only a shift */
	range = max - min;
	if (range < 255) {
		error_msgv(modname, "warning: down-shifting summary file to 8bpp with"
			" shift %d (min = %d, max = %d, dr = %d).\n", min, min, max, range);
		for (pel = 0; pel < npels; ++pel)
			*ptr8++ = (u8)(*data++ - min);
		*type = TIFF_U8;
		return(REMAP_SHIFTED);
	} else if (range < 65535) {
		error_msgv(modname, "warning: down-shifting summary file to 16bpp with"
			" shift %d (min = %d, max = %d, dr = %d).\n", min, min, max, range);
		for (pel = 0; pel < npels; ++pel)
			*ptr16++ = (u16)(*data++ - min);
		*type = TIFF_U16;
		return(REMAP_SHIFTED);
	}
	
	/* Plan C: we're going to have to down-convert and scale */
	scale = (f32)(65535.0/range);
	error_msgv(modname, "warning: scaling summary file to 16bpp with scale %f"
		" and shift %d (min = %d, max = %d, range = %d).\n", scale, min, min,
		max, range);
	for (pel = 0; pel < npels; ++pel)
		*ptr16++ = (u16)(scale*(*data++ - min));
	*type = TIFF_U16;
	return(REMAP_SCALED);
}

/* Routine:	mapsheet_make_summary
 * Purpose:	Generate a set of summary images in a specified directory
 * Inputs:	*sheet	Pointer to the sheet to use
 *			*dir	Directory in which to write the output images
 * Outputs:	True if everything went OK, othewise False.
 * Comment:	The information which gets writen depends very much on the contents
 *			of the mapsheet and in part on which depth algorithm (if any) is
 *			being used.  All available information is saved in the specified
 *			directory (which is created if possible and it doesn't already
 *			exist), using default names.
 */

Bool mapsheet_make_summary(MapSheet sheet, char *dir)
{
	MapSheetElem	elements[] = {
						MAP_DEPTH_ESTIMATE, MAP_DEPTH_UNCERTAINTY,
						MAP_NODE_HITS, MAP_BACKSCATTER, MAP_DEPTH_SDEV,
						(MapSheetElem)0
						};
	char			*names[] = {
						"depth.tif", "uncertainty.tif", "hits.tif",
						"backscatter.tif", "sample-sdev.tif", NULL
					};
	u32				elem, npels;
	void			*data;
	MapSheetDataType	type;
	TIFFFlags			flgs = { False, False, False, False };
	TIFFType			ttype;
	
	/* Make a directory in the correct place */
	if (!mapsheet_make_directory(dir)) {
		error_msg(modname, "error: failed making directory path for summary.\n");
		return(False);
	}
	
	elem = 0;
	npels = sheet->hdr->n_east * sheet->hdr->n_north;
	while (names[elem] != NULL) {
		if (mapsheet_contains(sheet, elements[elem])) {
			error_msgv(modname, "summary: constructing \"%s/%s\".\n",
				dir, names[elem]);
			if ((data = mapsheet_get_data(sheet, elements[elem])) == NULL) {
				error_msgv(modname, "error: failed to get data elements for"
					" \"%s\" from mapsheet.\n", names[elem]);
				return(False);
			}
			type = mapsheet_get_data_type(sheet, elements[elem]);
			switch(type) {
				case MAP_DATA_U8:
					ttype = TIFF_U8;
					break;
				case MAP_DATA_U16:
					ttype = TIFF_U16;
					break;
				case MAP_DATA_U32:
					if (mapsheet_remap_u32(data, npels, &ttype) !=REMAP_CLEAN) {
						error_msgv(modname, "warning: last message applied to"
							" output \"%s/%s\".\n", dir, names[elem]);
					}
					break;
				case MAP_DATA_F32:
					ttype = TIFF_F32;
					break;
				case MAP_DATA_F64:
					ttype = TIFF_F32;
					mapsheet_remap_f64(data, npels);
					break;
				default:
					error_msgv(modname, "error: internal: unknown data type"
						" (%d).\n", (u32)type);
					return(False);
			}
			if (!ccom_write_tiff(dir, names[elem], data, ttype,
					sheet->hdr->n_north, sheet->hdr->n_east, &flgs)) {
				error_msgv(modname, "error: failed to write \"%s/%s\" for"
					" summary structure.\n", dir, names[elem]);
				return(False);
			}
			free(data);
		}
		++elem;
	}
	if (sheet->hdr->depth == MAP_DEPTH_NODAL &&
		!mapsheet_nodal_extend_summary(sheet, dir)) {
		error_msg(modname, "error: failed writing extended Node summary.\n");
		return(False);
	}
	if (sheet->hdr->depth == MAP_DEPTH_HYPERCUBE &&
		!mapsheet_cube_extend_summary(sheet, dir)) {
		error_msg(modname, "error: failed writing extended HyperCUBE summary.\n");
		return(False);
	}
	return(True);
}

MapSheetDepthAlg mapsheet_get_depth_alg(MapSheet sheet)
{
	return(sheet->hdr->depth);
}

/* Routine:	mapsheet_finalise
 * Purpose:	Finalise estimates in the mapsheet so that they can be read
 * Inputs:	sheet	MapSheet to work on
 * Outputs:	True if finalisation was sucessful, otherwise False
 * Comment:	Not all mapsheet estimation algorithms need finalisation, in which
 *			case this routine quietly returns.  Note that mapsheet finalisation
 *			may involve non-reversible changes to the internal data structures,
 *			so that [update][finalise][update] may not result in the same
 *			estimates as [update][update][finalise] (although often in at most
 *			a trivial way).
 */

Bool mapsheet_finalise(MapSheet sheet)
{
	/* Finalise depth estimates */
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_NODAL:
			mapsheet_nodal_finalise(sheet, sheet->param->depth_param,
													sheet->grid->depth_grid);
			break;
		case MAP_DEPTH_HYPERCUBE:
			mapsheet_cube_finalise(sheet, sheet->param->hcube_param,
													sheet->grid->hcube_grid);
			break;
		default:
			/* Note that HyperCUBE doesn't use finalisation, since it doesn't
			 * need the estimator pre-queueing.
			 *
			 * (brc, 2002-02-18)  Looks like famous last words.  This can be
			 * required where there is very sparse data (otherwise unless there
			 * is sufficient data to fill the queue, you get gaps in the data).
			 * Hmm.  The original sentiment probably applies --- this shouldn't
			 * really be required.
			 */
			break;
	}
	return(True);
}

/* Routine: mapsheet_minimise_memory
 * Purpose:	Attempt to minimise the memory required for a MapSheet
 * Inputs:	sheet	MapSheet to operate on
 * Outputs:	-
 * Comment:	How much effort this routine puts into the process of minimising
 *			the memory usage depends on the components of the MapSheet.  If
 *			a HyperCUBE surface is being used, the savings could be considerable,
 *			because the code attempts to flush all active tiles (past their
 *			'sell-by' date) to disc.  In other circumstances, the saving might
 *			not be so significant.
 */

void mapsheet_minimise_memory(MapSheet sheet)
{
	u32	n;

	switch(sheet->hdr->depth) {
		case MAP_DEPTH_HYPERCUBE:
			if (!mapsheet_cube_purge_cache(sheet->grid->hcube_grid,
										   sheet->param->hcube_param, True, &n)) {
				error_msg(modname, "warning: HyperCUBE cache purge failed.\n");
			}
			break;
		default:
			break;
	}
}

/* Routine:	mapsheet_bin_depth
 * Purpose:	Insert depth into binned array, with dynamic re-sizing.
 * Inputs:	depth	Depth in beam
 *			grid	Pointer to BinNode grid
 *			row,col	Position of sounding in grid
 *			bufq	Buffer quantum size
 *			maxbuf	Maximum size to increase buffer to
 * Outputs:	Success as a Bool
 * Comment:	This inserts the depth into the working arrays, re-sizing the bin
 *			array if required.  After _maxbuf_ elements are entered, further
 *			insertion is done by uniform replacement.  Note that the conditions
 *			below on when we use _maxbuf_ and when we use _space_ are very
 *			carefully controlled to ensure that if _maxbuf_ is *lowered* after
 *			the buffer exceeds that limit, we still get correct interlock on
 *			the buffer space (i.e., we don't lock out the upper (space-maxbuf)
 *			elements from consideration for replacement).  This is a compromise
 *			between efficiency in time/simplicity in code and memory use (other
 *			alternatives would be to reduce the size of all buffers whenever
 *			_maxbuf_ is modified, or to interlock the use of the buffer and
 *			the setting of _maxbuf_).
 */

Bool mapsheet_bin_depth(f32 depth, BinNode **grid, u32 row, u32 col,
						u32 bufq, u32 maxbuf)
{
	u32		bin, size;
	
	bin = grid[row][col].hits;
	if (bin == grid[row][col].space) {
		/* i.e., we need a little more space */
		if (grid[row][col].space < maxbuf) {
			size = grid[row][col].space + bufq;
			if (size > maxbuf) size = maxbuf;
			if ((grid[row][col].bin =
				(f32p)realloc(grid[row][col].bin, size*sizeof(f32))) == NULL) {
				error_msgv(modname, "failed to reallocate space (%d,%d) to"
					" %d soundings (%d bytes).\n", row, col, size,
					size*sizeof(f32));
				return(False);
			}
			grid[row][col].space = size;
		} else
#ifdef WIN32
			bin = (u32)(grid[row][col].space*((f32)rand()/RAND_MAX));
#else
			bin = random() % grid[row][col].space;
#endif
	}
	grid[row][col].bin[bin] = depth;
	if (grid[row][col].hits < grid[row][col].space) grid[row][col].hits ++;
	return(True);
}

/* Routine:	mapsheet_count_hits
 * Purpose:	Add hit counts to the mapsheet, if available
 * Inputs:	sheet	Pointer to the mapsheet to use
 *			stream	Sounding stream for the data
 *			*snds	Pointer to the soundings to be entered
 *			nsnds	Number of soundings to be entered
 * Outputs:	True if the soundings were entered properly, else False
 * Comment:	This will return False only if the hits grid is not present in the
 *			mapsheet.  Use mapsheet_add_hits() to add.
 */

Bool mapsheet_count_hits(MapSheet sheet, SoundingStream stream,
						 Sounding *snds, u32 nsnds)
{
	u32			sounding;
	s32			row, col;
	MapHdr		*hdr = sheet->hdr;
	f64			left, top;

	/* Check that there is actually a hits count to be had */
	if (sheet->grid->hits_grid == NULL) {
		error_msg(modname, "internal: request to count hits with no hits"
			" grid defined in mapsheet!\n");
		return(False);
	}
	/* Offset bounds by half a pixel dimension to ensure that we round into
	 * the correct bin (i.e., coordinate of bin are center of area represented.
	 */
	left = hdr->w_bound - hdr->east_sz/2.0;
	top = hdr->n_bound + hdr->north_sz/2.0;
		
	for (sounding = 0; sounding < nsnds; ++sounding) {
		col = (s32)((snds[sounding].east - left)/hdr->east_sz + 0.5);
		row = (s32)((top - snds[sounding].north)/hdr->north_sz + 0.5);
		if (row < 0 || row >= (s32)hdr->n_north || col < 0 || col >= (s32)hdr->n_east)
			continue;
		sheet->grid->hits_grid[row][col] += 1;
	}
	return(True);
}

Bool mapsheet_add_soundings(MapSheet sheet, SoundingStream stream,
							Sounding *snds, u32 nsnds, u32 *nused)
{
	Bool	rc;
	
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_NONE:
			rc = False;
			break;
		case MAP_DEPTH_NODAL:
			rc = mapsheet_nodal_insert_depths(sheet, sheet->grid->depth_grid,
											  sheet->param->depth_param,
											  stream, snds, nsnds, nused);
			break;
		case MAP_DEPTH_HYPERCUBE:
			rc = mapsheet_cube_insert_depths(sheet, sheet->grid->hcube_grid,
											 sheet->param->hcube_param,
											 stream, snds, nsnds, nused);
			break;
		case MAP_DEPTH_BINED_MEAN:
			rc = mapsheet_mean_insert_depths(sheet, sheet->grid->bin_grid,
											 sheet->param->bin_param,
											 snds, nsnds, nused);
			break;
		case MAP_DEPTH_BINED_MEDIAN:
			rc = mapsheet_median_insert_depths(sheet, sheet->grid->bin_grid,
											   sheet->param->bin_param,
											   snds, nsnds, nused);
			break;
		default:
			error_msgv(modname, "internal: depth algorithm %d not known.\n",
						(u32)(sheet->hdr->depth));
			return(False);
	}
	return(rc);
}

/* Routine:	mapsheet_limit_bin_size
 * Purpose:	Limit the number of bin slots which will be allocated (see comment)
 * Inputs:	sheet	Sheet to operate on
 *			bin_limit	Maximum desired size of each bin
 * Outputs:	True on success, False otherwise
 * Comment:	When depths are inserted into a binned depth surface estimate, the
 *			algorithm will dynamically allocate memory for depths up to this
 *			limit *per bin*; after this, bin slots will be re-used on a
 *			random replacement basis.  Note that setting this to a lower limit
 *			after values have been placed into the bin in excess of the new
 *			limit does not free the already allocated space, it just stops
 *			other bins from growing.  Data which is already in an out-size
 *			bin is retained, and the full size of the bin is used in the
 *			random replacement.
 */

Bool mapsheet_limit_bin_size(MapSheet sheet, u32 bin_limit)
{
	Bool	rc = False;
	
	switch(sheet->hdr->depth) {
		case MAP_DEPTH_NONE:
		case MAP_DEPTH_NODAL:
			error_msg(modname,
						"cannot limit bin depth with non-binned surface.\n");
			break;
		case MAP_DEPTH_BINED_MEAN:
			mapsheet_mean_limit_buffer(sheet->param->bin_param, bin_limit);
			rc = True;
			break;
		case MAP_DEPTH_BINED_MEDIAN:
			mapsheet_median_limit_buffer(sheet->param->bin_param, bin_limit);
			rc = True;
			break;
		default:
			error_msgv(modname, "internal: depth algorithm %d not known.\n",
						(u32)(sheet->hdr->depth));
			break;
	}
	return(rc);
}

/* --------------------------------------------------------------------------
 *                            Convenience Methods
 * -------------------------------------------------------------------------- */

/* Note that we need to be careful about specifying the bounds since we are
 * using a grid of nodes representing *points* in space, rather than the usual
 * area representation.  Particularly, when the estimation method uses areal
 * pixels, the location of each is taken to be the center of the pixel area,
 * rather than the top-left corner as is often the case.  This ensures that we
 * have the same number of grid nodes in both point and area representations,
 * which makes the coding of the rest of the process much easier.  The immediate
 * corollary of this is the area representations have an extra pixel's width
 * and height added to the overall area covered, but split half on each edge
 * of the mapsheet (which isn't a massive problem).
 */

void mapsheet_get_bl(MapSheet sheet, f64p x, f64p y)
{
	*x = sheet->hdr->w_bound;
	*y = sheet->hdr->n_bound - (sheet->hdr->n_north-1)*sheet->hdr->north_sz;
}

void mapsheet_get_br(MapSheet sheet, f64p x, f64p y)
{
	*x = sheet->hdr->w_bound + (sheet->hdr->n_east-1)*sheet->hdr->east_sz;
	*y = sheet->hdr->n_bound - (sheet->hdr->n_north-1)*sheet->hdr->north_sz;
}

void mapsheet_get_tl(MapSheet sheet, f64p x, f64p y)
{
	*x = sheet->hdr->w_bound;
	*y = sheet->hdr->n_bound;
}

void mapsheet_get_tr(MapSheet sheet, f64p x, f64p y)
{
	*x = sheet->hdr->w_bound + (sheet->hdr->n_east-1)*sheet->hdr->east_sz;
	*y = sheet->hdr->n_bound;
}

f64 mapsheet_get_proj_width(MapSheet sheet)
{
	return((sheet->hdr->n_east-1) * sheet->hdr->east_sz);
}

f64 mapsheet_get_proj_height(MapSheet sheet)
{
	return((sheet->hdr->n_north-1) * sheet->hdr->north_sz);
}

void mapsheet_get_proj_width_height(MapSheet sheet, f64p width, f64p height)
{
	*width = (sheet->hdr->n_east-1) * sheet->hdr->east_sz;
	*height = (sheet->hdr->n_north-1) * sheet->hdr->north_sz;
}

/* --------------------------------------------------------------------------
 *                          Construction/Destruction
 * -------------------------------------------------------------------------- */

static void mapsheet_release_depth_grid(DepthNode **grid, u32 npels)
{
	if (grid != NULL) {
		mapsheet_nodal_release_nodes(grid[0], npels);
		free(grid[0]);
		free(grid);
	}
}

static BinNode **mapsheet_alloc_bin_grid(u32 width, u32 height)
{
	BinNode **rtn;
	u32	row;

	if ((rtn = (BinNode**)calloc(height, sizeof(BinNode*))) == NULL ||
		(rtn[0] = (BinNode*)calloc(height*width, sizeof(BinNode))) == NULL) {
		error_msgv(modname, "failed to allocate bin node grid (%dx%d).\n",
					width, height);
		if (rtn != NULL) { free(rtn[0]); free(rtn); rtn = NULL; }
	}
	for (row = 1; row < height; ++row) rtn[row] = rtn[row-1] + width;
	return(rtn);
}	

static void mapsheet_release_bin_grid(BinNode **grid, u32 npels)
{
	BinNode	*g;
	
	if (grid != NULL) {
		if (grid[0] != NULL) {
			g = grid[0];
			while (npels-- > 0) {
				free(g->bin);
				++g;
			}
		}
		free(grid[0]);
		free(grid);
	}
}

static u32 **mapsheet_alloc_hits_grid(u32 width, u32 height)
{
	u32 **rtn;
	u32	row;

	if ((rtn = (u32**)calloc(height, sizeof(u32*))) == NULL ||
		(rtn[0] = (u32*)calloc(height*width, sizeof(u32))) == NULL) {
		error_msgv(modname, "failed to allocate hits node grid (%dx%d).\n",
					width, height);
		if (rtn != NULL) { free(rtn[0]); free(rtn); rtn = NULL; }
	}
	for (row = 1; row < height; ++row) rtn[row] = rtn[row-1] + width;
	return(rtn);
}	

static void mapsheet_release_hits_grid(u32 **grid)
{
	if (grid != NULL) {
		free(grid[0]);
		free(grid);
	}
}

static void mapsheet_release_data(MapData *data, u32 npels)
{
	if (data->hits_grid != NULL)
		mapsheet_release_hits_grid(data->hits_grid);
	if (data->depth_grid != NULL)
		mapsheet_release_depth_grid(data->depth_grid, npels);
	if (data->hcube_grid != NULL) {
		mapsheet_cube_release(data->hcube_grid);
		data->hcube_grid = NULL;					/* Mark NULL since memory now released */
	}
	if (data->bin_grid != NULL)
		mapsheet_release_bin_grid(data->bin_grid, npels);
	free(data);
}

static void mapsheet_release_param(MapParam *param, MapSheetDepthAlg alg)
{
	if (param->depth_param != NULL)
		mapsheet_nodal_release_param(param->depth_param);
	if (param->hcube_param != NULL)
		mapsheet_cube_release_param(param->hcube_param);
	if (param->bin_param != NULL) {
		switch(alg) {
			case MAP_DEPTH_BINED_MEAN:
				mapsheet_mean_release_param(param->bin_param);
				break;
			case MAP_DEPTH_BINED_MEDIAN:
				mapsheet_median_release_param(param->bin_param);
				break;
			default:
				error_msgv(modname, "internal: depth algorithm %d found with"
					" non-null bin_param pointer.\n", (u32)alg);
				break;
		}
	}
	if (param->hits_param != NULL) {
		error_msg(modname, "internal: non-null hits_param pointer!\n");
	}
	if (param->refl_param != NULL) {
		error_msg(modname, "internal: non-null refl_param pointer!\n");
	}
}

/* Routine: mapsheet_reset_params
 * Purpose:	Change parameters in the mapsheet to reflect the parameters
 *			currently active in the module
 * Inputs:	sheet	MapSheet to work on
 * Outputs:	True if successful, otherwise False
 * Comment:	This releases the parameter structures for the various components
 *			of the mapsheet, and replaces them with those currently in force.
 *			This can be used to reset the parameters for the algorithm to
 *			produce alternative data representations, and to change parameters
 *			on a loaded mapsheet to a set over-ridden from another file.  Note
 *			that the algorithm allocates a new set of algorithm parameters
 *			before releasing the old set, so that on failure the sheet is still
 *			valid for use, albeit not with the new parameter set.
 */

Bool mapsheet_reset_params(MapSheet sheet)
{
	MapParam	rtn;
	
	memset(&rtn, 0, sizeof(MapParam));
	
	switch (sheet->hdr->depth) {
		case MAP_DEPTH_NONE:
			/* No depth algorithm => no depth surface => no parameters */
			break;
		case MAP_DEPTH_NODAL:
			if ((rtn.depth_param = mapsheet_nodal_init_param(sheet->hdr->order,
						sheet->hdr->north_sz, sheet->hdr->east_sz)) == NULL) {
				error_msg(modname, "failed allocating node parameters\n");
				return(False);
			}
			break;
		case MAP_DEPTH_HYPERCUBE:
			if ((rtn.hcube_param = mapsheet_cube_init_param(sheet->hdr->order,
						sheet->hdr->north_sz, sheet->hdr->east_sz)) == NULL) {
				error_msg(modname, "failed allocating HyperCUBE parameters\n");
				return(False);
			}
			break;
		case MAP_DEPTH_BINED_MEAN:
			if ((rtn.bin_param = mapsheet_mean_init_param()) == NULL) {
				error_msg(modname, "failed allocating mean parameters.\n");
				return(False);
			}
			break;
		case MAP_DEPTH_BINED_MEDIAN:
			if ((rtn.bin_param = mapsheet_median_init_param()) == NULL) {
				error_msg(modname, "failed allocating median parameters.\n");
				return(False);
			}
			break;
		default:
			error_msgv(modname, "internal: depth algorithm %d unknown.\n",
				(u32)(sheet->hdr->depth));
			return(False);
	}
	mapsheet_release_param(sheet->param, sheet->hdr->depth);
	memcpy(sheet->param, &rtn, sizeof(MapParam));
	return(True);
}

static MapData *mapsheet_alloc_data(void)
{
	MapData	*rtn;
	
	if ((rtn = (MapData*)calloc(1, sizeof(MapData))) == NULL) {
		error_msg(modname, "Failed to allocate mapsheet node memory.\n");
		rtn = NULL;
	}
	return(rtn);
}

static char *mapsheet_get_tempname(void)
{
	char	*rtn;
	char	*base;
	static char *default_base = DEFAULT_BASE_BACKING_STORE;
	
	if ((base = getenv("LIBCCOMTMP")) == NULL) base = default_base;
	if (!mapsheet_make_directory(base)) {
		error_msgv(modname, "error: failed to make base directory for "
			"backing store at \"%s\".\n", base);
		return(NULL);
	}
	if ((rtn = tempnam(base, "msht")) == NULL) {
		error_msgv(modname, "error: failed to make temporary filename in"
			" \"%s\".\n", base);
		return(NULL);
	}
	if (!mapsheet_make_directory(rtn)) {
		error_msgv(modname, "error: failed to make temporary backing store"
			" directory \"%s\".\n", rtn);
		return(NULL);
	}
#ifdef __DEBUG__
	error_msgv(modname, "debug: made temporary backing store "
		"directory \"%s\".\n", rtn);
#endif
	return(rtn);
}

static MapSheet mapsheet_construct(Projection projection,
									f64 center_est, f64 center_nth,
									u32 width, u32 height,
									f64 east_sz, f64 north_sz,
									char *backing_store)
{
	MapSheet	rtn;
	
	/* We make sure that the width and height of the grid of nodes we're
	 * about to allocate are both odd.  This ensures that the area we cover is
	 * at least the required size, and possible one pixel wider, but also
	 * allows us to make the center point of the mapsheet the center node of
	 * the grid, which makes working with the coordinates and bounds easier.
	 */
	if (width%2 == 0) ++width;
	if (height%2 == 0) ++height;
	
	if ((rtn = (MapSheet)calloc(1, sizeof(struct _mapsheet))) == NULL ||
		(rtn->hdr = (MapHdr*)calloc(1, sizeof(MapHdr))) == NULL ||
		(rtn->grid = mapsheet_alloc_data()) == NULL ||
		(rtn->param = (MapParam*)calloc(1, sizeof(MapParam))) == NULL) {
		error_msg(modname, "Failed to allocate mapsheet memory structures.\n");
		if (rtn != NULL) {
			free(rtn->hdr); free(rtn->param);
			mapsheet_release_data(rtn->grid, width*height);
			free(rtn);
		}
		return(NULL);		
	}
	rtn->hdr->projection = projection;
	rtn->hdr->easting = center_est; rtn->hdr->northing = center_nth;
	rtn->hdr->east_sz = east_sz; rtn->hdr->north_sz = north_sz;
	rtn->hdr->n_east = width; rtn->hdr->n_north = height;
	rtn->hdr->w_bound = center_est - (width/2)*east_sz;
	rtn->hdr->n_bound = center_nth + (height/2)*north_sz;	
	rtn->hdr->depth = MAP_DEPTH_NONE;
	rtn->hdr->order = ERRMOD_IHO_ORDER_UNKNOWN;
	if (backing_store == NULL) {
		if ((rtn->backing_store = mapsheet_get_tempname()) == NULL) {
			error_msg(modname, "error: can't create temporary name for "
				"backing store.\n");
			mapsheet_release(rtn);
			return(NULL);
		}
		rtn->backing_temp = True;
	} else {
		if ((rtn->backing_store = strdup(backing_store)) == NULL) {
			error_msg(modname, "error: failed to strdup() backing"
				" store directory string --- out of memory.\n");
			mapsheet_release(rtn);
			return(NULL);
		}
		rtn->backing_temp = False;
	}
#ifdef __DEBUG__
error_msgv(modname, "debug: center is (%lf, %lf) m.\n", center_est, center_nth);
error_msgv(modname, "debug: w_bound = %f n_bound = %f\n", rtn->hdr->w_bound,
	rtn->hdr->n_bound);
error_msgv(modname, "debug: n_east = %d n_north = %d\n", width, height);
#endif
	return(rtn);
}

MapSheet mapsheet_new_direct_backed(
					Projection projection, u32 width, u32 height,
					f64 east_sz, f64 north_sz, f64 west, f64 north,
					char *backing_store)
{
	u32	cols = width, rows = height;
	f64	center_east, center_north;

	if ((width%2) == 0) ++cols;
	if ((height%2) == 0) ++rows;
	center_east = west + floor(cols/2)*east_sz;
	center_north = north - floor(height/2)*north_sz;
	return(mapsheet_construct(projection, center_east, center_north,
							  cols, rows, east_sz, north_sz, backing_store));
}

MapSheet mapsheet_new_by_center_backed(
					Projection projection, f64 width, f64 height,
					f64 east_sz, f64 north_sz, f64 lon, f64 lat,
					char *backing_store)
{
	u32		size_east = (u32)(width/east_sz+0.5)+1,
			size_north = (u32)(height/north_sz+0.5)+1;
	f64		center_est, center_nth;
	
	if (width < 0.0 || height < 0.0) {
		error_msgv(modname, "error: attempt to make a mapsheet with "
			" negative width/height (width = %.2lf m, height = %.2lf m).\n",
			width, height);
		return(NULL);
	}
	projection_ll_to_en_deg(projection, lon, lat, &center_est, &center_nth);
	return(mapsheet_construct(projection, center_est, center_nth, size_east,
								size_north, east_sz, north_sz, backing_store));
}

MapSheet mapsheet_new_by_proj_center_backed(
					Projection projection, f64 width, f64 height,
					f64 east_sz, f64 north_sz, f64 easting, f64 northing,
					char *backing_store)
{
	u32		size_east = (u32)(width/east_sz+0.5)+1,
			size_north = (u32)(height/north_sz+0.5)+1;
	
	if (width < 0.0 || height < 0.0) {
		error_msgv(modname, "error: attempt to make a mapsheet with "
			" negative width/height (width = %.2lf m, height = %.2lf m).\n",
			width, height);
		return(NULL);
	}
	return(mapsheet_construct(projection, easting, northing, size_east,
								size_north, east_sz, north_sz, backing_store));
}

MapSheet mapsheet_new_by_bounds_backed(
					Projection projection, f64 east_sz, f64 north_sz,
					f64 left_lon, f64 bottom_lat, f64 right_lon, f64 top_lat,
					char *backing_store)
{
	f64		center_lon = (right_lon + left_lon)/2.0,
			center_lat = (top_lat + bottom_lat)/2.0,
			width, height;
	f64		x_min, x_max, y_min, y_max;
	
	/* Project coordinates of bounds to determine width and height */
	projection_ll_to_en_deg(projection, left_lon, bottom_lat, &x_min, &y_min);
	projection_ll_to_en_deg(projection, right_lon, top_lat, &x_max, &y_max);
	width = x_max - x_min;
	height = y_max - y_min;
	if (width < 0.0 || height < 0.0) {
		error_msgv(modname, "error: attempt to make a mapsheet with "
			" negative width/height (width = %.2lf m, height = %.2lf m).\n",
			width, height);
		return(NULL);
	}
	return(mapsheet_new_by_center_backed(projection, width, height, east_sz,
							north_sz, center_lon, center_lat, backing_store));
}

MapSheet mapsheet_new_by_proj_bounds_backed(
					Projection projection, f64 east_sz, f64 north_sz,
					f64 left_est, f64 bottom_nth, f64 right_est, f64 top_nth,
					char *backing_store)
{
	f64		center_est = (right_est + left_est)/2.0,
			center_nth = (top_nth + bottom_nth)/2.0,
			width = (right_est - left_est),
			height = (top_nth - bottom_nth);
	
	if (width < 0.0 || height < 0.0) {
		error_msgv(modname, "error: attempt to make a mapsheet with "
			" negative width/height (width = %.2lf m, height = %.2lf m).\n",
			width, height);
		return(NULL);
	}
	return(mapsheet_new_by_proj_center_backed(projection, width, height,
				east_sz, north_sz, center_est, center_nth, backing_store));
}

MapSheet mapsheet_new_direct(
					Projection projection, u32 width, u32 height,
					f64 east_sz, f64 north_sz, f64 west, f64 north)
{
	return(mapsheet_new_direct_backed(projection, width, height, east_sz,
			north_sz, west, north, NULL));
}

MapSheet mapsheet_new_by_center(Projection projection,
								f64 width, f64 height,
								f64 east_sz, f64 north_sz,
								f64 lon, f64 lat)
{
	return(mapsheet_new_by_center_backed(projection, width, height, east_sz,
				north_sz, lon, lat, NULL));
}

MapSheet mapsheet_new_by_proj_center(Projection projection,
									f64 width, f64 height,
									f64 east_sz, f64 north_sz,
									f64 easting, f64 northing)
{
	return(mapsheet_new_by_proj_center_backed(projection, width, height,
					east_sz, north_sz, easting, northing, NULL));
}

MapSheet mapsheet_new_by_bounds(Projection projection,
								f64 east_sz, f64 north_sz,
								f64 left_lon, f64 bottom_lat,
								f64 right_lon, f64 top_lat)
{
	return(mapsheet_new_by_bounds_backed(projection, east_sz, north_sz,
					left_lon, bottom_lat, right_lon, top_lat, NULL));
}

MapSheet mapsheet_new_by_proj_bounds(Projection projection,
								f64 east_sz, f64 north_sz,
								f64 left_est, f64 bottom_nth,
								f64 right_est, f64 top_nth)
{
	return(mapsheet_new_by_proj_bounds_backed(projection, east_sz, north_sz,
					left_est, bottom_nth, right_est, top_nth, NULL));
}

MapSheet mapsheet_new_from_omg_backed(OMGRaster raster, char *backing_store)
{
	MapSheet	rtn;
	Projection	proj = omg_get_projection(raster);
	f64			east_sz, north_sz;
	f64			left, right, bottom, top, cen_x, cen_y;
	u32			n_east, n_north;

	if (proj == NULL) {
		error_msgv(modname, "error: OMG1 raster does not contain a valid"
			" projection type.\n");
		return(NULL);
	}
	
	omg_get_spacings(raster, &east_sz, &north_sz);
	omg_get_sizes(raster, &n_east, &n_north);
	omg_get_bounds(raster, &left, &bottom, &right, &top);

#ifdef __DEBUG__
error_msgv(modname, "raster bounds: l = %lf r = %lf t = %lf b = %lf\n",
	left, right, top, bottom);
#endif

	cen_x = (left+right)/2.0;
	cen_y = (top+bottom)/2.0;
	
#ifdef __DEBUG__
error_msgv(modname, "debug: center at (%lf, %lf)\n", cen_x, cen_y);
#endif

	if (!omg_bounds_projected(raster)) {
		/* Bounds not projected, so project (cen_x, cen_y) */
		f64	x, y;

#ifdef __DEBUG__
projection_ll_to_en_deg(proj, left, bottom, &x, &y);
left = x; bottom = y;
projection_ll_to_en_deg(proj, right, top, &x, &y);
right = x; top = y;
error_msgv(modname, "debug: bounds project to (%lf, %lf) -> (%lf, %lf).\n",
	left, bottom, right, top);
#endif

		projection_ll_to_en_deg(proj, cen_x, cen_y, &x, &y);
		cen_x = x; cen_y = y;
	}
	if ((rtn = mapsheet_construct(proj, cen_x, cen_y, n_east, n_north,
					east_sz, north_sz, backing_store)) == NULL) {
		error_msg(modname, "failed to generate mapsheet from R4.\n");
		return(NULL);
	}

#ifdef __DEBUG__
{
f64	min_lon, max_lon, min_lat, max_lat;

left = rtn->hdr->w_bound;
right = rtn->hdr->w_bound + rtn->hdr->n_east*rtn->hdr->east_sz;
top = rtn->hdr->n_bound;
bottom = top - rtn->hdr->n_north*rtn->hdr->north_sz;
projection_en_to_ll_deg(proj, left, bottom, &min_lon, &min_lat);
projection_en_to_ll_deg(proj, right, top, &max_lon, &max_lat);
error_msgv(modname, "debug: generated sheet of %dx%d nodes, bounds"
	" (%lf, %lf) -> (%lf, %lf).\n", rtn->hdr->n_east, rtn->hdr->n_north,
	min_lon, min_lat, max_lon, max_lat);
}
#endif

	return(rtn);
}

MapSheet mapsheet_new_from_omg(OMGRaster raster)
{
	return(mapsheet_new_from_omg_backed(raster, NULL));
}

void mapsheet_release(MapSheet sheet)
{
#ifdef WIN32
	struct stat			s_stat;
	char				search[_MAX_FNAME];
	long				s_handle;
	struct _finddata_t	find;
#else
	DIR	*dir;
	struct direct *entry;
#endif

	if (sheet != NULL) {
		mapsheet_release_data(sheet->grid,
					sheet->hdr->n_east*sheet->hdr->n_north);
		mapsheet_release_param(sheet->param, sheet->hdr->depth);
		if (sheet->backing_temp) {
#ifdef WIN32
			if (stat(sheet->backing_store, &s_stat) < 0 ||
				!S_ISDIR(s_stat.st_mode)) {
				error_msgv(modname, "warning: backing store directory \"%s\""
					" does not exist at mapsheet release.\n",
					sheet->backing_store);
			} else {
				/* As below ... except that we use ANSI remove() rather than
				 * *nix unlink(2), and _find_first()/_find_next()/_find_close()
				 * rather than BSD directory processing.
				 */
				sprintf(search, "%s\\*", sheet->backing_store);
				if ((s_handle = _findfirst(search, &find)) < 0) {
					error_msgv(modname, "error: failed to match any files in"
							" the backing store directory \"%s\".\n",
							sheet->backing_store);
				} else {
					remove(find.name);
					while (_findnext(s_handle, &find) == 0)
						remove(find.name);
					_findclose(s_handle);
				}
				_rmdir(sheet->backing_store);
			}
#else
			if ((dir = opendir(sheet->backing_store)) == NULL) {
				error_msgv(modname, "warning: backing store directory \"%s\""
					" does not exist at mapsheet release.\n",
					sheet->backing_store);
			} else {
				/* We need to enumerate all of the files in the directory and
				 * unlink(2) them, and then rmdir(2) the directory to remove
				 * all traces of it.
				 */
				while ((entry = readdir(dir)) != NULL)
					unlink(entry->d_name);
				closedir(dir);
				rmdir(sheet->backing_store);
			}
#endif
		}
		free(sheet->backing_store);
		free(sheet->hdr);
		free(sheet->param);
		free(sheet);
	}
}

/* Routine:	mapsheet_new_from_ascii
 * Purpose:	Read an ASCII description of a mapsheet, and construct the base
 * Inputs:	*name	Name of the file to read and parse
 * Outputs:	Pointer to MapSheet structure if successful, otherwise NULL
 * Comment:	This reads a mapsheet description as specified in mapsheet_par.y,
 *			and more usefully in 'Mapsheet File Format Description V1.0'.
 */

extern int mapparse(void *);

MapSheet mapsheet_new_from_ascii(char *name)
{
	MapSheet 	rtn;
	SheetList	*root, *next;
	
	if ((mapin = fopen(name, "r")) == NULL) {
		error_msgv(modname, "failed to open \"%s\" for reading.\n", name);
		return(NULL);
	}
	if (mapparse(NULL) < 0) {
		error_msg(modname, "failed parsing input mapsheet description.\n");
		fclose(mapin);
		return(NULL);
	}
	fclose(mapin);
	if (sheets == NULL || sheets->sheet == NULL) {
		error_msg(modname, "failed to read any sheet descriptions from file.\n");
		return(NULL);
	}
	if (sheets->next != NULL) {
		error_msg(modname, "warning: only using last mapsheet description!\n");
		root = sheets->next;
		while (root != NULL) {
			mapsheet_release(root->sheet);
			next = root->next;
			free(root);
			root = next;
		}
	}
	rtn = sheets->sheet;	/* Extracts mapsheet from parser */
	free(sheets);
	sheets = NULL;
	return(rtn);
}

void mapsheet_delete_depth_surface(MapSheet sheet)
{
	u32	npels = sheet->hdr->n_east * sheet->hdr->n_north;
	
	if (sheet->grid->hcube_grid != NULL) {
		error_msg(modname, "warning: WARNING --- WARNING --- WARNING\n");
		error_msg(modname, "warning: deleting a HyperCUBE surface is "
			" currently not well supported.\n");
		error_msg(modname, "warning: You should delete the mapsheet directory"
			" and start from an ASCII descriptor.\n");
		mapsheet_cube_release(sheet->grid->hcube_grid);
		sheet->grid->hcube_grid = NULL;					/* Mark as gone */
	}
	mapsheet_release_depth_grid(sheet->grid->depth_grid, npels);
	mapsheet_release_bin_grid(sheet->grid->bin_grid, npels);
	sheet->grid->depth_grid = NULL;
	sheet->grid->bin_grid = NULL;
	sheet->hdr->depth = MAP_DEPTH_NONE;
}

Bool mapsheet_add_depth_surface(MapSheet sheet, MapSheetDepthAlg alg,
								ErrModIHOOrder order)
{
	u32		width = sheet->hdr->n_east, height = sheet->hdr->n_north;
	
	/* If we're changing algorithms, release old grid */
	if (alg != sheet->hdr->depth) mapsheet_delete_depth_surface(sheet);
	switch (alg) {
		case MAP_DEPTH_NONE:
			error_msg(modname, "must choose a depth algorithm!\n");
			return(False);
		case MAP_DEPTH_NODAL:
			if ((sheet->grid->depth_grid =
					mapsheet_nodal_alloc_grid(width, height)) == NULL) {
				error_msg(modname, "failed allocating node space.\n");
				return(False);
			}
			if ((sheet->param->depth_param =
					mapsheet_nodal_init_param(order, sheet->hdr->north_sz,
												sheet->hdr->east_sz)) == NULL) {
				error_msg(modname, "failed allocating node parameters\n");
				mapsheet_delete_depth_surface(sheet);
				return(False);
			}
			break;
		case MAP_DEPTH_HYPERCUBE:
			if (sheet->param->hcube_param == NULL &&
				(sheet->param->hcube_param =
					mapsheet_cube_init_param(order, sheet->hdr->north_sz,
												sheet->hdr->east_sz)) == NULL) {
				error_msg(modname, "failed allocating node parameters\n");
				return(False);
			}
			if ((sheet->grid->hcube_grid =
					mapsheet_cube_alloc_grid(sheet, sheet->param->hcube_param,
							sheet->backing_store)) == NULL) {
				error_msg(modname, "failed allocating node space.\n");
				return(False);
			}
			break;
		case MAP_DEPTH_BINED_MEAN:
			if ((sheet->grid->bin_grid =
					mapsheet_alloc_bin_grid(width, height)) == NULL) {
				error_msg(modname, "failed allocating node space.\n");
				return(False);
			}
			if ((sheet->param->bin_param =
					mapsheet_mean_init_param()) == NULL) {
				error_msg(modname, "failed allocating mean parameters.\n");
				mapsheet_delete_depth_surface(sheet);
				return(False);
			}
			break;
		case MAP_DEPTH_BINED_MEDIAN:
			if ((sheet->grid->bin_grid =
					mapsheet_alloc_bin_grid(width, height)) == NULL) {
				error_msg(modname, "failed allocating node space.\n");
				return(False);
			}
			if ((sheet->param->bin_param =
					mapsheet_median_init_param()) == NULL) {
				error_msg(modname, "failed allocating median parameters.\n");
				mapsheet_delete_depth_surface(sheet);
				return(False);
			}
			break;
		default:
			error_msgv(modname, "internal: depth algorithm %d unknown.\n",
				(u32)alg);
			return(False);
	}
	sheet->hdr->depth = alg;
	sheet->hdr->order = order;
	return(True);
}

/* Routine:	mapsheet_add_depth_fixunct
 * Purpose:	Add a depth surface using the MAP_DEPTH_NODAL or MAP_DEPTH_HYPERCUBE
 *			method, and initialise from previous data.
 * Inputs:	sheet		MapSheet to use
 *			alg			Depth algorithm to use
 *			order		IHO Order to use in calibration depth surface
 *			*data		Pointer to depth buffer initialisation data
 *			unct		Fixed uncertainty (see below for interpretation)
 *			unct_pcnt	Flag: True => uncertainty is to be interpreted as a
 *						percentage (0.0 ... 100.0) of the initialisation depth,
 *						rather than as a fixed (1 s.d.) uncertainty in meters.
 *			mask		Mask image used to zero variance estimates (or NULL)
 *			cols, rows	Size of the data input
 * Outputs:	True if surface was added and initialised, otherwise False
 * Comment:	This is the primary method of loading already gridded data into the
 *			mapsheet, and assigning subjective certainty.  The code checks that
 *			the data provided is of the correct size for the mapsheet, and then
 *			allows the mapsheet to initialise via the mapsheet_nodal.c code.
 *			Ideally, the user could be trusted to do this directly, but that
 *			would expose an interface to the underlying code, which is not
 *			desireable here.
 *				Note that the initialisation done depends on the algorithm
 *			specified in the command line.  For MAP_DEPTH_NODAL, the code
 *			reads the initialiser and makes a single hypothesis for each node
 *			not masked out (q.v.) with the uncertainty specified.  For the
 *			MAP_DEPTH_HYPERCUBE algorithm, the code sets the predicted depth
 *			value as well as generating a base hypothesis.  The predicted depth
 *			is used to make slope corrections.
 *				The mask component of the call allows the use to specify areas
 *			of the data which should have zero estimate variance set; in effect,
 *			these areas will never be updated, even if data appears there.  This
 *			is really intended to set land areas which can avoid problems with
 *			stray near-shore beams.  The mask input can be set to NULL if all of
 *			the grid is to be initialised.
 */

Bool mapsheet_add_depth_fixunct(MapSheet sheet, MapSheetDepthAlg alg,
								ErrModIHOOrder order,
								f32 *data, f32 unct, Bool unct_pcnt, u8 *mask,
								u32 cols, u32 rows)
{
	if (alg != MAP_DEPTH_NODAL && alg != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: depth algorithm must be NODAL or HYPERCUBE"
			" for initialisation (not enum value %d).\n", (u32)(alg));
		return(False);
	}
	mapsheet_delete_depth_surface(sheet);
	if (!mapsheet_add_depth_surface(sheet, alg, order)) {
		error_msg(modname, "error: failed adding depth surface for "
			"initialisation.\n");
		return(False);
	}
	sheet->hdr->order = order;
	sheet->hdr->depth = alg;
	if (alg == MAP_DEPTH_NODAL) {
		if (!mapsheet_nodal_reinit_fixunct(sheet, data, unct, unct_pcnt, mask,
																cols, rows)) {
			error_msg(modname, "error: failed reinitialising Nodal depth "
				"surface.\n");
			return(False);
		}
	} else {
		if (!mapsheet_cube_initialise(sheet, sheet->grid->hcube_grid,
									  sheet->param->hcube_param, data,
									  unct, unct_pcnt, mask, cols, rows)) {
			error_msg(modname, "error: failed reinitialising HyperCUBE depth "
				"surface.\n");
			return(False);
		}
	}
	return(True);
}

/* Routine:	mapsheet_add_depth_unct
 * Purpose:	Add a depth surface using the MAP_DEPTH_HYPERCUBE
 *			method, and initialise from previous data.
 * Inputs:	sheet		MapSheet to use
 *			alg			Depth algorithm to use
 *			order		IHO Order to use in calibration depth surface
 *			*data		Pointer to depth buffer initialisation data
 *			*unct		Posterior variance surface in meters^2
 *			mask		Mask image used to zero variance estimates (or NULL)
 *			cols, rows	Size of the data input
 * Outputs:	True if surface was added and initialised, otherwise False
 * Comment:	This is the primary method of loading already gridded data into the
 *			mapsheet, and assigning subjective certainty.  The code checks that
 *			the data provided is of the correct size for the mapsheet, and then
 *			allows the mapsheet to initialise via the mapsheet_cube.c code.
 *			Ideally, the user could be trusted to do this directly, but that
 *			would expose an interface to the underlying code, which is not
 *			desireable here.
 *				This code behaves similarly to the mapsheet_add_depth_fixunct()
 *			call except that it only works for the MAP_DEPTH_HYPERCUBE method
 *			and a direct posterior variance is provided for each node.
 *				The mask component of the call allows the use to specify areas
 *			of the data which should have zero estimate variance set; in effect,
 *			these areas will never be updated, even if data appears there.  This
 *			is really intended to set land areas which can avoid problems with
 *			stray near-shore beams.  The mask input can be set to NULL if all of
 *			the grid is to be initialised.
 */

Bool mapsheet_add_depth_unct(MapSheet sheet, MapSheetDepthAlg alg,
							 ErrModIHOOrder order,
							 f32 *data, f32 *unct, u8 *mask,
							 u32 cols, u32 rows)
{
	if (alg != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: depth algorithm must be NODAL or HYPERCUBE"
			" for initialisation (not enum value %d).\n", (u32)(alg));
		return(False);
	}
	mapsheet_delete_depth_surface(sheet);
	if (!mapsheet_add_depth_surface(sheet, alg, order)) {
		error_msg(modname, "error: failed adding depth surface for "
			"initialisation.\n");
		return(False);
	}
	sheet->hdr->order = order;
	sheet->hdr->depth = alg;
	if (!mapsheet_cube_init_unct(sheet, sheet->grid->hcube_grid,
								 sheet->param->hcube_param, data,
								 unct, mask, cols, rows)) {
		error_msg(modname, "error: failed reinitialising HyperCUBE depth "
			"surface.\n");
		return(False);
	}
	return(True);
}

void mapsheet_delete_hits(MapSheet sheet)
{
	mapsheet_release_hits_grid(sheet->grid->hits_grid);
	sheet->grid->hits_grid = NULL;
}

void mapsheet_zero_hits(MapSheet sheet)
{
	u32	npels = sheet->hdr->n_east * sheet->hdr->n_north;
	memset(sheet->grid->hits_grid[0], 0, sizeof(u32)*npels);
}

Bool mapsheet_add_hits(MapSheet sheet)
{
	u32		width = sheet->hdr->n_east, height = sheet->hdr->n_north;

	mapsheet_delete_hits(sheet);	/* Just in case any are already there */
	if ((sheet->grid->hits_grid =
		 mapsheet_alloc_hits_grid(width, height)) == NULL) {
		error_msg(modname, "failed allocating node space.\n");
		return(False);
	}
	return(True);
}

/* --------------------------------------------------------------------------
 *                        Version 1 Serialization
 * NOTE:
 *  V1.0 mapsheets have been effectively retired because they are very limited
 *  in size and don't work well with HyperCUBE.  This code is really intended
 *  for loading old sheets and converting them to the newest internal format,
 *  rather than for full read/write.  However, the write portion is retained
 *  just in case someone feels the urge ... 
 * -------------------------------------------------------------------------- */

static Bool mapsheet_write_header_v1(FILE *f, const MapHdr *hdr)
{
	u32			hdr_size, base, start;
	MapHdr_V1_2	wr_hdr;

	base = (u32)(&(wr_hdr.easting));
	start = (u32)(&wr_hdr);
	
	hdr_size = sizeof(MapHdr_V1_2) - (base-start);
	
	/* We need to back-convert bounds to f32 for compatability. */
	wr_hdr.projection = hdr->projection;
	wr_hdr.easting = hdr->easting;
	wr_hdr.northing = hdr->northing;
	wr_hdr.n_east = hdr->n_east;
	wr_hdr.n_north = hdr->n_north;
	wr_hdr.east_sz = hdr->east_sz;
	wr_hdr.north_sz = hdr->north_sz;
	wr_hdr.w_bound = (f32)(hdr->w_bound);	/* Note compression! */
	wr_hdr.n_bound = (f32)(hdr->n_bound);
	wr_hdr.depth = hdr->depth;
	wr_hdr.order = hdr->order;
	
	if (!projection_write_projection(f, hdr->projection) ||
		fwrite(&(wr_hdr.easting), 1, hdr_size, f) != 1) return(False);
	return(True);
}

static Bool mapsheet_hits_write_param_v1(void *param, FILE *f)
{
	return(True);	/* No parameters */
}

static Bool mapsheet_refl_write_param_v1(void *param, FILE *f)
{
	return(True);	/* Placeholder */
}

static Bool mapsheet_write_parameters_v1(FILE *f, const MapParam *par,
									  const MapSheetDepthAlg alg)
{
	Bool	rc = True;
	
	if (fwrite(par, sizeof(MapParam), 1, f) != 1) {
		error_msg(modname, "failed to write mapsheet parameter indicators.\n");
		return(False);
	}
	if (par->depth_param != NULL)
		rc &= mapsheet_nodal_write_param(par->depth_param, f);
	if (rc && par->bin_param != NULL) {
		if (alg == MAP_DEPTH_BINED_MEAN)
			rc &= mapsheet_mean_write_param(par->bin_param, f);
		else if (alg == MAP_DEPTH_BINED_MEDIAN)
			rc &= mapsheet_median_write_param(par->bin_param, f);
		else {
			error_msgv(modname, "internal: depth algorithm %d not known.\n",
				(u32)alg);
			return(False);
		}
	}
	if (rc && par->hits_param != NULL)
		rc &= mapsheet_hits_write_param_v1(par->hits_param, f);
	if (rc && par->refl_param != NULL)
		rc &= mapsheet_refl_write_param_v1(par->refl_param, f);
	return(rc);
}

static Bool mapsheet_read_parameters_v1(FILE *f, const MapParam *inpar,
									 MapParam *outpar,
									 const MapSheetDepthAlg alg)
{
	Bool	rc = True;
	
	if (inpar->depth_param != NULL) {
		if ((outpar->depth_param = mapsheet_nodal_read_param(f)) == NULL)
			rc = False;
	}
	if (rc && inpar->bin_param != NULL) {
		switch(alg) {
			case MAP_DEPTH_BINED_MEAN:
				outpar->bin_param = mapsheet_mean_read_param(f);
				break;
			case MAP_DEPTH_BINED_MEDIAN:
				outpar->bin_param = mapsheet_median_read_param(f);
				break;
			default:
				error_msgv(modname, "internal: non-null bin grid indicator, and"
					" algorithm (%d) isn't mean/median binner!.\n", (u32)alg);
				rc = False;
				break;
		}
		if (rc && outpar->bin_param == NULL) {
			error_msg(modname, "failed reading bin grid parameters.\n");
			rc = False;
		}
	}
	if (rc && inpar->hits_param != NULL) {
		error_msg(modname, "internal: found non-null hits parameter indicator!\n");
		rc = False;
	}
	if (rc && inpar->refl_param != NULL) {
		error_msg(modname, "internal: found non-null refl parameter indicator!\n");
		rc = False;
	}
	return(rc);
}

static Bool mapsheet_write_bin_grid_v1(FILE *f, BinNode **g, u32 npels)
{
	BinNode	*grid = g[0];
	u32		size;
	
	while (npels-- > 0) {
		if (fwrite(&(grid->hits), sizeof(u32), 1, f) != 1) return(False);
		size = sizeof(f32)*grid->hits;
		if (fwrite(grid->bin, sizeof(f32), grid->hits, f) != grid->hits) return(False);
		++grid;
	}
	return(True);
}

static Bool mapsheet_write_hits_grid_v1(FILE *f, u32 **g, u32 npels)
{	
	if (fwrite(g[0], sizeof(u32), npels, f) != npels) return(False);
	return(True);
}

static Bool mapsheet_write_refl_grid_v1(FILE *f, u16 **g, u32 npels)
{
	if (fwrite(g[0], sizeof(u16), npels, f) != npels) return(False);
	return(True);
}

static Bool mapsheet_write_data_v1(FILE *f, const MapSheet sheet)
{
	u32		npels = sheet->hdr->n_east * sheet->hdr->n_north;
	
	/* Note that we're saving pointers in the MapData structure, which won't
	 * map when we read back in ... but will indicate whether the component
	 * existed or not.
	 */
	if (fwrite(sheet->grid, sizeof(MapData), 1, f) != 1)
		return(False);

	/* Write the individual grid components in sequence as defined in the
	 * header file.
	 */
	if (sheet->grid->depth_grid != NULL &&
		!mapsheet_nodal_write_grid(f, sheet->grid->depth_grid, 
									sheet->param->depth_param, npels))
		return(False);
	if (sheet->grid->hcube_grid != NULL) {
		error_msg(modname, "warning: found a HyperCUBE grid in mapsheet,"
			" which cannot be serialised into a V1 mapsheet.  Your data"
			" will be incomplete.\n");
	}
	if (sheet->grid->bin_grid != NULL &&
		!mapsheet_write_bin_grid_v1(f, sheet->grid->bin_grid, npels))
		return(False);
	if (sheet->grid->hits_grid != NULL &&
		!mapsheet_write_hits_grid_v1(f, sheet->grid->hits_grid, npels))
		return(False);
	if (sheet->grid->refl_grid != NULL &&
		!mapsheet_write_refl_grid_v1(f, sheet->grid->refl_grid, npels))
		return(False);
	return(True);
}

static Bool mapsheet_read_header_v1(FILE *f, MapHdr *header)
{
	u32			hdr_size, base, start;
	MapHdr_V1_2	rd_hdr;

	base = (u32)(&(rd_hdr.easting));
	start = (u32)(&rd_hdr);
	
	hdr_size = sizeof(MapHdr_V1_2) - (base-start);
	
	if ((header->projection = projection_new_from_file(f)) == NULL) {
		error_msg(modname, "failed reading mapsheet projection.\n");
		return(False);
	}
	if (fread(&(rd_hdr.easting), hdr_size, 1, f) != 1) {
		error_msg(modname, "failed reading mapsheet header.\n");
		projection_release(header->projection);
		return(False);
	}
	
	/* We are mainly converting the W/N bounds to f64 for new internal fmt */
	header->easting = rd_hdr.easting;
	header->northing = rd_hdr.northing;
	header->n_east = rd_hdr.n_east;
	header->n_north = rd_hdr.n_north;
	header->east_sz = rd_hdr.east_sz;
	header->north_sz = rd_hdr.north_sz;
	header->w_bound = rd_hdr.w_bound;	/* Note f32 -> f64 conversion */
	header->n_bound = rd_hdr.n_bound;
	header->depth = rd_hdr.depth;
	header->order = rd_hdr.order;
	
	/* Check that the header is of reasonable dimension */
	if (header->n_east > mapsheet_max_width ||
										header->n_north > mapsheet_max_height) {
		error_msgv(modname, "mapsheet it too large (file: %dx%d nodes, max: "
					"%dx%d.\n)\n", header->n_east, header->n_north,
					mapsheet_max_width, mapsheet_max_height);
		projection_release(header->projection);
		return(False);
	}
	return(True);
}

static Bool mapsheet_read_params_v1(FILE *f, MapSheet sheet)
{
	MapParam	f_param;
	
	if (fread(&f_param, sizeof(MapParam), 1, f) != 1) {
		error_msg(modname, "failed reading mapsheet parameters structure.\n");
		return(False);
	}
	if (!mapsheet_read_parameters_v1(f, &f_param, sheet->param, sheet->hdr->depth)) {
		error_msg(modname, "failed reading algorithm parameters.\n");
		return(False);
	}
	return(True);
}

static Bool mapsheet_read_bin_surface_v1(FILE *f, MapSheet sheet)
{
	u32		pel, npels = sheet->hdr->n_east * sheet->hdr->n_north;
	u32		size;
	BinNode	*g = sheet->grid->bin_grid[0];
	
	for (pel = 0; pel < npels; ++pel) {
		if (fread(&size, sizeof(u32), 1, f) != 1) return(False);
		if (size != 0) {
			if ((g[pel].bin = (f32p)malloc(sizeof(f32)*size)) == NULL)
				return(False);
			g[pel].space = size;
			if (fread(g[pel].bin, sizeof(f32), size, f) != size)
				return(False);
			g[pel].hits = size;
		}
	}
	return(True);
}

static Bool mapsheet_read_hits_v1(FILE *f, MapSheet sheet)
{
	u32	n_pels = sheet->hdr->n_east * sheet->hdr->n_north;
	
	if (fread(sheet->grid->hits_grid[0], sizeof(u32), n_pels, f) != n_pels) return(False);
	return(True);
}

static Bool mapsheet_read_data_v1(FILE *f, MapSheetDepthAlg alg, MapSheet sheet)
{
	MapData	ptrs;
	
	if (fread(&ptrs, sizeof(MapData), 1, f) != 1) {
		error_msg(modname, "failed reading mapsheet data indicators.\n");
		return(False);
	}
	if (ptrs.depth_grid != NULL &&
			(!mapsheet_add_depth_surface(sheet, alg, sheet->hdr->order) ||
			 !mapsheet_nodal_read_surface(f, sheet))) {
		error_msg(modname, "failed reading mapsheet continuous depth buffer.\n");
		return(False);
	}
	if (ptrs.bin_grid != NULL &&
			(!mapsheet_add_depth_surface(sheet, alg, sheet->hdr->order) ||
			 !mapsheet_read_bin_surface_v1(f, sheet))) {
		error_msg(modname, "failed reading mapsheet bined depth buffer.\n");
		return(False);
	}
	if (ptrs.hits_grid != NULL &&
			(!mapsheet_add_hits(sheet) || !mapsheet_read_hits_v1(f, sheet))) {
		error_msg(modname, "failed reading mapsheet hit count buffer.\n");
		return(False);
	}
	if (ptrs.refl_grid != NULL) {
		error_msg(modname, "unexpected backscatter grid found in file!\n");
		return(False);
	}
	return(True);
}

static MapSheet mapsheet_load_v1(FILE *f)
{
	MapHdr		header;
	MapParam	params;
	MapSheet	rtn;
	
	if (!mapsheet_read_header_v1(f, &header)) {
		error_msg(modname, "failed reading mapsheet header.\n");
		return(NULL);
	}

	if ((rtn = mapsheet_construct(header.projection, header.easting,
								header.northing, header.n_east, header.n_north,
								header.east_sz, header.north_sz,
								NULL)) == NULL) {
		error_msg(modname, "failed constructing mapsheet.\n");
		projection_release(header.projection);
		return(NULL);
	}
	rtn->hdr->order = header.order;	/* Copy in IHO order */
	
	if (!mapsheet_read_params_v1(f, rtn)) {
		error_msg(modname, "failed reading algorithm params from file.\n");
		mapsheet_release(rtn);
		return(NULL);
	}
	
	/* Copy pointers for file data to avoid overwrite in
	 * mapsheet_add_depth_surface() etc. later.  We copy them back at the end.
	 */
	memcpy(&params, rtn->param, sizeof(MapParam));

	if (!mapsheet_read_data_v1(f, header.depth, rtn)) {
		error_msg(modname, "failed reading data structures for sheet.\n");
		mapsheet_release(rtn);
		return(NULL);
	}
	
	/* Copy file versions of parameters back into structure. */
	mapsheet_release_param(rtn->param, rtn->hdr->depth);
	memcpy(rtn->param, &params, sizeof(MapParam));
	return(rtn);
}

Bool mapsheet_save_sheet_v1(const MapSheet sheet, char *name)
{
	FILE			*f;
	MapPreHeader	phdr = { MAPSHEET_ID, MAPSHEET_VERSION_1 };
	
	if ((f = fopen(name, "wb")) == NULL)
		return(False);
	if (fwrite(&phdr, sizeof(MapPreHeader), 1, f) != 1 ||
		!mapsheet_write_header_v1(f, sheet->hdr) ||
		!mapsheet_write_parameters_v1(f, sheet->param, sheet->hdr->depth) ||
		!mapsheet_write_data_v1(f, sheet)) {
		fclose(f); return(False);
	}
	fclose(f);
	return(True);
}

/* --------------------------------------------------------------------------
 *                        Version 2 Serialization
 * NOTE:
 *  V2.0.0 serialisation is supported, but is not recommended, since it
 *  retains the f32 header elements which result in truncation due to
 *  mantissa limitations.  Current version is 2.0.1, which uses f64's for the
 *  bounds of the mapsheet, and resolves this problem.  Note that we do not
 *  implement a completely new mapsheet version to do this, simply replacing the
 *  header reader with a switch.
 * -------------------------------------------------------------------------- */

#define MAPSHEET_V2_HEADER		"header"
#define MAPSHEET_V2_PARAMETERS	"parameters"
#define MAPSHEET_V2_DATAPTR		"data"
#define 	MAPSHEET_V2_BINGRID		"bin_grid"
#define 	MAPSHEET_V2_HITSGRID	"hits_grid"
#define 	MAPSHEET_V2_REFLGRID	"refl_grid"
#define 	MAPSHEET_V2_NODEGRID	"node_grid"
#define		MAPSHEET_V2_CUBEGRID	"cube_grid"

char *mapsheet_make_name(const char *dir, const char *file)
{
	char	*rtn;
	u32		length = strlen(dir) + strlen(file) + 2;
	
	if ((rtn = (char*)malloc(length)) == NULL) {
		error_msgv(modname, "error: failed obtaining memory (%d bytes).\n",
			length);
		return(NULL);
	}
	sprintf(rtn, "%s/%s", dir, file);
	return(rtn);
}

static Bool mapsheet_write_header_v2(const char *dirname, const MapHdr *hdr)
{
	u32		hdr_size, base, start;
	char	*filename;
	FILE	*f;
	MapHdr_V1_2	wr_hdr;

	base = (u32)(&(wr_hdr.easting));
	start = (u32)(&wr_hdr);
	
	hdr_size = sizeof(MapHdr_V1_2) - (base-start);

	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_HEADER)) == NULL) {
		error_msgv(modname, "error: failed to allocate memory for mapsheet"
			"header filename.\n");
		return(False);
	}
	if ((f = fopen(filename, "wb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			filename);
		free(filename);
		return(False);
	}
	
	/* We need to back-convert bounds to f32 for compatability. */
	wr_hdr.projection = hdr->projection;
	wr_hdr.easting = hdr->easting;
	wr_hdr.northing = hdr->northing;
	wr_hdr.n_east = hdr->n_east;
	wr_hdr.n_north = hdr->n_north;
	wr_hdr.east_sz = hdr->east_sz;
	wr_hdr.north_sz = hdr->north_sz;
	wr_hdr.w_bound = (f32)(hdr->w_bound);	/* Note compression! */
	wr_hdr.n_bound = (f32)(hdr->n_bound);
	wr_hdr.depth = hdr->depth;
	wr_hdr.order = hdr->order;
	
	if (!projection_write_projection(f, hdr->projection) ||
		fwrite(&(wr_hdr.easting), hdr_size, 1, f) != 1) {
		error_msgv(modname, "error: failed writing mapsheet header file \"%s\""
			".\n", filename);
		free(filename);
		return(False);
	}
	fclose(f);
	free(filename);
	return(True);
}

static Bool mapsheet_write_header_v201(const char *dirname, const MapHdr *hdr)
{
	u32		hdr_size, base, start;
	char	*filename;
	FILE	*f;

	base = (u32)(&(hdr->easting));
	start = (u32)hdr;
	
	hdr_size = sizeof(MapHdr) - (base-start);
	
	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_HEADER)) == NULL) {
		error_msgv(modname, "error: failed to allocate memory for mapsheet"
			"header filename.\n");
		return(False);
	}
	if ((f = fopen(filename, "wb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			filename);
		free(filename);
		return(False);
	}
		
	if (!projection_write_projection(f, hdr->projection) ||
		fwrite(&(hdr->easting), hdr_size, 1, f) != 1) {
		error_msgv(modname, "error: failed writing mapsheet header file \"%s\""
			".\n", filename);
		free(filename);
		return(False);
	}
	fclose(f);
	free(filename);
	return(True);
}

static Bool mapsheet_write_parameters_v2(const char *dirname, const MapParam *par,
									  const MapSheetDepthAlg alg)
{
	Bool	rc = True;
	char	*filename;
	FILE	*f;
	
	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_PARAMETERS)) == NULL) {
		error_msgv(modname, "error: failed to make parameters file in \"%s\".\n",
			dirname);
		return(False);
	}
	if ((f = fopen(filename, "wb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			filename);
		free(filename);
		return(False);
	}
	
	if (fwrite(par, sizeof(MapParam), 1, f) != 1) {
		error_msgv(modname, "error: failed to write mapsheet parameter "
			"indicators to \"%s\".\n", filename);
		free(filename);
		fclose(f);
		return(False);
	}
	if (par->hcube_param != NULL)
		rc &= mapsheet_cube_write_param(par->hcube_param, f);
	if (par->depth_param != NULL)
		rc &= mapsheet_nodal_write_param(par->depth_param, f);
	if (rc && par->bin_param != NULL) {
		if (alg == MAP_DEPTH_BINED_MEAN)
			rc &= mapsheet_mean_write_param(par->bin_param, f);
		else if (alg == MAP_DEPTH_BINED_MEDIAN)
			rc &= mapsheet_median_write_param(par->bin_param, f);
		else {
			error_msgv(modname, "internal: depth algorithm %d not known.\n",
				(u32)alg);
			free(filename);
			fclose(f);
			return(False);
		}
	}
	if (rc && par->hits_param != NULL)
		rc &= mapsheet_hits_write_param_v1(par->hits_param, f);
	if (rc && par->refl_param != NULL)
		rc &= mapsheet_refl_write_param_v1(par->refl_param, f);
	fclose(f);
	free(filename);
	return(rc);
}

static Bool mapsheet_read_parameters_v2(FILE *f, const MapParam *inpar,
									 MapParam *outpar,
									 const MapSheetDepthAlg alg)
{
	Bool	rc = True;


	if (inpar->hcube_param != NULL) {
#ifdef __DEBUG__
		error_msgv(modname, "debug: reading HyperCUBE parameters.\n");
#endif
		if ((outpar->hcube_param = mapsheet_cube_read_param(f)) == NULL)
			rc = False;
	}
	if (rc && inpar->depth_param != NULL) {
		if ((outpar->depth_param = mapsheet_nodal_read_param(f)) == NULL)
			rc = False;
	}
	if (rc && inpar->bin_param != NULL) {
		switch(alg) {
			case MAP_DEPTH_BINED_MEAN:
				outpar->bin_param = mapsheet_mean_read_param(f);
				break;
			case MAP_DEPTH_BINED_MEDIAN:
				outpar->bin_param = mapsheet_median_read_param(f);
				break;
			default:
				error_msgv(modname, "internal: non-null bin grid indicator, and"
					" algorithm (%d) isn't mean/median binner!.\n", (u32)alg);
				rc = False;
				break;
		}
		if (rc && outpar->bin_param == NULL) {
			error_msg(modname, "failed reading bin grid parameters.\n");
			rc = False;
		}
	}
	if (rc && inpar->hits_param != NULL) {
		error_msg(modname, "internal: found non-null hits parameter indicator!\n");
		rc = False;
	}
	if (rc && inpar->refl_param != NULL) {
		error_msg(modname, "internal: found non-null refl parameter indicator!\n");
		rc = False;
	}
	return(rc);
}

static Bool mapsheet_write_bin_grid_v2(const char *dirname, BinNode **g, u32 npels)
{
	BinNode	*grid = g[0];
	char	*filename;
	FILE	*f;
	
	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_BINGRID)) == NULL) {
		error_msgv(modname, "error: failed to make parameters file in \"%s\".\n",
			dirname);
		return(False);
	}
	if ((f = fopen(filename, "wb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			filename);
		free(filename);
		return(False);
	}

	while (npels-- > 0) {
		if (fwrite(&(grid->hits), sizeof(u32), 1, f) != 1) {
			free(filename);
			fclose(f);
			return(False);
		}
		if (fwrite(grid->bin, sizeof(f32), grid->hits, f) != grid->hits) {
			free(filename);
			fclose(f);
			return(False);
		}
		++grid;
	}
	free(filename);
	fclose(f);
	return(True);
}

static Bool mapsheet_write_hits_grid_v2(const char *dirname, u32 **g, u32 npels)
{
	Bool	rc = True;
	char	*filename;
	FILE	*f;
	
	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_HITSGRID)) == NULL) {
		error_msgv(modname, "error: failed to make hits grid file in \"%s\".\n",
			dirname);
		rc = False;
	}
	if (rc && (f = fopen(filename, "wb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			filename);
		free(filename);
		return(False);
	}
	
	if (rc && fwrite(g[0], sizeof(u32), npels, f) != npels) {
		error_msgv(modname, "error: failed writing \"%s\".\n", filename);
		rc = False;
	}
	free(filename);
	if (f != NULL) fclose(f);
	return(rc);
}

static Bool mapsheet_write_refl_grid_v2(const char *dirname, u16 **g, u32 npels)
{
	Bool	rc = True;
	char	*filename;
	FILE	*f;
	
	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_REFLGRID)) == NULL) {
		error_msgv(modname, "error: failed to make reflection grid file in "
			"\"%s\".\n", dirname);
		rc = False;
	}
	if (rc && (f = fopen(filename, "wb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			filename);
		rc = False;
	}
	
	if (rc && fwrite(g[0], sizeof(u32), npels, f) != npels) {
		error_msgv(modname, "error: failed writing \"%s\".\n",
			filename);
		rc = False;
	}
	free(filename);
	if (f != NULL) fclose(f);
	return(rc);
}

static Bool mapsheet_write_data_v2(char *dirname, const MapSheet sheet)
{
	u32		npels = sheet->hdr->n_east * sheet->hdr->n_north;
	char	*filename;
	FILE	*f;
	Bool	rc = True;
	
	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_DATAPTR)) == NULL) {
		error_msgv(modname, "error: failed to make data pointer file in "
			"\"%s\".\n", dirname);
		rc = False;
	}
	if (rc && (f = fopen(filename, "wb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			filename);
		rc = False;
	}
	
	/* Note that we're saving pointers in the MapData structure, which won't
	 * map when we read back in ... but will indicate whether the component
	 * existed or not.
	 */
	if (rc && fwrite(sheet->grid, sizeof(MapData), 1, f) != 1) {
		error_msgv(modname, "error: failed writing \"%s\".\n", filename);
		rc = False;
	}
	free(filename);
	if (f != NULL) fclose(f);

	/* Write the individual grid components in sequence as defined in the
	 * header file.
	 */
	if (rc && sheet->grid->depth_grid != NULL) {
		if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_NODEGRID)) == NULL) {
			error_msgv(modname, "error: failed to make nodal grid filename in"
				" \"%s\".\n", dirname);
			rc = False;
		}
		if (rc && (f = fopen(filename, "wb")) == NULL) {
			error_msgv(modname, "error: failed to open \"%s\".\n", filename);
			rc = False;
		}
		if (rc && !mapsheet_nodal_write_grid(f, sheet->grid->depth_grid, 
									sheet->param->depth_param, npels))
			rc = False;
		free(filename);
		if (f != NULL) fclose(f);
	}
	if (rc && sheet->grid->hcube_grid != NULL &&
		!mapsheet_cube_serialise(sheet, dirname, sheet->grid->hcube_grid,
									sheet->param->hcube_param))
		rc = False;
	if (rc && sheet->grid->bin_grid != NULL &&
		!mapsheet_write_bin_grid_v2(dirname, sheet->grid->bin_grid, npels))
		rc = False;
	if (rc && sheet->grid->hits_grid != NULL &&
		!mapsheet_write_hits_grid_v2(dirname, sheet->grid->hits_grid, npels))
		rc = False;
	if (rc && sheet->grid->refl_grid != NULL &&
		!mapsheet_write_refl_grid_v2(dirname, sheet->grid->refl_grid, npels))
		rc = False;
	return(rc);
}

static Bool mapsheet_read_header_v2(const char *dirname, MapHdr *header)
{
	u32		hdr_size, base, start;
	char	*filename;
	FILE	*f;
	Bool	rc = True;
	MapHdr_V1_2	rd_hdr;
	
	base = (u32)(&(rd_hdr.easting));
	start = (u32)(&rd_hdr);
	
	hdr_size = sizeof(MapHdr_V1_2) - (base-start);
	
	error_msgv(modname, "debug: header size is %d read size %d\n",
		sizeof(MapHdr_V1_2), hdr_size);
	error_msgv(modname, "debug: &rd_hdr = %p, &rd_hdr.easting = %p\n",
		&rd_hdr, &(rd_hdr.easting));
	
	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_HEADER)) == NULL) {
		error_msgv(modname, "error: failed to get namespace for header file"
			" in \"%s\".\n", dirname);
		rc = False;
	}
	if (rc && (f = fopen(filename, "rb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for input.\n",
			filename);
		rc = False;
	}
	
	if (rc && (header->projection = projection_new_from_file(f)) == NULL) {
		error_msg(modname, "error: failed reading mapsheet projection.\n");
		rc = False;
	}

	if (rc && fread(&(rd_hdr.easting), hdr_size, 1, f) != 1) {
		error_msg(modname, "error: failed reading mapsheet header.\n");
		rc = False;
	}
	
	/* We are mainly converting the W/N bounds to f64 for new internal fmt */
	header->easting = rd_hdr.easting;
	header->northing = rd_hdr.northing;
	header->n_east = rd_hdr.n_east;
	header->n_north = rd_hdr.n_north;
	header->east_sz = rd_hdr.east_sz;
	header->north_sz = rd_hdr.north_sz;
	header->w_bound = rd_hdr.w_bound;	/* Note f32 -> f64 conversion */
	header->n_bound = rd_hdr.n_bound;
	header->depth = rd_hdr.depth;
	header->order = rd_hdr.order;
	
	/* Check that the header is of reasonable dimension */
	if (rc && (header->n_east > mapsheet_max_width ||
			   header->n_north > mapsheet_max_height)) {
		error_msgv(modname, "error: mapsheet it too large (file: %dx%d nodes, "
					"max: %dx%d.\n)\n", header->n_east, header->n_north,
					mapsheet_max_width, mapsheet_max_height);
		rc = False;
	}
	if (!rc && header->projection != NULL)
		projection_release(header->projection);
	free(filename);
	if (f != NULL) fclose(f);
	
	return(rc);
}

static Bool mapsheet_read_header_v201(const char *dirname, MapHdr *header)
{
	u32		hdr_size, base, start;
	char	*filename;
	FILE	*f;
	Bool	rc = True;

	base = (u32)(&(header->easting));
	start = (u32)header;
	
	hdr_size = sizeof(MapHdr) - (base-start);

	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_HEADER)) == NULL) {
		error_msgv(modname, "error: failed to get namespace for header file"
			" in \"%s\".\n", dirname);
		rc = False;
	}
	if (rc && (f = fopen(filename, "rb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for input.\n",
			filename);
		rc = False;
	}
	
	if (rc && (header->projection = projection_new_from_file(f)) == NULL) {
		error_msg(modname, "error: failed reading mapsheet projection.\n");
		rc = False;
	}
	if (rc && fread(&(header->easting), hdr_size, 1, f) != 1) {
		error_msg(modname, "error: failed reading mapsheet header.\n");
		rc = False;
	}
	
	/* Check that the header is of reasonable dimension */
	if (rc && (header->n_east > mapsheet_max_width ||
			   header->n_north > mapsheet_max_height)) {
		error_msgv(modname, "error: mapsheet it too large (file: %dx%d nodes, "
					"max: %dx%d.\n)\n", header->n_east, header->n_north,
					mapsheet_max_width, mapsheet_max_height);
		rc = False;
	}
	if (!rc && header->projection != NULL)
		projection_release(header->projection);
	free(filename);
	if (f != NULL) fclose(f);

#ifdef __DEBUG__
	error_msgv(modname, "debug: (V2.0.1) depth algorithm %d read from file.\n",
		(u32)(header->depth));
#endif

	return(rc);
}

static Bool mapsheet_read_params_v2(const char *dirname, MapSheet sheet,
									MapSheetDepthAlg alg)
{
	MapParam	f_param;
	char		*filename;
	FILE		*f;
	Bool		rc = True;

	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_PARAMETERS)) == NULL) {
		error_msgv(modname, "error: failed to get namespace for parameters file"
			" in \"%s\".\n", dirname);
		rc = False;
	}
	if (rc && (f = fopen(filename, "rb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for input.\n",
			filename);
		rc = False;
	}
	
	if (rc && fread(&f_param, sizeof(MapParam), 1, f) != 1) {
		error_msg(modname, "error: failed reading \"%s\" for mapsheet "
			"parameters structure.\n");
		rc = False;
	}
	if (rc && !mapsheet_read_parameters_v2(f, &f_param, sheet->param, alg)) {
		error_msgv(modname, "error: failed reading algorithm parameters"
			" from \"%s\".\n", filename);
		rc = False;
	}
	free(filename);
	if (f != NULL) fclose(f);
	return(rc);
}

static Bool mapsheet_read_bin_surface_v2(const char *dirname, MapSheet sheet)
{
	u32		pel, npels = sheet->hdr->n_east * sheet->hdr->n_north;
	u32		size;
	BinNode	*g = sheet->grid->bin_grid[0];
	char	*filename;
	FILE	*f;
	Bool	rc = True;
	
	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_BINGRID)) == NULL) {
		error_msgv(modname, "error: failed to get namespace for bin_grid file"
			" in \"%s\".\n", dirname);
		rc = False;
	}
	if (rc && (f = fopen(filename, "rb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for input.\n",
			filename);
		rc = False;
	}		
		
	if (rc) {
		for (pel = 0; pel < npels; ++pel) {
			if (fread(&size, sizeof(u32), 1, f) != 1) {
				error_msgv(modname, "error: failed reading size for bin %d.\n",
					pel);
				rc = False;
				break;
			}
			if (size != 0) {
				if ((g[pel].bin = (f32p)malloc(sizeof(f32)*size)) == NULL) {
					error_msgv(modname, "error: failed allocating memory for"
						" bin %d.\n", pel);
					rc = False;
					break;
				}
				g[pel].space = size;
				if (fread(g[pel].bin, sizeof(f32), size, f) != size) {
					error_msgv(modname, "error: failed reading bin %d.\n",
						pel);
					rc = False;
					break;
				}
				g[pel].hits = size;
			}
		}
	}
	if (!rc)
		error_msgv(modname, "error: failed reading \"%s\".\n", filename);
	free(filename);
	if (f != NULL) fclose(f);
	return(rc);
}

static Bool mapsheet_read_hits_v2(const char *dirname, MapSheet sheet)
{
	u32		npels = sheet->hdr->n_east * sheet->hdr->n_north;
	Bool	rc = True;
	char	*filename;
	FILE	*f;
	
	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_HITSGRID)) == NULL) {
		error_msgv(modname, "error: failed to get namespace for hits_grid file"
			" in \"%s\".\n", dirname);
		rc = False;
	}
	if (rc && (f = fopen(filename, "rb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for input.\n",
			filename);
		rc = False;
	}
	
	if (rc && fread(sheet->grid->hits_grid[0], sizeof(u32), npels, f) != npels) {
		error_msgv(modname, "error: failed reading \"%s\".\n", filename);
		rc = False;
	}
	free(filename);
	if (f >= 0) fclose(f);
	return(rc);
}

static Bool mapsheet_read_data_v2(const char *dirname, MapSheetDepthAlg alg,
								  MapSheet sheet)
{
	MapData	ptrs;
	Bool	rc = True;
	char	*filename;
	FILE	*f;

	if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_DATAPTR)) == NULL) {
		error_msgv(modname, "error: failed to get namespace for data ptr. file"
			" in \"%s\".\n", dirname);
		rc = False;
	}
	if (rc && (f = fopen(filename, "rb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for input.\n",
			filename);
		rc = False;
	}
	if (rc && fread(&ptrs, sizeof(MapData), 1, f) != 1) {
		error_msgv(modname, "failed reading mapsheet data indicators from"
			" \"%s\".\n", filename);
		rc = False;
	}
	free(filename);
	if (f != NULL) fclose(f);
	
	if (rc && ptrs.depth_grid != NULL &&
			mapsheet_add_depth_surface(sheet, alg, sheet->hdr->order)) {
		if ((filename = mapsheet_make_name(dirname, MAPSHEET_V2_NODEGRID)) == NULL) {
			error_msgv(modname, "error: failed to get namespace for node grid "
				"file in \"%s\".\n", dirname);
			rc = False;
		}
		if (rc && (f = fopen(filename, "rb")) == NULL) {
			error_msgv(modname, "error: failed to open \"%s\" for input.\n",
				filename);
			rc = False;
		}
		if (rc && !mapsheet_nodal_read_surface(f, sheet)) {
			error_msg(modname, "failed reading mapsheet continuous depth "
				" buffer.\n");
			rc = False;
		}
		free(filename);
		if (f != NULL) fclose(f);
	}
	if (rc && ptrs.hcube_grid != NULL &&
			(!mapsheet_add_depth_surface(sheet, alg, sheet->hdr->order) ||
			 !mapsheet_cube_deserialise(dirname, sheet))) {
		error_msg(modname, "error: failed reading HyperCUBE surface.\n");
		rc = False;
	}
	if (rc && ptrs.bin_grid != NULL &&
			(!mapsheet_add_depth_surface(sheet, alg, sheet->hdr->order) ||
			 !mapsheet_read_bin_surface_v2(dirname, sheet))) {
		error_msg(modname, "failed reading mapsheet bined depth buffer.\n");
		rc = False;
	}
	if (rc && ptrs.hits_grid != NULL &&
			(!mapsheet_add_hits(sheet) || !mapsheet_read_hits_v2(dirname, sheet))) {
		error_msg(modname, "failed reading mapsheet hit count buffer.\n");
		rc = False;
	}
	if (rc && ptrs.refl_grid != NULL) {
		error_msg(modname, "unexpected backscatter grid found in file!\n");
		rc = False;
	}
	return(rc);
}

Bool mapsheet_save_sheet_v2(const MapSheet sheet, char *dirname)
{
	FILE			*f;
	MapPreHeader	phdr = { MAPSHEET_ID, MAPSHEET_VERSION_2 };
	char			*filename = NULL;
	Bool			rc = True;
	
	if (!mapsheet_make_directory(dirname)) {
		error_msgv(modname, "error: failed to make output mapsheet directory"
			" \"%s\".\n", dirname);
		rc = False;
	}
	if (rc && (filename = mapsheet_make_name(dirname, MAPSHEET_FILE_ID)) == NULL) {
		error_msgv(modname, "error: failed to get namespace for ID file"
			" in \"%s\".\n", dirname);
		rc = False;
	}
	if (rc && (f = fopen(filename, "wb")) == NULL) {
		error_msgv(modname, "error: failed opening \"%s\" for output.\n",
			filename);
		rc = False;
	}
	if (rc &&
		(fwrite(&phdr, sizeof(MapPreHeader), 1, f) != 1 ||
		!mapsheet_write_header_v2(dirname, sheet->hdr) ||
		!mapsheet_write_parameters_v2(dirname, sheet->param, sheet->hdr->depth) ||
		!mapsheet_write_data_v2(dirname, sheet))) {
		rc = False;
	}
	if (!rc)
		error_msgv(modname, "error: failed serialisation to \"%s\".\n",
			dirname);
	if (filename != NULL) free(filename);
	if (f != NULL) fclose(f);
	return(rc);
}

Bool mapsheet_save_sheet_v201(const MapSheet sheet, char *dirname)
{
	FILE			*f = NULL;
	MapPreHeader	phdr = { MAPSHEET_ID, MAPSHEET_VERSION_201 };
	char			*filename = NULL;
	Bool			rc = True;
	
	if (!mapsheet_make_directory(dirname)) {
		error_msgv(modname, "error: failed to make output mapsheet directory"
			" \"%s\".\n", dirname);
		rc = False;
	}
	if (rc && (filename = mapsheet_make_name(dirname, MAPSHEET_FILE_ID)) == NULL) {
		error_msgv(modname, "error: failed to get namespace for ID file"
			" in \"%s\".\n", dirname);
		rc = False;
	}
	if (rc && (f = fopen(filename, "wb")) == NULL) {
		error_msgv(modname, "error: failed opening \"%s\" for output.\n",
			filename);
		rc = False;
	}
	if (rc &&
		(fwrite(&phdr, sizeof(MapPreHeader), 1, f) != 1 ||
		!mapsheet_write_header_v201(dirname, sheet->hdr) ||
		!mapsheet_write_parameters_v2(dirname, sheet->param, sheet->hdr->depth) ||
		!mapsheet_write_data_v2(dirname, sheet))) {
		rc = False;
	}
	if (!rc)
		error_msgv(modname, "error: failed serialisation to \"%s\".\n",
			dirname);
	if (filename != NULL) free(filename);
	if (f != NULL) fclose(f);
	return(rc);
}

Bool mapsheet_save_sheet(const MapSheet sheet, char *name)
{
	Bool	rc;
	
	switch (MAPSHEET_CUR_VERSION) {
		case MAPSHEET_VERSION_1:
			rc = mapsheet_save_sheet_v1(sheet, name);
			break;
		case MAPSHEET_VERSION_2:
			rc = mapsheet_save_sheet_v2(sheet, name);
			break;
		case MAPSHEET_VERSION_201:
			rc = mapsheet_save_sheet_v201(sheet, name);
			break;
		default:
			error_msg(modname, "internal: error: current version of mapsheet"
				" is not known to mapsheet_save_sheet().\n");
			rc = False;
			break;
	}
	return(rc);
}

static MapSheet mapsheet_load_v2(char *dirname, u32 version)
{
	MapHdr		header;
	MapParam	params;
	MapSheet	rtn;
	
	if (version == MAPSHEET_VERSION_2) {
		if (!mapsheet_read_header_v2(dirname, &header)) {
			error_msg(modname, "failed reading mapsheet header.\n");
			return(NULL);
		}
	} else if (version == MAPSHEET_VERSION_201) {
		if (!mapsheet_read_header_v201(dirname, &header)) {
			error_msg(modname, "failed reading mapsheet header.\n");
			return(NULL);
		}
	} else {
		error_msgv(modname, "error: internal: version 2 sub-type (%d) not"
			" known.\n", version);
		return(NULL);
	}

	if ((rtn = mapsheet_construct(header.projection, header.easting,
								header.northing, header.n_east, header.n_north,
								header.east_sz, header.north_sz,
								dirname)) == NULL) {
		error_msg(modname, "failed constructing mapsheet.\n");
		projection_release(header.projection);
		return(NULL);
	}
	rtn->hdr->order = header.order;	/* Copy in IHO order */
	
	if (!mapsheet_read_params_v2(dirname, rtn, header.depth)) {
		error_msg(modname, "failed reading algorithm params from file.\n");
		mapsheet_release(rtn);
		return(NULL);
	}

	/* Copy pointers for file data to avoid overwrite in
	 * mapsheet_add_depth_surface() etc. later.  We copy them back at the end.
	 */
	memcpy(&params, rtn->param, sizeof(MapParam));

	if (!mapsheet_read_data_v2(dirname, header.depth, rtn)) {
		error_msg(modname, "failed reading data structures for sheet.\n");
		mapsheet_release(rtn);
		return(NULL);
	}
	
	/* Copy file versions of parameters back into structure. */
	rtn->param->hcube_param = NULL;
		/* HyperCUBE is a special case: the parameters of the algorithm *must*
		 * be preserved over load/store cycles because they determine the size
		 * of the grids and tiles.  Consequently, the remainder of the code
		 * in mapsheet_read_data_v2() and mapsheet_add_depth_surface() ensure
		 * that the pointer is not modified if it is set at all (i.e., there
		 * was a surface there to start with).  If we don't remove it from the
		 * mix here, the mapsheet_release_param() before causes it to be freed
		 * since the mapsheet_read_data_v2() components don't compute another
		 * parameter set.
		 */
	mapsheet_release_param(rtn->param, rtn->hdr->depth);
	memcpy(rtn->param, &params, sizeof(MapParam));

	return(rtn);
}

MapSheet mapsheet_load_sheet(char *name)
{
	FILE			*f;
	MapPreHeader	phdr;
	MapSheet		rtn;
	struct stat		fstat;
	char			*filename;
	
	if (stat(name, &fstat) < 0) {
		error_msgv(modname, "error: file \"%s\" cannot be stat(2)ed.\n",
			name);
		return(NULL);
	}
	if (S_ISDIR(fstat.st_mode)) {
		/* This may be a V2 or higher mapsheet with broken out structures. */
		if ((filename = mapsheet_make_name(name, MAPSHEET_FILE_ID)) == NULL) {
			error_msgv(modname, "error: failed getting namespace for file ID"
				" file in \"%s\".\n", name);
			return(NULL);
		}
	} else
		filename = name;
	if ((f = fopen(filename, "rb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for mapsheet ID.\n",
			filename);
		if (S_ISDIR(fstat.st_mode)) free(filename);
		return(NULL);
	}
	if (fread(&phdr, sizeof(MapPreHeader), 1, f) != 1) {
		error_msgv(modname, "error: failed to read mapsheet pre-header from "
			"\"%s\".\n", filename);
		fclose(f);
		if (S_ISDIR(fstat.st_mode)) free(filename);
		return(NULL);
	}
	if (S_ISDIR(fstat.st_mode)) free(filename);
	if (phdr.magic_num != MAPSHEET_ID) {
		error_msgv(modname, "error: file \"%s\" is not a binary mapsheet.\n",
			name);
		fclose(f);
		return(NULL);
	}
	switch(phdr.version) {
		case MAPSHEET_VERSION_1:
			rtn = mapsheet_load_v1(f);
			fclose(f);
			break;
		case MAPSHEET_VERSION_2:
		case MAPSHEET_VERSION_201:
			rtn = mapsheet_load_v2(name, phdr.version);
			break;
		default:
			error_msg(modname, "File version not known.\n");
			rtn = NULL;
			break;
	}
	
	return(rtn);
}

/* --------------------------------------------------------------------------
 *                          Parameter Handling
 * -------------------------------------------------------------------------- */

/* Routine:	mapsheet_execute_params
 * Purpose:	Execute parameters for this module and its sub-modules
 * Inputs:	*list	ParList to execute
 * Outputs:	True if parameters were executed properly in this module and its
 *			sub-modules, otherwise False
 * Comment:	This looks for maximum height and width of the mapsheet, and then
 *			passes the list to the sub-module estimators for further work.
 */

typedef enum {
	MAP_BS_UNKNOWN = 0,
	MAP_BS_MAX_HEIGHT,
	MAP_BS_MAX_WIDTH
} MapParamEnum;

Bool mapsheet_execute_params(ParList *list)
{
	ParTable tab[] = {
		{ "max_height",	MAP_BS_MAX_HEIGHT	},
		{ "max_width",	MAP_BS_MAX_WIDTH	},
		{ NULL,			MAP_BS_UNKNOWN		}
	};
	Bool	rc;
	ParList	*node, *match;
	u32		id;
	u32		dummy_int;

	node = list;
	do {
		node = params_match(node, "mapsheet", tab, &id, &match);
		switch (id) {
			case MAP_BS_UNKNOWN:
				break;
			case MAP_BS_MAX_HEIGHT:
				dummy_int = atoi(match->data);
				if (dummy_int < MIN_MAX_HEIGHT || dummy_int > MAX_MAX_HEIGHT) {
					error_msgv(modname, "error: mapsheet max height"
						" must be in range [%d, %d] (not %d).\n",
						MIN_MAX_HEIGHT, MAX_MAX_HEIGHT, dummy_int);
					return(False);
				}
				mapsheet_max_height = dummy_int;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting mapsheet maximum height to %d (%s).\n",
	dummy_int, match->data);
#endif
				break;
			case MAP_BS_MAX_WIDTH:
				dummy_int = atoi(match->data);
				if (dummy_int < MIN_MAX_WIDTH || dummy_int > MAX_MAX_WIDTH) {
					error_msgv(modname, "error: mapsheet max width"
						" must be in range [%d, %d] (not %d).\n",
						MIN_MAX_WIDTH, MAX_MAX_WIDTH, dummy_int);
					return(False);
				}
				mapsheet_max_width = dummy_int;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting mapsheet maximum width to %d (%s).\n",
	dummy_int, match->data);
#endif
				break;
			default:
				error_msgv(modname, "error: unknown return from parameter"
					" matching module (%d).\n", id);
				return(False);
				break;
		}
	} while (node != NULL);
	
	rc = True;
	rc &= mapsheet_median_execute_params(list);
	rc &= mapsheet_mean_execute_params(list);
	rc &= mapsheet_nodal_execute_params(list);
	rc &= mapsheet_cube_execute_params(list);
	
	return(rc);
}

extern void mapsheet_cube_describe_params(void *);

void mapsheet_describe_params(MapSheet sheet)
{
	mapsheet_cube_describe_params(sheet->param->hcube_param);
}
