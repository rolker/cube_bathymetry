/*
 * $Id: mapsheet_private.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:00  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.6  2001/12/07 21:15:36  brc
 * Archived the V1.0/2.0.0 mapsheet header structure, and then modified to
 * set the bounds and other georeferencing parameters to be f64 rather than
 * f32.  This is required to ensure that we have sufficient dynamic range and
 * precision to specify any size of node spacing in UTM coordinates.  In some
 * cases, 6 d.p. of mantissa is insufficient, with quiet underflow resulting
 * in bounds of sheets being rounded inappropriately.
 *
 * Revision 1.5  2001/09/23 17:54:23  brc
 * Added type for HyperCUBE and its parameters, and integrated into the main
 * MapSheet type.  Added prototype for directory construction code.
 *
 * Revision 1.4  2001/05/14 20:53:30  brc
 * Added holder for IHO survey order intended for the survey (used to determine
 * maximum allowable uncertainty for propagation of soundings).
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
 * File:	mapsheet_private.h
 * Purpose:	Private types used within the mapsheet module
 * Date:	07 July 2000
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

#ifndef __MAPSHEET_PRIVATE_H__
#define __MAPSHEET_PRIVATE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "projection.h"
#include "mapsheet.h"
#include "errmod.h"

/* A node in the surface estimation grid. */
typedef struct _depth_node DepthNode;

/* The HyperCUBE super-grid */
typedef struct _hyper_cube HyperCube;

/* A node in the binned estimation grid (common to all binned estimators) */
typedef struct _bin_node {
	f32p	bin;	/* Pointer to the data in the bin */
	u32		space;	/* Total amount of space available in the bin */
	u32		hits;	/* Number of entries in the bin */
} BinNode;

/* Private parameters for the access methods */
typedef struct _private_param {
	void	*hcube_param;
	void	*depth_param;
	void	*bin_param;
	void	*hits_param;
	void	*refl_param;
} MapParam;

/* A map-sheet header information structure, encapsulates all information
 * required to identify how to access and interpret the map-sheet.  Note that
 * this information is the core of what is saved to file as header information.
 */

/* This is an archive version for version 1 and 2.0.0 of mapsheets, which used
 * f32 for west/north bounds.  This is inadequate on large areas where the
 * 6 d.p. of floats if unsuitable for UTM coordinates.  This structure has to
 * be retained so that we can still load older mapsheets.
 */
typedef struct _private_hdr_v1_2 {
	Projection	projection;			/* Projection info from projection.h */
	f64			easting, northing;	/* Projected map-sheet center location */
	u32			n_east, 			/* Size of map-sheet in pixels */
				n_north;			/* (N.B. always 2n+1) */
	f64			east_sz,			/* Size of a node spacing in projected */
				north_sz;			/* coordinates at center of map-sheet. */
	f32			w_bound,			/* West/North edge of map-sheet, projected */
				n_bound;
	MapSheetDepthAlg	depth;		/* How the depths are computed */
	ErrModIHOOrder		order;		/* Order of survey under way for depths */
} MapHdr_V1_2;

typedef struct _private_hdr {
	Projection	projection;			/* Projection info from projection.h */
	f64			easting, northing;	/* Projected map-sheet center location */
	u32			n_east, 			/* Size of map-sheet in pixels */
				n_north;			/* (N.B. always 2n+1) */
	f64			east_sz,			/* Size of a node spacing in projected */
				north_sz;			/* coordinates at center of map-sheet. */
	f64			w_bound,			/* West/North edge of map-sheet, projected */
				n_bound;
	MapSheetDepthAlg	depth;		/* How the depths are computed */
	ErrModIHOOrder		order;		/* Order of survey under way for depths */
} MapHdr;

/* A map-sheet data structure.  Essentially a list of pointers which can be
 * allocated according to what the map-sheet has been configured to contain.
 */

typedef struct _private_data {
	DepthNode	**depth_grid;	/* Depth estimation algorithm */
	HyperCube	*hcube_grid;	/* HyperCUBE grid estimation algorithm */
	BinNode		**bin_grid;		/* Binning algorithms for f32 parameters */
	u32			**hits_grid;	/* Count of hits per node */
	u16			**refl_grid;	/* Backscatter reflectivity grid */
} MapData;

/* Opaque type for mapsheet contents */
struct _mapsheet {
	MapHdr		*hdr;	/* Header information used by the surface estimator */
	MapData		*grid;	/* Pointer(s) to workspace(s) */
	MapParam	*param;	/* Private parameters used by the surface estimator */
	char		*backing_store;	/* Location of backing store for sheet */
	Bool		backing_temp;	/* Indicates whether backing store is temporary
								 * and hence should be removed when the sheet
								 * is released, or whether it is persistent and
								 * should be left alone.
								 */
} /* *MapSheet */;

extern Bool mapsheet_bin_depth(f32 depth, BinNode **grid, u32 row, u32 col,
							   u32 bufq, u32 maxbuf);
extern char *mapsheet_make_name(const char *dir, const char *file);

/* Routine:	mapsheet_make_directory
 * Purpose:	Make a directory to specification, including path if required
 * Inputs:	*dir	Directory name to construct
 * Outputs:	True if made sucessfully, otherwise False
 * Comment:	This parses the passed string, testing each node as it does, until
 *			it either hits a failure, or encounters the first node that does
 *			not exist.  It then parses the remainder of the string, making
 *			directories as it goes (similar for mkdir -p).
 */

extern Bool mapsheet_make_directory(char *dir);

#ifdef __cplusplus
}
#endif

#endif
