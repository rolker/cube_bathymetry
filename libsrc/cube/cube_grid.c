/* $Id: cube_grid.c 17 2004-02-24 00:02:30Z brc $
 * $Log$
 * Revision 1.2  2004/02/24 00:02:30  brc
 * Fixed a bug with computation of the pseduo-posterior prob. in
 * cube_node_extract_post_depth_uncrt().  The code was using the
 * variance of the hypotheses it was testing to do the computation,
 * rather than the variance of the guide depth.  This leads to the
 * hypotheses being compared to a shifting baseline, and the values
 * don't make a lot of sense.  The new code allows the user to pass
 * in a variance from the outside (presumably from the guide node),
 * and computes the simplified log posterior using it (actually making
 * the computation significantly simpler by elimination of a log()
 * evaluation in the inner loop).  Modifications to the cube_node.h
 * declaration, cube_node.c definition and code, and the call in
 * cube_grid.c (no other calls found).
 *
 * Revision 1.1.1.1  2003/02/03 20:18:42  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2.4.2  2003/01/31 21:51:15  dneville
 * (BRC)  Added code for nomination, reset of nomination, extraction and
 * removal of hypotheses for HyperCUBE surfaces.
 *
 * Revision 1.2.4.1  2003/01/28 14:29:44  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.6.2.3  2002/12/15 01:20:48  brc
 * Modified to ensure the having a NULL median filter queue isn't interpreted as
 * 'no data has ever hit this node' --- this now happens when the queue is flushed
 * before the data is extracted (which didn't use to happen).  Also modified to avoid
 * initialisation hypotheses in general, since they're only really useful if you have
 * pretty good initialisation data (otherwise they are generally overwhelmed
 * immediately by the data, but can show up with some disambiguation modes
 * in a rather unexpected manner).  Extended maximum spread of data to 99%
 * confidence rather than 95% in order to give marginally sparse data more of a
 * chance.  Finally, fixed a couple of no-brainer bugs with use of dx instead
 * of dy, and not checking that a data point was there before attempting to write
 * something to the debug initialisation.
 *
 * Revision 1.6.2.2  2002/08/15 18:08:19  brc
 * Updates to make the windows port compile and operate on Unix, and to make
 * sure that blendsurfaces is added to the project.
 *
 * Revision 1.6.2.1  2002/07/14 02:20:46  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.6  2002/06/16 02:31:54  brc
 * Added code to make sure that the predicted increase in vertical error due
 * to interpolation via the predicted depth surface is set to zero if the
 * interpolation code does not actually do any interpolation.  This means that
 * the call sequence is simpler --- the answer is right whether the interpolation
 * can be done or not, and the caller doesn't have to check or care.  Also
 * added a check in cube_grid_insert_data() to make sure that every data point
 * reaches at least one node spacing away.  This is a retrograde step, in a
 * sense, since it was this assumption that caused a significant number of
 * problems in the early Snow Passage work (i.e., due to over-spreading in
 * regions of significant slope).  This might still be the case, but it is an
 * interim solution where data might not reach any nodes due to good horizontal
 * accuracy and low grid resolution (e.g., with the Nootka EM300 data).  This
 * otherwise leaves gaps in the swath coverage which look very strange ...
 *
 * Revision 1.5  2002/05/10 19:46:20  brc
 * Multiple modifications for robustness and slope corrections brought on by
 * the Snow Passage data verification project.  Modifications are:
 * 1. Updated the size estimation code so that the problem is now resolved
 * internally, and is more accurately reported.  That is, when it gives you
 * a size back, four tiles of this size *will* fit into the memory that the
 * user requested!
 * 2. Added the Hypothesis Strength Ratio surface component as something that
 * can be read back and/or cached.  This is meant to indicate the belief that
 * CUBE has in how well it resolved the disambiguation problem.  Patched around
 * the previous call interface so that the user sees the same API.
 * 3. Added facility to initialise a CubeGrid with a depth and uncertainty
 * surface, either defined as a fixed uncertainty, or as a percentage of depth,
 * or as another surface.  This upgrades the fixed uncertainty reinit code.  Note
 * that the code also adds a base hypothesis from the initialisation surface as
 * well as setting the predicted surface depth and uncertainty.
 * 4. Modified the data insertion code so that the decision on whether to add
 * the sounding to a node is taken at the node.  This is in keeping with the
 * new philosophy that the node should determine its own capture range, rather
 * than having this directed from above.  This allows, among other things, having
 * the node determine capture radius taking into account slope, predicted depth,
 * etc.
 * 5. Added facility to have the CubeGrid flushed as part of the finalisation
 * sequence.  This is a knock-on effect from (4) since limiting the capture range
 * can mean that the data density drops dramatically, and hence we can have
 * the problem of the median queues not being filled enough to start generating
 * data into the DLMs.  However, because of the way that the data structures are
 * cached on disc, this doesn't give us the same problem as it did with the
 * NODAL estimator, so you can still go back and re-assimilate more data if
 * required.
 * 6. Added code to interpolate surface points, and to compute the error associated
 * in making the slope correction for which this is normally used.
 *
 * Revision 1.4  2001/12/07 20:47:57  brc
 * Changed geo-referencing offsets from f32 to f64 to provide enough dynamic
 * range for small spacing grids in UTM coordinates.  Added a HypGrid structure
 * to hold all of the hypotheses available in a particular CubeGrid, organised
 * by node, and the access routines to allocate and free it.
 *
 * Revision 1.3  2001/09/20 18:00:04  brc
 * Added sheet size estimation in order to allow HyperCUBE to work out the size
 * of tiles.  Added call-backs for sizes and spacing of grids.  Modified to use
 * mapsheet.c for invalid data values to ensure that they are the same across the
 * board.  Modified to use buffered FILE streams for speed.  Changed bounds
 * checking to use signed values so that points off the left hand edge are caught.
 *
 * Revision 1.2  2001/08/21 02:25:19  brc
 * Removed extraneous variables which are not being used to avoid compiler
 * warning messages.
 *
 * Revision 1.1  2001/08/20 22:40:19  brc
 * Added CUBE algorithm as a callable entity, rather than being tied into
 * the mapsheet implementation.  This means that it is significantly easier
 * to use from other systems, and to implement in different ways (e.g., node-
 * wise or grid-wise) as might be required.
 *
 *
 * File:	cube_grid.c
 * Purpose:	Implement grid of CUBE nodes functionality
 * Date:	31 July 2001
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2001.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include "stdtypes.h"
#include "error.h"
#include "cube.h"
#include "cube_node.h"
#include "cube_private.h"
#include "cube_grid.h"
#include "sounding.h"		/* Interface to soundings structures */
#include "mapsheet.h"

static char *modname = "cube_grid";

/* For some reason, WIN32 has _isnan() defined, instead of isnan().  Wierd. */
#ifdef WIN32
#define isnan	_isnan
#endif

static char *modrev = "$Revision: 17 $";

static f32 cube_grid_nan = 0.f;

#define __HYP_DEBUG__
#undef __DEBUG__

typedef struct _cube_grid {
	u32		nx, ny;			/* Size of grid in nodes */
	f32		dx, dy;			/* Node spacing in meters */
	f32		**surface;		/* Pointer to extracted surface cache */
	CubeNode	*grid;		/* Pointer to underlying estimation nodes */
	CubeCache	cached;		/* What type of element is being cached */
} CGrid;

#define CONF_95PC				1.96	/* Scale for 95% CI on Unit Normal */
#define CONF_99PC				2.95	/* Scale for 99% CI on Unit Normal */

/* Routine:	cube_grid_estimate_size
 * Purpose:	Estimate sizes required for components on the CUBE algorithm
 * Inputs:	size		Available size for CubeGrid, in bytes
 *			exp_hyp		Expected number of Hypotheses at a node
 *			prob_use	Probability that a node will have *any* hypotheses
 *			param		CUBE parameters in force.
 * Outputs:	Returns the number of nodes that can be accommodated in a square
 *			grid of the given size, or 0 if there is insufficient memory.
 * Comment:	This code assumes that the tile is going to be square (although you
 *			can just square the number returned if required).  This takes into
 *			account the sizes of caches, fixed and variable memory and the
 *			expected number of nodes which are in use, and the number of
 *			hypotheses that each throws.  The result is a quadratic in the
 *			side length of the tile, which only has one admissable solution
 *			(i.e., with N>0).
 */

u32 cube_grid_estimate_size(u32 size, f32 exp_hyp, f32 prob_use, Cube param)
{
	f32	a, b, c, det;
	u32	n;
	
	a = sizeof(f32) + sizeof(CNode) +
			prob_use*cube_node_estimate_mem(exp_hyp, param);
	b = sizeof(f32*) + sizeof(CubeNode*);
	c = sizeof(CGrid);

#ifdef __DEBUG__
	error_msgv(modname, "debug: size equation coefficients: a = %f, b = %f,"
		" c = %f.\n", a, b, c);
	error_msgv(modname, "debug: mapsheet size = %d bytes.\n", size);
#endif

	det = b*b - 4*a*(c-size);

#ifdef __DEBUG__
	error_msgv(modname, "debug: determinant = %f.\n", det);
#endif

	if (det < 0) return(0); /* Quadratic ax^2+bx+(c-s)=0 has no solution */
	
	n = (u32)(floor(-b + sqrt(det))/(2*a));

#ifdef __DEBUG__
	error_msgv(modname, "debug: solution is N=%d nodes.\n", n);
#endif

	if ((n%2)==0 && n >= 2) --n;
	return(n);
}

/* Routine:	cube_grid_get_sizes
 * Purpose:	Return the size of the CubeGrid
 * Inputs:	grid	CubeGrid to work through
 * Outputs:	*width	Width of CubeGrid in nodes
 *			*height	Height of CubeGrid in nodes
 * Comment:	-
 */

void cube_grid_get_sizes(CubeGrid grid, u32 *width, u32 *height)
{
	*width = grid->nx; *height = grid->ny;
}

/* Routine:	cube_grid_get_spacings
 * Purpose:	Return the size of the CubeGrid
 * Inputs:	grid	CubeGrid to work through
 * Outputs:	*dx		Spacing in eastings
 *			*dy		Spacing in northings
 * Comment:	-
 */

void cube_grid_get_spacings(CubeGrid grid, f32 *dx, f32 *dy)
{
	*dx = grid->dx; *dy = grid->dy;
}

/* Routine:	cube_grid_release
 * Purpose:	Releases all dynamic memory associated with a CubeGrid
 * Inputs:	grid	CubeGrid to work through
 * Outputs:	-
 * Comment:	Note that the CubeGrid is itself dynamically allocated, and hence
 *			is also freed up during this call.
 */

void cube_grid_release(CubeGrid grid)
{
	u32		node, nnodes;
	
	if (grid == NULL) return;
	
	if (grid->surface != NULL) {
		if (grid->surface[0] != NULL) free(grid->surface[0]);
		free(grid->surface);
	}
	if (grid->grid != NULL) {
		if (grid->grid[0] != NULL) {
			/* The CNodes are composite structures, and hence we have to walk
			 * the list and free them all using cube_node_release().
			 */
			nnodes = grid->nx * grid->ny;
			for (node = 0; node < nnodes; ++node)
				cube_node_reinit(grid->grid[0] + node);
					/* N.B.: Reinitialisation removes all dynamic memory
					 * associated with the CNode, but *not the CNode itself*
					 * (in contrast with cube_node_release()).  This is
					 * essential since we allocated the memory for the CNode
					 * grid in this module to provide a uniform grid.
					 */
			free(grid->grid[0]);
		}
		free(grid->grid);
	}
	free(grid);
}

/* Routine:	cube_grid_invalidate_cache
 * Purpose:	Set all elements in the cache space to invalid values
 * Inputs:	g	CubeGrid to work with
 * Outputs:	-
 * Comment:	Note that this assumes that the surface has been initialised, if
 *			requied with the appropriate "no data" value, so that it doesn't
 *			need to reset any points so marked (i.e., because a node marked with
 *			no data has no data irrespective of what's being read back).  This
 *			makes switches of cache more speedy with sparse mapsheets.
 */

void cube_grid_invalidate_cache(CubeGrid g)
{
	u32		row, col;
	f32		no_data_value;

#ifdef WIN32
	if (!_isnan(cube_grid_nan)) cube_grid_nan = cube_make_nan();
#else
	if (!isnan(cube_grid_nan)) cube_grid_nan = cube_make_nan();
#endif

	mapsheet_get_invalid(MAP_DATA_F32, &no_data_value);
	
	for (row = 0; row < g->ny; ++row) {
		for (col = 0; col < g->nx; ++col) {
			if (g->surface[row][col] != no_data_value)
				g->surface[row][col] = cube_grid_nan;
		}
	}
}

/* Routine:	cube_grid_set_cache
 * Purpose:	Set the component of the grid to be cached internally
 * Inputs:	g	CubeGrid on which to work
 *			dat	Data type to cache
 * Outputs:	True if cache was reset, otherwise False
 * Comment:	-
 */

Bool cube_grid_set_cache(CubeGrid g, CubeCache dat)
{
	if (dat == g->cached) return(True);	/* No change, saves reset */
	switch(dat) {
		case CUBE_GRID_DEPTH:
		case CUBE_GRID_UNCT:
		case CUBE_GRID_NHYP:
		case CUBE_GRID_RATIO:
			g->cached = dat;
			cube_grid_invalidate_cache(g);
			break;
		default:
			error_msgv(modname, "error: cache data type unknown (%d).\n",
				(u32)dat);
			return(False);
			break;
	}
	return(True);
}

/* Routine:	cube_grid_new
 * Purpose:	Construct a new CubeGrid of the required size
 * Inputs:	nx, ny	Size of the grid in nodes
 *			dx, dy	Spacing of the grid nodes in meters
 *			cache	Indicate element to be cached in the internal space
 * Outputs:	Pointer to a CubeGrid, or NULL on failure
 * Comment:	This initialises all memory associated with a CubeGrid, and
 *			initialises the node structures appropriately.  Note that the grid
 *			nodes have no absolute geographic location, only offsets from the
 *			upper left corner of the grid.
 */

CubeGrid cube_grid_new(u32 nx, u32 ny, f32 dx, f32 dy, CubeCache cache)
{
	CubeGrid	rtn;
	u32			row, col;
	f32			no_data;
	
	if ((rtn = (CubeGrid)malloc(sizeof(CGrid))) == NULL) {
		error_msg(modname, "error: failed allocating memory for base data"
			" structure.\n");
		return(NULL);
	}
	rtn->nx = nx; rtn->ny = ny;
	rtn->dx = dx; rtn->dy = dy;
	rtn->cached = cache;
	
	/* Allocate surface for cached results, then initialise to NaN */
	if ((rtn->surface = (f32**)malloc(sizeof(f32*)*ny)) == NULL ||
		(rtn->surface[0] = (f32*)malloc(sizeof(f32)*nx*ny)) == NULL) {
		error_msgv(modname, "error: failed allocating memory for the cache"
			" depth surface (%dx%d nodes).\n", nx, ny);
		cube_grid_release(rtn);
		return(NULL);
	}
	for (row = 1; row < ny; ++row) rtn->surface[row] = rtn->surface[row-1]+nx;
	/* On first setup, all of the nodes will have no hypotheses, and hence the
	 * first read-back would indicate 'no-data' irrespective of what we read
	 * back.  Therefore, instead of invalidating the grid, we set it to no-data,
	 * and if anything is modified before it is read back, the surface will be
	 * reset appropriately by the reset routine.
	 */
	mapsheet_get_invalid(MAP_DATA_F32, &no_data);
	for (row = 0; row < ny; ++row)
		for (col = 0; col < nx; ++col)
			rtn->surface[row][col] = no_data;
	
	/* Construct grid of CNode structures, which hold the essential estimation
	 * environment for a particular node.
	 */
	if ((rtn->grid = (CNode**)malloc(sizeof(CNode*)*ny)) == NULL ||
		(rtn->grid[0] = (CNode*)malloc(sizeof(CNode)*nx*ny)) == NULL) {
		error_msgv(modname, "error: failed allocating memory for the estimate"
			" node grid (%dx%d nodes).\n", nx, ny);
		cube_grid_release(rtn);
		return(NULL);
	}
	for (row = 1; row < ny; ++row) rtn->grid[row] = rtn->grid[row-1]+nx;
	
	for (row = 0; row < ny; ++row) {
		for (col = 0; col < nx; ++col) {
			cube_node_init(rtn->grid[row]+col);
		}
	}

/*	Set any debug nodes that you want to be permanently active here.  Note that
 *	depending on the compile time flags that are set, you may get a great deal
 *  of information output on stdout/stderr, and into a number of files that are
 *	used to cache results in time sequence.  Of course, you should be sure that
 *	the node exists before setting it ... hence, you should also update the 'if'
 *  test statement if you change things.
 */
	if (ny > 15 && nx > 6)
		cube_node_start_debug(rtn->grid[15]+6);
	
	return(rtn);
}

/* Routine:	cube_grid_deserialise
 * Purpose:	De-serialise data for surface from input
 * Inputs:	fd		File descriptor to read from
 *			p		Cube parameter structure to work with
 * Outputs:	Pointer to reconstituted CubeGrid if successful, otherwise NULL.
 * Comment:	The CubeGrid is written as a header structure, which is then
 *			followed by CNode structures in row-major order.  Note that there
 *			is no way to check (and no way to guarantee) that the sizes and
 *			sampling rates here correspond to any higher-rate structures which
 *			are wrapped around this data (e.g., a mapsheet() like structure).
 *			However, it is unlikely that this routine would be called except as
 *			part of a larger structure (it's not much good without geographical
 *			and geodetic information), and hence it should not be an issue.
 */

CubeGrid cube_grid_deserialise(FILE *fd, Cube p)
{
	u32			pel, npels;
	CGrid		dummy;
	CubeGrid	rtn;
	
	if (fread(&dummy, sizeof(CGrid), 1, fd) != 1) {
		error_msg(modname, "error: failed reading base CubeGrid structure"
			" from file.\n");
		return(NULL);
	}
	if ((rtn = cube_grid_new(dummy.nx, dummy.ny, dummy.dx, dummy.dy,
													dummy.cached)) == NULL) {
		error_msgv(modname, "error: failed constructing CubeGrid from file"
			" (nx = %d, ny = %d, dx = %.1f m, dy = %.1f m.\n",
			dummy.nx, dummy.ny, dummy.dx, dummy.dy);
		return(NULL);
	}
		
	npels = rtn->nx * rtn->ny;
	for (pel = 0; pel < npels; ++pel) {
		if (!cube_node_deserialise(fd, rtn->grid[0] + pel, p)) {
			error_msgv(modname, "error: failed reading node %d.\n", pel);
			cube_grid_release(rtn);
			return(NULL);
		}
	}
	if (fread(rtn->surface[0], sizeof(f32), npels, fd) != npels) {
		error_msg(modname, "error: failed to read cache buffer.\n");
		cube_grid_release(rtn);
		return(NULL);
	}
	
	return(rtn);
}

/* Routine:	cube_grid_serialise
 * Purpose:	Serialise a CubeNode grid to file
 * Inputs:	fd		File descriptor to write on
 *			g		CubeGrid structure to work on
 *			p		Cube parameter structure to work with
 * Outputs:	True if the sheet was converted and flushed, otherwise False
 * Comment:	This code writes the CubeGrid structure, and then a sequence of
 *			CubeNode structures using a call-through to cube_node_serialise().
 */

Bool cube_grid_serialise(FILE *fd, CubeGrid g, Cube p)
{
	u32			pel, npels;
	
	if (fwrite(g, sizeof(CGrid), 1, fd) != 1) {
		error_msgv(modname, "error: failed writing CubeGrid header.\n");
		return(True);
	}
	
	npels = g->nx * g->ny;
	
	for (pel = 0; pel < npels; ++pel) {
		if (!cube_node_serialise(g->grid[0] + pel, p, fd)) {
			error_msgv(modname, "error: failed writing node %d.\n", pel);
			return(False);
		}
	}
	if(fwrite(g->surface[0], sizeof(f32), npels, fd) != npels) {
		error_msg(modname, "error: failed writing cache surface.\n");
		return(False);
	}
	
	return(True);
}

/* Routine:	cube_grid_get_context
 * Purpose:	Extract depth surface from nodal representation, with spatial
 *			context used to choose the best hypothesis
 * Inputs:	g		Pointer to CubeGrid structure to use
 *			*par	Raw parameter structure associated with grid
 * Outputs:	depth	Pointer to linear (row major) array of data to hold the
 *					output depth sequence (or NULL)
 *			unct	Pointer to linear (row major) array of data to hold the
 *					output uncertainty sequence (or NULL)
 *			ratio	Pointer to linear (row major) array of data to hold the
 *					output hypothesis strength ratio sequence (or NULL)
 * Comment:	This extracts the current best estimates from the CubeNode grid of
 *			structures.  This provides a current snap-shot, not necessarily the
 *			final results from the whole estimation (but then, you don't have to
 *			wait for all of the data to be available either!)
 *				Where there is no current depth hypothesis (i.e., there is no
 *			data), the depth is reported as DEFAULT_NULL_DEPTH.  Where there is
 *			only one hypothesis, that depth is reported.  Where there are
 *			multiple competing hypotheses, the algorithm searches the immediate
 *			vicinity in the grid looking for the closest node with only one
 *			depth hypothesis (and hence a presumably good node with no outliers
 *			or level shifts, etc.)  The hypothesis closest in the minimum
 *			distance sense according to the current estimate of mean and
 *			variance is chosen as the output.  Target search nodes outside the
 *			grid bounds are treated as nodes with no depth hypothesis.
 *				Note that this code can be used to extract depth, uncertainty,
 *			or both.  If _depth_ or _unct_ are non-NULL, then the data is
 *			extracted; otherwise the dataset is ignored.  An error is reported
 *			if both are NULL!
 */

static Bool cube_grid_get_context(CubeGrid g, Cube p,
								  f32p depth, f32p unct, f32p ratio)
{
	s32			row, col;
	s32			offset, dr, dc, target_r, target_c;
	f32			mean, var, dummy_ratio;
	CubeNode	closest;
#ifdef __HYP_DEBUG__
	u32			closest_r, closest_c;
#endif
	
	for (row = 0; row < (s32)g->ny; ++row) {
		for (col = 0; col < (s32)g->nx; ++col) {
			/* First, we check whether the node's cached value is valid.  This
			 * is true if we are only looking for either depth *or* uncertainty
			 * and the cached surface is the one we're looking for, and the
			 * node's cache value is not NaN.
			 */
			if (((depth != NULL && unct == NULL && ratio == NULL
					&& g->cached == CUBE_GRID_DEPTH) ||
				(unct != NULL && depth == NULL && ratio == NULL
					&& g->cached == CUBE_GRID_UNCT) ||
				(ratio != NULL && depth == NULL && unct == NULL
					&& g->cached == CUBE_GRID_RATIO)) &&
				!isnan(g->surface[row][col])) {
				/* Cache is caching the right thing, and its component in the
				 * cache surface is valid.
				 */
				if (depth != NULL)
					/* i.e., we're only asking for depth, and the cache is
					 * recording depth, and it's valid for this point.  Copy
					 * to the output, advancing the pointer.
					 */
					*depth++ = g->surface[row][col];
				else if (unct != NULL)
					/* i.e., we're only asking for uncertainty, ... (we can't
					 * have both, by the logic of the if test, so we don't need
					 * to check for anything more).
					 */
					*unct++ = g->surface[row][col];
				else
					/* i.e., we're only asking for hypothesis strength ratio.
					 */
					*ratio++ = g->surface[row][col];
			} else {
				/* We have to do this the hard way ... Note that we write into
				 * the current position in depth/unct, if they are non-null,
				 * and then update the cache and advance the pointers only once
				 * at the end of the switch clause.
				 */
				switch((u32)cube_node_extract_nhyp(g->grid[row]+col, p)) {
					case 0:
					case 1:
						/* Zero or one hypotheses mean that we don't have to
						 * look for context to disambiguate, because there is
						 * nothing to disambiguate!
						 */
						if (!cube_node_extract_depth_unct(g->grid[row]+col, p,
								depth, unct, ratio)) {
							error_msgv(modname, "failed extracting node "
								"(%d, %d).\n", row, col);
							return(False);
						}
						break;
					default:
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	error_msgv(modname, "debug: node (%d, %d) - multiple (%d) hypotheses:\n",
		row, col, cube_node_extract_nhyp(g->grid[row]+col, p));
	cube_node_dump_hypotheses(g->grid[row]+col);
}
#endif
						/* Multiple depth estimates are present.  We have to
						 * search the local grid in ever increasing circles
						 * (well, square annuli) until we find the first node
						 * with only one depth hypothesis.
						 */
						closest = NULL;
						for (offset = (s32)(p->min_context);
							 offset <= (s32)(p->max_context) &&
							 closest == NULL; ++offset) {
							target_r = row - offset;
							if (target_r >= 0 && target_r < (s32)g->ny)
								for (dc = -offset; dc <= offset; ++dc) {
									target_c = col + dc;
									if (target_c < 0 || target_c >= (s32)g->nx)
										continue;
									if (cube_node_extract_nhyp(g->grid[target_r]+target_c, p) == 1
										/*&& g->grid[target_r][target_c].queue != NULL*/) {
										closest = g->grid[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
									}
								}
							target_r = row + offset;
							if (target_r >= 0 && target_r < (s32)g->ny)
								for (dc = -offset; dc <= offset; ++dc) {
									target_c = col + dc;
									if (target_c < 0 || target_c >= (s32)g->nx)
										continue;
									if (cube_node_extract_nhyp(g->grid[target_r]+target_c, p) == 1
										/*&& g->grid[target_r][target_c].queue != NULL*/) {
										closest = g->grid[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
									}
								}
							target_c = col - offset;
							if (target_c >= 0 && target_c < (s32)g->nx)
								for (dr = -offset+1; dr <= offset-1; ++dr) {
									target_r = row + dr;
									if (target_r < 0 || target_r >= (s32)g->ny)
										continue;
									if (cube_node_extract_nhyp(g->grid[target_r]+target_c, p) == 1
										/*&& g->grid[target_r][target_c].queue != NULL*/) {
										closest = g->grid[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
									}
								}
							target_c = col + offset;
							if (target_c >= 0 && target_c < (s32)g->nx)
								for (dr = -offset+1; dr <= offset-1; ++dr) {
									target_r = row + dr;
									if (target_r < 0 || target_r >= (s32)g->ny)
										continue;
									if (cube_node_extract_nhyp(g->grid[target_r]+target_c, p) == 1
										/*&& g->grid[target_r][target_c].queue != NULL*/) {
										closest = g->grid[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
									 }
								 }
						} /* End offset loop */
						
						if (closest == NULL) {
							/* Didn't match anything within the allowed search
							 * region, so we reconstruct using the basic node
							 * algorithm (typically maximum occurrence).
							 */
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	error_msgv(modname, "debug:  at (%d, %d), no matching closest single"
		" hypothesis node in range (+- %f nodes -> +- %f nodes).  Reverting to "
		"longest-standing algorithm.\n", row, col, p->min_context, p->max_context);
}
#endif
							if (!cube_node_extract_depth_unct(g->grid[row]+col,
									p, depth, unct, ratio)) {
								error_msgv(modname, "error: failed choosing a "
									"best hypothesis at node (%d,%d).\n",
									row, col);
								return(False);
							}
						} else {
							/* Search for the closest current hypothesis to the
							 * specified node's single hypothesis.
							 */
							if (!cube_node_extract_depth_unct(closest,
									p, &mean, &var, &dummy_ratio)) {
								error_msgv(modname, "failed extracting single "
									"hypothesis at (%d, %d).\n", row, col);
								return(False);
							}
							if (!cube_node_extract_closest_depth_unct(
									g->grid[row]+col, p, mean, var,
									depth, unct, ratio)) {
								error_msgv(modname, "failed extracting closest"
									" hypothesis at (%d, %d).\n", row, col);
								return(False);
							}
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	error_msgv(modname, "debug:  closest matching single hypothesis node at"
		"(%d, %d), z = %f m, v = %f m^2.\n", closest_r, closest_c,
		mean, var);
	error_msgv(modname, "debug:  best match: depth %f m, var %f m^2.\n",
		depth!=NULL ? *depth : 0.0f, unct != NULL ? *unct : 0.0f);
}
#endif
						}
				}	/* End number of hypothesis switch statement */
				/* Update cache surface */
				if (g->cached == CUBE_GRID_DEPTH) {
					if (depth != NULL) g->surface[row][col] = *depth;
				} else if (g->cached == CUBE_GRID_UNCT) {
					if (unct != NULL) g->surface[row][col] = *unct;
				} else {
					if (ratio != NULL) g->surface[row][col] = *ratio;
				}
				if (depth != NULL) ++depth;
				if (unct != NULL) ++unct;
				if (ratio != NULL) ++ratio;
			}	/* End Cache Validity Check Else Clause */
		}	/* End Columns Loop */
	}	/* End Rows Loop */
	return(True);
}

/* Routine: cube_grid_get_context_depth
 * Purpose:	Extract just depth component of MapSheet using context specific
 *			(guide node selection) algorithm
 * Inputs:	g		CubeGrid structure on which to work
 *			*par	Private parameter pointer for algorithm
 * Outputs:	*depth	Linear (row major) array for output
 * Comment:	This is a convenience function that just constructs a call-through
 *			to the combined depth/uncertainty/ratio extractor.
 */

static Bool cube_grid_get_context_depth(CubeGrid g, Cube par, f32p depth)
{
	return(cube_grid_get_context(g, par, depth, NULL, NULL));
}

/* Routine: cube_grid_get_context_unct
 * Purpose:	Extract just uncertainty component of MapSheet using context
 *			specific (guide node selection) algorithm
 * Inputs:	g		CubeGrid structure on which to work
 *			*par	Private parameter pointer for algorithm
 * Outputs:	*unct	Linear (row major) array for output
 * Comment:	This is a convenience function that just constructs a call-through
 *			to the combined depth/uncertainty/ratio extractor.
 */

static Bool cube_grid_get_context_unct(CubeGrid g, Cube par, f32p unct)
{
	return(cube_grid_get_context(g, par, NULL, unct, NULL));
}

/* Routine: cube_grid_get_context_ratio
 * Purpose:	Extract just hypothesis strength component of MapSheet using context
 *			specific (guide node selection) algorithm
 * Inputs:	g		CubeGrid structure on which to work
 *			*par	Private parameter pointer for algorithm
 * Outputs:	*ratio	Linear (row major) array for output
 * Comment:	This is a convenience function that just constructs a call-through
 *			to the combined depth/uncertainty/ratio extractor.
 */

static Bool cube_grid_get_context_ratio(CubeGrid g, Cube par, f32p ratio)
{
	return(cube_grid_get_context(g, par, NULL, NULL, ratio));
}

/* Routine:	cube_grid_get_posterior
 * Purpose:	Extract depth/uncertainty/hypothesis strenght ratios from the nodes
 *			using a mixture of hypothesis probability (weight of evidence) and
 *			context driven likelihood function (closest hypothesis to a node
 *			considered reliable).
 * Inputs:	g		CubeGrid structure on which to work
 *			p		Cube parameter structure associated with CUBE algorithm
 * Outputs:	depth	Pointer to linear (row major) array of data to hold the
 *					output depth sequence (or NULL)
 *			unct	Pointer to linear (row major) array of data to hold the
 *					output uncertainty sequence (or NULL)
 *			ratio	Pointer to linear (row major) array of data to hold the
 *					output hypothesis strength ratio sequence (or NULL)
 * Comment:	This extracts the current best estimates from the DepthNode grid of
 *			structures.  This provides a current snap-shot, not necessarily the
 *			final results from the whole estimation (but then, you don't have to
 *			wait for all of the data to be available either!)
 *				Where there is no current depth hypothesis (i.e., there is no
 *			data), the depth is reported as DEFAULT_NULL_DEPTH.  Where there is
 *			only one hypothesis, that depth is reported.  Where there are
 *			multiple competing hypotheses, the algorithm searches the immediate
 *			vicinity in the grid looking for the closest node with only one
 *			depth hypothesis (and hence a presumably good node with no outliers
 *			or level shifts, etc.)  The hypothesis closest in the maximum
 *			probability sense according to the current estimate of mean and
 *			variance, and approximate hypothesis posterior density is chosen as
 *			the output.  Target search nodes outside the grid bounds are treated
 *			as nodes with no depth hypothesis.
 *				Note that this code can be used to extract depth, uncertainty,
 *			or both.  If _depth_ or _unct_ are non-NULL, then the data is
 *			extracted; otherwise the dataset is ignored.  An error is reported
 *			if both are NULL!
 */

static Bool cube_grid_get_posterior(CubeGrid g, Cube p,
									f32p depth, f32p unct, f32p ratio)
{
	s32			row, col;
	s32			offset, dr, dc, target_r, target_c;
	f32			mean, var, dummy_ratio;
	CubeNode	closest;
#ifdef __HYP_DEBUG__
	u32			closest_r, closest_c;
#endif

	for (row = 0; row < (s32)g->ny; ++row) {
		for (col = 0; col < (s32)g->nx; ++col) {
			/* First, we check whether the node's cached value is valid.  This
			 * is true if we are only looking for either depth *or* uncertainty
			 * and the cached surface is the one we're looking for, and the
			 * node's cache value is not NaN.
			 */
			if (((depth != NULL && unct == NULL && ratio == NULL &&
					g->cached == CUBE_GRID_DEPTH) ||
				(unct != NULL && depth == NULL && ratio == NULL &&
					g->cached == CUBE_GRID_UNCT) ||
				(ratio != NULL && depth == NULL && unct == NULL &&
					g->cached == CUBE_GRID_RATIO)) &&
				!isnan(g->surface[row][col])) {
				/* Cache is caching the right thing, and its component in the
				 * cache surface is valid.
				 */
				if (depth != NULL)
					/* i.e., we're only asking for depth, and the cache is
					 * recording depth, and it's valid for this point.  Copy
					 * to the output, advancing the pointer.
					 */
					*depth++ = g->surface[row][col];
				else if (unct != NULL)
					/* i.e., we're only asking for uncertainty, ... (we can't
					 * have both, by the logic of the if test, so we don't need
					 * to check for anything more).
					 */
					*unct++ = g->surface[row][col];
				else
					/* i.e., we're only asking for the depth hypothesis ratio
					 */
					*ratio++ = g->surface[row][col];
			} else {
				/* We have to do this the hard way ... Note that we write into
				 * the current position in depth/unct, if they are non-null,
				 * and then update the cache and advance the pointers only once
				 * at the end of the switch clause.
				 */
				switch((u32)cube_node_extract_nhyp(g->grid[row]+col, p)) {
					case 0:
					case 1:
						if (!cube_node_extract_depth_unct(g->grid[row]+col, p,
								depth, unct, ratio)) {
							error_msgv(modname, "failed extracting node"
								"(%d, %d).\n", row, col);
							return(False);
						}
						break;
					default:					
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	error_msgv(modname, "debug: node (%d, %d) - multiple (%d) hypotheses:\n",
		row, col, cube_node_extract_nhyp(g->grid[row]+col, p));
	cube_node_dump_hypotheses(g->grid[row]+col);
}
#endif
						/* Multiple depth estimates are present.  We have to
						 * search the local grid in ever increasing circles
						 * (well, square annuli) until we find the first node
						 * with only one depth hypothesis.
						 */
						closest = NULL;
						for (offset = (s32)(p->min_context);
							 offset <= (s32)(p->max_context) && closest == NULL;
							 ++offset) {
							target_r = row - offset;
							if (target_r >= 0 && target_r < (s32)g->ny)
								for (dc = -offset; dc <= offset; ++dc) {
									target_c = col + dc;
									if (target_c < 0 || target_c >= (s32)g->nx)
										continue;
									if (cube_node_extract_nhyp(g->grid[target_r]+target_c, p) == 1
										/*&& g->grid[target_r][target_c].queue != NULL*/) {
										closest = g->grid[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
									}
								}
							target_r = row + offset;
							if (target_r >= 0 && target_r < (s32)g->ny)
								for (dc = -offset; dc <= offset; ++dc) {
									target_c = col + dc;
									if (target_c < 0 || target_c >= (s32)g->nx)
										continue;
									if (cube_node_extract_nhyp(g->grid[target_r]+target_c, p) == 1
										/*&& g->grid[target_r][target_c].queue != NULL*/) {
										closest = g->grid[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
									}
								}
							target_c = col - offset;
							if (target_c >= 0 && target_c < (s32)g->nx)
								for (dr = -offset+1; dr <= offset-1; ++dr) {
									target_r = row + dr;
									if (target_r < 0 || target_r >= (s32)g->ny)
										continue;
									if (cube_node_extract_nhyp(g->grid[target_r]+target_c, p) == 1
										/*&& g->grid[target_r][target_c].queue != NULL*/) {
										closest = g->grid[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
									}
								}
							target_c = col + offset;
							if (target_c >= 0 && target_c < (s32)g->nx)
								for (dr = -offset+1; dr <= offset-1; ++dr) {
									target_r = row + dr;
									if (target_r < 0 || target_r >= (s32)g->ny)
										continue;
									if (cube_node_extract_nhyp(g->grid[target_r]+target_c, p) == 1
										/*&& g->grid[target_r][target_c].queue != NULL*/) {
										closest = g->grid[target_r] + target_c;
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	closest_r = target_r; closest_c = target_c;
}
#endif
									}
								}					
						}
						if (closest == NULL) {
							/* Didn't match anything within the allowed search
							 * region, so we reconstruct using the maximum
							 * posterior occurence.
							 */
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	error_msgv(modname, "debug:  no matching closest single hypothesis node in "
		"range (+- %d nodes).  Reverting to longest-standing algorithm.\n",
		row, col, p->max_context);
}
#endif
							if (!cube_node_extract_depth_unct(g->grid[row]+col, p,
										depth, unct, ratio)) {
									error_msgv(modname, "failed extracting best prior"
										" hypothesis at (%d, %d).\n", row, col);
									return(False);
							}
						} else {
							/* Search for the closest current hypothesis to the
							 * specified node's single hypothesis.
							 */
							if (!cube_node_extract_depth_unct(closest, p,
									&mean, &var, &dummy_ratio)) {
								error_msgv(modname, "failed extracting guide "
									"depth at node (%d, %d).\n", row, col);
								return(False);
							}
							if (!cube_node_extract_post_depth_unct(
									g->grid[row]+col, p, mean, var,
									depth, unct, ratio)) {
								error_msgv(modname, "failed extracting posterior "
									"weighted hypothesis at node (%d, %d).\n",
									row, col);
								return(False);
							}
#ifdef __HYP_DEBUG__
if (g->grid[row][col].write) {
	error_msgv(modname, "debug:  closest matching single hypothesis node at"
		"(%d, %d), z = %f m, v = %f m.\n", closest_r, closest_c,
		mean, var);
	error_msgv(modname, "debug:  best match: hyp. m = %f m, v = %f m^2.\n",
		*depth, *unct);
}
#endif
						}
				}	/* End of Switch Block */
				/* Update cache surface */
				if (g->cached == CUBE_GRID_DEPTH) {
					if (depth != NULL) g->surface[row][col] = *depth;
				} else if (g->cached == CUBE_GRID_UNCT) {
					if (unct != NULL) g->surface[row][col] = *unct;
				} else if (g->cached == CUBE_GRID_RATIO) {
					if (ratio != NULL) g->surface[row][col] = *ratio;
				}
				if (depth != NULL) ++depth;
				if (unct != NULL) ++unct;
				if (ratio != NULL) ++ratio;
			}	/* End of Cache Check Block */
		}	/* End of Col Loop */
	}	/* End of Row Loop */
	return(True);
}

/* Routine: cube_grid_get_posterior_depth
 * Purpose:	Extract just depth component of MapSheet using approximate posterior
 *			probability algorithm
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure for the algorithm
 * Outputs:	*depth	Linear (row major) array for output
 * Comment:	This is a convenience function that just constructs a call-through
 *			to the combined depth/uncertainty/ratio extractor.
 */

static Bool cube_grid_get_posterior_depth(CubeGrid g, Cube p, f32p depth)
{
	return(cube_grid_get_posterior(g, p, depth, NULL, NULL));
}

/* Routine: cube_grid_get_posterior_unct
 * Purpose:	Extract just uncertainty component of MapSheet using approximate
 *			posterior probability algorithm
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube structure for algorithm parameters
 * Outputs:	*unct	Linear (row major) array for output
 * Comment:	This is a convenience function that just constructs a call-through
 *			to the combined depth/uncertainty/ratio extractor.
 */

static Bool cube_grid_get_posterior_unct(CubeGrid g, Cube p, f32p unct)
{
	return(cube_grid_get_posterior(g, p, NULL, unct, NULL));
}

/* Routine: cube_grid_get_posterior_ratio
 * Purpose:	Extract just uncertainty component of MapSheet using approximate
 *			posterior probability algorithm
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube structure for algorithm parameters
 * Outputs:	*ratio	Linear (row major) array for output
 * Comment:	This is a convenience function that just constructs a call-through
 *			to the combined depth/uncertainty/ratio extractor.
 */

static Bool cube_grid_get_posterior_ratio(CubeGrid g, Cube p, f32p ratio)
{
	return(cube_grid_get_posterior(g, p, NULL, NULL, ratio));
}

/* Routine:	cube_grid_get_prior
 * Purpose:	Extract depth/uncertainty/hyp. strength surfaces using the
 *			approximate hypothesis probability as computed by number of samples
 * Inputs:	g		CubeGrid structure on which to work
 *			p		Cube parameter structure for algorithm
 * Outputs:	*depth	Linear (row major) array for output
 *			*unct	Linear (row major) array for output
 *			*ratio	Linear (row major) array for output
 *			True if extraction worked, otherwise False.
 * Comment:	This routine extracts either surface (set pointer to NULL to avoid
 *			extraction) for efficiency purposes (takes a while to work out the
 *			disambiguation, so we might as well extract both surfaces while we
 *			have them).
 */

static Bool cube_grid_get_prior(CubeGrid g, Cube p,
								f32p depth, f32p unct, f32p ratio)
{
	u32			row, col;
	
	for (row = 0; row < g->ny; ++row) {
		for (col = 0; col < g->nx; ++col) {
			/* First, we check whether the node's cached value is valid.  This
			 * is true if we are only looking for either depth *or* uncertainty
			 * and the cached surface is the one we're looking for, and the
			 * node's cache value is not NaN.
			 */
			if (((depth != NULL && unct == NULL && ratio == NULL
					&& g->cached == CUBE_GRID_DEPTH) ||
				(unct != NULL && depth == NULL && ratio == NULL
					&& g->cached == CUBE_GRID_UNCT) ||
				(ratio != NULL && depth == NULL && unct == NULL
					&& g->cached == CUBE_GRID_RATIO)) &&
				!isnan(g->surface[row][col])) {
				/* Cache is caching the right thing, and its component in the
				 * cache surface is valid.
				 */
				if (depth != NULL)
					/* i.e., we're only asking for depth, and the cache is
					 * recording depth, and it's valid for this point.  Copy
					 * to the output, advancing the pointer.
					 */
					*depth++ = g->surface[row][col];
				else if (unct != NULL)
					/* i.e., we're only asking for uncertainty, ... (we can't
					 * have both, by the logic of the if test, so we don't need
					 * to check for anything more).
					 */
					*unct++ = g->surface[row][col];
				else
					*ratio++ = g->surface[row][col];
			} else {
				/* We have to do this the hard way ... well, sort of semi-hard!
				 */
				if (!cube_node_extract_depth_unct(g->grid[row]+col, p,
													depth, unct, ratio)) {
					error_msgv(modname, "failed extracting node (%d, %d).\n",
						row, col);
					return(False);
				}
				/* Update cache surface */
				if (g->cached == CUBE_GRID_DEPTH) {
					if (depth != NULL) g->surface[row][col] = *depth;
				} else if (g->cached == CUBE_GRID_UNCT) {
					if (unct != NULL) g->surface[row][col] = *unct;
				} else if (g->cached == CUBE_GRID_RATIO) {
					if (ratio != NULL) g->surface[row][col] = *ratio;
				}
				if (depth != NULL) ++depth;
				if (unct != NULL) ++unct;
				if (ratio != NULL) ++ratio;
			}
		}
	}
	return(True);
}

/* Routine:	cube_grid_get_prior_depth
 * Purpose:	Extract depth surface from nodal representation
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure for algorithm
 * Outputs:	op	Pointer to linear (row major) array of data to hold the
 *				output sequence.
 * Comment:	This extracts the current best estimates from the CubeNode grid
 *			of structures.  In particular, it does not flush the node pre-filter
 *			input queues into the CUBE input sequence, and hence the
 *			estimates may not accurately reflect the current state of knowledge
 *			about the nodes.  Users requiring a current snapshot should call
 *			cube_finalise() before reading the grid with this call.
 *				In this context, `best' means `hypothesis with most samples',
 *			rather than through any other metric.  This may not be the `best'
 *			until add of the data is in, but it should give an idea of what's
 *			going on in the data structure at any point (particularly if it
 *			changes dramatically from sample to sample).
 */

static Bool cube_grid_get_prior_depth(CubeGrid g, Cube p, f32p op)
{
	return(cube_grid_get_prior(g, p, op, NULL, NULL));
}

/* Routine:	cube_grid_get_prior_unct
 * Purpose:	Extract uncertainty surface from nodal representation
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure for algorithm
 * Outputs:	op	Pointer to linear (row major) array of data to hold the
 *				output sequence.
 * Comment:	This simply extracts the current estimates from the CubeNode grid
 *			of structures.  In particular, it does not flush the node pre-filter
 *			input queues into the CUBE input sequence, and hence the
 *			estimates may not accurately reflect the current state of knowledge
 *			about the nodes.  Users requiring a current snapshot should call
 *			cube_finalise() before reading the grid with this call.
 *			  Note that the value returned is actually the predicted
 *			confidence interval in meters, rather than the true variance, based
 *			on the assumption that the sampling distribution is normal (i.e.,
 *			standard error x scale chosen from parameters list).
 */

static Bool cube_grid_get_prior_unct(CubeGrid g, Cube p, f32p op)
{
	return(cube_grid_get_prior(g, p, NULL, op, NULL));
}

/* Routine:	cube_grid_get_prior_ratio
 * Purpose:	Extract uncertainty surface from nodal representation
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure for algorithm
 * Outputs:	op	Pointer to linear (row major) array of data to hold the
 *				output sequence.
 * Comment:	This simply extracts the current estimates from the CubeNode grid
 *			of structures.  In particular, it does not flush the node pre-filter
 *			input queues into the CUBE input sequence, and hence the
 *			estimates may not accurately reflect the current state of knowledge
 *			about the nodes.  Users requiring a current snapshot should call
 *			cube_finalise() before reading the grid with this call.
 *			  Note that the value returned is actually the predicted
 *			confidence interval in meters, rather than the true variance, based
 *			on the assumption that the sampling distribution is normal (i.e.,
 *			standard error x scale chosen from parameters list).
 */

static Bool cube_grid_get_prior_ratio(CubeGrid g, Cube p, f32p op)
{
	return(cube_grid_get_prior(g, p, NULL, NULL, op));
}

/* Routine:	cube_grid_get_predsurf
 * Purpose:	Extract depth/uncertainty/hyp. strength surfaces using the
 *			predicted depth surface as a guide
 * Inputs:	g		CubeGrid structure on which to work
 *			p		Cube parameter structure for algorithm
 * Outputs:	*depth	Linear (row major) array for output
 *			*unct	Linear (row major) array for output
 *			*ratio	Linear (row major) array for output
 *			True if extraction worked, otherwise False.
 * Comment:	This routine extracts either surface (set pointer to NULL to avoid
 *			extraction) for efficiency purposes (takes a while to work out the
 *			disambiguation, so we might as well extract both surfaces while we
 *			have them).
 */

static Bool cube_grid_get_predsurf(CubeGrid g, Cube p,
								f32p depth, f32p unct, f32p ratio)
{
	u32			row, col;
	
	for (row = 0; row < g->ny; ++row) {
		for (col = 0; col < g->nx; ++col) {
			/* First, we check whether the node's cached value is valid.  This
			 * is true if we are only looking for either depth *or* uncertainty
			 * and the cached surface is the one we're looking for, and the
			 * node's cache value is not NaN.
			 */
			if (((depth != NULL && unct == NULL && ratio == NULL
					&& g->cached == CUBE_GRID_DEPTH) ||
				(unct != NULL && depth == NULL && ratio == NULL
					&& g->cached == CUBE_GRID_UNCT) ||
				(ratio != NULL && depth == NULL && unct == NULL
					&& g->cached == CUBE_GRID_RATIO)) &&
				!isnan(g->surface[row][col])) {
				/* Cache is caching the right thing, and its component in the
				 * cache surface is valid.
				 */
				if (depth != NULL)
					/* i.e., we're only asking for depth, and the cache is
					 * recording depth, and it's valid for this point.  Copy
					 * to the output, advancing the pointer.
					 */
					*depth++ = g->surface[row][col];
				else if (unct != NULL)
					/* i.e., we're only asking for uncertainty, ... (we can't
					 * have both, by the logic of the if test, so we don't need
					 * to check for anything more).
					 */
					*unct++ = g->surface[row][col];
				else
					*ratio++ = g->surface[row][col];
			} else {
				/* We have to do this the hard way ... well, sort of semi-hard!
				 */
				if (!cube_node_extract_closest_depth_unct(
						g->grid[row]+col, p,
						g->grid[row][col].pred_depth,
						g->grid[row][col].pred_var, depth, unct, ratio)) {
					error_msgv(modname, "failed extracting node (%d, %d).\n",
						row, col);
					return(False);
				}
				/* Update cache surface */
				if (g->cached == CUBE_GRID_DEPTH) {
					if (depth != NULL) g->surface[row][col] = *depth;
				} else if (g->cached == CUBE_GRID_UNCT) {
					if (unct != NULL) g->surface[row][col] = *unct;
				} else if (g->cached == CUBE_GRID_RATIO) {
					if (ratio != NULL) g->surface[row][col] = *ratio;
				}
				if (depth != NULL) ++depth;
				if (unct != NULL) ++unct;
				if (ratio != NULL) ++ratio;
			}
		}
	}
	return(True);
}

/* Routine:	cube_grid_get_predsurf_depth
 * Purpose:	Extract depth surface from nodal representation
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure for algorithm
 * Outputs:	op	Pointer to linear (row major) array of data to hold the
 *				output sequence.
 * Comment:	This extracts the current best estimates from the CubeNode grid
 *			of structures.  In particular, it does not flush the node pre-filter
 *			input queues into the CUBE input sequence, and hence the
 *			estimates may not accurately reflect the current state of knowledge
 *			about the nodes.  Users requiring a current snapshot should call
 *			cube_finalise() before reading the grid with this call.
 *				In this context, `best' means `hypothesis with most samples',
 *			rather than through any other metric.  This may not be the `best'
 *			until add of the data is in, but it should give an idea of what's
 *			going on in the data structure at any point (particularly if it
 *			changes dramatically from sample to sample).
 */

static Bool cube_grid_get_predsurf_depth(CubeGrid g, Cube p, f32p op)
{
	return(cube_grid_get_predsurf(g, p, op, NULL, NULL));
}

/* Routine:	cube_grid_get_predsurf_unct
 * Purpose:	Extract uncertainty surface from nodal representation
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure for algorithm
 * Outputs:	op	Pointer to linear (row major) array of data to hold the
 *				output sequence.
 * Comment:	This simply extracts the current estimates from the CubeNode grid
 *			of structures.  In particular, it does not flush the node pre-filter
 *			input queues into the CUBE input sequence, and hence the
 *			estimates may not accurately reflect the current state of knowledge
 *			about the nodes.  Users requiring a current snapshot should call
 *			cube_finalise() before reading the grid with this call.
 *			  Note that the value returned is actually the predicted
 *			confidence interval in meters, rather than the true variance, based
 *			on the assumption that the sampling distribution is normal (i.e.,
 *			standard error x scale chosen from parameters list).
 */

static Bool cube_grid_get_predsurf_unct(CubeGrid g, Cube p, f32p op)
{
	return(cube_grid_get_predsurf(g, p, NULL, op, NULL));
}

/* Routine:	cube_grid_get_predsurf_ratio
 * Purpose:	Extract uncertainty surface from nodal representation
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure for algorithm
 * Outputs:	op	Pointer to linear (row major) array of data to hold the
 *				output sequence.
 * Comment:	This simply extracts the current estimates from the CubeNode grid
 *			of structures.  In particular, it does not flush the node pre-filter
 *			input queues into the CUBE input sequence, and hence the
 *			estimates may not accurately reflect the current state of knowledge
 *			about the nodes.  Users requiring a current snapshot should call
 *			cube_finalise() before reading the grid with this call.
 *			  Note that the value returned is actually the predicted
 *			confidence interval in meters, rather than the true variance, based
 *			on the assumption that the sampling distribution is normal (i.e.,
 *			standard error x scale chosen from parameters list).
 */

static Bool cube_grid_get_predsurf_ratio(CubeGrid g, Cube p, f32p op)
{
	return(cube_grid_get_predsurf(g, p, NULL, NULL, op));
}

/* Routine:	cube_grid_get_depth
 * Purpose:	Extract depth using the selected multiple hypothesis disambiguation
 *			algorithm
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameters structure for algorithm
 * Outputs:	op	Pointer to linear output space
 * Comment:	This acts as a simple dispatching switch for the three extraction
 *			algorithms.
 */

Bool cube_grid_get_depth(CubeGrid g, Cube p, f32p op)
{
	Bool		rc;
	
	switch(p->mthd) {
		case CUBE_PRIOR:
			rc = cube_grid_get_prior_depth(g, p, op);
			break;
		case CUBE_LHOOD:
			rc = cube_grid_get_context_depth(g, p, op);
			break;
		case CUBE_POSTERIOR:
			rc = cube_grid_get_posterior_depth(g, p, op);
			break;
		case CUBE_PREDSURF:
			rc = cube_grid_get_predsurf_depth(g, p, op);
			break;
		default:
			error_msgv(modname, "error: unknown multi-hypothesis disambiguation"
				" algorithm (%d).\n", (u32)(p->mthd));
			rc = False;
			break;
	}
	return(rc);
}

/* Routine:	cube_grid_get_unct
 * Purpose:	Extract uncertianty using the selected multiple hypothesis
 *			disambiguation algorithm
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure for algorithm
 * Outputs:	op		Pointer to linear output space
 * Comment:	This acts as a simple dispatching switch for the three extraction
 *			algorithms.
 */

Bool cube_grid_get_unct(CubeGrid g, Cube p, f32p op)
{
	Bool		rc;
	
	switch(p->mthd) {
		case CUBE_PRIOR:
			rc = cube_grid_get_prior_unct(g, p, op);
			break;
		case CUBE_LHOOD:
			rc = cube_grid_get_context_unct(g, p, op);
			break;
		case CUBE_POSTERIOR:
			rc = cube_grid_get_posterior_unct(g, p, op);
			break;
		case CUBE_PREDSURF:
			rc = cube_grid_get_predsurf_unct(g, p, op);
			break;
		default:
			error_msgv(modname, "error: unknown multi-hypothesis disambiguation"
				" algorithm (%d).\n", (u32)(p->mthd));
			rc = False;
			break;
	}
	return(rc);
}

/* Routine:	cube_grid_get_ratio
 * Purpose:	Extract hypothesis strength ratio using the selected multiple
 *			hypothesis disambiguation algorithm
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure for algorithm
 * Outputs:	op		Pointer to linear output space
 * Comment:	This acts as a simple dispatching switch for the three extraction
 *			algorithms.
 */

Bool cube_grid_get_ratio(CubeGrid g, Cube p, f32p op)
{
	Bool		rc;
	
	switch(p->mthd) {
		case CUBE_PRIOR:
			rc = cube_grid_get_prior_ratio(g, p, op);
			break;
		case CUBE_LHOOD:
			rc = cube_grid_get_context_ratio(g, p, op);
			break;
		case CUBE_POSTERIOR:
			rc = cube_grid_get_posterior_ratio(g, p, op);
			break;
		case CUBE_PREDSURF:
			rc = cube_grid_get_predsurf_ratio(g, p, op);
			break;
		default:
			error_msgv(modname, "error: unknown multi-hypothesis disambiguation"
				" algorithm (%d).\n", (u32)(p->mthd));
			rc = False;
			break;
	}
	return(rc);
}

/* Routine:	cube_grid_get_depth_unct
 * Purpose:	Get both depth and uncertainty surface using the selected multi-
 *			hypothesis disambiguation algorithm
 * Inputs:	g		CubeGrid structure on which to work
 *			p		Cube parameter structure for algorithm
 * Outputs:	*depth	Linear (row major) array for depth output
 *			*unct	Linear (row major) array for uncertanty output
 * Comment:	This is basically a dispatch switch for the various algorithm
 *			implementations.  Note that it is faster (and sometimes very
 *			significantly faster) to use this if you are going to need both
 *			surfaces, since it only has to do the sorting/context search once
 *			per node, rather than twice.
 */

Bool cube_grid_get_depth_unct(CubeGrid g, Cube p, f32p depth, f32p unct)
{
	Bool		rc;
	
	switch(p->mthd) {
		case CUBE_PRIOR:
			rc = cube_grid_get_prior(g, p, depth, unct, NULL);
			break;
		case CUBE_LHOOD:
			rc = cube_grid_get_context(g, p, depth, unct, NULL);
			break;
		case CUBE_POSTERIOR:
			rc = cube_grid_get_posterior(g, p, depth, unct, NULL);
			break;
		case CUBE_PREDSURF:
			rc = cube_grid_get_predsurf(g, p, depth, unct, NULL);
			break;
		default:
			error_msgv(modname, "error: unknown multi-hypothesis disambiguation"
				" algorithm (%d).\n", (u32)(p->mthd));
			rc = False;
			break;
	}
	return(rc);
}

/* Routine:	cube_grid_get_depth_unct
 * Purpose:	Get both depth and uncertainty surface using the selected multi-
 *			hypothesis disambiguation algorithm
 * Inputs:	g		CubeGrid structure on which to work
 *			p		Cube parameter structure for algorithm
 * Outputs:	*depth	Linear (row major) array for depth output
 *			*unct	Linear (row major) array for uncertanty output
 *			*ratio	Linear (row major) array for hypothesis strength ratio o/p
 * Comment:	This is basically a dispatch switch for the various algorithm
 *			implementations.  Note that it is faster (and sometimes very
 *			significantly faster) to use this if you are going to need both
 *			surfaces, since it only has to do the sorting/context search once
 *			per node, rather than twice.
 */

Bool cube_grid_get_data(CubeGrid g, Cube p, f32p depth, f32p unct, f32p ratio)
{
	Bool		rc;
	
	switch(p->mthd) {
		case CUBE_PRIOR:
			rc = cube_grid_get_prior(g, p, depth, unct, ratio);
			break;
		case CUBE_LHOOD:
			rc = cube_grid_get_context(g, p, depth, unct, ratio);
			break;
		case CUBE_POSTERIOR:
			rc = cube_grid_get_posterior(g, p, depth, unct, ratio);
			break;
		case CUBE_PREDSURF:
			rc = cube_grid_get_predsurf(g, p, depth, unct, ratio);
			break;
		default:
			error_msgv(modname, "error: unknown multi-hypothesis disambiguation"
				" algorithm (%d).\n", (u32)(p->mthd));
			rc = False;
			break;
	}
	return(rc);
}

/* Routine:	cube_grid_get_nhyp
 * Purpose:	Extract surface containing number of hypotheses from nodes
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure
 * Outputs:	*h	Pointer to row major linear output workspace
 * Comment:	This essentially calls the cube_node_extract_nhyp() call for each
 *			node, with the addition that the cache is used and updated if it is
 *			currently caching numbers of hypotheses.
 */

void cube_grid_get_nhyp(CubeGrid g, Cube p, f32p h)
{
	u32		row, col;
	
	if (g->cached == CUBE_GRID_NHYP) {
		/* We're caching the number of hypotheses, so update if required */
		for (row = 0; row < g->ny; ++row) {
			for (col = 0; col < g->nx; ++col) {
				if (isnan(g->surface[row][col]))
					g->surface[row][col] =
							cube_node_extract_nhyp(g->grid[row]+col, p);
				*h++ = g->surface[row][col];
			}
		}
	} else {
		for (row = 0; row < g->ny; ++row) {
			for (col = 0; col < g->nx; ++col) {
				*h++ = (f32)cube_node_extract_nhyp(g->grid[row]+col, p);
			}
		}
	}			
}

/* Routine:	cube_grid_reinit
 * Purpose:	Initialises the CubeGrid
 * Inputs:	g	CubeGrid structure on which to work
 * Outputs:	-
 * Comment:	-
 */

void cube_grid_reinit(CubeGrid g)
{
	u32			npels, pel;
	
	npels = g->ny * g->nx;
	
	for (pel = 0; pel < npels; ++pel)
		cube_node_reinit(g->grid[0]+pel);
	cube_grid_invalidate_cache(g);
}

/* Routine:	cube_grid_initialise
 * Purpose:	Initialise a CubeGrid with given data and uncertainties
 * Inputs:	g			CubeGrid structure to work in
 *			*p			Cube parameter structure to work with
 *			*data		Depth data to use for initialisation
 *			unct		Uncertainty (see below for interpretation)
 *			unct_pcnt	Flag: True=>use unct as a percentage of depth, rather
 *						than fixed 1 s.d. uncertainty in meters
 *			*mask		Mask data (U8, 255 => do no update)
 *			row_stride	Length to offset *data and *mask after each row
 * Outputs:	True if initialisation was done, otherwise False
 * Comment:	This initialises each node in the CubeGrid with the appropriate
 *			data, using *data as the predicted depth, and *mask for indication
 *			of where updates should be allowed.  mask[][]==255 indicates that
 *			no update should be allowed; data[][] == INVALID indicates that
 *			no depth information is available, so no initial hypothesis is made
 *			in the node, and no slope corrections will be allowed.  This
 *			convention also allows us to specify particular depths, or to stop
 *			any particular node from being updated (e.g., if the user decides
 *			that a particular node is correct, or wants to choose a particular
 *			hypothesis, etc.)
 */

Bool cube_grid_initialise(CubeGrid g, Cube p, f32 *data, f32 unct,
						  Bool unct_pcnt, u8 *mask, u32 row_stride)
{
	u32		x, y;
	f32		invalid_data, scale, depth, var;

#ifdef __DEBUG__	
	error_msgv(modname, "info: initialising CubeGrid, data = %p mask = %p,"
		" %d rows, %d cols, unct = %.2f.\n",
		data, mask, g->ny, g->nx, unct);
#endif

	mapsheet_get_invalid(MAP_DATA_F32, &invalid_data);
	if (!isnan(cube_grid_nan)) cube_grid_nan = cube_make_nan();
	
	if (unct_pcnt)
		scale = (f32)((unct/100.0f)*(unct/100.0f)*(1.0/CONF_95PC)*(1.0/CONF_95PC));
	else
		scale = unct*unct;

	for (y = 0; y < g->ny; ++y) {
		for (x = 0; x < g->nx; ++x) {
			/* First, determine whether update should be allowed, and set the
			 * predicted depth appropriately.
			 */
			if (mask != NULL && mask[x] == 255) {
				/* Mask set to indicate that no update should take place.
				 * Consequently, we set the predicted depth to NaN.
				 */
				depth = cube_grid_nan;
			} else {
				/* Mask says that we should allow update, so we set to the
				 * supplied depth.  Note that this could be INVALID, meaning
				 * that no depth information is available, and slope corrections
				 * should not be applied.
				 */
				depth = data[x];
				if (isnan(data[x])) {
					error_msgv(modname, "error: initialisation data is NaN"
						" at (%d, %d) (data: %f).\n", y, x, data[x]);
					return(False);
				}
			}
			if (unct_pcnt)
				var = scale * depth * depth;
			else
				var = scale;
			cube_node_set_preddepth(g->grid[y]+x, depth, var);
				/* Note that we don't have to invalidate the cache for this,
				 * since currently the prediction depth isn't going to affect
				 * the reconstruction.
				 */
			/* Next, initialise a base hypothesis if the initialisation data is
			 * valid.  Otherwise, let the node work things out on its own.  Note that
			 * we only add a base hypothesis if the node is being allowed to be
			 * updated.  Otherwise, it just takes up space and time.  The logic is
			 * that if you are not allowing the node to be updated, then you already
			 * know what you want it to be (at least in this initialisation sense,
			 * although later modifications to 'freeze' nodes would be a different
			 * story, although possible by just setting the pred-depth to NaN).  Thus,
			 * it is pointless to have a Hypothesis whose only job is to make a
			 * continuous surface ... pretty peectures?  we don' need no steenkeeng
			 * pretty peectures!
			 */
			if (!isnan(depth) && data[x] != invalid_data) {
				if (!cube_node_add_null_hypothesis(g->grid[y]+x, data[x], var)) {
					error_msgv(modname, "error: failed to initialise node "
						"(%d, %d) hypo. at depth=%.1fm, var=%.1fm^2\n", y, x,
						data[x], var);
					return(False);
				}
				g->surface[y][x] = cube_grid_nan;
			}
		}
		data += row_stride;
		if (mask != NULL) mask += row_stride;
	}
	return(True);
}

/* Routine:	cube_grid_init_unct
 * Purpose:	Initialise a CubeGrid with given data and uncertainties
 * Inputs:	g			CubeGrid structure to work in
 *			*p			Cube parameter structure to work with
 *			*data		Depth data to use for initialisation
 *			*unct		Uncertainty (see below for interpretation)
 *			*mask		Mask data (U8, 255 => do no update)
 *			row_stride	Length to offset *data and *mask after each row
 * Outputs:	True if initialisation was done, otherwise False
 * Comment:	This initialises each node in the CubeGrid with the appropriate
 *			data, using *data as the predicted depth, and *mask for indication
 *			of where updates should be allowed.  mask[][]==255 indicates that
 *			no update should be allowed; data[][] == INVALID indicates that
 *			no depth information is available, so no initial hypothesis is made
 *			in the node, and no slope corrections will be allowed.  This
 *			convention also allows us to specify particular depths, or to stop
 *			any particular node from being updated (e.g., if the user decides
 *			that a particular node is correct, or wants to choose a particular
 *			hypothesis, etc.)
 */

Bool cube_grid_init_unct(CubeGrid g, Cube p, f32 *data, f32 *unct,
						 u8 *mask, u32 row_stride)
{
	u32		x, y;
	f32		invalid_data, depth, var;

#ifdef __DEBUG__
	error_msgv(modname, "info: initialising CubeGrid, data = %p mask = %p,"
		" %d rows, %d cols, unct = %p.\n",
		data, mask, g->ny, g->nx, unct);
#endif

	mapsheet_get_invalid(MAP_DATA_F32, &invalid_data);
	if (!isnan(cube_grid_nan)) cube_grid_nan = cube_make_nan();
	
	for (y = 0; y < g->ny; ++y) {
		for (x = 0; x < g->nx; ++x) {
			/* First, determine whether update should be allowed, and set the
			 * predicted depth appropriately.
			 */
			if (mask != NULL && mask[x] == 255) {
				/* Mask set to indicate that no update should take place.
				 * Consequently, we set the predicted depth to NaN.
				 */
				depth = cube_grid_nan;
				var = cube_grid_nan;
			} else {
				/* Mask says that we should allow update, so we set to the
				 * supplied depth.  Note that this could be INVALID, meaning
				 * that no depth information is available, and slope corrections
				 * should not be applied.
				 */
				depth = data[x];
				var = unct[x];
				if (isnan(data[x]) || isnan(unct[x])) {
					error_msgv(modname, "error: initialisation data is NaN"
						" at (%d, %d) (data: %f).\n", y, x, data[x]);
					return(False);
				}
				if (unct[x] < 0) {
					error_msgv(modname, "error: initialisation uncertainty"
						" is out of range at (%d, %d) (depth: %g m, var: %g m^2).\n",
						y, x, data[x], unct[x]);
					return(False);
				}
			}
			cube_node_set_preddepth(g->grid[y]+x, depth, var);
				/* Note that we don't have to invalidate the cache for this,
				 * since currently the prediction depth isn't going to affect
				 * the reconstruction.
				 */
			/* Next, initialise a base hypothesis if the initialisation data is
			 * valid.  Otherwise, let the node work things out on its own.  Note that
			 * we only add a base hypothesis if the node is being allowed to be
			 * updated.  Otherwise, it just takes up space and time.  The logic is
			 * that if you are not allowing the node to be updated, then you already
			 * know what you want it to be (at least in this initialisation sense,
			 * although later modifications to 'freeze' nodes would be a different
			 * store, although possible by just setting the pred-depth to NaN).  Thus,
			 * it is pointless to have a Hypothesis whose only job is to make a
			 * continuous surface ... pretty peectures?  we don' need no steenkeeng
			 * pretty peectures!
			 */
			if (!isnan(depth) && data[x] != invalid_data) {
				if (!cube_node_add_null_hypothesis(g->grid[y]+x, data[x], unct[x])) {
					error_msgv(modname, "error: failed to initialise node "
						"(%d, %d) hypo. at depth=%.1fm, var=%.1fm^2\n", y, x,
						data[x], var);
					return(False);
				}
				g->surface[y][x] = cube_grid_nan;
			}
		}
		data += row_stride;
		unct += row_stride;
		if (mask != NULL) mask += row_stride;
	}
	return(True);
}

/* Routine:	cube_grid_insert_depths
 * Purpose:	Add a sequence of depth estimates to the mapsheet
 * Inputs:	g		CubeGrid structure on which to work
 *			p		Cube algorithm parameters structure
 *			stream	SoundingStream which supplied the data
 *			*data	Pointer to the soundings which should be added
 *			nsnds	Number of soundings in this batch
 *			west	Easting of the most westerly node in the grid
 *			north	Northing of the most northerly node in the grid
 * Outputs:	True if data was added correctly, otherwise False
 *			*n_used set to number of points actually integrated into CubeGrid
 * Comment:	This code integrates the soundings presented into the current
 *			grid.  Note that in the newer version of the algorithm, soundings
 *			are queued in a median pre-filter before going to CUBE so there is
 *			a delay between presentation and readback effect.  Note that we
 *			assume at this point that the soundings presented have suitably
 *			defined error variances in depth and position associated with them,
 *			so that we don't have to try to re-generate them here.
 *				Remember that the grid doesn't have an absolute coordinate
 *			system, which is really a function of the higher order entity which
 *			would typically wrap the raw grid.  However, the soundings are
 *			linked to a particular projection system, and it is unreasonable
 *			for the code calling this to modify to local coordinates, then
 *			call here, then change everything back to global coordinates (since
 *			the caller of *that* routine is providing the soundings, and has a
 *			reasonable right to expect that they aren't going to be munged by
 *			subsidiary calls in the process of being integrated).  Hence, the
 *			caller needs to specify absolute bounds here, and we modify the
 *			node positions from the CubeGrid relative system accordingly.
 */

Bool cube_grid_insert_depths(CubeGrid g, Cube p, SoundingStream stream,
							 Sounding *data, u32 nsnds, f64 west, f64 north,
							 u32 *n_used)
{
	u32			snd, x, y;
	s32			min_x, max_x, min_y, max_y;
	f64			max_var_allowed, radius, node_x, node_y, ratio, max_radius,
				radius_sq, dist_sq;
	
#ifdef __DEBUG__
error_msgv("debug", "dist_scale = %lf, var_scale = %lf, nsnds = %d,"
					" iho_fixed = %lf m^2, iho_pcent = %lf\n",
			p->dist_scale, p->var_scale, nsnds, p->iho_fixed,
			p->iho_pcent);
error_msg(modname, "debug: ping start ...\n");
#endif

	if (!isnan(cube_grid_nan)) cube_grid_nan = cube_make_nan();

#ifdef __DEBUG__
error_msgv(modname, "debug: bounds west = %.1lf north = %.1lf\n", west, north);
#endif

	*n_used = 0;
	for (snd = 0; snd < nsnds; ++snd) {

#ifdef __DEBUG__
error_msgv(modname, "debug: z = %f r = %f dr = %f dz = %f (e,n)=(%lf,%lf)\n",
	data[snd].depth, data[snd].range, data[snd].dr, data[snd].dz,
	data[snd].east, data[snd].north);
#endif
		/* Determine IHO S-44 derived limits on maximum variance */
		max_var_allowed = (p->iho_fixed +
							p->iho_pcent*data[snd].depth*data[snd].depth)/
								(CONF_95PC * CONF_95PC); /* 95% -> 1 s.d. */
		ratio = max_var_allowed / data[snd].dz;

		if (ratio <= 2.0) ratio = 2.0;
			/* Ensure some spreading on point */
		
		max_radius = CONF_99PC * sqrt(data[snd].dr);

#ifdef __DEBUG__
error_msgv(modname, "debug: dist_scale = %lf ratio = %lf max_radius = %lf\n",
	p->dist_scale, ratio, max_radius);
error_msgv(modname, "debug: inv_dist_exp = %lf\n", p->inv_dist_exp);
#endif

		radius = p->dist_scale * pow(ratio - 1.0, p->inv_dist_exp)
					- max_radius;
		if (radius < 0.0) radius = p->dist_scale;
		if (radius > max_radius) radius = max_radius;
		if (radius < p->dist_scale) radius = p->dist_scale;

#ifdef __DEBUG__
error_msgv(modname, "debug: max_var = %lf, radius = %lf\n", max_var_allowed,
			radius);
#endif

		/* Determine coordinates of effect square.  This is designed to
		 * compute the largest region that the sounding can affect, and hence
		 * to make the insertion more efficient by only offering the sounding
		 * where it is likely to be used.
		 */
		min_x = (s32)(((data[snd].east - radius) - west)/g->dx);
		max_x = (s32)(((data[snd].east + radius) - west)/g->dx);
		min_y = (s32)((north - (data[snd].north + radius))/g->dy);
		max_y = (s32)((north - (data[snd].north - radius))/g->dy);
		/* Check that the sounding hits somewhere in the grid */
		if (max_x < 0 || min_x >= (s32)g->nx-1 || max_y < 0 || min_y >= (s32)g->ny-1) {
#ifdef __DEBUG__
			error_msgv(modname, "debug: point out of bounds llc=(%d, %d) "
				"urc=(%d, %d).\n", min_x, min_y, max_x, max_y);
#endif
			continue;
		}
		/* Clip to interior of current grid */
		min_x = (min_x < 0) ? 0 : min_x;
		max_x = (max_x >= (s32)g->nx) ? (s32)g->nx-1 : max_x;
		min_y = (min_y < 0) ? 0 : min_y;
		max_y = (max_y >= (s32)g->ny) ? (s32)g->ny-1 : max_y;

#ifdef __DEBUG__
		error_msgv(modname, "min_x = %d max_x = %d min_y = %d max_y = %d\n",
			min_x, max_x, min_y, max_y);
		error_msgv(modname, "touchdown (x,y) = (%lf, %lf) m\n", data[snd].east,
			data[snd].north);
#endif
		radius_sq = radius * radius;
			/* So we don't have to compute sqrt(dist_sq) ... at least here */
		for (y = min_y; y <= (u32)max_y; ++y) {
			for (x = min_x; x <= (u32)max_x; ++x) {
				node_x = west + x*g->dx;
				node_y = north - y*g->dy;
				dist_sq = (node_x-data[snd].east)*(node_x-data[snd].east)
						  +(node_y-data[snd].north)*(node_y-data[snd].north);
				if (dist_sq >= radius_sq) {
#ifdef __DEBUG__
					error_msgv(modname, "debug: rejecting point as out of"
						" distance to node at (%.2lf, %.2lf) m.\n",
						node_x, node_y);
#endif
					continue;
				}
				if (!cube_node_insert(g->grid[y]+x,node_x,node_y,dist_sq,
																data+snd,p)) {
					error_msgv(modname, "error: failed inserting sounding %d"
						" into node (%d, %d).\n", snd, x, y);
					return(False);
				}
				g->surface[y][x] = cube_grid_nan;	/* Indicate cache invalid */
				++(*n_used);
			}
		}
	}
	return(True);
}

/* Routine:	cube_grid_release_hypgrid
 * Purpose:	Release a HypGrid allocated via cube_grid_enumerate_hypotheses
 * Inputs:	g		Associated CubeGrid structure
 *			*hgrid	HypGrid to operate on
 * Outputs:	-
 * Comment:	-
 */

void cube_grid_release_hypgrid(CubeGrid g, HypGrid *h)
{
	u32		x, y;
	
	if (h == NULL) return;
	if (h->hypotheses != NULL) {
		if (h->hypotheses[0] != NULL) {
			for (y = 0; y < g->ny; ++y)
				for (x = 0; x < g->nx; ++x)
					if (h->hypotheses[y][x] != NULL) free(h->hypotheses[y][x]);
			free(h->hypotheses[0]);
		}
		free(h->hypotheses);
	}
	if (h->counts != NULL) {
		if (h->counts[0] != NULL) free(h->counts[0]);
		free(h->counts);
	}
	free(h);
}

/* Routine:	cube_grid_alloc_hypgrid
 * Purpose:	Allocate a HypGrid structure
 * Inputs:	g	CubeGrid structure to allocate for
 * Outputs:	Pointer to HypGrid, or NULL on failure
 * Comment:	-
 */

static HypGrid *cube_grid_alloc_hypgrid(CubeGrid g)
{
	u32		y;
	HypGrid	*rtn;
	
	if ((rtn = (HypGrid*)calloc(1, sizeof(HypGrid))) == NULL ||
		(rtn->hypotheses = (Sounding***)calloc(g->ny, sizeof(Sounding **))) == NULL ||
		(rtn->hypotheses[0] = (Sounding**)calloc(g->nx*g->ny, sizeof(Sounding*))) == NULL ||
		(rtn->counts = (s32**)calloc(g->ny, sizeof(s32*))) == NULL ||
		(rtn->counts[0] = (s32*)calloc(g->ny*g->nx, sizeof(s32))) == NULL) {
		error_msgv(modname, "error: insufficient memory for HypGrid structure.\n");
		cube_grid_release_hypgrid(g, rtn);
		return(NULL);
	}
	for (y = 1; y < g->ny; ++y) {
		rtn->hypotheses[y] = rtn->hypotheses[y-1] + g->nx;
		rtn->counts[y] = rtn->counts[y-1] + g->nx;
	}
	return(rtn);
}

/* Routine:	cube_grid_enumerate_hypotheses
 * Purpose:	Enumerate all of the hypotheses in a CubeGrid
 * Inputs:	g		CubeGrid to enumerate hypotheses
 *			p		Cube parameters for the CubeGrid
 *			west	Location of the western-most node in the cube-grid
 *			north	Location of the northern-most node in the cube-grid
 * Outputs:	Returns a pointer to a HypGrid on success, or NULL on failure
 * Comment:	The HypGrid structure contains an array of pointers to Sounding
 *			arrays (filled in by CubeNode code), and a co-located array of the
 *			counts of hypotheses per node.  If h->counts[y][x] == 0, then a
 *			NULL pointer is stored in the Sounding* array.
 */

HypGrid *cube_grid_enumerate_hypotheses(CubeGrid g, Cube p, f64 west, f64 north)
{
	HypGrid	*rtn;
	u32		hyp, x, y;
	f64		node_x, node_y;
	
	if ((rtn = cube_grid_alloc_hypgrid(g)) == NULL) {
		error_msgv(modname, "error: no memory for hypothesis enumeration"
			" storage.\n");
		return(NULL);
	}
#ifdef __DEBUG__
	error_msgv(modname, "debug: enumerating tile for (w,n) = (%.2lf, %.2lf) m.\n",
		west, north);
#endif
	for (y = 0; y < g->ny; ++y) {
		for (x = 0; x < g->nx; ++x) {
			node_x = west + x*g->dx;
			node_y = north - y*g->dy;
			if ((rtn->hypotheses[y][x] =
					cube_node_enumerate_hypotheses(
									g->grid[y]+x, rtn->counts[y]+x)) == NULL &&
					rtn->counts[y][x] < 0) {
				error_msgv(modname, "error: node enumeration failed (node"
					" (%d,%d))).\n", y, x);
				cube_grid_release_hypgrid(g, rtn);
				return(NULL);
			}
			
			/* Take advantage of the fact that we've counted the hypotheses
			 * to update the cache, if we're using it for hyp. count.
			 */
			if (g->cached == CUBE_GRID_NHYP)
				g->surface[y][x] = (f32)rtn->counts[y][x];
				
			if (rtn->counts[y][x] == 0) continue;
			
			for (hyp = 0; hyp < (u32)rtn->counts[y][x]; ++hyp) {
				rtn->hypotheses[y][x][hyp].east = node_x;
				rtn->hypotheses[y][x][hyp].north = node_y;
			}
		}
	}
	return(rtn);
}

/* Routine:	cube_grid_get_hypotheses
 * Purpose:	Return all hypotheses associated with a particular node
 * Inputs:	g			CubeGrid to work
 *			p			Cube parameters to use
 *			west, north	Bounds of the CubeGrid
 *			col, row	Location of node to extract
 * Outputs:	Pointer to HypoArray, or NULL on failure
 * Comment:	Offsets are relative to this CubeGrid.  The array returned is
 *			fully configured with location information.  If a hypothesis
 *			has been nominated, then it is indicated by the order of the
 *			hypothesis in the array returned.  A zero indicates no nomination
 *			has been done.  An array with zero hypotheses is valid and might
 *			be returned.  In this case, the pointer for Hypo in the HypoArray
 *			is returned NULL.
 */

HypoArray *cube_grid_get_hypotheses(CubeGrid g, Cube p, f64 west, f64 north,
									u32 col, u32 row)
{
	HypoArray	*rtn;
	Sounding	*hypothesis;
	u32			hypo;
	f32			depth, ratio;

	if ((rtn = (HypoArray*)malloc(sizeof(HypoArray))) == NULL) {
		error_msgv(modname, "error: failed to get memory for HypoArray.\n");
		return(NULL);
	}
	if (col >= g->nx || row >= g->ny) {
		error_msgv(modname, "error: cell (%d, %d) is outside of tile, size (%d, %d).\n",
			row, col, g->ny, g->nx);
		free(rtn);
		return(NULL);
	}
	if ((hypothesis = cube_node_enumerate_hypotheses(g->grid[row]+col, &rtn->n_hypos)) == NULL) {
		error_msgv(modname, "error: failed to enumerate hypotheses from node (%d,%d).\n",
			row, col);
		free(rtn);
		return(NULL);
	}
	if (rtn->n_hypos == 0)
		rtn->array = NULL;
	else {
		if ((rtn->array = (Hypo*)malloc(sizeof(Hypo)*rtn->n_hypos)) == NULL) {
			error_msgv(modname, "error: failed to get memory for HypoArray array.\n");
			free(rtn); free(hypothesis);
			return(NULL);
		}
	}
	if (cube_node_is_nominated(g->grid[row]+col))
		/* If a hypothesis has been nominated, then any of the extraction
		 * routines return it directly.  Hence, we can just call the basic
		 * extraction routine to generate the depth that we need.
		 */
		cube_node_extract_depth(g->grid[row]+col, p, &depth, &ratio);
	else
		depth = FLT_MAX;
	rtn->nominated = -1;
	for (hypo = 0; hypo < rtn->n_hypos; ++hypo) {
		rtn->array[hypo].z = hypothesis[hypo].depth;
		rtn->array[hypo].n_snds = hypothesis[hypo].file_id;
		rtn->array[hypo].ci = (f32)(p->sd2conf_scale*sqrt(hypothesis[hypo].dz));
		if (fabs(rtn->array[hypo].z - depth) < 0.001)
			rtn->nominated = hypo;
	}
	rtn->x = west + col*g->dx;
	rtn->y = north - row*g->dy;
	free(hypothesis);
	return(rtn);
}

/* Routine:	cube_grid_nominate_hypothesis
 * Purpose:	Nominate a particular hypothesis associated with a node
 * Inputs:	g			CubeGrid to work
 *			col, row	Location of the node to extract
 *			depth		Depth of the hypothesis to nominate
 * Outputs:	True on success, otherwise False
 * Comment:	Offsets are relative to this CubeGrid.  This might return False
 *			if the node is outside of the CubeGrid, or if there is no
 *			hypothesis that matches the depth.  An attempt to nominate a
 *			hypothesis where there are none is ignored.
 */

Bool cube_grid_nominate_hypothesis(CubeGrid g, u32 col, u32 row, f32 depth)
{
	Bool	rc = False;

	if (col >= g->nx || row >= g->ny) {
		error_msgv(modname, "error: cell (%d, %d) is outside of tile, size (%d, %d).\n",
			row, col, g->ny, g->nx);
		return(False);
	}
	if (rc = cube_node_nominate_hypothesis(g->grid[row]+col, depth)) {
		/* Nomination means that the cache will be invalid */
		if (!isnan(cube_grid_nan)) cube_grid_nan = cube_make_nan();
		g->surface[row][col] = cube_grid_nan;
	}
	return(rc);
}

/* Routine:	cube_grid_unnominate_hypothesis
 * Purpose:	Remove any nomination associated with a node
 * Inputs:	g			CubeGrid to work
 *			col, row	Location of the node to extract
 * Outputs:	True on success, otherwise False
 * Comment:	Offsets are relative to this CubeGrid.  This might return False
 *			if the node is outside of the CubeGrid.  An attempt to remove nomination
 *			of a hypothesis where there are none is reported as an error.  See
 *			cube_node_unnominate_hypothesis() for details on how the code
 *			removes hypotheses.
 */

Bool cube_grid_unnominate_hypothesis(CubeGrid g, u32 col, u32 row)
{
	Bool	rc = False;

	if (col >= g->nx || row >= g->ny) {
		error_msgv(modname, "error: cell (%d, %d) is outside of tile, size (%d, %d).\n",
			row, col, g->ny, g->nx);
		return(False);
	}
	cube_node_reset_nomination(g->grid[row]+col);
	/* Denomination means that the cache will be invalid */
	if (!isnan(cube_grid_nan)) cube_grid_nan = cube_make_nan();
	g->surface[row][col] = cube_grid_nan;
	return(rc);
}

/* Routine:	cube_grid_remove_hypothesis
 * Purpose:	Remove a particular hypothesis associated with a node
 * Inputs:	g			CubeGrid to work
 *			col, row	Location of the node to extract
 *			depth		Depth of the hypothesis to nominate
 * Outputs:	True on success, otherwise False
 * Comment:	Offsets are relative to this CubeGrid.  This might return False
 *			if the node is outside of the CubeGrid, or if there is no
 *			hypothesis that matches the depth.  An attempt to remove a
 *			hypothesis where there are none is reported as an error.  See
 *			cube_node_remove_hypothesis() for details on how the code
 *			removes hypotheses.
 */

Bool cube_grid_remove_hypothesis(CubeGrid g, u32 col, u32 row, f32 depth)
{
	Bool	rc = False;

	if (col >= g->nx || row >= g->ny) {
		error_msgv(modname, "error: cell (%d, %d) is outside of tile, size (%d, %d).\n",
			row, col, g->ny, g->nx);
		return(False);
	}
	if (rc = cube_node_remove_hypothesis(g->grid[row]+col, depth)) {
		/* Removal means that the cache will be invalid */
		if (!isnan(cube_grid_nan)) cube_grid_nan = cube_make_nan();
		g->surface[row][col] = cube_grid_nan;
	}
	return(rc);
}

/* Routine:	cube_grid_flush
 * Purpose:	Flush all nodes in the CubeGrid
 * Inputs:	g	CubeGrid to flush
 *			p	Cube parameters to use
 * Outputs:	-
 * Comment:	This steps through all of the CubeNodes and flushes each in turn.
 *			Warnings attached to cube_node_queue_flush_node() (see cube_node.h)
 *			also apply here.  This is *not* something you should be doing
 *			unless you *really* understand why you want to and what it does.
 *			I'm not sure that making this publically accessible is even a
 *			particularly good idea.
 */

void cube_grid_flush(CubeGrid g, Cube param)
{
	u32		x, y;
	
	if (!isnan(cube_grid_nan)) cube_grid_nan = cube_make_nan();
	for (y = 0; y < g->ny; ++y)
		for (x = 0; x < g->nx; ++x) {
			cube_node_queue_flush_node(g->grid[y]+x, param);
			g->surface[y][x] = cube_grid_nan;
		}
}

/* Routine:	cube_grid_est_interp_error
 * Purpose:	Estimate the error in vertical prediction due to the interpolated
 *			depth solution
 * Inputs:	*z		Prediction depths at interp nodes
 *			*var_z	Variances in vertical depth prediction at interp nodes
 *			dx, dy	Normalised offsets into the interpolation section
 *			var_h	Horizontal variance of interpolation point
 *			sx, sy	Spacing of nodes in easting and northing directions
 * Outputs:	Returns variance of the interpolated depth solution
 * Comment:	This routine computes a propagation-of-variance solution for
 *			variance of the interpolated depth given the variance of the
 *			predicted depths on which it is based, and the positioning
 *			variance of the interpolation point.  Of course, the routine still
 *			assumes that we're using bilinear interpolation.
 *				Note that this is only the variance of the depth solution, and
 *			further computation is required to convert it into an increase in
 *			variance when applied to soundings, etc.
 *				The routine assumes that the depths and variances are ordered
 *			in the array (NW, NE, SW, SE).
 */

static f32 cube_grid_est_interp_error(f32 *z, f32 *var_z, f64 dx, f64 dy,
									  f32 var_h, f32 sx, f32 sy)
{
	f32	var_dx, var_dy, var_interp, var_grad_e, var_grad_n;
	f64	dx_comp = 1.0 - dx, dy_comp = 1.0 - dy;
	
	var_dx = var_h / (sx * sx); var_dy = var_h / (sy * sy);
	var_interp = (f32)(dx_comp * dx_comp * dy * dy * var_z[0]
				 + dx * dx * dy * dy * var_z[1]
				 + dx_comp * dx_comp * dy_comp * dy_comp * var_z[2]
				 + dx * dx * dy_comp * dy_comp * var_z[3]);

	var_grad_e = (f32)((z[1] - z[0])*dy + (z[3]-z[2])*dy_comp);
	var_grad_e *= var_grad_e * var_dx;

	var_grad_n = (f32)((z[1]-z[3])*dx + (z[0] - z[3])*dx_comp);
	var_grad_n *= var_grad_n * var_dy;
	
	return(var_interp + var_grad_e + var_grad_n);
}

/* Routine:	cube_grid_interpolate
 * Purpose:	Interpolate a predicted depth for a given point inside a CubeGrid
 * Inputs:	g		CubeGrid to work with
 *			p		Cube parameters currently in force.
 *			x, y	Position within grid w.r.t left handed coordinate system
 *					at top left of the CubeGrid.
 *			var_h	Horizontal variance associated with interpolation point
 *					(typically a sounding)
 * Outputs:	Returns interpolated depth, or 0.0 if no interpolation can be done.
 *			var_pred	Set to the variance of the interpolation depth
 * Comment:	This determines the 4-NN of the point, and then does straight
 *			bilinear interpolation in the grid.  If the predicted depth for the
 *			4-NN are not set, no interpolation is done and 0.0 is returned.
 *				The principal use of this interpolation routine is to make
 *			available a correction for slope when integrating data.  This
 *			correction induces an error in the depth which is characteristic
 *			of the interpolation function and hence is best computed here.  If
 *			not required, set var_pred == NULL on input.  The value returned is
 *			the depth prediction error variance in meter^2.  Note that this is
 *			*only* for the depth prediction, and is not corrected for any other
 *			factors (e.g., use as a slope corrector).
 */

f32 cube_grid_interpolate(CubeGrid g, Cube p, f64 x, f64 y, f32 var_h,
						  f32p var_pred)
{
	s32	col, row;
	f64	delta_x, delta_y;
	f32	depth, z[4], var_z[4];
	
	col = (s32)floor(x/g->dx); row = (s32)floor(y/g->dy);
	if (col < 0 || col >= (s32)(g->nx-1) || row < 0 || row >= (s32)(g->ny-1)) {
		error_msgv(modname, "error: point (%.lf, %.lf) m is not within the"
			" tile selected.\n", x, y);
		return(0.0f);
	}
	
	z[0] = g->grid[row][col].pred_depth;
	z[1] = g->grid[row][col+1].pred_depth;
	z[2] = g->grid[row+1][col].pred_depth;
	z[3] = g->grid[row+1][col+1].pred_depth;
	
	if (z[0] == p->no_data_value || z[1] == p->no_data_value ||
		z[2] == p->no_data_value || z[3] == p->no_data_value) {
		/* Not all data points are available for interpolation, so do none */
		if (var_pred != NULL) *var_pred = 0.0f;
		return(0.0f);
	}
	if (isnan(z[0]) || isnan(z[1]) || isnan(z[2]) || isnan(z[3])) {
		error_msg(modname, "error: NaNs in predicted depths.\n");
		if (var_pred != NULL) *var_pred = 0.0f;
		return(0.0f);
	}
	delta_x = (x - g->dx*col)/g->dx;
	delta_y = (g->dy*(row+1) - y)/g->dy;
	depth = (f32)(z[0] * (1.0 - delta_x) * delta_y +
			z[1] * delta_x * delta_y +
			z[2] * (1.0 - delta_x) * (1.0 - delta_y) +
			z[3] * delta_x * (1.0 - delta_y));

	if (var_pred != NULL) {
		var_z[0] = g->grid[row][col].pred_var;
		var_z[1] = g->grid[row][col+1].pred_var;
		var_z[2] = g->grid[row+1][col].pred_var;
		var_z[3] = g->grid[row+1][col+1].pred_var;
		*var_pred = cube_grid_est_interp_error(z, var_z, delta_x, delta_y,
						var_h, g->dx, g->dy);
	}

#ifdef __DEBUG__
if (g->grid[row][col].write) {
error_msgv(modname, "debug: interp x = %.2lf y = %.2lf\n", x, y);
error_msgv(modname, "debug: interp delta_x = %.2lf delta_y = %.2lf\n",
	delta_x, delta_y);
error_msgv(modname, "debug: interp tl = %.1f tr = %.1f bl = %.1f br = %.1f\n",
	g->grid[row][col].pred_depth, g->grid[row][col+1].pred_depth,
	g->grid[row+1][col].pred_depth, g->grid[row+1][col+1].pred_depth);
}
#endif
	return(depth);
}
