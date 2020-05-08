/* $Id: cube_grid.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.2.4.2  2003/01/31 21:51:15  dneville
 * (BRC)  Added code for nomination, reset of nomination, extraction and
 * removal of hypotheses for HyperCUBE surfaces.
 *
 * Revision 1.2.4.1  2003/01/28 14:29:44  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.4  2002/05/10 19:46:20  brc
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
 * Revision 1.3  2001/12/07 20:47:57  brc
 * Changed geo-referencing offsets from f32 to f64 to provide enough dynamic
 * range for small spacing grids in UTM coordinates.  Added a HypGrid structure
 * to hold all of the hypotheses available in a particular CubeGrid, organised
 * by node, and the access routines to allocate and free it.
 *
 * Revision 1.2  2001/09/20 18:00:04  brc
 * Added sheet size estimation in order to allow HyperCUBE to work out the size
 * of tiles.  Added call-backs for sizes and spacing of grids.  Modified to use
 * mapsheet.c for invalid data values to ensure that they are the same across the
 * board.  Modified to use buffered FILE streams for speed.  Changed bounds
 * checking to use signed values so that points off the left hand edge are caught.
 *
 * Revision 1.1  2001/08/21 02:00:08  brc
 * Added headers for CUBE module.
 *
 *
 * File:	cube_grid.h
 * Purpose:	Implement grid of CUBE nodes functionality
 * Date:	31 July 2001
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2001.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __CUBE_GRID_H__
#define __CUBE_GRID_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "mapsheet.h"
#include "sounding.h"
#include "cube.h"
#include "cube_node.h"

typedef struct _cube_grid *CubeGrid;

/* CubeCache: indicate which element of the estimation the routine should be
 * caching internally for faster access when only parts of the data are being
 * updated at any one time (e.g., in real-time swath mode for MBES).
 */
typedef enum _cube_cache {
	CUBE_GRID_DEPTH,
	CUBE_GRID_UNCT,
	CUBE_GRID_NHYP,
	CUBE_GRID_RATIO
} CubeCache;


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

extern u32 cube_grid_estimate_size(u32 size, f32 exp_hyp, f32 prob_use,
								   Cube param);

/* Routine:	cube_grid_get_sizes
 * Purpose:	Return the size of the CubeGrid
 * Inputs:	grid	CubeGrid to work through
 * Outputs:	*width	Width of CubeGrid in nodes
 *			*height	Height of CubeGrid in nodes
 * Comment:	-
 */

extern void cube_grid_get_sizes(CubeGrid grid, u32 *width, u32 *height);

/* Routine:	cube_grid_get_spacings
 * Purpose:	Return the size of the CubeGrid
 * Inputs:	grid	CubeGrid to work through
 * Outputs:	*dx		Spacing in eastings
 *			*dy		Spacing in northings
 * Comment:	-
 */

extern void cube_grid_get_spacings(CubeGrid grid, f32 *dx, f32 *dy);

/* Routine:	cube_grid_release
 * Purpose:	Releases all dynamic memory associated with a CubeGrid
 * Inputs:	grid	CubeGrid to work through
 * Outputs:	-
 * Comment:	Note that the CubeGrid is itself dynamically allocated, and hence
 *			is also freed up during this call.
 */

extern void cube_grid_release(CubeGrid grid);

/* Routine:	cube_grid_invalidate_cache
 * Purpose:	Set all elements in the cache space to invalid values
 * Inputs:	g	CubeGrid to work with
 * Outputs:	-
 * Comment:	-
 */

extern void cube_grid_invalidate_cache(CubeGrid g);

/* Routine:	cube_grid_reinit
 * Purpose:	Initialises the CubeGrid
 * Inputs:	g	CubeGrid structure on which to work
 * Outputs:	-
 * Comment:	-
 */

extern void cube_grid_reinit(CubeGrid g);

/* Routine:	cube_grid_set_cache
 * Purpose:	Set the component of the grid to be cached internally
 * Inputs:	g	CubeGrid on which to work
 *			dat	Data type to cache
 * Outputs:	True if cache was reset, otherwise False
 * Comment:	-
 */

extern Bool cube_grid_set_cache(CubeGrid g, CubeCache dat);


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

extern CubeGrid cube_grid_new(u32 nx, u32 ny, f32 dx, f32 dy, CubeCache cache);

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

extern CubeGrid cube_grid_deserialise(FILE *fd, Cube p);

/* Routine:	cube_grid_serialise
 * Purpose:	Serialise a CubeNode grid to file
 * Inputs:	fd		File descriptor to write on
 *			g		CubeGrid structure to work on
 *			p		Cube parameter structure to work with
 * Outputs:	True if the sheet was converted and flushed, otherwise False
 * Comment:	This code writes the CubeGrid structure, and then a sequence of
 *			CubeNode structures using a call-through to cube_node_serialise().
 */

extern Bool cube_grid_serialise(FILE *fd, CubeGrid g, Cube p);

/* Routine:	cube_grid_get_depth
 * Purpose:	Extract depth using the selected multiple hypothesis disambiguation
 *			algorithm
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameters structure for algorithm
 * Outputs:	op	Pointer to linear output space
 * Comment:	This acts as a simple dispatching switch for the three extraction
 *			algorithms.
 */

extern Bool cube_grid_get_depth(CubeGrid g, Cube p, f32p op);

/* Routine:	cube_grid_get_unct
 * Purpose:	Extract uncertianty using the selected multiple hypothesis
 *			disambiguation algorithm
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure for algorithm
 * Outputs:	op		Pointer to linear output space
 * Comment:	This acts as a simple dispatching switch for the three extraction
 *			algorithms.
 */

extern Bool cube_grid_get_unct(CubeGrid g, Cube p, f32p op);

/* Routine:	cube_grid_get_ratio
 * Purpose:	Extract hypothesis strength ratio using the selected multiple
 *			hypothesis disambiguation algorithm
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure for algorithm
 * Outputs:	op		Pointer to linear output space
 * Comment:	This acts as a simple dispatching switch for the three extraction
 *			algorithms.
 */

extern Bool cube_grid_get_ratio(CubeGrid g, Cube p, f32p op);

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

extern Bool cube_grid_get_depth_unct(CubeGrid g, Cube p, f32p depth, f32p unct);

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

extern Bool cube_grid_get_data(CubeGrid g, Cube p,
							   f32p depth, f32p unct, f32p ratio);

/* Routine:	cube_grid_get_nhyp
 * Purpose:	Extract surface containing number of hypotheses from nodes
 * Inputs:	g	CubeGrid structure on which to work
 *			p	Cube parameter structure to work from
 * Outputs:	*h	Pointer to row major linear output workspace
 * Comment:	This essentially calls the cube_node_extract_nhyp() call for each
 *			node, with the addition that the cache is used and updated if it is
 *			currently caching numbers of hypotheses.
 */

extern void cube_grid_get_nhyp(CubeGrid g, Cube p, f32p h);

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

extern Bool cube_grid_insert_depths(CubeGrid g, Cube p, SoundingStream stream,
							 		Sounding *data, u32 nsnds,
									f64 west, f64 north, u32 *n_used);

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

extern Bool cube_grid_initialise(CubeGrid g, Cube p, f32 *data, f32 unct,
								 Bool unct_pcnt, u8 *mask, u32 row_stride);

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

extern Bool cube_grid_init_unct(CubeGrid g, Cube p, f32 *data, f32 *unct,
						 		u8 *mask, u32 row_stride);

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

extern void cube_grid_flush(CubeGrid g, Cube param);

/* HypGrid: A structure to hold information about the hypotheses at each node
 * in the grid, and the number of such hypotheses.
 */

typedef struct _hyp_grid {
	Sounding	***hypotheses;
	s32			**counts;
} HypGrid;

/* Routine:	cube_grid_release_hypgrid
 * Purpose:	Release a HypGrid allocated via cube_grid_enumerate_hypotheses
 * Inputs:	g		Associated CubeGrid structure
 *			*hgrid	HypGrid to operate on
 * Outputs:	-
 * Comment:	-
 */

extern void cube_grid_release_hypgrid(CubeGrid g, HypGrid *h);

/* Routine:	cube_grid_enumerate_hypotheses
 * Purpose:	Enumerate all of the hypotheses in a CubeGrid
 * Inputs:	g		CubeGrid to enumerate hypotheses
 *			p		Cube parameters for the CubeGrid
 *			west	Location of the western-most node in the cube-grid
 *			north	Location of the northern-most node in the cube-grid
 * Outputs:	Returns a pointer to an array of Sounding's containing the
 *			hypotheses returned by the underlying nodes, or NULL if there was
 *			a problem getting memory.
 *			*nhyp	Number of soundings (a.k.a. hypotheses) being returned.
 *					Note that in the (unlikely) case that there are no
 *					hypotheses, *nhyp == 0, and NULL is returned.
 * Comment:	This counts the total number of hypotheses in all nodes in the
 *			CubeGrid and allocates space for them.  The CubeNode's are then
 *			told to enumerate hypotheses, which are copied into the space.  Note
 *			that it is possible for this routine to return NULL and *nhyp == 0
 *			meaning that no hypotheses were found in any node.  On error (i.e.,
 *			not enough memory), *nhyp == -1.
 */

extern HypGrid *cube_grid_enumerate_hypotheses(CubeGrid g, Cube p,
										 f64 west, f64 north);

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

extern HypoArray *cube_grid_get_hypotheses(CubeGrid g, Cube p, f64 west, f64 north,
											u32 col, u32 row);

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

extern Bool cube_grid_nominate_hypothesis(CubeGrid g, u32 col, u32 row, f32 depth);

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

extern Bool cube_grid_unnominate_hypothesis(CubeGrid g, u32 col, u32 row);

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

extern Bool cube_grid_remove_hypothesis(CubeGrid g, u32 col, u32 row, f32 depth);

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

extern f32 cube_grid_interpolate(CubeGrid g, Cube p, f64 x, f64 y, f32 var_h,
						  		 f32p var_pred);

#ifdef __cplusplus
}
#endif

#endif
