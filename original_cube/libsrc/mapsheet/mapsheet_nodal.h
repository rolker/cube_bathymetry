/*
 * $Id: mapsheet_nodal.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:30:00  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.8.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.8  2002/05/10 22:03:53  brc
 * Added small modification to initialisation sequence so that the fixed
 * uncertainty being specified can be set as a percentage of depth, rather
 * than a fixed value in meters.
 *
 * Revision 1.7  2001/09/23 20:35:51  brc
 * Added prototype for hypothesis surface readback code.
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
 * File:	mapsheet_nodal.h
 * Purpose:	Depth surface construction based on nodal update equations
 * Date:	8 July 2000
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

#ifndef __MAPSHEET_NODAL_H__
#define __MAPSHEET_NODAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "mapsheet.h"
#include "mapsheet_private.h"
#include "sounding.h"

/* Routine:	mapsheet_nodal_finalise
 * Purpose:	Flush all input pre-filter queues into the grid so that true
 *			estimates can be extracted
 * Inputs:	sheet	MapSheet associated with grid of DepthNodes
 *			*param	Pointer to the parameters structure for the module
 *			**ip	Pointer to the DepthNode grid
 * Outputs:	- (queues are flushed and freed)
 * Comment:	This flushes all of the queues into the Kalman filter input sequence
 *			for their respective nodes, and then frees the queue structures,
 *			returning the memory to the heap.  This routine should be called
 *			before extracting data from the grid, and may be used to free up
 *			memory for other nodes.
 */

extern void mapsheet_nodal_finalise(MapSheet sheet, void *param, DepthNode **ip);

/* Routine:	mapsheet_nodal_get_depth
 * Purpose:	Extract depth surface from nodal representation
 * Inputs:	sheet	Mapsheet to get information from
 *			**ip	Input sequence of depth nodes
 *			*par	Raw parameter structure associated with depth node grid
 * Outputs:	op		Pointer to linear (row major) array of data to hold the
 *					output sequence.
 * Comment:	This simply extracts the current estimates from the DepthNode grid
 *			of structures.  In particular, it does not flush the node pre-filter
 *			input queues into the Kalman Filter input sequence, and hence the
 *			estimates may not accurately reflect the current state of knowledge
 *			about the nodes.  Users requiring a current snapshot should call
 *			mapsheet_nodal_finalise() before reading the grid with this call.
 */

extern Bool mapsheet_nodal_get_depth(MapSheet sheet, DepthNode **ip,
									 void *par, f32p op);

/* Routine:	mapsheet_nodal_get_uncertainty
 * Purpose:	Extract uncertainty surface from nodal representation
 * Inputs:	sheet	Mapsheet to get information from
 *			**ip	Input sequence of depth nodes
 *			*par	Raw parameter structure associated with depth node grid
 * Outputs:	op		Pointer to linear (row major) array of data to hold the
 *					output sequence.
 * Comment:	This simply extracts the current estimates from the DepthNode grid
 *			of structures.  In particular, it does not flush the node pre-filter
 *			input queues into the Kalman Filter input sequence, and hence the
 *			estimates may not accurately reflect the current state of knowledge
 *			about the nodes.  Users requiring a current snapshot should call
 *			mapsheet_nodal_finalise() before reading the grid with this call.
 *			  Note that the value returned is actually the 90% predicted
 *			confidence interval in meters, rather than the true variance, based
 *			on the assumption that the sampling distribution is normal (i.e.,
 *			standard error x 1.64).
 */

extern Bool mapsheet_nodal_get_uncertainty(MapSheet sheet, DepthNode **ip,
										   void *par, f32p op);

/* Routine:	mapsheet_nodal_get_depth_unct
 * Purpose:	Get both depth and uncertainty surface using the selected multi-
 *			hypothesis disambiguation algorithm
 * Inputs:	sheet	MapSheet to work from
 *			**ip	Grid of DepthNodes to use
 *			*par	Parameter structure for the algorithm
 * Outputs:	*depth	Linear (row major) array for depth output
 *			*unct	Linear (row major) array for uncertanty output
 * Comment:	This is basically a dispatch switch for the various algorithm
 *			implementations.  Note that it is faster (and sometimes very
 *			significantly faster) to use this if you are going to need both
 *			surfaces, since it only has to do the sorting/context search once
 *			per node, rather than twice.
 */

extern Bool mapsheet_nodal_get_depth_unct(MapSheet sheet, DepthNode **ip,
										  void *par, f32p depth, f32p unct);

/* Routine:	mapsheet_nodal_get_sdev
 * Purpose:	Extract standard deviation surface as a function of current estimate
 *			of depth
 * Inputs:	sheet	Mapsheet to get information from
 *			**ip	Input sequence of depth nodes
 *			*par	Raw parameters associated with the depth node grid
 * Outputs:	op		Pointer to linear (row major) output array
 * Comment:	This converts the sum-of-squares data in the nodes into s.dev
 *			using the current depth estimate as the `mean'.
 */

extern void mapsheet_nodal_get_sdev(MapSheet sheet, DepthNode **ip, void *par,
									f32p op);

/* Routine:	mapsheet_nodal_get_hypo
 * Purpose:	Return an array with number of hypotheses counted
 * Inputs:	sheet	MapSheet to work from
 * Outputs:	Returns True if sucessful, otherwise False
 *			*op is filled with the hypotheses counts
 * Comment:	This does over-flow rounding, so that nodes with more than 255
 *			hypotheses (as well as being very sick) are caped at 255.
 */

extern Bool mapsheet_nodal_get_hypo(MapSheet sheet, u8 *op);

/* Routine:	mapsheet_nodal_insert_depths
 * Purpose:	Add a sequence of depth estimates to the mapsheet
 * Inputs:	sheet	MapSheet to work on
 *			**ip	Input DepthNode grid to add data to
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

extern Bool mapsheet_nodal_insert_depths(MapSheet sheet, DepthNode **ip,
										 void *par, SoundingStream stream,
										 Sounding *data, u32 nsnds, u32 *nused);

/* Routine:	mapsheet_nodal_reinit_fixunct
 * Purpose:	Reinitialise a grid with given depths, but fixed uncertainty
 * Inputs:	sheet	MapSheet to work on
 *			*data	Pointer to the depth data to be used (meters, +ve up)
 *			unct	Uncertainty to use (see below for interpretation)
 *			unct_pcnt	Flag: True => uncertainty is a percentage of the depth
 *						being used for initialisation, otherwise the uncertainty
 *						is 1 s.d. in meters.
 *			*mask	Mask image (U8, 255 => no update at node), or NULL
 *			cols,
 *			rows	Size of the depth data grid available
 * Outputs:	True if initialisation worked, otherwise False.
 * Comment:	This copies the depth data into place and initialises the variance
 *			grid to the same value.  The code checks that the size of the input
 *			data grid is compatible with the mapsheet, but doesn't check
 *			that the data is in the correct (geodetic) place --- that's up to
 *			the user to confirm.  We also check that a MAP_DEPTH_NODAL surface
 *			has been assigned to the mapsheet before starting.  Note that we
 *			initialise the data structure with predicted values equal to the
 *			current estimates (just as the main initialisation sequence does).
 */

extern Bool mapsheet_nodal_reinit_fixunct(MapSheet sheet, f32 *data, f32 unct,
								   		  Bool unct_pcnt, u8 *mask,
										  u32 cols, u32 rows);

/* Routine:	mapsheet_nodal_init_param
 * Purpose:	Initialise parameters for the algorithm to their default values
 * Inputs:	order	IHO Survey Order as defined in errmod.h
 *			dn, de	Northing and Easting spacings for sheet
 * Outputs:	Pointer to an opaqued parameter structure for the algorithm
 * Comment:	This initialises the parameter to the currently compiled values.
 *			At present, there is no other way to change the parameters than to
 *			recompile the source.
 */

extern void *mapsheet_nodal_init_param(ErrModIHOOrder order, f64 dn, f64 de);

/* Routine:	mapsheet_nodal_release_param
 * Purpose:	Releases all memory associated with the algorithm parameters struct
 * Inputs:	*param	Pointer to the (opaqued) algorithm parameter structure
 * Outputs:	-
 * Comment:	-
 */

extern void mapsheet_nodal_release_param(void *param);

/* Routine:	mapsheet_modal_alloc_grid
 * Purpose:	Alocate workspace for the DepthNode estimation grid
 * Inputs:	width, height	Dimensions required for the grid
 * Outputs:	Returns pointer to the DepthNode grid, or NULL
 * Comment:	-
 */

extern DepthNode **mapsheet_nodal_alloc_grid(u32 width, u32 height);

/* Routine:	mapsheet_nodal_read_surface
 * Purpose:	De-serialise data for surface from input
 * Inputs:	f		File descriptor to read from
 *			sheet	Mapsheet to work on
 * Outputs:	True if sheet was read, otherwise False.
 * Comment:	-
 */

extern Bool mapsheet_nodal_read_surface(FILE *f, MapSheet sheet);

/* Routine:	mapsheet_nodal_write_grid
 * Purpose:	Serialise a mapsheet node grid to file
 * Inputs:	f		File descriptor to write on
 *			**g		Grid of DepthNode structures to write
 *			*param	Parameters for the depth construction algorithm
 *			npels	Number of pels in the whole node grid
 * Outputs:	True if the sheet was converted and flushed, otherwise False
 * Comment:	Note that this routine assumes that the mapsheet data space was
 *			allocated in one contiguous chunk on the first element so that it
 *			can write the whole lot in one call.  It also causes the pre-filter
 *			queues to be flushed into the KF input stream which (a) results in
 *			a non-reversible modification of the data grid, and (b) may take
 *			some time.  The first side effect means that grids should not be
 *			saved randomly, and that a grid which is accumulated in stages will
 *			be different in possibly not-so-subtle ways from one which is
 *			accumulated all together.
 */

extern Bool mapsheet_nodal_write_grid(FILE *f, DepthNode **g, void *param,
										u32 npels);

/* Routine:	mapsheet_nodal_write_param
 * Purpose:	Write parameter structure to file
 * Inputs:	param	Pointer to (opaqued) parameter structure
 *			f		File descriptor to write on
 * Outputs:	True if write was sucessful, otherwise False
 * Comment:	-
 */

extern Bool mapsheet_nodal_write_param(void *param, FILE *f);

/* Routine:	mapsheet_nodal_read_param
 * Purpose:	Read parameter structure from file
 * Inputs:	f	File descriptor to read from
 * Outputs:	Pointer to (opaqued) parameter structure, or NULL on failure.
 * Comment:	This doesn't do any version checking, which may cause problems in
 *			the future.
 */

extern void *mapsheet_nodal_read_param(FILE *f);


/* Routine:	mapsheet_nodal_release_nodes
 * Purpose:	Release all dynamically allocated memory associated with a node
 * Inputs:	*node	Pointer to the node array
 *			nnodes	Number of nodes in the array
 * Outputs:	-
 * Comment:	-
 */

extern void mapsheet_nodal_release_nodes(DepthNode *node, u32 nnodes);

/* Routine:	mapsheet_nodal_extend_summary
 * Purpose:	Write extended elements from private data store to summary directory
 * Inputs:	*sheet	Sheet to write from
 *			*dir	Name of the directory to extend elements into
 * Outputs:	True on success, otherwise False
 * Comment:	This writes out a set of images in TIFF format which are only found
 *			in the nodal estimation method.  The actual components which are
 *			written depend on the algorithm currently in use ...
 */

extern Bool mapsheet_nodal_extend_summary(MapSheet sheet, char *dir);

/* Routine:	mapsheet_nodal_execute_params
 * Purpose:	Execute parameters list for this sub-module
 * Inputs:	*list	ParList linked list to work through
 * Outputs:	True if the list was parsed properly, otherwise False
 * Comment:	This looks for a whole screed of parameters used to control all
 *			aspects of the CUBE algorithm ... basically the list at the top
 *			of the source file.  All of the parameters are limit checked as
 *			far as possible.
 */

#include "params.h"

extern Bool mapsheet_nodal_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
