/*
 * $Id: mapsheet_cube.c 21 2006-01-19 16:00:07Z brc $
 * $Log$
 * Revision 1.2  2006/01/19 16:00:07  brc
 * Fixed memory leak in mapsheet_cube_release() reported by Ding Zheng at
 * Reson bv.
 *
 * Revision 1.1.1.1  2003/02/03 20:18:42  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2.4.2  2003/01/31 21:47:13  dneville
 * (BRC)  Added code for nomination, reset of nomination, extraction and
 * removal of hypotheses for HyperCUBE surfaces.  It is difficult to expose these
 * at this level because the calls have to go through a number of layers to make
 * it to the data, leading to inefficiency.
 *
 * Revision 1.2.4.1  2003/01/28 14:30:00  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.6.2.2  2002/12/15 01:41:16  brc
 * Added facility to flush all tiles in a MapSheet back to disc, rather than all
 * but the last.  This is important when there are multiple, possibly disparate
 * MapSheets in memory, since each one would otherwise hold on to the
 * last tile, irrespective of whether is is currently important or not.  Turned off
 * interpolation error insertion since it appears to be significantly amplifying the
 * errors when is shouldn't.  There should be some amplification, of course, but
 * not on the level we see with the current setup.  Also fixed a subtle bug in
 * the tile sizeing code which sometimes allowed the last tile in a sheet to be
 * slightly bigger than the base tiles, with wierd consequences.
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
 * Revision 1.6  2002/06/16 02:35:36  brc
 * Fixed bug in addition of interpolation error --- used to be that the extra
 * error was being erroneously added to the horizontal error component of the
 * soundings, rather than the vertical error.  Of course, once you observe the
 * effect of this in the assimilation engine, it's hard to tell them apart, and
 * they have about the same effect.  Bugger.
 *
 * Revision 1.5  2002/05/10 22:01:36  brc
 * Multiple modifications based on the Snow Passage verification project.
 * 1. Updated the size estimate code so that the expected number of nodes that
 * will be used is specified directly.  This is multiplied into the mixture
 * in the cube() code so that we don't allocate space for nodes that are
 * unlikely to be used in sparse data.  Default is 80% occupancy.  Added code
 * to the parameter module interface to specify this.
 * 2. Added interface to read back hypothesis strength ratio.
 * 3. Added generic mapsheet_cube_get_data() call which reads back all of the
 * data components in one call (more efficient, since we only have to run the
 * disam. engine once).
 * 4. Modified the mapsheet_cube_insert_data() call so that an interpolated
 * 'predicted depth' is computed once for each sounding.  This is then stored
 * in the 'range' element of the Sounding structure (*smack* bad!!) so that it
 * is available for the underlying code to use in making slope corrections.
 * We also add the slope interpolation error to the vertical error at this
 * point assuming that slope corrections will be done later.  This isn't ideal,
 * and really should be fixed later --- e.g., with a switch to determine
 * whether slope corrections are being done or not.
 *
 * Revision 1.4  2002/03/14 04:33:27  brc
 * Added code to honour the mapsheet_finalise() call at the top level of the
 * module, and to pass these on to the HyperCUBE code when appropriate.  Note
 * that this just sets a flag in the data structure, rather than forcing a
 * flush immediately.  This ensures that the buffers are flushed just before
 * the data are used, rather than being flushed before the data structure is
 * written to disc, therefore maintaining the integrity of the estimation
 * surface.
 *
 * Revision 1.3  2002/01/23 15:22:25  brc
 * Fixed bugs in surface readbacks.  This was using the cache status flags to
 * determine whether or not to read back a tile.  Of course, whether the tile
 * was read before or not, it should always be read; the STATUS_READ flag only
 * really tells you about how long it is going to take to read back the data,
 * since if set, the underlying CubeGrid cache will be valid, and hence only
 * a memory copy, rather than a full disambiguation, is required.  However,
 * unless and until we can guarantee that the data buffer isn't going to change
 * between calls to the code, we have to read back each grid every time.
 *
 * Revision 1.2  2001/12/07 21:12:10  brc
 * Added facility to dump the hypotheses from all nodes in the grid to a
 * HypGrid structure, and to sub-set a HypGrid for a specific area.  Modified
 * the RSS space factor to be 100% of the recommended size, although this
 * may in fact be a Very Bad Idea (TM), since the current size reporting for
 * cube_node.c code isn't terribly accurate, and hence the grids may get a
 * little too big to have four of them in memory, and have the buffers and
 * flat grids required to do disambiguation and read-back.  Hmm.  This would
 * be better if the code did something a little more active, like checking
 * the memory situation currently available on the machine, and matching the
 * number of tiles cached to this sort of spacing on a dynamic basis.  This
 * would avoid problems with other programs grabbing memory, too.
 *     Modified all code to use mapsheet_cube_get_tilesize() so that they
 * all agree on the size of the tiles that we're using.  This was the cause of
 * many not-so-subtle problems with the code when there is only a tail tile,
 * rather than at least one full-sized tile.
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
 * replace (eventually).  The following log is from mapsheet_nodal.c up to the
 * point where the code was cloned.
 *
 * Revision 1.9  2001/08/28 16:02:01  brc
 * Added debugging on prior disambiguation method, and indicator of which
 * method is actually in use.
 *
 * Revision 1.8  2001/08/21 01:45:43  brc
 * Added facility to count the number of soundings actually used from each swath
 * to update the data in the mapsheet region.  This can be used, among other things,
 * to determine which data files in a set are actually used in updating the data.
 *
 * Revision 1.7  2001/05/16 21:08:01  brc
 * Added float.h for FLT_MAX, DBL_MAX, etc. under Linux
 *
 * Revision 1.6  2001/05/14 21:54:24  brc
 * Major modifications to support multi-hypothesis estimation and other things:
 * 1.  Added parameter executive code to make module 'params'-aware, and hence
 *     making it possible to modify the algorithm control parameters from the
 *     outside without recompiling the code (huzzah!)  This also means that the
 *     static parameters have become variables, and the variables are now used
 *     in the code.  The KalParam structure has a few new variables as well.
 *     The parameters file is read and parsed into the default_param file
 *     static variable, which is then cloned every time a new estimation
 *     sheet is constructed.
 * 2.  Restructured extraction code to allow depth and uncertainty to be
 *     generated together (with retrofit to the base call sequence for
 *     compatibility).  This can make a very significant difference when the
 *     multi-hypothesis code has to disambiguate a set of depth hypotheses,
 *     which can take a while.
 * 3.  Made the mapping from standard error to confidence interval a full
 *     parameter (i.e., an arbitrary scale factor).  The parameter reading
 *     code understands 68% (1se), 90%, 95% and 99%.
 * 4.  Modified the depth integration code so that the IHO survey order
 *     specified when the mapsheet is constructed is used to determine, per
 *     depth sounding, the maximum allowable vertical error.  This is used
 *     to compute the allowable radius of influence for the data point, making
 *     the integration more reasonable.
 * 5.  The multi-hypothesis estimation itself:
 *     a.  A hypothesis is a separate depth track, held in a linked list of
 *         structures at each DepthNode structure in the estimation grid.
 *     b.  DepthNodes now only contain minimal information and the pre-queue,
 *         with hypotheses being spawned and added as required.
 *     c.  When new data arrives, it is matched to the current list of
 *         hypotheses using a minimum standardised prediction error (like the
 *         monitoring code).  The data point is then compared with the closest
 *         current hypothesis using the standard monitoring code.  If the
 *         data looks good, it is integrated; if it would cause an exception,
 *         a new hypothesis is initialised with the current data point and
 *         then added to the linked list already in existence.
 *     d.  Since we can have multiple hypotheses, we need a way to generate
 *         a single depth/uncertainty surface (i.e., choosing the `best' or
 *         `most probable' hypothesis).  Three methods have been developed:
 *         pseudo-MAP hypothesis probability, pseudo-likelihood based on the
 *         closest single-hypothesis estimation node, and pseudo-posterior
 *         based on the product of the previous two.  The method to use is
 *         stored in the KalParam structure, and the extraction code auto-
 *         matically chooses the correct routines.  In most cases, the
 *         simple prior will work; in very heavy noise, the likelihood method
 *         seems to work well.  The posterior method seems to make little
 *         improvement over the likelihood method in the case of noisy data
 *         where it might be expected to be more appropriate.
 *
 * Revision 1.5  2001/04/10 23:14:08  brc
 * Multiple modifications to look at robustness in the estimation sequence.
 * 1.  Added facility to estimate the standard deviation of the input sample
 * sequence in the node in order to give a backward error measure of the
 * expected noise.  Added facility to have this read back from the mapsheet.c
 * interface (only works with this estimation sub-module).
 * 2.  Turned on the continual Eeg-style culling, although the utility of this
 * is still in doubt.  The values which are computed depend strongly on the
 * particular dataset, and hence the culling threshold required is not very
 * clearly defined.  Jorgen's suggestion of q_max = 30 would result in no data
 * being culled in Pinnacles EM1002 or Portsmouth SB8101, for example, where
 * the values are much closer to 1.0.  Added lots of debug code for this,
 * controlled by the __EEG_DEBUG__ macro (very noisy when on).
 * 3.  Added a sheet reinitialisation feature where arbitrary data can be used
 * to initialise the mapsheet, using a particular uncertainty across the entire
 * sheet.  Added a mask element so that a second binary data array can be used
 * to select which parts of the sheet get initialised.  This can be used to
 * fake a previous dataset so that the ideas of recursive updating can be tested.
 * 4.  Added a discounting structure (West & Harrison, 'Bayesian Forecasting
 * and Dynamic Models', Ch. 2ff.) rather than a fixed system variance.  This
 * allows the model to adapt to the quality of the data, allowing faster adaption
 * when the estimates are poor, and better smoothing when the model is clean.
 * W&H suggest that discounting rates of about 0.8-1.0 are reasonable; the
 * default value of 0.9 seems to be good, at least for Portsmouth, where this
 * has been tested most.
 * 5.  Added West & Harrison's model monitoring code (as above, Ch.11).  This
 * is designed to determine when a model stops looking like the input data,
 * and hence when some intervention is required.  The model is based on
 * sequentially computed Bayes factors, with an additional monitoring of how
 * long the current sequence of `bad' factors have been going on, so that it
 * can also detect slow drifts away from the current model.  The alternate
 * model here (Bayes factors compare two models) is one based on detecting a
 * level shift (i.e., d.c. shift in depth, e.g., burst mode sounder failure), and
 * the parameters are the expected shift, h, the Bayes factor threshold, tau, and
 * the expected total length of a bad drift sequence, M.  The defaults are based
 * on the W&H analysis, which should be good everywhere since it based on a
 * unit standard normal.  The defaults are h=4.0, tau=0.135 and M=5.  This
 * model remediates indicated intervention points by resetting the current track
 * to the input value which caused the intervention.  This means that the chain
 * moves about a great deal more quickly, but also that the last estimate may
 * not be the `best' one.  However, the number of interventions gives a good
 * indication that something is going wrong.  In Portsmouth, about 99.5% of the
 * time there are none; the remainder of the interventions are clustered around
 * regions known to be bad, which is encouraging.  This is also the basis for
 * a model which maintains multiple hypotheses.
 *
 * Revision 1.4  2001/02/10 18:12:24  brc
 * Modifications to support the new error modeling structure, where errors in
 * depth and location are computed externally as the soundings are imported,
 * rather than trying to do this in the mapsheet code.
 *
 * Revision 1.3  2000/12/03 20:27:43  brc
 * Multiple modifications to improve robustness of the algorithm in the face
 * of outliers early in the KF input sequence.  Added a pre-filtering queue to
 * each node, or fixed by user specified length.  Data are added to the queue
 * in depth sorted order, shallowest at the top until the queue is filled.  Then,
 * as a new point becomes available, the median depth is removed from the queue
 * and inserted into the KF, with the new value being inserted into the queue
 * again to maintain the sort order.  Linear search is used to keep the queue in
 * order since (a) it's usually quite short, and (b) is always in sorted order,
 * so the algorithm is almost linear.  Next, added Jorgen Eeg's outlier detection
 * mechanism to deal with the case where the input sequence for the KF is quite
 * short, but still has wild fliers.  This works by computing sums of squared
 * differences on a leave-one-out basis, ranking the points according to a
 * quotient which is approximately F distributed.  This is currently only applied
 * when the pre-filter queue is flushed into the KF before the surface is read
 * back, and hence only has an effect with very limited data.  However, it could
 * also be used on a rolling basis to keep the queue free of duff points, rather
 * than having them accumulate in the outer reaches of the pre-filter queue.  This
 * would need to have a way to record which points are being culled, however,
 * to avoid any hydrographic concerns.  To support these two modifications, also
 * added a finalisation concept through mapsheet_nodal_finalise().  This causes
 * all of the queues to be processed and flushed into the KF, freeing up the
 * memory associated with them (queues are only added when there is extant data).
 * Note that this means that there is a potential problem with consistency of the
 * algorithm in that a sequence applied all at once, and a sequence applied in
 * two parts with a mapsheet_nodal_finalise() between will not necessarily give
 * the same answer.  The user can avoid this if they are willing to accept that
 * the depth/uncert. surface they retrieve may not be the current best estimate,
 * simply by not calling the finalisation code.  However, finalisation code is
 * always called when the mapsheet is saved, so the sheet would have to remain
 * in memory at all times.  This is being designed with a view to a display prog.
 * to show current estimate on screen as data is being incorporated.
 *     The error model has also been modified to ensure that in <30m of water,
 * the error returned is not artificially small.  Otherwise, rogue points which are
 * reported as being very shallow can significantly bias the estimation if they
 * occur early in the input sequence (i.e., depth 1m +- 0.01m when the true depth
 * is 18m +- 0.18m).  This makes the system closer to IHO V3 anyway for touchdown
 * variance limits, which is quite useful.  Typical EM1000 accuracy in 25m is about
 * 0.25m at nadir (90% conf.), so setting this to the IHO 0.30m is appropriate.
 * The code also now reads back 90% confidence limits for the uncertainty surface
 * rather than straight std. err.  This assumes that the errors are Gaussian so
 * that it can do a simple conversion of 90% = 1.64*err.
 *     Finally, improved comments on the function headers and added a debug structure
 * to report the node's input sequence, the KF's input sequence (essentially a
 * peturbation of the node's inputs) and the buffer flush sequence.  Controlled
 * by a .write in the node's data structure, set in mapsheet_nodal_init_grid().
 *
 * Revision 1.2  2000/08/24 15:11:35  brc
 * Modified numerous files to allow the code to compile cleanly under Linux.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:25  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	mapsheet_cube.c
 * Purpose:	Depth update algorithm based on nodal estimate of depth and
 *			prediction uncertainty
 * Date:	1 September 2001
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
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef WIN32
	#include <malloc.h>
#else
	#include <sys/resource.h>
#endif
#include <fcntl.h>
#include <errno.h>
#include "stdtypes.h"
#include "error.h"
#include "mapsheet.h"
#include "mapsheet_private.h"
#include "mapsheet_cube.h"
#include "sounding.h"		/* Interface to soundings structures */
#include "device.h"			/* Interface to device performance metrics */
#include "errmod.h"
#include "ccom_general.h"
#include "params.h"
#include "cube.h"
#include "cube_grid.h"

#define MAX(a,b) ((a)>(b)?(a):(b))
#define MIN(a,b) ((a)<(b)?(a):(b))

/* ---- DEBUG ---- DEBUG ---- DEBUG ---- DEBUG ---- DEBUG ---- DEBUG ----
 * The following flags, along with the generic __DEBUG__ turn on component
 * parts of the debuging structure for the mapsheet integration algorithm.  All
 * of the code is selected by the grid.write element for the nodes required;
 * if this is set to True, then the node debug information is reported.  Note
 * that many of the routines do not indicate which node is being debuged, and
 * hence it can get a little confusing when you have more than one set ....
 *     Note also that some of the debuging routines add files into the local
 * filespace with default names, so that if you have something in the way, it's
 * going to get blitzed on each run.  Set write filter options at the grid
 * initialisation stage in mapsheet_cube_init_grid().
 */

/* Generic debuging of the source.  This actually turns on a great deal of code
 * in the section that deals with reporting data coming in, all going to the
 * standard error reporting stream (typically stderr).  This can make the code
 * quite noisy.
 */

#undef __DEBUG__

typedef struct _hyper_param {
	u32		max_memory;			/* Maximum amount of memory to use */
	f32		hypothesis_hint;	/* Exageration factor for # hypotheses */
	f32		prob_use;			/* Probability of node being used */
	u32		max_tile_dimension;	/* Maximum size of tiles in nodes */
	u32		tile_expiry_delta;	/* User timeout for tiles in cache */
	Cube	cube;				/* Parameters for the CUBE algorithm */
} HyperParam;

#define MAX_MEM_RATIO	1.0				/* Maximum proportion of Max RSS to take */
#define MIN_MEMORY		(1024*1024*20)	/* Minimum memory worth talking about */
#define DEFAULT_MAX_MEMORY	(1024*1024*128)

#define MIN_HYPOHINT	1.0				/* Minimum exageration factor: none */
#define MAX_HYPOHINT	5.0				/* Maximum exageration factor */
#define DEFAULT_HYPOTHESIS_HINT 1.25	/* Mean 1.25 hypotheses per node */

#define MIN_PROBUSE		0.1f			/* Min. probability of node use */
#define MAX_PROBUSE		1.0f			/* Max. probability of node use */
#define DEFAULT_PROBUSE	0.8f			/* Expect 80% nodes used */

#define MIN_MAXDIM		255				/* Minimum maximum side length */
#define MAX_MAXDIM		5001			/* Maximum maximum side length */
#define DEFAULT_MAX_TILE_DIMENSION 511	/* Nominal tile dimensions */

#define MIN_EXPIRY			10		/* Minimum time to retain tiles */
#define MAX_EXPIRY			3600	/* Maximum time to retain tiles */
#define DEFAULT_TILE_EXPIRY	600		/* Default time-out of 10 min. */

typedef struct _hyper_cube {
	CubeGrid	**tiles;		/* The grid of CubeGrids */
	u32			**stamps;		/* Timestamps for LRU replacement */
	u8			**status;		/* Flags marking status of tiles */
	u32			n_rows;			/* Height of grid in tiles */
	u32			n_cols;			/* Width of grid in tiles */
	u32			tile_width;		/* Full size width of tile in nodes */
	u32			tile_height;	/* Full size height of tile in nodes */
	u32			end_width;		/* Width of RH end tile */
	u32			end_height;		/* Height of bottom end tile */
	char		*backing_store;	/* Directory storing the tiles */
	CubeCache	cache_type;		/* Cache type to set in CubeGrids */
	Bool		flush_tiles;	/* Flag: flush tiles before readback */
} HCube /* HyperCube */;

typedef enum _cube_status {
	MAP_CUBE_STATUS_READ = 1,	/* Whether tile has been read since it was last
								 * modified in memory.
								 */
	MAP_CUBE_STATUS_CLEAN = 2	/* Whether tile has been modified since it was
								 * last synchronised with disc
								 */
} MapCubeStatusEnum;

#define MAP_CUBE_STATUS_IS(stat, mask) (((stat)&(mask)) != 0)

typedef u8 MapCubeStatus;

static char *modname = "mapsheet_cube";
static char *modrev = "$Revision: 21 $";

static HyperParam default_param = {
	DEFAULT_MAX_MEMORY,
	DEFAULT_HYPOTHESIS_HINT,
	DEFAULT_PROBUSE,
	DEFAULT_MAX_TILE_DIMENSION,
	DEFAULT_TILE_EXPIRY,
	NULL						/* CUBE parameters, filled in on load */
};

/* Routine:	mapsheet_cube_get_caching
 * Purpose:	Returns caching currently set in the system
 * Inputs:	*hcube	HyperCube structure to work with
 * Outputs:	-
 * Comment:	-
 */

MapCache mapsheet_cube_get_caching(HyperCube *hcube)
{
	MapCache	rc;
	
	switch(hcube->cache_type) {
		case CUBE_GRID_DEPTH:
			rc = MAP_CACHE_DEPTH;
			break;
		case CUBE_GRID_UNCT:
			rc = MAP_CACHE_UNCERTAINTY;
			break;
		case CUBE_GRID_NHYP:
			rc = MAP_CACHE_HYPOTHESES;
			break;
		case CUBE_GRID_RATIO:
			rc = MAP_CACHE_HYPOSTRENGTH;
			break;
		default:
			error_msgv(modname, "internal: error: caching type (%d) not known"
				" to mapsheet_cache_get_caching().\n", (u32)(hcube->cache_type));
			rc = MAP_CACHE_NONE;
			break;
	}
	return(rc);
}

/* Routine:	mapsheet_cube_get_status
 * Purpose:	Return status word for a particular tile
 * Inputs:	*hcube	HyperCube to work with
 *			row, col	Position within HyperCube cache grid
 * Outputs:	Returns status byte
 * Comment:	-
 */

static MapCubeStatus mapsheet_cube_get_status(HyperCube *hcube,
											  u32 row, u32 col)
{
	return((MapCubeStatus)hcube->status[row][col]);
}

/* Routine:	mapsheet_cube_set_status
 * Purpose:	Set status of a HyperCube tile
 * Inputs:	*hcube		HyperCube to work with
 *			row, col	Position in HyperCube tile cache to work
 * Outputs:	-
 * Comment:	This sets the relevant bit in the status word.
 */

static void mapsheet_cube_set_status(HyperCube *hcube, u32 row, u32 col,
							  		 MapCubeStatusEnum status)
{
	u8	statint = (u8)status;
	
	hcube->status[row][col] |= statint;
}

/* Routine:	mapsheet_cube_clear_status
 * Purpose:	Resets status of a HyperCube tile
 * Inputs:	*hcube		HyperCube to work with
 *			row, col	Position in HyperCube tile cache to work
 * Outputs:	-
 * Comment:	This resets the relevant bit in the status word.
 */

extern void mapsheet_cube_clear_status(HyperCube *hcube, u32 row, u32 col,
									   MapCubeStatusEnum status)
{
	u8	statint = (u8)status;

	hcube->status[row][col] &= ~statint;
}

/* Routine:	mapsheet_cube_set_caching
 * Purpose:	Set caching required from HyperCUBE
 * Inputs:	*hcube	HyperCube structure to work with
 *			cache	Type of data to cache
 * Outputs:	-
 * Comment:	This records the caching type, translates it into the CubeGrid
 *			value required, and then moves through the grid setting cache
 *			types for all of the CubeGrid structures.
 */

void mapsheet_cube_set_caching(HyperCube *hcube, MapCache cache)
{
	CubeCache	ctype;
	u32			row, col;
	
	switch(cache) {
		case MAP_CACHE_DEPTH:
			ctype = CUBE_GRID_DEPTH;
#ifdef __DEBUG__
			error_msgv(modname, "debug: set caching to CUBE_GRID_DEPTH.\n");
#endif
			break;
		case MAP_CACHE_UNCERTAINTY:
			ctype = CUBE_GRID_UNCT;
#ifdef __DEBUG__
			error_msgv(modname, "debug: set caching to CUBE_GRID_UNCT.\n");
#endif
			break;
		case MAP_CACHE_HYPOTHESES:
			ctype = CUBE_GRID_NHYP;
#ifdef __DEBUG__
			error_msgv(modname, "debug: set caching to CUBE_GRID_NHYP.\n");
#endif
			break;
		case MAP_CACHE_HYPOSTRENGTH:
			ctype = CUBE_GRID_RATIO;
#ifdef __DEBUG__
			error_msgv(modname, "debug: set caching to CUBE_GRID_RATIO.\n");
#endif
			break;
		default:
			/* This isn't for us, so we can ignore it */
			return;
	}
	if (ctype == hcube->cache_type) {
#ifdef __DEBUG__
		error_msgv(modname, "debug: no change in caching ... ignoring.\n");
#endif
		return;
	}

#ifdef __DEBUG__
	error_msgv(modname, "debug: resetting caching properties and clearing"
		" read-back flags on all tiles.\n");
#endif
	hcube->cache_type = ctype;
	for (row = 0; row < hcube->n_rows; ++row) {
		for (col = 0; col < hcube->n_cols; ++col) {
			if (hcube->tiles[row][col] != NULL)
				cube_grid_set_cache(hcube->tiles[row][col], ctype);
			mapsheet_cube_clear_status(hcube, row, col,
													MAP_CUBE_STATUS_READ);
			mapsheet_cube_clear_status(hcube, row, col,
													MAP_CUBE_STATUS_CLEAN);
		}
	}
}

/* Routine:	mapsheet_cube_get_tilesize
 * Purpose:	Return tile sizes for main sequence tiles, or for the single
 *			tile if there are none
 * Inputs:	*hcube	HyperCUBE to work from
 * Outputs:	*width, *height set to correct tile sizes
 * Comment:	This returns the tile size iff there is more than one tile, and
 *			otherwise the end sizes.
 */

void mapsheet_cube_get_tilesize(HyperCube *hcube, u32p width, u32p height)
{
	if (hcube->n_rows == 1)
		*height = hcube->end_height;
	else
		*height = hcube->tile_height;
	if (hcube->n_cols == 1)
		*width = hcube->end_width;
	else
		*width = hcube->tile_width;
}

/* Routine:	mapsheet_cube_name_tile
 * Purpose:	Construct name by which a tile is known
 * Inputs:	row, col	Row and Column of the tile grid required
 *			*hcube		HyperCUBE parameter area
 * Outputs:	Returns pointer to an internal buffer which stores the leaf name
 *			of the tile specified
 * Comment:	Note that the buffer makes this code non-reentrant, and therefore
 *			you should copy the string before anything makes another call to
 *			this function.
 */

static char *mapsheet_cube_name_tile(u32 row, u32 col, HyperCube *hcube)
{
	static char buffer[128];
	u32			n_digits, limit = MAX(hcube->n_rows, hcube->n_cols);
	
	n_digits = (u32)log10(limit) + 1;
	sprintf(buffer, "%.*dx%.*d", n_digits, row, n_digits, col);
	return(buffer);
}

/* Routine:	mapsheet_cube_flush
 * Purpose:	Flush a tile back to disc
 * Inputs:	*hcube		HyperCube structure to work from
 *			row, col	Location of the node in the HyperCUBE grid
 *			*par		HyperParam structure
 * Outputs:	True if element was flushed, otherwise False
 * Comment:	
 */

static Bool mapsheet_cube_flush(HyperCube *hcube, u32 row, u32 col,
								HyperParam *par)
{
	char	*filename;
	FILE	*fd;
	Bool	rc = True;
	
	if ((filename = mapsheet_make_name(hcube->backing_store,
				mapsheet_cube_name_tile(row, col, hcube))) == NULL) {
		error_msg(modname, "error: couldn't get memory for name"
			" of tile being flushed!\n");
		rc = False;
	}
#ifdef __DEBUG__
	error_msgv(modname, "debug: starting flush of tile (%d, %d) to backing"
		" store \"%s\".\n", row, col, filename);
#endif
	if (rc && (fd = fopen(filename, "wb")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\".\n",
			filename);
		rc = False;
	}
	if (rc && !cube_grid_serialise(fd, hcube->tiles[row][col], par->cube)) {
		error_msgv(modname, "error: failed to flush tile (%d, %d) to disc.\n",
			row, col);
		rc = False;
	}
#ifdef __DEBUG__
	error_msgv(modname, "debug: flushed tile (%d, %d) to backing store "
		"\"%s\".\n", row, col, filename);
#endif
	free(filename);
	if (fd != NULL) fclose(fd);
	mapsheet_cube_set_status(hcube, row, col, MAP_CUBE_STATUS_CLEAN);
	return(rc);
}

/* Routine:	mapsheet_cube_count_tiles
 * Purpose:	Count the number of tiles currently loaded in the HyperCube
 * Inputs:	*hcube	Pointer to the HyperCUBE to use
 * Outputs:	Number of tiles currently loaded (may be zero)
 * Comment:	-
 */

static u32 mapsheet_cube_count_tiles(HyperCube *hcube)
{
	u32	n_tiles, row, col;
	
	n_tiles = 0;
	for (row = 0; row < hcube->n_rows; ++row)
		for (col = 0; col < hcube->n_cols; ++col)
			if (hcube->tiles[row][col] != NULL) ++n_tiles;
#ifdef __DEBUG__
/*	error_msgv(modname, "debug: counted %d tiles in cache.\n", n_tiles);*/
#endif
	return(n_tiles);
}

/* Routine:	mapsheet_cube_make_list
 * Purpose:	Make a list of CubeGrid in the cache, in order of use, most recently
 *			used first.
 * Inputs:	*hcube	HyperCube structure to use
 *			n_tiles	Number of tiles in the cache
 *			weight	Flag: True => use status weighting (see comment)
 * Outputs:	Returns pointer to the list, if not NULL
 *			*nrtn	Number of CubeGrids in list
 * Comment:	This makes a list of all of the tiles in the cache, and sorts them
 *			according to their last used timestamp.  Optionally, it can apply
 *			weighting to the values based on the tile's cache flags (i.e., clean
 *			and read, a.k.a., clean wrt disc and clean wrt user buffer) so as
 *			to minimise the amount of work required on a deletion by attempting
 *			to remove cache tiles which are clean first.
 */

typedef struct {
	u32	stamp;
	u32	row, col;
} Purge;

int purge_sort(const void *a, const void *b)
{
	Purge	*pa = (Purge*)a, *pb = (Purge*)b;
	
	if (pa->stamp > pb->stamp) return(-1);
	else if (pa->stamp == pb->stamp) return(0);
	else return(1);
}

static Purge *mapsheet_cube_make_list(HyperCube *hcube, u32 n_tiles, Bool weight)
{
	Purge	*purge;
	u32		tile, row, col, max_stamp, min_stamp, weight_scale;
	
	if ((purge = (Purge*)malloc(sizeof(Purge)*n_tiles)) == NULL) {
		error_msgv(modname, "error: failed to get memory for purge sort"
			" (total %d bytes).\n", sizeof(Purge)*n_tiles);
		return(NULL);
	}
	tile = 0;
	for (row = 0; row < hcube->n_rows; ++row)
		for (col = 0; col < hcube->n_cols; ++col)
			if (hcube->tiles[row][col] != NULL) {
				purge[tile].stamp = hcube->stamps[row][col];
				purge[tile].row = row;
				purge[tile].col = col;
				++tile;
			}
	/* We now compute weights based on the status of the tiles.  This ensures
	 * that the algorithm picks tiles which are clean and read first, then
	 * those that are clean and un-read, then those that are dirty and read,
	 * and only as a last resort those that are dirty and unread.  This
	 * algorithm should ensure that tiles which are not going to have to be
	 * flushed to backing store are picked preferentially, which should speed
	 * things up a little.
	 */
	if (weight) {
		max_stamp = 0;
		min_stamp = INT_MAX;
		for (tile = 0; tile < n_tiles; ++tile) {
			max_stamp = MAX(max_stamp, purge[tile].stamp);
			min_stamp = MIN(min_stamp, purge[tile].stamp);
		}
		weight_scale = 2*(max_stamp - min_stamp);
			/* Doubling the difference ensures that we're way ahead to anything
			 * which is going to occur in the list itself.
			 */
		for (tile = 0; tile < n_tiles; ++tile)
			purge[tile].stamp =
				(purge[tile].stamp - min_stamp) +
				weight_scale*((~hcube->status[purge[tile].row][purge[tile].col])&0x3);
	}
	qsort(purge, n_tiles, sizeof(Purge), purge_sort);
	return(purge);
}

/* Routine:	mapsheet_cube_delete
 * Purpose:	Delete a tile from cache
 * Inputs:	*hcube		HyperCube structure to work from
 *			row, col	Row/Column in the cache to remove
 *			*p			HyperParam structure to work from
 * Outputs:	True if deleted, False otherwise.
 * Comment:	Note that this deletes the tile, flushing it to backing store if
 *			it has the dirty flag set.  The only failure mode is if the tile
 *			is being flushed, and it doesn't flush correctly.
 */

static Bool mapsheet_cube_delete(HyperCube *hcube, u32 row, u32 col,
								 HyperParam *p)
{
	MapCubeStatus	stat = mapsheet_cube_get_status(hcube, row, col);

#ifdef __DEBUG__	
	error_msgv(modname, "debug: deleting tile (%d, %d), status %d.\n",
		row, col, (u32)stat);
#endif
	if (!MAP_CUBE_STATUS_IS(stat, MAP_CUBE_STATUS_CLEAN)) {
		if (!mapsheet_cube_flush(hcube, row, col, p)) {
			error_msgv(modname, "error: failed to flush tile (%d, %d) to "
				"backing store before deleting.\n", row, col);
			return(False);
		}
#ifdef __DEBUG__
		else
			error_msgv(modname, "debug: pre-delete flushed tile (%d, %d).\n",
				row, col);
#endif
	}
#ifdef __DEBUG__
	else
		error_msgv(modname, "debug: tile (%d, %d) is clean, no flush reqd.\n",
			row, col);
#endif
	cube_grid_release(hcube->tiles[row][col]);
	hcube->tiles[row][col] = NULL;
	return(True);
}

/* Routine:	mapsheet_cube_purge_cache
 * Purpose:	Walk cache grid and flush in oldest first (LRU) order any tiles
 *			which have been unused longer than the timeout limit
 * Inputs:	*hcube		HyperCube structure to use
 *			*par		HyperParam structure to use
 *			flush_all	Flag: True => flush all tiles if they are over limit
 * Outputs:	Returns True if purge went OK, otherwise False;
 *			*nleft set to number of tiles left in the cache
 * Comment:	This will not flush all of the tiles unless flush_all is True; the
 *			last used tile is probably going to be used immediately again, so
 *			we'd incur a load penalty in getting it back into memory from disc.
 *				One reason to flush all of the tiles is that it minimises the
 *			memory footprint of the MapSheet to just those CubeGrids that are
 *			still actively being used.  In a long run of the algorithm with
 *			multiple MapSheets in memory, this is essential to allowing the algorithm
 *			to continue running without eating through all of the memory resources
 *			(eventually ...)
 */

Bool mapsheet_cube_purge_cache(HyperCube *hcube, void *par, Bool flush_all, u32 *nleft)
{
	s32		tile, min_tile;
	u32		n_tiles, row, col, stamp;
	time_t	now;
	Purge	*purge;
	HyperParam	*hparam = (HyperParam*)par;
	
	n_tiles = mapsheet_cube_count_tiles(hcube);
	
	if (n_tiles == 0 || (!flush_all && n_tiles == 1)) {
		/* None or only one in the table, nothing else to do */
		*nleft = n_tiles;
		return(True);
	}
	
	/* Make list of tiles in the mapsheet.  Note that we do this without status
	 * weighting to ensure that we have strict LRU replacement.  Otherwise we
	 * can't implement the user specified time-out limit in seconds.
	 */
	if ((purge = mapsheet_cube_make_list(hcube, n_tiles, False)) == NULL) {
		error_msgv(modname, "error: failed to generate list for node purge.\n");
		return(False);
	}
	
	/* Walk purge list from last element up to second, checking whether they
	 * can be flushed.
	 */
	if (flush_all) min_tile = 0; else min_tile = 1;
	time(&now);
	for (tile = n_tiles-1; tile >= min_tile; --tile) {
		row = purge[tile].row; col = purge[tile].col;
		stamp = purge[tile].stamp;
		if ((now - stamp) >= hparam->tile_expiry_delta) {
			/* Last used timestamp is more than max quantum from now - remove */
			if (!mapsheet_cube_delete(hcube, row, col, hparam)) {
				error_msgv(modname, "error: failed to delete tile (%d, %d)"
					" t=%d.\n", row, col, hcube->stamps[row][col]);
				return(False);
			}
		}
	}
	free(purge);
	
	/* Finally, count the number of tiles left */
	*nleft = mapsheet_cube_count_tiles(hcube);
	return(True);
}

/* Routine:	mapsheet_cube_delete_oldest
 * Purpose:	Flush the oldest tile in the grid to disc
 * Inputs:	*hcube	Pointer to the HyperCube structure to use
 *			*par	Pointer to the HyperParam structure
 * Outputs:	True if flushed correctly, False if not.
 * Comment:	-
 */

static Bool mapsheet_cube_delete_oldest(HyperCube *hcube, HyperParam *par)
{
	u32		n_tiles, row, col;
	Purge	*purge;
#ifdef __DEBUG__
	u32		stamp;
#endif
	
	if ((n_tiles = mapsheet_cube_count_tiles(hcube)) <= 1) return(True);
	if ((purge = mapsheet_cube_make_list(hcube, n_tiles, True)) == NULL) {
		error_msg(modname, "error: failed getting memory for purge sort"
			" list.\n");
		return(False);
	}
	row = purge[n_tiles-1].row; col = purge[n_tiles-1].col;
#ifdef __DEBUG__
	stamp = purge[n_tiles-1].stamp;
#endif
	free(purge);

	if (!mapsheet_cube_delete(hcube, row, col, par)) {
		error_msgv(modname, "error: failed to delete oldest tile (%d, %d).\n",
			row, col);
		return(False);
	}
#ifdef __DEBUG__
	error_msgv(modname, "debug: deleted oldest tile (%d, %d) t=%d.\n",
		row, col, stamp);
#endif
	return(True);
}

/* Routine:	mapsheet_cube_ensure_tile
 * Purpose:	Make sure that a tile is in place, and in memory
 * Inputs:	*hcube	Pointer to HyperCube structure to use
 *			row, col	Position in HyperCube array
 * Outputs:	Returns True if the tile is now available, or else False
 * Comment:	This will attempt to load the tile from disc, if it exists, or to
 *			make it internally if not, and write a copy to disc.  Failure
 *			mechanisms include not being able to load, and failing to make a
 *			new, blank, tile if it doesn't exist on disc.  If the file is
 *			available, but four or more tiles are currently loaded, one is
 *			unloaded in order to make space, using the LRU algorithm.
 */

static Bool mapsheet_cube_ensure_tile(MapSheet sheet, HyperCube *hcube,
									  u32 row, u32 col, HyperParam *par)
{
	char 		*filename;
	struct stat	fstat;
	CubeGrid	grid;
	u32			width, height;
	time_t		now;
	FILE		*fd;
	
	if (hcube->tiles[row][col] != NULL) return(True);
	
	time(&now);

	/* Construct filename and check for presence on disc */
	if (mapsheet_cube_count_tiles(hcube) >= 4) {
		if (!mapsheet_cube_delete_oldest(hcube, par)) {
			error_msgv(modname, "error: failed to delete oldest tile in store.\n");
			return(False);
		}
	}
	
	if ((filename = mapsheet_make_name(hcube->backing_store,
						mapsheet_cube_name_tile(row, col, hcube))) == NULL) {
		error_msg(modname, "error: failed to get namespace memory for"
			" tile name.\n");
		return(False);
	}
	if (stat(filename, &fstat) < 0) {
		if (errno != ENOENT) {
			error_msgv(modname, "error: failed to stat(2) the tile backing"
				" store \"%s\".\n", filename);
			free(filename);
			return(False);
		}

		/* Attempt to make the tile internally */
		if (row == hcube->n_rows-1)
			height = hcube->end_height;
		else
			height = hcube->tile_height;

		if (col == hcube->n_cols-1)
			width = hcube->end_width;
		else
			width = hcube->tile_width;
		
		if ((grid = cube_grid_new(width, height, (f32)sheet->hdr->east_sz,
								(f32)sheet->hdr->north_sz, hcube->cache_type)) == NULL) {
			error_msg(modname, "error: failed to create new tile internally.\n");
			free(filename);
			return(False);
		}
#ifdef __DEBUG__
		error_msgv(modname, "debug: created tile (%d, %d) at %d.\n",
			row, col, now);
#endif
	} else {
		if ((fd = fopen(filename, "rb")) == NULL) {
			error_msgv(modname, "error: failed to open \"%s\".\n", filename);
			free(filename);
			return(False);
		}
		if ((grid = cube_grid_deserialise(fd, par->cube)) == NULL) {
			error_msgv(modname, "error: failed to deserialise \"%s\" from "
				"backing store.\n", filename);
			free(filename);
			fclose(fd);
			return(False);
		}
		fclose(fd);
#ifdef __DEBUG__
		error_msgv(modname, "debug: recovered tile (%d, %d) from \"%s\" at"
			" %d.\n", row, col, filename, now);
		{
			u32	nx, ny;
			f32	dx, dy;
			
			cube_grid_get_sizes(grid, &nx, &ny);
			cube_grid_get_spacings(grid, &dx, &dy);
			error_msgv(modname, "debug: grid with (%d, %d) rows/cols and"
				" spacings (%.1f, %.1f) m.\n", ny, nx, dy, dx);
		}
#endif
	}
	free(filename);
	cube_grid_set_cache(grid, hcube->cache_type);
	hcube->tiles[row][col] = grid;
	hcube->stamps[row][col] = now;
	mapsheet_cube_set_status(hcube, row, col, MAP_CUBE_STATUS_CLEAN);
	mapsheet_cube_clear_status(hcube, row, col, MAP_CUBE_STATUS_READ);
#ifdef __DEBUG__
	error_msgv(modname, "debug: tile (%d, %d) status %d.\n", row, col,
		(u32)hcube->status[row][col]);
#endif
	return(True);
}

/* Routine:	mapsheet_cube_write_param
 * Purpose:	Write parameter structure to file
 * Inputs:	param	Pointer to (opaqued) parameter structure
 *			fd		File descriptor to write on
 * Outputs:	True if write was sucessful, otherwise False
 * Comment:	-
 */

Bool mapsheet_cube_write_param(void *param, FILE *f)
{
	HyperParam	*p = (HyperParam*)param;
	
	if (fwrite(p, sizeof(HyperParam), 1, f) != 1) {
		error_msg(modname, "error: failed writing HyperCUBE parameters.\n");
		return(False);
	}
	if (!cube_write_param(p->cube, f)) {
		error_msg(modname, "error: failed writing algorithm parameters.\n");
		return(False);
	}
	return(True);
}

/* Routine:	mapsheet_cube_read_param
 * Purpose:	Read parameter structure from file
 * Inputs:	f	File descriptor to read from
 * Outputs:	Pointer to (opaqued) parameter structure, or NULL on failure.
 * Comment:	This doesn't do any version checking, which may cause problems in
 *			the future.
 */

void *mapsheet_cube_read_param(FILE *f)
{
	HyperParam *rtn;

#ifdef __DEBUG__
	error_msgv(modname, "debug: reading HyperCUBE parameters.\n");
#endif

	if ((rtn = (HyperParam*)calloc(1, sizeof(HyperParam))) == NULL) {
		error_msg(modname, "error: failed getting memory for HyperCUBE"
			" algorithm parameters.\n");
		return(NULL);
	}
	if (fread(rtn, sizeof(HyperParam), 1, f) != 1) {
		error_msg(modname, "error: failed reading HyperCUBE parameters.\n");
		free(rtn);
		return(NULL);
	}
#ifdef __DEBUG__
	error_msgv(modname, "debug: max_memory = %d hyp_hint = %f max_tile = %d"
		" tile_expire = %d cube = %p\n", rtn->max_memory, rtn->hypothesis_hint,
		rtn->max_tile_dimension, rtn->tile_expiry_delta, rtn->cube);
	error_msgv(modname, "debug: reading CUBE parameters.\n");
#endif
	if ((rtn->cube = cube_read_param(f)) == NULL) {
		error_msg(modname, "error: failed to read algorithm parameters.\n");
		cube_release_param(rtn->cube);
		free(rtn);
		return(NULL);
	}
	return(rtn);
}

/* Routine:	mapsheet_cube_describe_param
 * Purpose:	Describe parameter structure for debugging
 * Inputs:	*param	Pointer to what should be a HyperParam structure
 * Outputs:	- (Output to error.c output stream)
 * Comment:	This was used for debugging, and is not normally provided in the
 *			external interface of the module.
 */

extern void cube_describe_params(Cube p);

void mapsheet_cube_describe_params(void *param)
{
	HyperParam *p = (HyperParam*)param;

	error_msgv(modname, "debug: max_memory = %d hyp_hint = %f max_tile = %d"
		" tile_expire = %d cube = %p\n", p->max_memory, p->hypothesis_hint,
		p->max_tile_dimension, p->tile_expiry_delta, p->cube);
	cube_describe_params(p->cube);
}

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

Bool mapsheet_cube_get_hypo(MapSheet sheet, HyperCube *hcube, void *par,
							u8p op)
{
	u32		tile_width, tile_height, tile_npels, tile_row, tile_col, row, col,
			start_row, start_col, width, cols, rows;
	f32		*hyp_buf, f32_no_data_value;
	u8		*base, *start, u8_no_data_value;
	Cube	cparam;
	HyperParam	*hparam = (HyperParam*)par;

#ifdef __DEBUG__
	error_msgv(modname, "debug: started read of hypotheses from grid.\n");
#endif

	/* N.B.: Use external interface to ensure that we get the right width and
	 * height when there is only one tile in the sheet.
	 */
	mapsheet_cube_get_tilesize(hcube, &tile_width, &tile_height);
	tile_npels = tile_width * tile_height;

	cparam = (Cube)hparam->cube;
	width = sheet->hdr->n_east;
	mapsheet_get_invalid(MAP_DATA_U8, &u8_no_data_value);
	mapsheet_get_invalid(MAP_DATA_F32, &f32_no_data_value);
	
	/* Construct the hypothesis buffer space and then fill, clipping above */
	if ((hyp_buf = (f32*)malloc(tile_npels*sizeof(f32))) == NULL) {
		error_msgv(modname, "error: failed getting tile buffer area for"
			"summary output (%dx%d, %d bytes).\n",
			tile_height, tile_width, sizeof(f32)*tile_npels);
		return(False);
	}
	base = op;
	for (tile_row = 0; tile_row < hcube->n_rows;
							++tile_row, base += (tile_height-1)*width) {
		start = base;
		for (tile_col = 0; tile_col < hcube->n_cols; ++tile_col,
												start += tile_width-1) {
/* --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE ---
 * Suspect that this code is not required, since whether the tile has been read
 * or not should be irrelevant in whether to read the data or not --- although
 * it does affect the choice to which tile to remove should more space be
 * required.
			stat = mapsheet_cube_get_status(hcube, tile_row, tile_col);
			if (hcube->cache_type == CUBE_GRID_NHYP &&
				MAP_CUBE_STATUS_IS(stat, MAP_CUBE_STATUS_READ)) continue;
*/

#ifdef __DEBUG__
			error_msgv(modname, "debug: reading tile (%d, %d).\n",
				tile_row, tile_col);
#endif
			if (!mapsheet_cube_ensure_tile(sheet, hcube, tile_row, tile_col, hparam)) {
				error_msgv(modname, "error: failed to load tile (%d, %d).\n",
					tile_row, tile_col);
				free(hyp_buf);
				return(False);
			}
			if (hcube->flush_tiles) {
				error_msgv(modname, "debug: flushing queues in tile (%d,%d).\n",
					tile_row, tile_col);
				cube_grid_flush(hcube->tiles[tile_row][tile_col], cparam);
			}
			cube_grid_get_nhyp(hcube->tiles[tile_row][tile_col], cparam,
							   hyp_buf);
			if (hcube->cache_type == CUBE_GRID_NHYP)
				mapsheet_cube_set_status(hcube, tile_row, tile_col,
					MAP_CUBE_STATUS_READ);
#ifdef __DEBUG__
			error_msgv(modname, "debug: copying tile (%d, %d) to output.\n",
				tile_row, tile_col);
#endif
			cube_grid_get_sizes(hcube->tiles[tile_row][tile_col],
								&cols, &rows);
			if (tile_row == 0) start_row = 0; else start_row = 1;
			for (row = start_row; row < rows; ++row) {
				if (tile_col == 0) start_col = 0; else start_col = 1;
				for (col = start_col; col < cols; ++col) {
					if (hyp_buf[row*cols + col] == f32_no_data_value)
						start[row*width+col] = u8_no_data_value;
					else if (hyp_buf[row*cols + col] > 255.f)
						start[row*width+col] = 255;
					else
						start[row*width + col] = (u8)hyp_buf[row*cols+col];
				}
			}
		}
	}
	free(hyp_buf);
#ifdef __DEBUG__
	error_msgv(modname, "debug: finished read of hypotheses from grid.\n");
#endif
	return(True);
}

/* Routine:	mapsheet_cube_subset_hyp
 * Purpose:	Sub-set a HypGrid structure by removing points outside bounds
 * Inputs:	*grid	The HypGrid to work on
 *			*bnds	Bounds to use in sub-setting
 *			rows,
 *			cols	Size of the HypGrid to work with
 * Outputs:	-
 * Comment:	This checks the coordinates of the hypotheses in the HypGrid, and
 *			removes any which are outwith the specified bounds by releasing the
 *			memory and setting the count to zero.  Note that since the
 *			hypotheses are all assumed to come from the same node, we only
 *			need to check the top one to see whether they are all in the right
 *			bounds or not.
 */

static void mapsheet_cube_subset_hyp(HypGrid *grid, MapBounds *bnds,
									 u32 rows, u32 cols)
{
	u32	row, col;
	f64	east, north;
	
	for (row = 0; row < rows; ++row) {
		for (col = 0; col < cols; ++col) {
			if (grid->counts[row][col] == 0 ||
				grid->hypotheses[row][col] == NULL) continue;
			east = grid->hypotheses[row][col][0].east;
			north = grid->hypotheses[row][col][0].north;
			if (east > bnds->west && east < bnds->east &&
				north > bnds->south && north < bnds->north) continue;
			free(grid->hypotheses[row][col]);
			grid->hypotheses[row][col] = NULL;
			grid->counts[row][col] = 0;
		}
	}
}

/* Routine:	mapsheet_cube_dump_hypo
 * Purpose: Dump hypotheses counts from nodes to GeoZui3D file
 * Inputs:	sheet	MapSheet with which to work
 *			*hcube	HyperCube structure to work from
 *			*par	HyperParam structure
 *			*name	Name of the file to write
 *			*bnds	MapBounds structure to sub-set, or NULL
 * Outputs:	Returns True if the data is extracted, or False otherwise
 *			*name is written, along with auxilliary files as specified in
 *			ccom_general.h
 * Comment:	
 */

Bool mapsheet_cube_dump_hypo(MapSheet sheet, HyperCube *hcube, void *par,
							 char *name, MapBounds *bnds)
{
	u32		tile_row, tile_col, row, col, start_row, start_col, cols, rows;
	f64		west, north, tile_width, tile_height;
	u32		pel_tile_width, pel_tile_height;
	HypGrid	*hgrid;
	Cube	cparam;
	HyperParam	*hparam = (HyperParam*)par;
	GdpFILE		*gdp;
	
#ifdef __DEBUG__
	error_msgv(modname, "debug: started dump of hypotheses from grid.\n");
#endif

	if ((gdp = ccom_open_gdp(name, COLOUR_BY_DEPTH)) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" as GDP file.\n",
			name);
		return(False);
	}

	cparam = (Cube)hparam->cube;
	
	/* N.B.: Use external interface to ensure that we get the right width and
	 * height when there is only one tile in the sheet.
	 */
	mapsheet_cube_get_tilesize(hcube, &pel_tile_width, &pel_tile_height);
	tile_width = (pel_tile_width-1) * sheet->hdr->east_sz;
	tile_height = (pel_tile_height-1) * sheet->hdr->north_sz;
	
	for (tile_row = 0; tile_row < hcube->n_rows; ++tile_row) {
		for (tile_col = 0; tile_col < hcube->n_cols; ++tile_col) {
#ifdef __DEBUG__
			error_msgv(modname, "debug: reading tile (%d, %d).\n",
				tile_row, tile_col);
#endif
			if (!mapsheet_cube_ensure_tile(sheet, hcube, tile_row, tile_col, hparam)) {
				error_msgv(modname, "error: failed to load tile (%d, %d).\n",
					tile_row, tile_col);
				ccom_close_gdp(gdp);
				return(False);
			}
			if (hcube->flush_tiles) {
				error_msgv(modname, "debug: flushing queues in tile (%d,%d).\n",
					tile_row, tile_col);
				cube_grid_flush(hcube->tiles[tile_row][tile_col], cparam);
			}
			west = sheet->hdr->w_bound + tile_col*tile_width;
			north = sheet->hdr->n_bound - tile_row*tile_height;
			if ((hgrid = cube_grid_enumerate_hypotheses(
								hcube->tiles[tile_row][tile_col], cparam,
								west, north)) == NULL) {
				error_msgv(modname, "error: failed to enumerate hypotheses"
					" from tile (%d, %d).\n", tile_row, tile_col);
				ccom_close_gdp(gdp);
				return(False);
			}
								
			if (hcube->cache_type == CUBE_GRID_NHYP)
				mapsheet_cube_set_status(hcube, tile_row, tile_col,
					MAP_CUBE_STATUS_READ);
#ifdef __DEBUG__
			error_msgv(modname, "debug: copying tile (%d, %d) to output.\n",
				tile_row, tile_col);
#endif
			
			cube_grid_get_sizes(hcube->tiles[tile_row][tile_col],
								&cols, &rows);
			if (bnds != NULL)
				mapsheet_cube_subset_hyp(hgrid, bnds, rows, cols);
			
			if (tile_row == 0) start_row = 0; else start_row = 1;
			for (row = start_row; row < rows; ++row) {
				if (tile_col == 0) start_col = 0; else start_col = 1;
				for (col = start_col; col < cols; ++col) {
					if (hgrid->counts[row][col] != 0 &&
						!ccom_write_gdp(gdp, hgrid->hypotheses[row][col],
										hgrid->counts[row][col])) {
						error_msgv(modname, "error: failed writing hypotheses"
							" for node (%d, %d) in tile (%d, %d) to file.\n",
							row, col, tile_row, tile_col);
						cube_grid_release_hypgrid(
							hcube->tiles[tile_row][tile_col], hgrid);
						ccom_close_gdp(gdp);
						return(False);
					}
				}
			}
		}
	}
	ccom_close_gdp(gdp);
#ifdef __DEBUG__
	error_msgv(modname, "debug: finished read of hypotheses from grid.\n");
#endif
	return(True);
}

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

HypoArray *mapsheet_cube_get_hypo_by_node(MapSheet sheet, HyperCube *hcube,
										  void *par, u32 col, u32 row)
{
	u32			tile_row, tile_col;
	f64			west, north, tile_width, tile_height;
	u32			pel_tile_width, pel_tile_height;
	HypoArray	*hyp_array;
	Cube		cparam;
	HyperParam	*hparam = (HyperParam*)par;

	cparam = (Cube)hparam->cube;
	
	/* N.B.: Use external interface to ensure that we get the right width and
	 * height when there is only one tile in the sheet.
	 */
	mapsheet_cube_get_tilesize(hcube, &pel_tile_width, &pel_tile_height);
	tile_width = (pel_tile_width-1) * sheet->hdr->east_sz;
	tile_height = (pel_tile_height-1) * sheet->hdr->north_sz;
	
	tile_col = col / (pel_tile_width-1);
	tile_row = row / (pel_tile_height-1);

	if (!mapsheet_cube_ensure_tile(sheet, hcube, tile_row, tile_col, hparam)) {
		error_msgv(modname, "error: failed to load tile (%d, %d).\n",
			tile_row, tile_col);
		return(NULL);
	}
	if (hcube->flush_tiles) {
		error_msgv(modname, "debug: flushing queues in tile (%d,%d).\n",
			tile_row, tile_col);
		cube_grid_flush(hcube->tiles[tile_row][tile_col], cparam);
	}
	west = sheet->hdr->w_bound + tile_col*tile_width;
	north = sheet->hdr->n_bound - tile_row*tile_height;
	col -= tile_col*(pel_tile_width-1);
	row -= tile_row*(pel_tile_height-1);
	if ((hyp_array = cube_grid_get_hypotheses(
						hcube->tiles[tile_row][tile_col], cparam,
						west, north, col, row)) == NULL) {
		error_msgv(modname, "error: failed to enumerate hypotheses"
			" from tile (%d, %d) at node (%d,%d).\n", tile_row, tile_col, row, col);
		return(NULL);
	}
	return(hyp_array);
}

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

Bool mapsheet_cube_nominate_hypo_by_node(MapSheet sheet, HyperCube *hcube,
										 void *par, u32 col, u32 row, f32 depth)
{
	u32			tile_row, tile_col;
	f64			west, north, tile_width, tile_height;
	u32			pel_tile_width, pel_tile_height;
	Cube		cparam;
	HyperParam	*hparam = (HyperParam*)par;
	Bool		rc;

	cparam = (Cube)hparam->cube;
	
	/* N.B.: Use external interface to ensure that we get the right width and
	 * height when there is only one tile in the sheet.
	 */
	mapsheet_cube_get_tilesize(hcube, &pel_tile_width, &pel_tile_height);
	tile_width = (pel_tile_width-1) * sheet->hdr->east_sz;
	tile_height = (pel_tile_height-1) * sheet->hdr->north_sz;
	
	tile_col = col / (pel_tile_width-1);
	tile_row = row / (pel_tile_height-1);

	if (!mapsheet_cube_ensure_tile(sheet, hcube, tile_row, tile_col, hparam)) {
		error_msgv(modname, "error: failed to load tile (%d, %d).\n",
			tile_row, tile_col);
		return(False);
	}
	if (hcube->flush_tiles) {
		error_msgv(modname, "debug: flushing queues in tile (%d,%d).\n",
			tile_row, tile_col);
		cube_grid_flush(hcube->tiles[tile_row][tile_col], cparam);
	}
	west = sheet->hdr->w_bound + tile_col*tile_width;
	north = sheet->hdr->n_bound - tile_row*tile_height;
	col -= tile_col*(pel_tile_width-1);
	row -= tile_row*(pel_tile_height-1);
	if (rc = cube_grid_nominate_hypothesis(hcube->tiles[tile_row][tile_col],
																col, row, depth)) {
		/* Nomination means that the internal structure of the CubeGrid has
		 * changed, so we need to make sure that it gets marked 'Dirty' and
		 * is flushed to disc if it needs to be replaced.
		 */
		mapsheet_cube_clear_status(hcube, tile_row, tile_col, MAP_CUBE_STATUS_CLEAN);
		mapsheet_cube_clear_status(hcube, tile_row, tile_col, MAP_CUBE_STATUS_READ);
	}
	return(rc);
}

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

Bool mapsheet_cube_unnominate_hypo_by_node(MapSheet sheet, HyperCube *hcube,
										 void *par, u32 col, u32 row)
{
	u32			tile_row, tile_col;
	f64			west, north, tile_width, tile_height;
	u32			pel_tile_width, pel_tile_height;
	Cube		cparam;
	HyperParam	*hparam = (HyperParam*)par;
	Bool		rc;

	cparam = (Cube)hparam->cube;
	
	/* N.B.: Use external interface to ensure that we get the right width and
	 * height when there is only one tile in the sheet.
	 */
	mapsheet_cube_get_tilesize(hcube, &pel_tile_width, &pel_tile_height);
	tile_width = (pel_tile_width-1) * sheet->hdr->east_sz;
	tile_height = (pel_tile_height-1) * sheet->hdr->north_sz;
	
	tile_col = col / (pel_tile_width-1);
	tile_row = row / (pel_tile_height-1);

	if (!mapsheet_cube_ensure_tile(sheet, hcube, tile_row, tile_col, hparam)) {
		error_msgv(modname, "error: failed to load tile (%d, %d).\n",
			tile_row, tile_col);
		return(False);
	}
	if (hcube->flush_tiles) {
		error_msgv(modname, "debug: flushing queues in tile (%d,%d).\n",
			tile_row, tile_col);
		cube_grid_flush(hcube->tiles[tile_row][tile_col], cparam);
	}
	west = sheet->hdr->w_bound + tile_col*tile_width;
	north = sheet->hdr->n_bound - tile_row*tile_height;
	col -= tile_col*(pel_tile_width-1);
	row -= tile_row*(pel_tile_height-1);
	if (rc = cube_grid_unnominate_hypothesis(hcube->tiles[tile_row][tile_col], col, row)) {
		/* Reset of nomination means that the internal structure of the CubeGrid has
		 * changed, so we need to make sure that it gets marked 'Dirty' and
		 * is flushed to disc if it needs to be replaced.
		 */
		mapsheet_cube_clear_status(hcube, tile_row, tile_col, MAP_CUBE_STATUS_CLEAN);
		mapsheet_cube_clear_status(hcube, tile_row, tile_col, MAP_CUBE_STATUS_READ);
	}
	return(rc);
}

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

Bool mapsheet_cube_remove_hypo_by_node(MapSheet sheet, HyperCube *hcube,
										 void *par, u32 col, u32 row, f32 depth)
{
	u32			tile_row, tile_col;
	f64			west, north, tile_width, tile_height;
	u32			pel_tile_width, pel_tile_height;
	Cube		cparam;
	HyperParam	*hparam = (HyperParam*)par;
	Bool		rc;

	cparam = (Cube)hparam->cube;
	
	/* N.B.: Use external interface to ensure that we get the right width and
	 * height when there is only one tile in the sheet.
	 */
	mapsheet_cube_get_tilesize(hcube, &pel_tile_width, &pel_tile_height);
	tile_width = (pel_tile_width-1) * sheet->hdr->east_sz;
	tile_height = (pel_tile_height-1) * sheet->hdr->north_sz;
	
	tile_col = col / (pel_tile_width-1);
	tile_row = row / (pel_tile_height-1);

	if (!mapsheet_cube_ensure_tile(sheet, hcube, tile_row, tile_col, hparam)) {
		error_msgv(modname, "error: failed to load tile (%d, %d).\n",
			tile_row, tile_col);
		return(False);
	}
	if (hcube->flush_tiles) {
		error_msgv(modname, "debug: flushing queues in tile (%d,%d).\n",
			tile_row, tile_col);
		cube_grid_flush(hcube->tiles[tile_row][tile_col], cparam);
	}
	west = sheet->hdr->w_bound + tile_col*tile_width;
	north = sheet->hdr->n_bound - tile_row*tile_height;
	col -= tile_col*(pel_tile_width-1);
	row -= tile_row*(pel_tile_height-1);
	if (rc = cube_grid_remove_hypothesis(hcube->tiles[tile_row][tile_col],
																col, row, depth)) {
		/* Removal means that the internal structure of the CubeGrid has
		 * changed, so we need to make sure that it gets marked 'Dirty' and
		 * is flushed to disc if it needs to be replaced.
		 */
		mapsheet_cube_clear_status(hcube, tile_row, tile_col, MAP_CUBE_STATUS_CLEAN);
		mapsheet_cube_clear_status(hcube, tile_row, tile_col, MAP_CUBE_STATUS_READ);
	}
	return(rc);
}

/* Routine:	mapsheet_cube_extend_summary
 * Purpose:	Write extended elements from private data store to summary directory
 * Inputs:	*sheet	Sheet to write from
 *			*dir	Name of the directory to extend elements into
 * Outputs:	True on success, otherwise False
 * Comment:	This writes out a set of images in TIFF format which are only found
 *			in the nodal estimation method.  The actual components which are
 *			written depend on the algorithm currently in use ...
 */

Bool mapsheet_cube_extend_summary(MapSheet sheet, char *dir)
{
	HyperCube	*hcube = (HyperCube*)sheet->grid->hcube_grid;
	HyperParam	*hparam = (HyperParam*)sheet->param->hcube_param;
	u8			*hypotheses;
	u32			npels = sheet->hdr->n_east * sheet->hdr->n_north;
	TIFFFlags	flgs = { False, False, False, False }; /* Convert to U8 */
		
	/* Construct the hypothesis buffer space and then fill, clipping above */
	if ((hypotheses = (u8*)malloc(npels)) == NULL) {
		error_msgv(modname, "error: failed getting hypothesis count buffer"
			" for summary output (%d bytes).\n", npels);
		return(False);
	}
	if (!mapsheet_cube_get_hypo(sheet, hcube, hparam, hypotheses)) {
		error_msg(modname, "error: failed getting hypothesis data.\n");
		free(hypotheses);
		return(False);
	}
	if (!ccom_write_tiff(dir, "hypercube-nhyp.tif", hypotheses, TIFF_U8,
			sheet->hdr->n_north, sheet->hdr->n_east, &flgs)) {
		error_msgv(modname, "error: failed writing \"%s/hypercube-nhyp.tif\" as"
			" hypothesis count data.\n", dir);
		free(hypotheses);
		return(False);
	}
	free(hypotheses);
	return(True);
}

/* Routine:	mapsheet_cube_release
 * Purpose:	Release all dynamically allocated memory associated with a HyperCUBE
 * Inputs:	*hcube	HyperCube to work with
 * Outputs:	-
 * Comment:	-
 */

void mapsheet_cube_release(HyperCube *hcube)
{
	u32	tile_row, tile_col;
	
	if (hcube->tiles != NULL && hcube->tiles[0] != NULL) {
		for (tile_row = 0; tile_row < hcube->n_rows; ++tile_row)
			for (tile_col = 0; tile_col < hcube->n_cols; ++tile_col)
				cube_grid_release(hcube->tiles[tile_row][tile_col]);
		free(hcube->tiles[0]);
	}
	free(hcube->tiles);
	hcube->tiles = NULL;
	if (hcube->stamps != NULL)
		free(hcube->stamps[0]);
	free(hcube->stamps);
	hcube->stamps = NULL;
	if (hcube->status != NULL)
		free(hcube->status[0]);
	free(hcube->status);
	hcube->status = NULL;
	free(hcube->backing_store);
	hcube->backing_store = NULL;
	free(hcube);					/* Fixes leak reported by Ding Zheng at Reson bv */
}

/* Routine:	mapsheet_cube_deserialise
 * Purpose:	De-serialise a HyperCUBE grid
 * Inputs:	*dirname	Directory containing the HyperCUBE
 *			sheet		Mapsheet to work on
 * Outputs:	True if sheet was read, otherwise False.
 * Comment:	In fact, there is nothing much to be done here ... the HyperCUBE
 *			structure is demand loaded anyway, so that apart from pre-loading
 *			a tile, there's nothing to be done.  We don't know where we are,
 *			so we can't pre-load a tile.
 */

Bool mapsheet_cube_deserialise(const char *dirname, MapSheet sheet)
{
	return(True);
}

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

Bool mapsheet_cube_serialise(MapSheet sheet, char *dirname, HyperCube *hcube,
							 void *param)
{
	HyperParam	*p = (HyperParam*)param;
	u32			row, col;
	MapCubeStatus	stat;
	char		*backing_store = hcube->backing_store;
	
	for (row = 0; row < hcube->n_rows; ++row) {
		for (col = 0; col < hcube->n_cols; ++col) {
			stat = mapsheet_cube_get_status(hcube, row, col);
			if (hcube->tiles[row][col] != NULL &&
				!MAP_CUBE_STATUS_IS(stat, MAP_CUBE_STATUS_CLEAN)) {
				if (!mapsheet_cube_flush(hcube, row, col, p)) {
					error_msgv(modname, "error: failed to flush tile (%d, %d)"
						" to disc.\n", row, col);
					return(False);
				}
			}
		}
	}
	
	/* Check if we need to redirect the output to another directory */
	if (strcmp(dirname, hcube->backing_store) == 0) return(True);
	
	/* Redirect by reading each tile from input, then setting the tile buffer
	 * backing store to the output before writing
	 */
	for (row = 0; row < hcube->n_rows; ++row)
		for (col = 0; col < hcube->n_cols; ++col) {
			if (!mapsheet_cube_ensure_tile(sheet, hcube, row, col, p)) {
				error_msgv(modname, "error: failed to load tile (%d, %d)"
					" from disc.\n", row, col);
				return(False);
			}
			stat = mapsheet_cube_get_status(hcube, row, col);
			hcube->backing_store = dirname;	/* Reset to re-direct output */
			if (!mapsheet_cube_flush(hcube, row, col, p)) {
				error_msgv(modname, "error: failed to flush tile (%d, %d)"
					" to disc.\n", row, col);
				return(False);
			}
			hcube->backing_store = backing_store;	/* Back to store */
			if (!MAP_CUBE_STATUS_IS(stat, MAP_CUBE_STATUS_CLEAN))
				mapsheet_cube_clear_status(hcube, row, col, MAP_CUBE_STATUS_CLEAN);
				/* We reset the tile's status to dirty if it was before the
				 * flush to another location, since it is not clean wrt the
				 * backing store normally used.
				 */
		}
	return(True);
}

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

HyperCube *mapsheet_cube_alloc_grid(MapSheet sheet, void *param, char *dirname)
{
	HyperCube	*rtn;
	HyperParam	*p = (HyperParam*)param;
	u32			row, mem_per_sheet, side;
	
	if ((rtn = (HyperCube*)calloc(1, sizeof(HyperCube))) == NULL) {
		error_msgv(modname, "error: failed to allocate memory for HyperCUBE"
			" structure (%d bytes).\n", sizeof(HyperCube));
		return(NULL);
	}
	
	mem_per_sheet = p->max_memory / 4;	/* Worst case expected to be 4 */
	side = cube_grid_estimate_size(mem_per_sheet, p->hypothesis_hint,
									p->prob_use, p->cube);
	if (side >= p->max_tile_dimension)
		side = p->max_tile_dimension;

	if ((side % 2) == 0) ++side; /* Ensure side is an odd number for CUBE */

#ifdef __DEBUG__
	error_msgv(modname, "debug: memory per sheet = %d bytes, so"
		" %dx%d nodes/sheet.\n", mem_per_sheet, side, side);
#endif

	if (sheet->hdr->n_north <= side) {
		/* Very small mapsheet --- less than the nominal cache tile height.
		 * set this up as one row
		 */
		rtn->n_rows = 1;
		rtn->tile_height = 0;
		rtn->end_height = sheet->hdr->n_north;
	} else {
/*
		rtn->n_rows = (u32)(sheet->hdr->n_north / side);
		if ((sheet->hdr->n_north % side) != 0) ++rtn->n_rows;
		rtn->tile_height = side;
		rtn->end_height = sheet->hdr->n_north - (rtn->n_rows-1)*(side-1);
*/
		/* This code sizes the number of tiles which are represented
		 * fully in the grid, and then sizes the remaining tile at the
		 * end of the grid, if there is one.  Note that we need to be
		 * particularly careful with the sizes, since there is a one-node
		 * overlap region between the tiles, so that the effective span
		 * of all tiles after the first row (resp. column) is one node
		 * less than expected.  This can have wierd side-effects if you
		 * get it wrong, including the end tile being slightly larger than
		 * the base tiles! --- brc, 2002-08-21.
		 */
		rtn->n_rows = (u32)((sheet->hdr->n_north - side) / (side-1)) + 1;
		rtn->tile_height = side;
		if (((sheet->hdr->n_north - side) % (side-1)) != 0) {
			rtn->end_height = sheet->hdr->n_north - side - (side-1)*(rtn->n_rows-1) + 1;
			if ((rtn->end_height % 2) == 0) ++rtn->end_height;
			++rtn->n_rows;
		} else
			rtn->end_height = side;
	}
	if (sheet->hdr->n_east <= side) {
		/* Very small mapsheet --- less than the nominal cache tile width.
		 * set this up as one column
		 */
		rtn->n_cols = 1;
		rtn->tile_width = 0;
		rtn->end_width = sheet->hdr->n_east;
	} else {
/*
		rtn->n_cols = (u32)(sheet->hdr->n_east / side);
		if ((sheet->hdr->n_east % side) != 0) ++rtn->n_cols;
		rtn->tile_width = side;
		rtn->end_width = sheet->hdr->n_east - (rtn->n_cols-1)*(side-1);
*/
		/* Sizing code in the same way as above */
		rtn->n_cols = (u32)((sheet->hdr->n_east - side) / (side - 1)) + 1;
		rtn->tile_width = side;
		if (((sheet->hdr->n_east - side) % (side - 1)) != 0) {
			rtn->end_width = sheet->hdr->n_east - side - (side - 1)*(rtn->n_cols-1) + 1;
			if ((rtn->end_width % 2) == 0) ++rtn->end_width;
			++rtn->n_cols;
		} else
			rtn->end_width = side;
	}
	rtn->cache_type = CUBE_GRID_DEPTH;

#ifdef __DEBUG__
	error_msgv(modname, "debug: fixed at %d rows, %d cols of CubeGrids.\n",
		rtn->n_rows, rtn->n_cols);
	error_msgv(modname, "debug: main tiles are (%d, %d) nodes, end tiles are"
		" (%d, %d) nodes.\n", rtn->tile_height, rtn->tile_width,
		rtn->end_height, rtn->end_width);
#endif

	if ((rtn->backing_store = strdup(dirname)) == NULL) {
		error_msgv(modname, "error: failed to get memory for mapsheet backing"
			" store directory name (%d bytes).\n", strlen(dirname)+1);
		mapsheet_cube_release(rtn);
		return(NULL);
	}
	if (!mapsheet_make_directory(dirname)) {
		error_msgv(modname, "error: failed to make backing store directory"
			" \"%s\".\n", dirname);
		mapsheet_cube_release(rtn);
		return(NULL);
	}
	
	if ((rtn->tiles = (CubeGrid**)calloc(rtn->n_rows, sizeof(CubeGrid*))) == NULL ||
		(rtn->tiles[0] = (CubeGrid*)calloc(rtn->n_rows*rtn->n_cols, sizeof(CubeGrid))) == NULL) {
		error_msgv(modname, "error: failed to allocate memory for HyperCUBE"
			" grid (%dx%d).\n", rtn->n_rows, rtn->n_cols);
		mapsheet_cube_release(rtn);
		return(NULL);
	}
	if ((rtn->stamps = (u32**)calloc(rtn->n_rows, sizeof(u32*))) == NULL ||
		(rtn->stamps[0] = (u32*)calloc(rtn->n_rows*rtn->n_cols, sizeof(u32))) == NULL) {
		error_msgv(modname, "error: failed to allocate memory for HyperCUBE"
			" timestamps (%dx%d).\n", rtn->n_rows, rtn->n_cols);
		mapsheet_cube_release(rtn);
		return(NULL);
	}
	if ((rtn->status = (u8**)calloc(rtn->n_rows, sizeof(u8*))) == NULL ||
		(rtn->status[0] = (u8*)calloc(rtn->n_rows*rtn->n_cols, sizeof(u8))) == NULL) {
		error_msgv(modname, "error: failed to allocate memory for HyperCUBE"
			" status bytes (%dx%d).\n", rtn->n_rows, rtn->n_cols);
		mapsheet_cube_release(rtn);
		return(NULL);
	}
	for (row = 1; row < rtn->n_rows; ++row) {
		rtn->tiles[row] = rtn->tiles[row-1] + rtn->n_cols;
		rtn->stamps[row] = rtn->stamps[row-1] + rtn->n_cols;
		rtn->status[row] = rtn->status[row-1] + rtn->n_cols;
	}
	rtn->flush_tiles = False;
	return(rtn);
}

/* Routine:	mapsheet_cube_get_depth
 * Purpose:	Extract depth using the selected multiple hypothesis disambiguation
 *			algorithm
 * Inputs:	sheet	MapSheet to use for parameters
 *			*ip		Pointer to HyperCube structure to work
 *			*par	Parameters for the algorithm
 * Outputs:	op		Pointer to linear output space
 * Comment:	This acts as a simple dispatching switch for the three extraction
 *			algorithms.
 */

Bool mapsheet_cube_get_depth(MapSheet sheet, HyperCube *ip, void *par,
							  f32p op)
{
	HyperParam	*p = ((HyperParam*)par);
	u32			tile_width, tile_height, tile_row, tile_col, row, col,
				start_row, start_col, cols, rows, sheet_width, size;
	f32			*buffer, *start, *base;

#ifdef __DEBUG__
	error_msgv(modname, "debug: starting read-back for depth surface.\n");
#endif

	mapsheet_cube_get_tilesize(ip, &tile_width, &tile_height);
	size = sizeof(f32)*tile_width*tile_height;

	if ((buffer = (f32p)malloc(size)) == NULL) {
		error_msgv(modname, "error: failed to get memory for tile read-back"
			" buffer (%dx%d, %d bytes).\n", tile_height, tile_width, size);
		return(False);
	}
	base = op;
	sheet_width = sheet->hdr->n_east;
	for (tile_row = 0; tile_row < ip->n_rows;
						++tile_row, base += (tile_height-1)*sheet_width) {
		start = base;
		for (tile_col = 0; tile_col < ip->n_cols; ++tile_col,
												  start += tile_width-1) {
/* --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE ---
 * Suspect that this code is not required, since whether the tile has been read
 * or not should be irrelevant in whether to read the data or not --- although
 * it does affect the choice to which tile to remove should more space be
 * required.
			stat = mapsheet_cube_get_status(ip, tile_row, tile_col);
			if (ip->cache_type == CUBE_GRID_DEPTH &&
				MAP_CUBE_STATUS_IS(stat, MAP_CUBE_STATUS_READ)) continue;
*/
			if (!mapsheet_cube_ensure_tile(sheet, ip, tile_row, tile_col, p)) {
				error_msgv(modname, "error: failed to ensure tile (%d,%d)"
					" was in cache.\n", tile_row, tile_col);
				free(buffer);
				return(False);
			}
			if (ip->flush_tiles) {
				error_msgv(modname, "debug: flushing queues in tile (%d,%d).\n",
					tile_row, tile_col);
				cube_grid_flush(ip->tiles[tile_row][tile_col], p->cube);
			}
#ifdef __DEBUG__
			error_msgv(modname, "debug: reading tile (%d, %d) from CubeGrid.\n",
				tile_row, tile_col);
#endif
			if (!cube_grid_get_depth(ip->tiles[tile_row][tile_col],
									 p->cube, buffer)) {
				error_msgv(modname, "error: failed to extract depth surface"
					" for tile (%d, %d).\n", tile_row, tile_col);
				free(buffer);
				return(False);
			}
			if (ip->cache_type == CUBE_GRID_DEPTH)
				mapsheet_cube_set_status(ip, tile_row, tile_col,
										 MAP_CUBE_STATUS_READ);
			cube_grid_get_sizes(ip->tiles[tile_row][tile_col], &cols, &rows);
			if (tile_row == 0) start_row = 0; else start_row = 1;
#ifdef __DEBUG__
			error_msgv(modname, "debug: copying tile (%d, %d) into output"
				" data buffer.\n", tile_row, tile_col);
#endif
			for (row = start_row; row < rows; ++row) {
				if (tile_col == 0) start_col = 0; else start_col = 1;
/*				memcpy(start+row*sheet_width+start_col,
					   buffer+row*cols+start_col,
					   sizeof(f32)*(cols-start_col));
*/
				for (col = start_col; col < cols; ++col)
					start[row*sheet_width + col] = buffer[row*cols + col];

			}
		}
	}
	free(buffer);

#ifdef __DEBUG__
	error_msgv(modname, "debug: finished read-back of depth surface.\n");
#endif

	return(True);
}

/* Routine:	mapsheet_cube_get_uncertainty
 * Purpose:	Extract uncertianty using the selected multiple hypothesis
 *			disambiguation algorithm
 * Inputs:	sheet	MapSheet to use for parameters
 *			*ip		Pointer to HyperCube structure to work
 *			*par	Parameters for the algorithm
 * Outputs:	op		Pointer to linear output space
 * Comment:	This acts as a simple dispatching switch for the three extraction
 *			algorithms.
 */

Bool mapsheet_cube_get_uncertainty(MapSheet sheet, HyperCube *ip, void *par,
							  		f32p op)
{
	HyperParam	*p = ((HyperParam*)par);
	u32			tile_width, tile_height, tile_row, tile_col, row, col,
				start_row, start_col, cols, rows, sheet_width, size;
	f32			*buffer, *start, *base;

#ifdef __DEBUG__
	error_msgv(modname, "debug: starting read-back for uncertainty surface.\n");
#endif

	mapsheet_cube_get_tilesize(ip, &tile_width, &tile_height);
	size = sizeof(f32)*tile_width*tile_height;

	if ((buffer = (f32p)malloc(size)) == NULL) {
		error_msgv(modname, "error: failed to get memory for tile read-back"
			" buffer (%dx%d, %d bytes).\n", tile_height, tile_width, size);
		return(False);
	}
	base = op;
	sheet_width = sheet->hdr->n_east;
	for (tile_row = 0; tile_row < ip->n_rows;
						++tile_row, base += (tile_height-1)*sheet_width) {
		start = base;
		for (tile_col = 0; tile_col < ip->n_cols; ++tile_col,
												  start += tile_width-1) {
/* --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE ---
 * Suspect that this code is not required, since whether the tile has been read
 * or not should be irrelevant in whether to read the data or not --- although
 * it does affect the choice to which tile to remove should more space be
 * required.
			stat = mapsheet_cube_get_status(ip, tile_row, tile_col);
			if (ip->cache_type == CUBE_GRID_UNCT &&
				MAP_CUBE_STATUS_IS(stat, MAP_CUBE_STATUS_READ)) continue;
*/
			if (!mapsheet_cube_ensure_tile(sheet, ip, tile_row, tile_col, p)) {
				error_msgv(modname, "error: failed to ensure tile (%d,%d)"
					" was in cache.\n", tile_row, tile_col);
				free(buffer);
				return(False);
			}
			if (ip->flush_tiles) {
				error_msgv(modname, "debug: flushing queues in tile (%d,%d).\n",
					tile_row, tile_col);
				cube_grid_flush(ip->tiles[tile_row][tile_col], p->cube);
			}
#ifdef __DEBUG__
			error_msgv(modname, "debug: reading tile (%d, %d) from CubeGrid.\n",
				tile_row, tile_col);
#endif
			if (!cube_grid_get_unct(ip->tiles[tile_row][tile_col],
									p->cube, buffer)) {
				error_msgv(modname, "error: failed to extract unct surface"
					" for tile (%d, %d).\n", tile_row, tile_col);
				free(buffer);
				return(False);
			}
			if (ip->cache_type == CUBE_GRID_UNCT)
				mapsheet_cube_set_status(ip, tile_row, tile_col,
										 MAP_CUBE_STATUS_READ);
			cube_grid_get_sizes(ip->tiles[tile_row][tile_col], &cols, &rows);
#ifdef __DEBUG__
			error_msgv(modname, "debug: copy tile (%d, %d) to output.\n",
				tile_row, tile_col);
#endif
			if (tile_row == 0) start_row = 0; else start_row = 1;
			for (row = start_row; row < rows; ++row) {
				if (tile_col == 0) start_col = 0; else start_col = 1;
				for (col = start_col; col < cols; ++col)
					start[row*sheet_width + col] = buffer[row*cols + col];
				
			}
		}
	}
	free(buffer);
#ifdef __DEBUG__
	error_msgv(modname, "debug: finished read-back for uncertainty surface.\n");
#endif
	return(True);
}

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

Bool mapsheet_cube_get_hypo_strength(MapSheet sheet, HyperCube *ip, void *par,
							  		f32p op)
{
	HyperParam	*p = ((HyperParam*)par);
	u32			tile_width, tile_height, tile_row, tile_col, row, col,
				start_row, start_col, cols, rows, sheet_width, size;
	f32			*buffer, *start, *base;

#ifdef __DEBUG__
	error_msgv(modname, "debug: starting read-back for hypo strength surface.\n");
#endif

	mapsheet_cube_get_tilesize(ip, &tile_width, &tile_height);
	size = sizeof(f32)*tile_width*tile_height;

	if ((buffer = (f32p)malloc(size)) == NULL) {
		error_msgv(modname, "error: failed to get memory for tile read-back"
			" buffer (%dx%d, %d bytes).\n", tile_height, tile_width, size);
		return(False);
	}
	base = op;
	sheet_width = sheet->hdr->n_east;
	for (tile_row = 0; tile_row < ip->n_rows;
						++tile_row, base += (tile_height-1)*sheet_width) {
		start = base;
		for (tile_col = 0; tile_col < ip->n_cols; ++tile_col,
												  start += tile_width-1) {
/* --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE ---
 * Suspect that this code is not required, since whether the tile has been read
 * or not should be irrelevant in whether to read the data or not --- although
 * it does affect the choice to which tile to remove should more space be
 * required.
			stat = mapsheet_cube_get_status(ip, tile_row, tile_col);
			if (ip->cache_type == CUBE_GRID_RATIO &&
				MAP_CUBE_STATUS_IS(stat, MAP_CUBE_STATUS_READ)) continue;
*/
			if (!mapsheet_cube_ensure_tile(sheet, ip, tile_row, tile_col, p)) {
				error_msgv(modname, "error: failed to ensure tile (%d,%d)"
					" was in cache.\n", tile_row, tile_col);
				free(buffer);
				return(False);
			}
			if (ip->flush_tiles) {
				error_msgv(modname, "debug: flushing queues in tile (%d,%d).\n",
					tile_row, tile_col);
				cube_grid_flush(ip->tiles[tile_row][tile_col], p->cube);
			}
#ifdef __DEBUG__
			error_msgv(modname, "debug: reading tile (%d, %d) from CubeGrid.\n",
				tile_row, tile_col);
#endif
			if (!cube_grid_get_ratio(ip->tiles[tile_row][tile_col],
									p->cube, buffer)) {
				error_msgv(modname, "error: failed to extract hypo strength"
					" surface for tile (%d, %d).\n", tile_row, tile_col);
				free(buffer);
				return(False);
			}
			if (ip->cache_type == CUBE_GRID_RATIO)
				mapsheet_cube_set_status(ip, tile_row, tile_col,
										 MAP_CUBE_STATUS_READ);
			cube_grid_get_sizes(ip->tiles[tile_row][tile_col], &cols, &rows);
#ifdef __DEBUG__
			error_msgv(modname, "debug: copy tile (%d, %d) to output.\n",
				tile_row, tile_col);
#endif
			if (tile_row == 0) start_row = 0; else start_row = 1;
			for (row = start_row; row < rows; ++row) {
				if (tile_col == 0) start_col = 0; else start_col = 1;
				for (col = start_col; col < cols; ++col)
					start[row*sheet_width + col] = buffer[row*cols + col];
				
			}
		}
	}
	free(buffer);
#ifdef __DEBUG__
	error_msgv(modname, "debug: finished read-back for hypo strength surface.\n");
#endif
	return(True);
}

/* Routine:	mapsheet_cube_get_depth_unct
 * Purpose:	Get both depth and uncertainty surface using the selected multi-
 *			hypothesis disambiguation algorithm
 * Inputs:	sheet	MapSheet to work from
 *			*ip		Pointer to HyperCUBE structure
 *			*par	Parameter structure for the algorithm
 * Outputs:	*depth	Linear (row major) array for depth output
 *			*unct	Linear (row major) array for uncertanty output
 * Comment:	This is basically a dispatch switch for the various algorithm
 *			implementations.  Note that it is faster (and sometimes very
 *			significantly faster) to use this if you are going to need both
 *			surfaces, since it only has to do the sorting/context search once
 *			per node, rather than twice.  However, the down-side is that it is
 *			not possible for the system to guarantee a cache read, since we
 *			can only cache one result at a time.  Therefore, there is no
 *			pre-read speed-up.
 */

Bool mapsheet_cube_get_depth_unct(MapSheet sheet, HyperCube *ip, void *par,
								   f32p depth, f32p unct)
{
	HyperParam	*p = ((HyperParam*)par);
	u32			tile_width, tile_height, tile_row, tile_col, row, col,
				start_row, start_col, cols, rows, sheet_width, size;
	f32			*dbuffer, *dstart, *dbase, *ubuffer, *ustart, *ubase;
	
	mapsheet_cube_get_tilesize(ip, &tile_width, &tile_height);
	size = sizeof(f32)*tile_width*tile_height;
	sheet_width = sheet->hdr->n_east;

	if ((dbuffer = (f32p)malloc(size)) == NULL ||
		(ubuffer = (f32p)malloc(size)) == NULL) {
		error_msgv(modname, "error: failed to get memory for tile read-back"
			" buffer (%dx%d, %d bytes).\n", tile_height, tile_width, size);
		return(False);
	}
	dbase = depth; ubase = unct;
	sheet_width = sheet->hdr->n_east;
	for (tile_row = 0; tile_row < ip->n_rows; ++tile_row) {
		dstart = dbase;
		ustart = ubase;
		for (tile_col = 0; tile_col < ip->n_cols; ++tile_col) {
			if (!mapsheet_cube_ensure_tile(sheet, ip, tile_row, tile_col, p)) {
				error_msgv(modname, "error: failed to ensure tile (%d,%d)"
					" was in cache.\n", tile_row, tile_col);
				free(dbuffer); free(ubuffer);
				return(False);
			}
			if (ip->flush_tiles) {
				error_msgv(modname, "debug: flushing queues in tile (%d,%d).\n",
					tile_row, tile_col);
				cube_grid_flush(ip->tiles[tile_row][tile_col], p->cube);
			}
			if (!cube_grid_get_depth_unct(ip->tiles[tile_row][tile_col],
									p->cube, dbuffer, ubuffer)) {
				error_msgv(modname, "error: failed to extract depth/unct surface"
					" for tile (%d, %d).\n", tile_row, tile_col);
				free(dbuffer); free(ubuffer);
				return(False);
			}
			cube_grid_get_sizes(ip->tiles[tile_row][tile_col], &cols, &rows);
			if (tile_row == 0) start_row = 0; else start_row = 1;
			for (row = start_row; row < rows; ++row) {
				if (tile_col == 0) start_col = 0; else start_col = 1;
				for (col = start_col; col < cols; ++col) {
					dstart[row*sheet_width + col] = dbuffer[row*cols + col];
					ustart[row*sheet_width + col] = ubuffer[row*cols + col];
				}
				
			}
			dstart += cols-1;
			ustart += cols-1;
		}
		dbase += (rows-1)*sheet_width;
		ubase += (rows-1)*sheet_width;
	}
	free(dbuffer); free(ubuffer);
	return(True);
}

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

Bool mapsheet_cube_get_data(MapSheet sheet, HyperCube *ip, void *par,
						    f32p depth, f32p unct, f32p hypstr)
{
	HyperParam	*p = ((HyperParam*)par);
	u32			tile_width, tile_height, tile_row, tile_col, row, col,
				start_row, start_col, cols, rows, sheet_width, size;
	f32			*dbuffer, *dstart, *dbase, *ubuffer, *ustart, *ubase,
				*hbuffer, *hstart, *hbase;
	
	mapsheet_cube_get_tilesize(ip, &tile_width, &tile_height);
	size = sizeof(f32)*tile_width*tile_height;

	if ((dbuffer = (f32p)malloc(size)) == NULL ||
		(ubuffer = (f32p)malloc(size)) == NULL ||
		(hbuffer = (f32p)malloc(size)) == NULL) {
		error_msgv(modname, "error: failed to get memory for tile read-back"
			" buffer (%dx%d, %d bytes).\n", tile_height, tile_width, size);
		return(False);
	}
	dbase = depth; ubase = unct; hbase = hypstr;
	sheet_width = sheet->hdr->n_east;
	for (tile_row = 0; tile_row < ip->n_rows; ++tile_row) {
		dstart = dbase;
		ustart = ubase;
		hstart = hbase;
		for (tile_col = 0; tile_col < ip->n_cols; ++tile_col) {
			if (!mapsheet_cube_ensure_tile(sheet, ip, tile_row, tile_col, p)) {
				error_msgv(modname, "error: failed to ensure tile (%d,%d)"
					" was in cache.\n", tile_row, tile_col);
				free(dbuffer); free(ubuffer);
				return(False);
			}
			if (ip->flush_tiles) {
				error_msgv(modname, "debug: flushing queues in tile (%d,%d).\n",
					tile_row, tile_col);
				/* Spot the major caveat here ... in fact, it's more like a
				 * gotcha than a caveat it's so big.  We flush the tiles here,
				 * but don't reset the CLEAN flag on the tile's status, so that
				 * it doesn't get written back into the backing store when
				 * we're done.  This means that we can flush the tiles multiple
				 * times without affecting the data, and hence can use it
				 * to get multiple surfaces.  Of course, this means that you
				 * have to be *very* careful in using this technique mid-way
				 * through a survey effort.
				 */
				cube_grid_flush(ip->tiles[tile_row][tile_col], p->cube);
			}
			if (!cube_grid_get_data(ip->tiles[tile_row][tile_col],
									p->cube, dbuffer, ubuffer, hbuffer)) {
				error_msgv(modname, "error: failed to extract depth/unct surface"
					" for tile (%d, %d).\n", tile_row, tile_col);
				free(dbuffer); free(ubuffer); free(hbuffer);
				return(False);
			}
			cube_grid_get_sizes(ip->tiles[tile_row][tile_col], &cols, &rows);
			if (tile_row == 0) start_row = 0; else start_row = 1;
			for (row = start_row; row < rows; ++row) {
				if (tile_col == 0) start_col = 0; else start_col = 1;
				for (col = start_col; col < cols; ++col) {
					dstart[row*sheet_width + col] = dbuffer[row*cols + col];
					ustart[row*sheet_width + col] = ubuffer[row*cols + col];
					hstart[row*sheet_width + col] = hbuffer[row*cols + col];
				}
				
			}
			dstart += cols-1;
			ustart += cols-1;
			hstart += cols-1;
		}
		dbase += (rows-1)*sheet_width;
		ubase += (rows-1)*sheet_width;
		hbase += (rows-1)*sheet_width;
	}
	free(dbuffer); free(ubuffer); free(hbuffer);
	return(True);
}

/* Routine:	mapsheet_cube_insert_depths
 * Purpose:	Add a sequence of depth estimates to the mapsheet
 * Inputs:	sheet	MapSheet to work on
 *			*ip		Pointer to HyperCUBE structure to use
 *			*par	Pointer to (opaqued) parameter structure for algorithm
 *			stream	SoundingStream which supplied the data
 *			*data	Pointer to the soundings which should be added
 *			nsnds	Number of soundings in this batch
 *			nused	Number of soundings used for incorporation into grid
 * Outputs:	True if data was added correctly, otherwise False
 * Comment:	This code integrates the soundings presented into the current mapsheet.
 *			Note that in the newer version of the algorithm, soundings are queued in
 *			a median pre-filter before going to the KF so there is a delay between
 *			presentation and readback effect.  Note that we assume at this point that
 *			the soundings presented have suitably defined error variances in depth
 *			and position associated with them, so that we don't have to try to re-
 *			generate them here.
 */

Bool mapsheet_cube_insert_depths(MapSheet sheet, HyperCube *ip,
								  void *par, SoundingStream stream,
								  Sounding *data, u32 nsnds, u32 *nused)
{
	HyperParam	*p = (HyperParam*)par;
	u32			pel_tile_width, pel_tile_height, tile_row, tile_col, snd,
				n_used;
	f64			east, north, tile_width, tile_height, de, dn,
				east_bound, south_bound;
	f32			pred_var;
	time_t		now;
	Bool		done;
	
	time(&now);
	mapsheet_cube_get_tilesize(ip, &pel_tile_width, &pel_tile_height);
	tile_width = (pel_tile_width-1) * sheet->hdr->east_sz;
	tile_height = (pel_tile_height-1) * sheet->hdr->north_sz;
	east_bound = sheet->hdr->east_sz*(sheet->hdr->n_east-1);
	south_bound = sheet->hdr->north_sz*(sheet->hdr->n_north-1);
	*nused = 0;
	/* Set up surface point interpolation for the data points.  We match the
	 * points into each tile until we find one that matches, then do the
	 * interpolation through cube_grid_interpolate() and fill the result into
	 * the 'range' element of the sounding [bad! <smack>] for cube_node_insert()
	 * to use for slope corrections.  Note that this means that we are doing
	 * somewhat the same computations twice (i.e., mapping data into tiles and
	 * computing local offsets).  However, this is a fairly small cost, and we
	 * can integrate the two loops here later if required.
	 */
	for (snd = 0; snd < nsnds; ++snd) {
		east = data[snd].east - sheet->hdr->w_bound;
		north = sheet->hdr->n_bound - data[snd].north;
		if (east < 0.0 || east > east_bound ||
											north < 0.0 || north > south_bound)
			continue;
		done = False;
		for (tile_row = 0; tile_row < ip->n_rows && !done; ++tile_row) {
			for (tile_col = 0; tile_col < ip->n_cols && !done; ++tile_col) {
				de = east - tile_col*tile_width;
				dn = north - tile_row*tile_height;
				if (de >= 0.0 && de <= tile_width &&
											dn >= 0.0 && dn <= tile_height) {
					if (!mapsheet_cube_ensure_tile(sheet, ip, tile_row, tile_col, p)) {
						error_msgv(modname, "error: failed to ensure that tile"
							" (%d, %d) was in memory.\n", tile_row, tile_col);
						return(False);
					}
					data[snd].range =
						cube_grid_interpolate(ip->tiles[tile_row][tile_col],
							p->cube, de, dn, data[snd].dr, &pred_var);
					/*data[snd].dz += pred_var;*/ /* Assumes correction later */
					done = True;
				}
			}
		}
	}
	for (snd = 0; snd < nsnds; ++snd) {
		east = data[snd].east - sheet->hdr->w_bound;
		north = sheet->hdr->n_bound - data[snd].north;
		if (east < 0.0 || east > east_bound ||
											north < 0.0 || north > south_bound)
			continue;
		for (tile_row = 0; tile_row < ip->n_rows; ++tile_row) {
			for (tile_col = 0; tile_col < ip->n_cols; ++tile_col) {
				de = east - tile_col*tile_width;
				dn = north - tile_row*tile_height;
				if (de >= 0.0 && de <= tile_width &&
					dn >= 0.0 && dn <= tile_height) {
#ifdef __DEBUG__
/*					error_msgv(modname, "debug: point %d in tile (%d, %d).\n",
						snd, tile_row, tile_col);*/
#endif
					if (!mapsheet_cube_ensure_tile(sheet, ip, tile_row, tile_col, p)) {
						error_msgv(modname, "error: failed to ensure that tile"
							" (%d, %d) was in memory.\n", tile_row, tile_col);
						return(False);
					}
					if (!cube_grid_insert_depths(ip->tiles[tile_row][tile_col],
							p->cube, stream, data+snd, 1,
							sheet->hdr->w_bound + tile_col*tile_width,
							sheet->hdr->n_bound - tile_row*tile_height,
							&n_used)) {
						error_msgv(modname, "error: failed to insert point %d"
							" in tile (%d, %d).\n", snd, tile_row, tile_col);
						return(False);
					}
					if (n_used > 0) {
#ifdef __DEBUG__
						/*error_msgv(modname, "debug: used point in tile;"
							" clearing clean and read flags.\n");*/
#endif
						ip->stamps[tile_row][tile_col] = now;
						mapsheet_cube_clear_status(ip, tile_row, tile_col,
							MAP_CUBE_STATUS_CLEAN);
						mapsheet_cube_clear_status(ip, tile_row, tile_col,
							MAP_CUBE_STATUS_READ);
						++(*nused);
					}
				}
			}
		}
	}
	mapsheet_cube_purge_cache(ip, p, False, &n_used);
	return(True);
}

/* Routine:	mapsheet_cube_finalise
 * Purpose:	Flush all of the tiles in the cache to ensure that all data is used
 * Inputs:	sheet	MapSheet to use
 *			param	Pointer to parameters (Cube) to use
 *			*hcube	Pointer to the HyperCUBE structure
 * Outputs:	True if successful, otherwise False
 * Comment:	Note that, in order to avoid thrashing the cache components, we
 *			only set a flag to indicate that such a request has been received
 *			from the user.  The read-back code then makes sure that each tile
 *			is flushed when it is about to be used.
 */

Bool mapsheet_cube_finalise(MapSheet sheet, void *param, HyperCube *hcube)
{
	hcube->flush_tiles = True;
	return(True);
}

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

Bool mapsheet_cube_initialise(MapSheet sheet, HyperCube *cube, void *par,
							  f32 *data, f32 unct, Bool unct_pcnt, u8 *mask,
							  u32 cols, u32 rows)
{
	u32			tile_row, tile_col, tile_pel_width, tile_pel_height;
	HyperParam	*p = (HyperParam*)par;
	f32			*data_base;
	u8			*mask_base = NULL;
	time_t		now;
	
	if (cols != sheet->hdr->n_east || rows != sheet->hdr->n_north) {
		error_msgv(modname, "error: initialisation data size (%d x %d) not the"
			"same as mapsheet (%d x %d).\n", rows, cols,
			sheet->hdr->n_north, sheet->hdr->n_east);
		return(False);
	}
	if (sheet->hdr->depth != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: mapsheet doesn't have a HyperCUBE surface"
			"to initialise! (method = %d)\n", (u32)(sheet->hdr->depth));
		return(False);
	}
	
	mapsheet_cube_get_tilesize(cube, &tile_pel_width, &tile_pel_height);
	time(&now);
	for (tile_row = 0; tile_row < cube->n_rows; ++tile_row) {
		if (mask != NULL)
			mask_base = mask + tile_row*(tile_pel_height-1)*cols;
		data_base = data + tile_row*(tile_pel_height-1)*cols;
		for (tile_col = 0; tile_col < cube->n_cols; ++tile_col) {
			if (!mapsheet_cube_ensure_tile(sheet,cube,tile_row,tile_col, p)) {
				error_msgv(modname, "error: failed to ensure (load) tile"
					" (row=%d, col=%d) for initialisation.\n",
					tile_row, tile_col);
				return(False);
			}
			if (!cube_grid_initialise(cube->tiles[tile_row][tile_col],
					p->cube, data_base, unct, unct_pcnt, mask_base, cols)) {
				error_msgv(modname, "error: failed to initialise tile"
					" (row=%d, col=%d).\n", tile_row, tile_col);
				return(False);
			}
			cube->stamps[tile_row][tile_col] = now;
			mapsheet_cube_clear_status(cube, tile_row, tile_col,
									   MAP_CUBE_STATUS_CLEAN);
			mapsheet_cube_clear_status(cube, tile_row, tile_col,
									   MAP_CUBE_STATUS_READ);
			if (mask != NULL)
				mask_base += tile_pel_width-1;
			data_base += tile_pel_width-1;
		}
	}
	return(True);
}

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

Bool mapsheet_cube_init_unct(MapSheet sheet, HyperCube *cube, void *par,
							 f32 *data, f32 *unct, u8 *mask,
							 u32 cols, u32 rows)
{
	u32			tile_row, tile_col, tile_pel_width, tile_pel_height;
	HyperParam	*p = (HyperParam*)par;
	f32			*data_base, *unct_base;
	u8			*mask_base = NULL;
	time_t		now;
	
	if (cols != sheet->hdr->n_east || rows != sheet->hdr->n_north) {
		error_msgv(modname, "error: initialisation data size (%d x %d) not the"
			"same as mapsheet (%d x %d).\n", rows, cols,
			sheet->hdr->n_north, sheet->hdr->n_east);
		return(False);
	}
	if (sheet->hdr->depth != MAP_DEPTH_HYPERCUBE) {
		error_msgv(modname, "error: mapsheet doesn't have a HyperCUBE surface"
			"to initialise! (method = %d)\n", (u32)(sheet->hdr->depth));
		return(False);
	}
	
	mapsheet_cube_get_tilesize(cube, &tile_pel_width, &tile_pel_height);
	time(&now);
	for (tile_row = 0; tile_row < cube->n_rows; ++tile_row) {
		if (mask != NULL)
			mask_base = mask + tile_row*(tile_pel_height-1)*cols;
		data_base = data + tile_row*(tile_pel_height-1)*cols;
		unct_base = unct + tile_row*(tile_pel_height-1)*cols;
		for (tile_col = 0; tile_col < cube->n_cols; ++tile_col) {
			if (!mapsheet_cube_ensure_tile(sheet,cube,tile_row,tile_col, p)) {
				error_msgv(modname, "error: failed to ensure (load) tile"
					" (row=%d, col=%d) for initialisation.\n",
					tile_row, tile_col);
				return(False);
			}
			if (!cube_grid_init_unct(cube->tiles[tile_row][tile_col],
					p->cube, data_base, unct_base, mask_base, cols)) {
				error_msgv(modname, "error: failed to initialise tile"
					" (row=%d, col=%d).\n", tile_row, tile_col);
				return(False);
			}
			cube->stamps[tile_row][tile_col] = now;
			mapsheet_cube_clear_status(cube, tile_row, tile_col,
									   MAP_CUBE_STATUS_CLEAN);
			mapsheet_cube_clear_status(cube, tile_row, tile_col,
									   MAP_CUBE_STATUS_READ);
			if (mask != NULL)
				mask_base += tile_pel_width-1;
			data_base += tile_pel_width-1;
			unct_base += tile_pel_width-1;
		}
	}
	return(True);
}

/* Routine:	mapsheet_cube_release_param
 * Purpose:	Releases all memory associated with the algorithm parameters struct
 * Inputs:	*param	Pointer to the (opaqued) algorithm parameter structure
 * Outputs:	-
 * Comment:	-
 */

void mapsheet_cube_release_param(void *param)
{
	HyperParam	*p = (HyperParam*)param;
	if (p->cube != NULL) cube_release_param(p->cube);
	free(p);
}

/* Routine:	mapsheet_cube_init_param
 * Purpose:	Initialise parameters for the algorithm to their default values
 * Inputs:	order	IHO Survey Order as defined in errmod.h
 *			dn, de	Northing and Easting spacings for sheet
 * Outputs:	Pointer to an opaqued parameter structure for the algorithm
 * Comment:	This initialises the parameter to the currently compiled values.
 *			At present, there is no other way to change the parameters than to
 *			recompile the source.
 */

void *mapsheet_cube_init_param(ErrModIHOOrder order, f64 dn, f64 de)
{
	HyperParam	*rtn;
	
	if ((rtn = (HyperParam*)calloc(1, sizeof(HyperParam))) == NULL) {
		error_msg(modname, "error: failed allocating HyperCUBE workspace.\n");
		return(NULL);
	}
	memcpy(rtn, &default_param, sizeof(HyperParam));
	if ((rtn->cube = cube_init_param(order, dn, de)) == NULL) {
		error_msg(modname, "error: failed to set up CUBE parameters.\n");
		mapsheet_cube_release_param(rtn);
		return(NULL);
	}
	return(rtn);
}

/* Routine:	mapsheet_cube_execute_params
 * Purpose:	Execute parameters list for this sub-module
 * Inputs:	*list	ParList linked list to work through
 * Outputs:	True if the list was parsed properly, otherwise False
 * Comment:	This looks for a whole screed of parameters used to control all
 *			aspects of the CUBE algorithm ... basically the list at the top
 *			of the source file.  All of the parameters are limit checked as
 *			far as possible.
 */

typedef enum {
	MAP_CB_UNKNOWN = 0,
	MAP_CB_MAXMEM,		/* Maximum memory required for all tiles */
	MAP_CB_HYPOHINT,	/* Guess expected hypotheses per node */
	MAP_CB_PROBUSE,		/* Guess at P(node used) */
	MAP_CB_MAXDIM,		/* Maximum tile dimension */
	MAP_CB_TILEEXPIRY	/* Maximum tile retention time */
} MapCbParamEnum;

Bool mapsheet_cube_execute_params(ParList *list)
{
	ParTable tab[] = {
		{ "max_memory",			MAP_CB_MAXMEM		},
		{ "hypothesis_hint",	MAP_CB_HYPOHINT		},
		{ "prob_use",			MAP_CB_PROBUSE		},
		{ "max_tile_dim",		MAP_CB_MAXDIM 		},
		{ "tile_expiry_delta",	MAP_CB_TILEEXPIRY	},
		{ NULL,					MAP_CB_UNKNOWN		}
	};
	ParList	*node, *match;
	u32		id, max_rss;
	f64		dummy_float;
	u32		dummy_int;
#ifndef WIN32
	struct rlimit	rss;
#endif
	Bool	rc = True;

#ifndef WIN32
	getrlimit(RLIMIT_RSS, &rss);
	max_rss = (u32)(MAX_MEM_RATIO * rss.rlim_max);
#else
	max_rss = _HEAP_MAXREQ;
		/* Note that the translation to Win32 here is not exact.
		 * The maximum RSS (resident set size) on *nix is the most
		 * memory a single process is allowed to have resident in
		 * RAM at any one time, irrespective of the maximum virtual
		 * memory which is allowed.  Since we are already tiling to
		 * manage actively the memory we're using, we need to ensure
		 * that we are not going to have the memory paged on us, and
		 * hence limit our requests to a proportion of the RSS and
		 * resize tiles appropriately.  The Win32 equivalent (as far
		 * as I can tell) is the maximum size for which the heap memory
		 * manager will honour requests.  The source available
		 * suggests that this is 0xFFFFFE0, or slightly under 4G ---
		 * basically as much as you can grab with a 32-bit pointer
		 * space.  This is very unlikely to be honoured, except on
		 * *really* big servers, and hence will probably fail as a
		 * backup parameter.  *sigh*
		 */
#endif

	node = list;
	do {
		node = params_match(node, "mapsheet.hcube", tab, &id, &match);
		switch (id) {
			case MAP_CB_UNKNOWN:
				break;
			case MAP_CB_MAXMEM:
				dummy_int = params_translate_memory(match->data);
				if (dummy_int < MIN_MEMORY || dummy_int > max_rss) {
					error_msgv(modname, "error: need memory request in range"
						"(%d, %d), not %d.\n", MIN_MEMORY, max_rss, dummy_int);
					rc = False;
				} else {
					match->used = True;
					default_param.max_memory = dummy_int;
				}
				break;
			case MAP_CB_HYPOHINT:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_HYPOHINT || dummy_float > MAX_HYPOHINT) {
					error_msgv(modname, "error: need hypothesis hint factor"
						" in range (%.1f, %.1f).\n", MIN_HYPOHINT,
						MAX_HYPOHINT);
					rc = False;
				} else {
					match->used = True;
					default_param.hypothesis_hint = (f32)dummy_float;
				}
				break;
			case MAP_CB_PROBUSE:
				dummy_float = atof(match->data);
				if (dummy_float < MIN_PROBUSE || dummy_float > MAX_PROBUSE) {
					error_msgv(modname, "error: probability of node usage must"
						" be in range [%.1f,%.1f] (not %.1f).\n",
						MIN_PROBUSE, MAX_PROBUSE, dummy_float);
					rc = False;
				} else {
					match->used = True;
					default_param.prob_use = (f32)dummy_float;
				}
				break;
			case MAP_CB_MAXDIM:
				dummy_int = atoi(match->data);
				if (dummy_int < MIN_MAXDIM || dummy_int > MAX_MAXDIM) {
					error_msgv(modname, "error: need maximum tile side length"
						" in range (%d, %d).\n", MIN_MAXDIM, MAX_MAXDIM);
					rc = False;
				} else {
					match->used = True;
					default_param.max_tile_dimension = dummy_int;
				}
				break;
			case MAP_CB_TILEEXPIRY:
				dummy_float = params_translate_duration(match->data);
				if (dummy_float < MIN_EXPIRY || dummy_float > MAX_EXPIRY) {
					error_msgv(modname, "error: need tile expiration time in"
						" the range (%.1f, %.1f).\n", MIN_EXPIRY, MAX_EXPIRY);
					rc = False;
				} else {
					match->used = True;
					default_param.tile_expiry_delta = (u32)dummy_float;
				}
				break;
			default:
				error_msgv(modname, "error: unknown return from parameter"
					" matching module (%d).\n", id);
				rc = False;
				break;
		}
	} while (node != NULL);
	return(rc);
}
