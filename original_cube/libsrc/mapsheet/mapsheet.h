/*
 * $Id: mapsheet.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:43  brc
 * Initial revision
 *
 * Revision 1.2.4.3  2003/01/31 21:46:26  dneville
 * (BRC)  Added prototypes for nomination, reset of nomination, extraction and
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
 * Revision 1.12.2.1  2002/12/15 01:35:28  brc
 * Added prototypes and types to support the multiple data-surface read-back
 * extension to user level, and to support call-through for memory reduction.
 *
 * Revision 1.12  2002/05/10 21:06:49  brc
 * Added a cache element for hypothesis strength readback (only for HyperCUBE).
 * Added the AOI structure to hold information and statistics on areas of
 * interest determined from analysis of a MapSheet.  Added prototypes for the
 * new 'precentage of depth' and 'full uncertainty' initialisation methods.
 *
 * Revision 1.11  2001/12/07 21:00:03  brc
 * Added facility to force dump of hypotheses.  Not recommended for normal
 * users, since it's a little flakey at the minute.  This is mostly used for
 * debug information, or to make some illustrations of the hypotheses.
 *
 * Revision 1.10  2001/09/20 18:19:43  brc
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
 * Revision 1.9  2001/08/28 16:01:13  brc
 * Added facility to allow the mapsheet parameters to be reset to those
 * currently in force.  This is required if the parameters on a mapsheet need
 * to be changed after a save/load cycle (otherwise you're stuck with whatever
 * is in the sheet, since the parameter construction is not run on load).  There
 * are very few checks that the new parameters currently in place make sense
 * w.r.t. the sheet (e.g., median queue lengths, estimation modes), so users
 * should be wary of indiscriminate use of this method.
 *
 * Revision 1.8  2001/08/21 01:45:43  brc
 * Added facility to count the number of soundings actually used from each swath
 * to update the data in the mapsheet region.  This can be used, among other things,
 * to determine which data files in a set are actually used in updating the data.
 *
 * Revision 1.7  2001/08/11 00:02:20  brc
 * Added mapsheet parser to core code, rather than having it hidden in the
 * utilities section.  This also means that the interface is nicely hidden, and
 * that the user just sees mapsheet_new_from_ascii().
 *
 * Revision 1.6  2001/05/14 21:05:49  brc
 * Added prototypes for parameter executor code and summary construction code.
 *
 * Revision 1.5  2001/04/10 22:53:44  brc
 * Added standard deviation surface type code and prototype for the data
 * reinitialisation sequence interface to mapsheet_nodal.c
 *
 * Revision 1.4  2000/12/03 19:48:14  brc
 * Added mapsheet_count_hits() and mapsheet_finalise() prototypes.
 *
 * Revision 1.3  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
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
 * File:	mapsheet.h
 * Purpose:	Types and prototypes for the map-sheet datastructure.
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

#ifndef __MAPSHEET_H__
#define __MAPSHEET_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "stdtypes.h"
#include "projection.h"
#include "sounding.h"

/* --------------------------------------------------------------------------
 *                            Private Interface Types
 * -------------------------------------------------------------------------- */

typedef struct _mapsheet *MapSheet;

/* --------------------------------------------------------------------------
 *                            Public Interface Types
 * -------------------------------------------------------------------------- */

/* Possible contents of a MapSheet (may contain more than one type) */
typedef enum {
	MAP_DEPTH_ESTIMATE = 1,		/* Estimated Depth (m) */
	MAP_DEPTH_UNCERTAINTY = 2,	/* Estimated Depth Uncertainty (m) */
	MAP_NODE_HITS = 4,			/* Number of points incorporated per node */
	MAP_BACKSCATTER	= 8,		/* Estimated Backscatter (dB) */
	MAP_DEPTH_SDEV = 16			/* Sample S.Dev from depths (m) */,
	MAP_DEPTH_HYPOTHESES = 32,	/* Number of potential depths estimated (int) */
	MAP_HYPOTHESIS_STRENGTH = 64/* Strength (evidence) for a hypothesis (unit) */
} MapSheetElem;

/* A data buffer to contain as many of the elements of a MapSheet as are available */
typedef struct {
	f32	*depth;				/* Elements here follow MapSheetElem above */
	f32	*uncrt;
	u32	*nhits;
	u16	*backscatter;
	f32	*sdev;
	u8	*nhypo;
	f32	*hypstr;
} MapDataBuffer;

/* Types of data which can be stored in elements of the map-sheet */
typedef enum {
	MAP_DATA_U8,
	MAP_DATA_U16,
	MAP_DATA_U32,
	MAP_DATA_F32,
	MAP_DATA_F64,
	MAP_DATA_ERROR	/* When nothing else fits */
} MapSheetDataType;

typedef enum {
	MAP_DEPTH_NONE = 0,		/* No depths being computed */
	MAP_DEPTH_NODAL,		/* Compute depths based on nodal estimate alg */
	MAP_DEPTH_BINED_MEAN,	/* Compute depths based on bined mean alg */
	MAP_DEPTH_BINED_MEDIAN,	/* Compute depths based on bined median alg */
	MAP_DEPTH_HYPERCUBE		/* Compute depths using the HyperCUBE super-grid */
} MapSheetDepthAlg;

typedef enum {
	MAP_CACHE_NONE = 0,		/* No caching currently being done */
	MAP_CACHE_DEPTH,		/* Caching of depths */
	MAP_CACHE_UNCERTAINTY,	/* Caching of uncertainties (implies HyperCUBE) */
	MAP_CACHE_HYPOTHESES,	/* Caching of hypothesis count (implies HyperCUBE) */
	MAP_CACHE_STDDEV,		/* Caching of standard deviations */
	MAP_CACHE_BACKSCATTER,	/* Caching of backscatter */
	MAP_CACHE_HITS,			/* Caching of hits */
	MAP_CACHE_HYPOSTRENGTH	/* Caching of hypothesis strength (HyperCUBE only) */
} MapCache;

typedef struct {
	f64	west;
	f64	south;
	f64	east;
	f64 north;
} MapBounds;				/* Bounding box for a mapsheet or sub-set */

/* AOI == Area of Interest.  When an analysis of a HyperCUBE depth estimate
 * sheet is done, the algorithm returns one of these for each area identified
 * for further investigation.
 */

typedef struct _aoi {
	f64		x0, y0,			/* Location of AOI in projected coordinates (m) */
			width, height;	/* Width/Height of AOI (m) */
	
	f32		shoalest_depth;	/* Shoalest depth in area (m) */
	
	f64		x_focus,		/* Location of worst point in AOI (projected */
			y_focus;		/* coordinates (m)) */
	u32		nhyp_focus;		/* Number of hypotheses at focus point */
	f32		hypstr_focus;	/* Hypothesis strength at focus point */
	f32		depth_focus;	/* Depth (m) at focus point */
	
	f32		mean_depth,		/* Mean depth over whole area (m) */
			mean_unct,		/* Mean variance over whole area (m^2) */
			mean_nhyp,		/* Mean number of hypotheses in area (unitless) */
			mean_hypstr,	/* Mean hypothesis strenght in area (unitless) */
			mean_angle;		/* Mean spike angle [local slope] (deg) */
	
	char	*aux_descript;	/* Text describing contents of aux_data, or NULL */
	f64		aux_data;		/* Catch-all 'anything else' as described above */

	struct _aoi *next;
} AOI;

/* Hypotheses == A grid of hypotheses from a HyperCUBE depth estimate sheet
 * represented for user interaction/display
 */

typedef struct _hypo {
	f64		z;				/* Depth associated with the hypothesis (m) */
	f64		ci;				/* CI associated with hypothesis (m) --- *which* CI depends
							 * on the parameter file, by default, 95%
							 */
	u32		n_snds;			/* Number of soundings associated with the hypotheis */
} Hypo;

typedef struct _hypo_array {
	f64		x, y;			/* Location of the node in projected coordiantes (m) */
	u32		n_hypos;		/* Number of hypotheses following in the array */
	s32		nominated;		/* Number of the nominated hypothesis, or -1 if none */
	Hypo	*array;			/* Array of hypotheses associated with the node */
} HypoArray;

typedef struct _hypotheses {
	f64		west, south;	/* Location of the reference point for the grid (m) */
	u32		cols, rows;		/* Size of the grid in nodes */
	f64		width, height;	/* Size of the grid in physical space (m) */
	HypoArray **grid;		/* 2D array of hypotheses, arranged in row major order,
							 * but organised from south to north */
} Hypotheses;

/* --------------------------------------------------------------------------
 *                            Access Methods
 * -------------------------------------------------------------------------- */

extern Projection mapsheet_get_projection(MapSheet sheet);
extern void mapsheet_get_center(MapSheet sheet, f64p east, f64p north);
extern u32 mapsheet_get_width(MapSheet sheet);
extern u32 mapsheet_get_height(MapSheet sheet);
extern void mapsheet_get_width_height(MapSheet sheet, u32p width, u32p height);
extern void mapsheet_get_spacing(MapSheet sheet, f64p east, f64p north);
extern Bool mapsheet_contains(MapSheet sheet, MapSheetElem data);
extern u32 mapsheet_get_data_size(MapSheet sheet, MapSheetElem data);
extern MapSheetDataType mapsheet_get_data_type(MapSheet sheet, MapSheetElem data);

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

extern void mapsheet_get_tile_width_height(MapSheet sheet, u32p width,
										   u32p height);

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

extern void mapsheet_set_caching(MapSheet sheet, MapCache cache);

/* Routine:	mapsheet_get_caching
 * Purpose:	Return the type of data being cached
 * Inputs:	sheet	MapSheet to work from
 * Outputs:	Returns a MapCache enum to indicate the type of data being
 *			cached.
 * Comment:	This calls through to the HyperCUBE module, currently the only
 *			estimation module capable of caching.  If it isn't being used in
 *			the mapsheet, MAP_CACHE_NONE is returned.
 */

extern MapCache mapsheet_get_caching(MapSheet sheet);

/* Routine:	mapsheet_get_invalid
 * Purpose:	Return value used to mark invalid data
 * Inputs:	type	MapSheetDataType to provide information for
 * Outputs:	*val	Set to invalid data value
 * Comment:	Note that *val must point to a value of the correct type for
 *			correct interpretation, and at least to one of the same size so
 *			that this doesn't stamp somewhere is shouldn't.
 */

extern void mapsheet_get_invalid(MapSheetDataType type, void *val);

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

extern Bool mapsheet_finalise(MapSheet sheet);

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

extern void mapsheet_minimise_memory(MapSheet sheet);

/* Routine:	mapsheet_get_data[_user]
 * Purpose:	Get data out of the MapSheet
 * Inputs:	sheet		Sheet on which to work
 *			data		Layer of data to extract
 *			*user_data	(if available), pointer for output buffer, or NULL
 * Outputs:	Pointer to data space allocated, or _user_data_ if it is set,
 *			or NULL on failure.
 * Comment:	This is the primary route to extracting one element of the data
 *			set from a MapSheet and returning it to user level code for further
 *			processing.  This checks whether the element requested is available,
 *			and if so extracts it from the appropriate layer and formats for
 *			return.  To allow the data elements to be different types, everything
 *			is passed back as a pointer to void, and users should call
 *			mapsheet_get_data_type() or mapsheet_get_data_size() as required to
 *			determine the base size for each node's/cell's output.
 *				The only difference between mapsheet_get_data() and the call to
 *			mapsheet_get_data_user() is that the latter allows the user to supply
 *			a buffer if they already have one, to avoid time spent in grabbing
 *			memory, and copying the data later.  If you pass NULL for the _user_data_
 *			pointer, then the calls are equivalent.
 */

extern void *mapsheet_get_data(MapSheet sheet, MapSheetElem data);
extern void *mapsheet_get_data_user(MapSheet sheet, MapSheetElem data,
									void *user_data);

/* Routine: mapsheet_release_databuffer
 * Purpose:	Release all memory currently associated with the MapDataBuffer
 * Inputs:	*buffer	Pointer to buffer on which to work
 * Outputs:	-
 * Comment:	Note that this does not de-allocate the buffer structure itself,
 *			should it be dynamically allocated.
 */

extern void mapsheet_release_databuffer(MapDataBuffer *buffer);

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

extern MapDataBuffer *mapsheet_get_all_data(MapSheet sheet);

extern MapSheetDepthAlg mapsheet_get_depth_alg(MapSheet sheet);

extern Bool mapsheet_add_soundings(MapSheet sheet, SoundingStream stream,
								   Sounding *snds, u32 nsnds, u32 *nused);

/* Routine:	mapsheet_dump_hypo
 * Purpose:	Dump GDP file of hypotheses (if available) to file
 * Inputs:	sheet	MapSheet to work with
 *			*name	Name of file to write
 *			*bnds	MapBounds structure to sub-set, or NULL if none.
 * Outputs:	True if write worked, or False.
 * Comment:	Note that this is designed almost entirely for debugging purposes,
 *			and in general should not be used by normal code.  This routine only
 *			works if there is a HyperCUBE surface attached to the mapsheet, and
 *			only writes GDP files (GeoZui3D), and not anything else.  Use with
 *			caution.  Better yet, don't use at all ...
 */

extern Bool mapsheet_dump_hypo(MapSheet sheet, char *name, MapBounds *bnds);

/* Routine: mapsheet_get_hypo_by_node
 * Purpose:	Extract all of the hypotheses for a single node, given the node's
 *			offset from the origin of the MapSheet
 * Inputs:	sheet		MapSheet structure to work with
 *			row, col	CubeNode to extract
 * Outputs:	Returns pointer to a HypoArray on success, otherwise NULL
 * Comment:	This extracts all of the hypotheses associated with a single node.
 *			Note that if there are no hypotheses, a valid return still occurs,
 *			but with the number of hypotheses set to zero.  This call only works
 *			if there is a HyperCUBE surface attached to the MapSheet.
 */

extern HypoArray *mapsheet_get_hypo_by_node(MapSheet sheet, u32 col, u32 row);

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

extern HypoArray *mapsheet_get_hypo_by_location(MapSheet sheet, f64 x, f64 y);

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

extern Bool mapsheet_nominate_hypo_by_node(MapSheet sheet, u32 col, u32 row, f32 depth);

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

extern Bool mapsheet_nominate_hypo_by_location(MapSheet sheet, f64 x, f64 y, f32 depth);

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

extern Bool mapsheet_unnominate_hypo_by_node(MapSheet sheet, u32 col, u32 row);

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

extern Bool mapsheet_unnominate_hypo_by_location(MapSheet sheet, f64 x, f64 y);

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

extern s32 mapsheet_match_hypothesis(f32 depth, HypoArray *array);

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

extern Hypotheses *mapsheet_get_hypo_area(MapSheet sheet, MapBounds *bnds);

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

extern Bool mapsheet_remove_hypo_by_node(MapSheet sheet, u32 col, u32 row, f32 depth);

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

extern Bool mapsheet_remove_hypo_by_location(MapSheet sheet, f64 x, f64 y, f32 depth);

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

extern AOI *mapsheet_analyse_sheet(MapSheet sheet, f32 context_size,
								f32 *depth, f32 *unct, f32 *hypstr, u8 *nhyp);

/* Routine:	mapsheet_release_aois
 * Purpose:	Release memory associated with an AOI list
 * Inputs:	*root	Head of the linked list to free
 * Outputs:	-
 * Comment:	-
 */

extern void mapsheet_release_aois(AOI *root);

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

extern Bool mapsheet_count_hits(MapSheet sheet, SoundingStream stream,
								Sounding *snds, u32 nsnds);

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

extern Bool mapsheet_limit_bin_size(MapSheet sheet, u32 bin_limit);

/* --------------------------------------------------------------------------
 *                            Convenience Methods
 * -------------------------------------------------------------------------- */

extern void mapsheet_get_bl(MapSheet sheet, f64p x, f64p y);
extern void mapsheet_get_br(MapSheet sheet, f64p x, f64p y);
extern void mapsheet_get_tl(MapSheet sheet, f64p x, f64p y);
extern void mapsheet_get_tr(MapSheet sheet, f64p x, f64p y);
extern f64 mapsheet_get_proj_width(MapSheet sheet);
extern f64 mapsheet_get_proj_height(MapSheet sheet);
extern void mapsheet_get_proj_width_height(MapSheet sheet,
											f64p width, f64p height);

/* --------------------------------------------------------------------------
 *                          Construction/Destruction
 * -------------------------------------------------------------------------- */

/* V2 call interface.  The mapsheet file storage structure changed with V2 of
 * the file format so that the mapsheet is now a directory containing a number
 * of different files to make up the composite.  This was prompted particularly
 * by the implementation of the HyperCUBE algorithm to allow really big
 * mapsheets to be handled in small-ish controlled chunks.  Keeping these all
 * together is going to be difficult unless they have some structure, hence the
 * directory organisation.
 *     However, this means that we now have to have a backing store in place
 * with each mapsheet, even though it may not use it directly (e.g., if it
 * doesn't have a HyperCUBE element).  This can be specified directly by the
 * user with this call-set, or by the code using those below.  The advantage
 * of specifying the location directly is that the code doesn't have to clone
 * everything when the mapsheet is written (i.e., from a temporary directory
 * to the final output location), which makes the process significantly faster.
 */

extern MapSheet mapsheet_new_direct_backed(
					Projection projection, u32 width, u32 height,
					f64 east_sz, f64 north_sz, f64 west, f64 north,
					char *backing_store);

extern MapSheet mapsheet_new_by_center_backed(
					Projection projection, f64 width, f64 height,
					f64 east_sz, f64 north_sz, f64 longitude, f64 latitude,
					char *backing_store);

extern MapSheet mapsheet_new_by_proj_center_backed(
					Projection projection, f64 width, f64 height,
					f64 east_sz, f64 north_sz, f64 easting, f64 northing,
					char *backing_store);

extern MapSheet mapsheet_new_by_bounds_backed(
					Projection projection, f64 east_sz, f64 north_sz,
					f64 left_lon, f64 bottom_lat, f64 right_lon, f64 top_lat,
					char *backing_store);

extern MapSheet mapsheet_new_by_proj_bounds_backed(
					Projection projection, f64 east_sz, f64 north_sz,
					f64 left_est, f64 bottom_nth, f64 right_est, f64 top_nth,
					char *backing_store);

/* Original call interface: no user-specified backing store, so the code
 * generates a temporary name using LIBCCOMTMP as a directory location.
 */

extern MapSheet mapsheet_new_direct(
					Projection projection, u32 width, u32 height,
					f64 east_sz, f64 north_sz, f64 west, f64 north);

extern MapSheet mapsheet_new_by_center(
							Projection projection,
							f64 width, f64 height,
							f64 east_sz, f64 north_sz,
							f64 longitude, f64 lattitude);

extern MapSheet mapsheet_new_by_proj_center(
							Projection projection,
							f64 width, f64 height,
							f64 east_sz, f64 north_sz,
							f64 easting, f64 northing);

extern MapSheet mapsheet_new_by_bounds(
							Projection projection,
							f64 east_sz, f64 north_sz,
							f64 left_lon, f64 bottom_lat,
							f64 right_lon, f64 top_lat);

extern MapSheet mapsheet_new_by_proj_bounds(
							Projection projection,
							f64 east_sz, f64 north_sz,
							f64 left_est, f64 bottom_nth,
							f64 right_est, f64 top_nth);

#include "omg.h"

extern MapSheet mapsheet_new_from_omg(OMGRaster raster);
extern MapSheet mapsheet_new_from_omg_backed(OMGRaster raster,
											 char *backing_store);

/* Routine:	mapsheet_new_from_ascii
 * Purpose:	Read an ASCII description of a mapsheet, and construct the base
 * Inputs:	*name	Name of the file to read and parse
 * Outputs:	Pointer to MapSheet structure if successful, otherwise NULL
 * Comment:	This reads a mapsheet description as specified in mapsheet_par.y,
 *			and more usefully in 'Mapsheet File Format Description V1.0'.
 */

extern MapSheet mapsheet_new_from_ascii(char *name);

extern Bool mapsheet_add_depth_surface(MapSheet sheet, MapSheetDepthAlg alg,
									   ErrModIHOOrder order);

extern void mapsheet_delete_depth_surface(MapSheet sheet);
extern Bool mapsheet_add_hits(MapSheet sheet);
extern void mapsheet_zero_hits(MapSheet sheet);
extern void mapsheet_delete_hits(MapSheet sheet);


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

extern Bool mapsheet_add_depth_fixunct(MapSheet sheet, MapSheetDepthAlg alg,
									   ErrModIHOOrder order,
									   f32 *data, f32 unct, Bool unct_pcnt,
									   u8 *mask, u32 cols, u32 rows);

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

extern Bool mapsheet_add_depth_unct(MapSheet sheet, MapSheetDepthAlg alg,
							 		ErrModIHOOrder order,
							 		f32 *data, f32 *unct, u8 *mask,
							 		u32 cols, u32 rows);

extern void mapsheet_release(MapSheet sheet);
extern Bool mapsheet_save_sheet_v1(const MapSheet sheet, char *name);
extern Bool mapsheet_save_sheet_v2(const MapSheet sheet, char *dirname);
extern Bool mapsheet_save_sheet(const MapSheet sheet, char *name);
extern MapSheet mapsheet_load_sheet(char *name);

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

extern Bool mapsheet_make_summary(MapSheet sheet, char *dir);

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

#include "params.h"

extern Bool mapsheet_execute_params(ParList *list);

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

extern Bool mapsheet_reset_params(MapSheet sheet);

#ifdef __cplusplus
}
#endif

#endif
