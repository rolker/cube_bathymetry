/*
 * $Id: mapsheet_median.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:42  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:30:00  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.5.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.5  2001/08/21 01:45:43  brc
 * Added facility to count the number of soundings actually used from each swath
 * to update the data in the mapsheet region.  This can be used, among other things,
 * to determine which data files in a set are actually used in updating the data.
 *
 * Revision 1.4  2001/05/14 21:07:22  brc
 * Added facilities to make sub-module 'params'-aware.
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
 * File:	mapsheet_median.h
 * Purpose:	Median `filtering' for bined depth estimation
 * Date:	07 July 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __MAPSHEET_MEDIAN_H__
#define __MAPSHEET_MEDIAN_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "mapsheet.h"

extern Bool mapsheet_bin_by_median(BinNode **ip, u32 width, u32 height,
									void *par, f32p op);
extern Bool mapsheet_median_insert_depths(MapSheet sheet, BinNode **grid,
										  void *par, Sounding *snds, u32 nsnds,
										  u32 *nused);
extern void *mapsheet_median_init_param(void);
extern void mapsheet_median_release_param(void *param);
extern Bool mapsheet_median_write_param(void *param, FILE *f);
extern void *mapsheet_median_read_param(FILE *f);

/* Routine:	mapsheet_median_limit_buffer
 * Purpose:	Set a limit on maximum size of buffer
 * Inputs:	*par	Pointer to private parameter workspace
 *			limit	Maximum size to which buffers should grow
 * Outputs:	-
 * Comment:	Note that this does not *reduce* the size of buffers currently
 *			allocated; it only stops new buffers from increasing to more than
 *			this size.
 */

extern void mapsheet_median_limit_buffer(void *par, u32 limit);

/* Routine:	mapsheet_median_execute_params
 * Purpose:	Execute parameter list for this sub-module
 * Inputs:	*list	ParList to work through
 * Outputs:	True if parameters were executed sucessfully, otherwise False
 * Comment:	This looks for a default null depth, a subsample ratio (i.e.,
 *			how many elements to choose for sorting into median order), a
 *			buffer quantum and a maximum buffer level.
 */

#include "params.h"

extern Bool mapsheet_median_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
