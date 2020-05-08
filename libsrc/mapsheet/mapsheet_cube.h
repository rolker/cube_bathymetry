/*
 * $Id: mapsheet_cube.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.2.4.2  2003/01/31 21:46:26  dneville
 * (BRC)  Added prototypes for nomination, reset of nomination, extraction and
 * removal of hypotheses for HyperCUBE surfaces.  It is difficult to expose these
 * at this level because the calls have to go through a number of layers to make
 * it to the data, leading to inefficiency.
 *
 * Revision 1.2.4.1  2003/01/28 14:30:00  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.4.2.2  2002/12/15 01:41:43  brc
 * Added prototype for cache purging call.
 *
 * Revision 1.4.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.4  2002/05/10 22:02:32  brc
 * Added prototypes for hypothesis strength readback and new initialisation
 * methods.
 *
 * Revision 1.3  2002/03/14 04:33:27  brc
 * Added code to honour the mapsheet_finalise() call at the top level of the
 * module, and to pass these on to the HyperCUBE code when appropriate.  Note
 * that this just sets a flag in the data structure, rather than forcing a
 * flush immediately.  This ensures that the buffers are flushed just before
 * the data are used, rather than being flushed before the data structure is
 * written to disc, therefore maintaining the integrity of the estimation
 * surface.
 *
 * Revision 1.2  2001/12/07 21:12:46  brc
 * Added prototype for hypothesis dumping function.  This should not usually
 * be called by user level code, since it can be a little flakey.
 *
 * Revision 1.1  2001/09/20 18:04:46  brc
 * Implementation of HyperCUBE --- a collection of CubeGrid structures strung
 * together to tile a much larger area than can be accomodated in main memory.
 * Included on-demand tile pager, modified least-recently-used tile cache
 * replacement policy (modification removes tiles which are clean first, rather
 * than simply removing oldest first), and backing store management.
 *
 *
 * ------ NOTE ------ NOTE ------ NOTE ------ NOTE ------ NOTE ------ NOTE ----
 * This module is based heavily on mapsheet_nodal.c, which it is intended to
 * replace (eventually).  The following log is from mapsheet_nodal.h up to the
 * point where the code was cloned.
 *
 * Revision 1.6  2001/08/21 01:45:43  brc
 * Added facility to count the number of soundings actually used from each swath
 * to update the data in the mapsheet region.  This can be used, among other things,
 * to determine which data files in a set are actually used in updating the data.
 *
 * Revision 1.5  2001/05/14 21:13:39  brc
 * Added prototypes for combined depth/uncertainty extraction, summary generation
 * and parameter executor.  Updated prototypes for parameter initialisation and
 * depth/uncertainty extractor.
 *
 * Revision 1.4  2001/04/10 22:54:43  brc
 * Added prototypes for the standard deviation retrieval code and the data
 * re-initialisation sequence.
 *
 * Revision 1.3  2000/12/03 20:29:20  brc
 * Added comments from mapsheet_nodal.c, and added prototype for
 * mapsheet_nodal_finalise().
 *
 * Revision 1.2  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
 *
 * Revision 1.1.1.1  2000/08/10 15:53:25  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	mapsheet_cube.h
 * Purpose:	Depth surface construction based on nodal update equations
 * Date:	1 September 2001
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2001.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __MAPSHEET_CUBE_H__
#define __MAPSHEET_CUBE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "mapsheet.h"
#include "mapsheet_private.h"
#include "sounding.h"

/* Routine:	mapsheet_cube_get_depth
 * Purpose:	Extract depth surface from nodal representation
 * Inputs:	sheet	Mapsheet to get information from
 *			*ip		Pointer to HyperCUBE structure
 *			*par	Raw parameter structure associated with depth node grid
 * Outputs:	op		Pointer to linear (row major) array of data to hold the
 *					output sequence.
 * Comment:	This simply extracts the current estimates from the DepthNode grid
 *			of structures.  In particular, it does not flush the node pre-filter
 *			input queues into the Kalman Filter input sequence, and hence the
 *			estimates may not accurately reflect the current state of knowledge
 *			about the nodes.  Users requiring a current snapshot should call
 *			mapsheet_cube_finalise() before reading the grid with this call.
 */

extern Bool mapsheet_cube_get_depth(MapSheet sheet, HyperCube *ip,
									 void *par, f32p op);

/* Routine:	mapsheet_cube_get_uncertainty
 * Purpose:	Extract uncertainty surface from nodal representation
 * Inputs:	sheet	Mapsheet to get information from
 *			*ip		Pointer to HyperCUBE structure
 *			*par	Raw parameter structure associated with depth node grid
 * Outputs:	op		Pointer to linear (row major) array of data to hold the
 *					output sequence.
 * Comment:	This simply extracts the current estimates from the DepthNode grid
 *			of structures.  In particular, it does not flush the node pre-filter
 *			input queues into the Kalman Filter input sequence, and hence the
 *			estimates may not accurately reflect the current state of knowledge
 *			about the nodes.  Users requiring a current snapshot should call
 *			mapsheet_cube_finalise() before reading the grid with this call.
 *			  Note that the value returned is actually the 90% predicted
 *			confidence interval in meters, rather than the true variance, based
 *			on the assumption that the sampling distribution is normal (i.e.,
 *			standard error x 1.64).
 */

extern Bool mapsheet_cube_get_uncertainty(MapSheet sheet, HyperCube *ip,
										   void *par, f32p op);

/* Routine:	mapsheet_cube_get_hypo_strength
 * Purpose:	Extract hypothesis strength using the selected multiple hypothesis
 *			disambiguation algorithm
 * Inputs:	sheet	MapSheet to use for parameters
 *			*ip		Pointer to HyperCube structure to work
 *			*par	Parameters for the algorithm
 * Outputs:	op		Pointer to linear output space
 * Comment:	This acts as a simple dispatching switch for the three extraction
 *			algorithms.
 */

extern Bool mapsheet_cube_get_hypo_strength(MapSheet sheet, HyperCube *ip,
											void *par, f32p op);

/* Routine:	mapsheet_cube_get_depth_unct
 * Purpose:	Get both depth and uncertainty surface using the selected multi-
 *			hypothesis disambiguation algorithm
 * Inputs:	sheet	MapSheet to work from
 *			*ip		Pointer to HyperCube structure
 *			*par	Parameter structure for the algorithm
 * Outputs:	*depth	Linear (row major) array for depth output
 *			*unct	Linear (row major) array for uncertanty output
 * Comment:	This is basically a dispatch switch for the various algorithm
 *			implementations.  Note that it is faster (and sometimes very
 *			significantly faster) to use this if you are going to need both
 *			surfaces, since it only has to do the sorting/context search once
 *			per node, rather than twice.
 */

extern Bool mapsheet_cube_get_depth_unct(MapSheet sheet, HyperCube *ip,
										  void *par, f32p depth, f32p unct);

/* Routine:	mapsheet_cube_get_data
 * Purpose:	Get all surfaces associated with the data portion of a HyperCUBE
 *			mapsheet surface
 * Inputs:	sheet	MapSheet to work from
 *			*ip		Pointer to HyperCUBE structure
 *			*par	Parameter structure for the algorithm
 * Outputs:	*depth	Linear (row major) array for depth output
 *			*unct	Linear (row major) array for uncertanty output
 *			*hypstr	Linear (row major) array for hypothesis strength output
 * Comment:	This is basically a dispatch switch for the various algorithm
 *			implementations.  Note that it is faster (and sometimes very
 *			significantly faster) to use this if you are going to need all
 *			surfaces, since it only has to do the sorting/context search once
 *			per node, rather than twice.  However, the down-side is that it is
 *			not possible for the system to guarantee a cache read, since we
 *			can only cache one result at a time.  Therefore, there is no
 *			pre-read speed-up.
 */

extern Bool mapsheet_cube_get_data(MapSheet sheet, HyperCube *ip, void *par,
						    	   f32p depth, f32p unct, f32p hypstr);

/* Routine:	mapsheet_cube_get_hypo
 * Purpose:	Get hypotheses counts from nodes
 * Inputs:	sheet	MapSheet with which to work
 *			*hcube	HyperCube structure to work from
 *			*par	HyperParam structure
 * Outputs:	Returns True if the data is extracted, or False otherwise
 *			*op is filled with the data, rounded down to U8
 * Comment:	This does overflow clipping so that any node with more than 255
 *			hypotheses is set to 255.
 */

extern Bool mapsheet_cube_get_hypo(MapSheet sheet, HyperCube *hcube, void *par,
								   u8p op);

/* Routine:	mapsheet_cube_dump_hypo
 * Purpose: Dump hypotheses counts from nodes to GeoZui3D file
 * Inputs:	sheet	MapSheet with which to work
 *			*hcube	HyperCube structure to work from
 *			*par	HyperParam structure
 *			*name	Name of the file to write
 *			*bnds	MapBounds structure to sub-set area, or NULL
 * Outputs:	Returns True if the data is extracted, or False otherwise
 *			*name is written, along with auxilliary files as specified in
 *			ccom_general.h
 * Comment:	
 */

extern Bool mapsheet_cube_dump_hypo(MapSheet sheet, HyperCube *hcube, void *par,
							 		char *name, MapBounds *bnds);

/* Routine: mapsheet_cube_nominate_hypo_by_node
 * Purpose:	Choose a particular hypothesis at a particular node
 * Inputs:	sheet		MapSheet to use
 *			*hcube		Pointer for HyperCUBE surface to work with
 *			*par		HyperCUBE parameters with Cube sub-parameters
 *			col, row	Offset into sheet to use
 *			depth		Depth of the hypothesis to choose
 * Outputs:	True on success, otherwise False.
 * Comment:	This chooses a particular hypothesis at the node specified, setting
 *			it as 'user nominated'.  This allows the user to selectively over-
 *			ride the algorithm's automatic choice, if required.  Note that the
 *			col/row offsets are with respect to the basic definition of the
 *			MapSheet, which has (0,0) in the top left (NW) corner, increasing
 *			row-major and north to south.
 */

extern Bool mapsheet_cube_nominate_hypo_by_node(MapSheet sheet, HyperCube *hcube,
										 void *par, u32 col, u32 row, f32 depth);

/* Routine: mapsheet_cube_unnominate_hypo_by_node
 * Purpose:	Reset nomination at a particular node
 * Inputs:	sheet		MapSheet to use
 *			*hcube		Pointer for HyperCUBE surface to work with
 *			*par		HyperCUBE parameters with Cube sub-parameters
 *			col, row	Offset into sheet to use
 * Outputs:	True on success, otherwise False.
 * Comment:	This resets any nomination of hypothesis, returning the node to
 *			algorithmic control of disambiguation.  Note that the
 *			col/row offsets are with respect to the basic definition of the
 *			MapSheet, which has (0,0) in the top left (NW) corner, increasing
 *			row-major and north to south.
 */

extern Bool mapsheet_cube_unnominate_hypo_by_node(MapSheet sheet, HyperCube *hcube,
										 void *par, u32 col, u32 row);

/* Routine: mapsheet_cube_get_hypo_by_node
 * Purpose:	Extract all of the hypotheses from a particular node
 * Inputs:	sheet		MapSheet to use
 *			*hcube		Pointer for the HyperCUBE structure
 *			*par		HyperCUBE parameters, with Cube parameter subset
 *			col, row	Offset into sheet to use
 * Outputs:	Returns pointer to HypoArray for the CubeNode, or NULL on failure
 * Comment:	This acts primarily as a marshalling sequence for the CubeGrid and
 *			CubeNode sub-layers.  Note that the col/row offsets are with
 *			respect to the basic definition of the MapSheet, which has (0,0) in
 *			the top left (NW) corner, increasing row-major and north to south.
 */

extern HypoArray *mapsheet_cube_get_hypo_by_node(MapSheet sheet, HyperCube *hcube,
										  void *par, u32 col, u32 row);

/* Routine: mapsheet_cube_remove_hypo_by_node
 * Purpose:	Remove a particular hypothesis at a particular node
 * Inputs:	sheet		MapSheet to use
 *			*hcube		Pointer for HyperCUBE surface to work with
 *			*par		HyperCUBE parameters with Cube sub-parameters
 *			col, row	Offset into sheet to use
 *			depth		Depth of the hypothesis to choose
 * Outputs:	True on success, otherwise False.
 * Comment:	This removes a particular hypothesis at the node specified. Note that the
 *			col/row offsets are with respect to the basic definition of the
 *			MapSheet, which has (0,0) in the top left (NW) corner, increasing
 *			row-major and north to south.  This removal is immediate and
 *			permanent.
 */

extern Bool mapsheet_cube_remove_hypo_by_node(MapSheet sheet, HyperCube *hcube,
										 void *par, u32 col, u32 row, f32 depth);

/* Routine:	mapsheet_cube_insert_depths
 * Purpose:	Add a sequence of depth estimates to the mapsheet
 * Inputs:	sheet	MapSheet to work on
 *			*ip		Pointer to HyperCube structure
 *			*par	Pointer to (opaqued) parameter structure for algorithm
 *			stream	SoundingStream which supplied the data
 *			*data	Pointer to the soundings which should be added
 *			nsnds	Number of soundings in this batch
 *			*nused	Number of soundings used for incorporation into the grid
 * Outputs:	True if data was added correctly, otherwise False
 * Comment:	This evaluates the error model for each sounding, and adds them into
 *			the depth estimates on the relevant nodes.  Note that in some
 *			versions of the algorithm, the estimates may be queued and pre-
 *			filtered before insertion into the estimate sequence, and hence may
 *			not be evident on immediate readback.
 */

extern Bool mapsheet_cube_insert_depths(MapSheet sheet, HyperCube *ip,
										 void *par, SoundingStream stream,
										 Sounding *data, u32 nsnds, u32 *nused);

/* Routine:	mapsheet_cube_initialise
 * Purpose:	Initialise predicted depths and initial hypothesis from input
 * Inputs:	sheet	MapSheet to work with
 *			*cube	The HyperCUBE to work on
 *			*par	Pointer to HyperCUBE private workspace
 *			*data	Pointer to initialisation data
 *			unct	Uncertainty to use in initialising hypos (see below)
 *			unct_pcnt	Flag: True => use _unct_ as a percentage (0.0 ... 100.0)
 *						of initialisation depth rather than a fixed 1 s.d.
 *						uncertainty in meters.
 *			*mask	Mask for areas to be initialised (255 => do not init.)
 *			cols,
 *			rows	Size of the initialisation data.
 * Outputs:	True if initialisation took place, otherwise False.
 * Comment:	This initialises the entire mapsheet using the data provided as the
 *			predicted depths.  The predicted depth is used to do slope
 *			correction on the data as it is inserted, but does not otherwise
 *			affect the estimation sequence.
 *				The code does, however, set up an initial hypothesis at each
 *			node using the same data.  It would be wise to specify a fairly
 *			high uncertainty for this, unless you're sure that the data is good,
 *			since otherwise you'll bias the estimate.  Think of the uncertainty
 *			as a prior variance --- as with all Bayesian schemes, it should be
 *			suitable vague unless you're *sure* you know what you're doing.
 *				One way of doing this is to set the uncertainty as a percentage
 *			of declared depth and then set the percentage to be relatively
 *			high (say 10-15%).
 */

extern Bool mapsheet_cube_initialise(MapSheet sheet, HyperCube *cube, void *par,
							  		 f32 *data, f32 unct, Bool unct_pcnt,
									 u8 *mask, u32 cols, u32 rows);

/* Routine:	mapsheet_cube_init_unct
 * Purpose:	Initialise predicted depths and initial hypothesis from input
 * Inputs:	sheet	MapSheet to work with
 *			*cube	The HyperCUBE to work on
 *			*par	Pointer to HyperCUBE private workspace
 *			*data	Pointer to initialisation data
 *			*unct	Uncertainty to use in initialising hypos (see below)
 *			*mask	Mask for areas to be initialised (255 => do not init.)
 *			cols,
 *			rows	Size of the initialisation data.
 * Outputs:	True if initialisation took place, otherwise False.
 * Comment:	This initialises the entire mapsheet using the data provided as the
 *			predicted depths.  The predicted depth is used to do slope
 *			correction on the data as it is inserted, but does not otherwise
 *			affect the estimation sequence.
 *				The code does, however, set up an initial hypothesis at each
 *			node using the same data.  It would be wise to specify a fairly
 *			high uncertainty for this, unless you're sure that the data is good,
 *			since otherwise you'll bias the estimate.  Think of the uncertainty
 *			as a prior variance --- as with all Bayesian schemes, it should be
 *			suitable vague unless you're *sure* you know what you're doing.
 */

extern Bool mapsheet_cube_init_unct(MapSheet sheet, HyperCube *cube, void *par,
							 		f32 *data, f32 *unct,
									u8 *mask, u32 cols, u32 rows);

/* Routine:	mapsheet_cube_finalise
 * Purpose:	Flush all of the tiles in the cache to ensure that all data is used
 * Inputs:	sheet	MapSheet to use
 *			param	Pointer to parameters (Cube) to use
 *			grid	Pointer to the HyperCUBE structure
 * Outputs:	True if successful, otherwise False
 * Comment:	Note that, in order to avoid thrashing the cache components, we
 *			only set a flag to indicate that such a request has been received
 *			from the user.  The read-back code then makes sure that each tile
 *			is flushed when it is about to be used.
 */

extern Bool mapsheet_cube_finalise(MapSheet sheet, void *param,
									HyperCube *hcube);

/* Routine:	mapsheet_cube_purge_cache
 * Purpose:	Walk cache grid and flush in oldest first (LRU) order any tiles
 *			which have been unused longer than the timeout limit
 * Inputs:	*hcube	HyperCube structure to use
 *			*par	Parameter structure (Cube) to use
 *			flush_all	Flag: Flush all tiles if they are over the time limit
 * Outputs:	Returns True if purge went OK, otherwise False;
 *			*nleft set to number of tiles left in the cache
 * Comment:	This will not flush all of the tiles; the last used tile is probably
 *			going to be used immediately again, so we'd incur a load penalty in
 *			getting it back into memory from disc.
 */

extern Bool mapsheet_cube_purge_cache(HyperCube *hcube, void *par, Bool flush_all, u32 *nleft);

/* Routine:	mapsheet_cube_get_caching
 * Purpose:	Returns caching currently set in the system
 * Inputs:	*hcube	HyperCube structure to work with
 * Outputs:	-
 * Comment:	-
 */

extern MapCache mapsheet_cube_get_caching(HyperCube *hcube);

/* Routine:	mapsheet_cube_set_caching
 * Purpose:	Set caching required from HyperCUBE
 * Inputs:	*hcube	HyperCube structure to work with
 *			cache	Type of data to cache
 * Outputs:	-
 * Comment:	This records the caching type, translates it into the CubeGrid
 *			value required, and then moves through the grid setting cache
 *			types for all of the CubeGrid structures.
 */

extern void mapsheet_cube_set_caching(HyperCube *hcube, MapCache cache);

/* Routine:	mapsheet_cube_get_tilesize
 * Purpose:	Return tile sizes for main sequence tiles, or for the single
 *			tile if there are none
 * Inputs:	*hcube	HyperCUBE to work from
 * Outputs:	*width, *height set to correct tile sizes
 * Comment:	This returns the tile size iff there is more than one tile, and
 *			otherwise the end sizes.
 */

extern void mapsheet_cube_get_tilesize(HyperCube *hcube,
									   u32p width, u32p height);

/* Routine:	mapsheet_cube_init_param
 * Purpose:	Initialise parameters for the algorithm to their default values
 * Inputs:	order	IHO Survey Order as defined in errmod.h
 *			dn, de	Northing and Easting spacings for sheet
 * Outputs:	Pointer to an opaqued parameter structure for the algorithm
 * Comment:	This initialises the parameter to the currently compiled values.
 *			At present, there is no other way to change the parameters than to
 *			recompile the source.
 */

extern void *mapsheet_cube_init_param(ErrModIHOOrder order, f64 dn, f64 de);

/* Routine:	mapsheet_cube_release_param
 * Purpose:	Releases all memory associated with the algorithm parameters struct
 * Inputs:	*param	Pointer to the (opaqued) algorithm parameter structure
 * Outputs:	-
 * Comment:	-
 */

extern void mapsheet_cube_release_param(void *param);


/* Routine:	mapsheet_cube_alloc_grid
 * Purpose:	Alocate workspace for the HyperCUBE
 * Inputs:	sheet	MapSheet for which to build the HyperCUBE
 *			*param	Pointer to private parameter structure
 *			*dirname	Name of the directory in which to store the mapsheet
 * Outputs:	Returns pointer to the HyperCUBE, or NULL
 * Comment:	This determines, given the parameters, the maximum size of
 *			CubeGrid it can make and still manage four in memory simultaneously
 *			within the user set limits.  This ensures that even in the worst
 *			case that we go across the corner between four sub-sheets, we'll
 *			still not thrash the cache (so to speak).  The implicit assumption
 *			in this design measure is that the swath of the multibeam doesn't
 *			get big on the scale of the tiles, otherwise we could end up re-
 *			caching across each swath, which would be very expensive (although
 *			it would still work).
 */

extern HyperCube *mapsheet_cube_alloc_grid(MapSheet sheet, void *param,
										   char *dirname);

/* Routine:	mapsheet_cube_deserialise
 * Purpose:	De-serialise data for surface from input
 * Inputs:	dirname	Directory containing the tiles for HyperCUBE
 *			sheet	Mapsheet to work on
 * Outputs:	True if sheet was read, otherwise False.
 * Comment:	-
 */

extern Bool mapsheet_cube_deserialise(const char *dirname, MapSheet sheet);


/* Routine:	mapsheet_cube_serialise
 * Purpose:	Serialise a HyperCUBE grid
 * Inputs:	sheet		MapSheet being serialised
 *			*dirname	Name of the directory into which we're writing
 *			*hcube		The HyperCUBE structure being used
 *			*param		HyperParams structure
 * Outputs:	True if the sheet was converted and flushed, otherwise False
 * Comment:	This code essentially just runs the cache grids, and serialises any
 *			tiles that it finds.
 */

extern Bool mapsheet_cube_serialise(MapSheet sheet, char *dirname,
									HyperCube *hcube, void *param);

/* Routine:	mapsheet_cube_write_param
 * Purpose:	Write parameter structure to file
 * Inputs:	param	Pointer to (opaqued) parameter structure
 *			f		File descriptor to write on
 * Outputs:	True if write was sucessful, otherwise False
 * Comment:	-
 */

extern Bool mapsheet_cube_write_param(void *param, FILE *f);

/* Routine:	mapsheet_cube_read_param
 * Purpose:	Read parameter structure from file
 * Inputs:	f	File descriptor to read from
 * Outputs:	Pointer to (opaqued) parameter structure, or NULL on failure.
 * Comment:	This doesn't do any version checking, which may cause problems in
 *			the future.
 */

extern void *mapsheet_cube_read_param(FILE *f);


/* Routine:	mapsheet_cube_release
 * Purpose:	Release all dynamically allocated memory associated with a node
 * Inputs:	*h	Pointer to HyperCUBE structure
 * Outputs:	-
 * Comment:	-
 */

extern void mapsheet_cube_release(HyperCube *h);

/* Routine:	mapsheet_cube_extend_summary
 * Purpose:	Write extended elements from private data store to summary directory
 * Inputs:	*sheet	Sheet to write from
 *			*dir	Name of the directory to extend elements into
 * Outputs:	True on success, otherwise False
 * Comment:	This writes out a set of images in TIFF format which are only found
 *			in the nodal estimation method.  The actual components which are
 *			written depend on the algorithm currently in use ...
 */

extern Bool mapsheet_cube_extend_summary(MapSheet sheet, char *dir);

/* Routine:	mapsheet_cube_execute_params
 * Purpose:	Execute parameters list for this sub-module
 * Inputs:	*list	ParList linked list to work through
 * Outputs:	True if the list was parsed properly, otherwise False
 * Comment:	This looks for a whole screed of parameters used to control all
 *			aspects of the CUBE algorithm ... basically the list at the top
 *			of the source file.  All of the parameters are limit checked as
 *			far as possible.
 */

#include "params.h"

extern Bool mapsheet_cube_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
