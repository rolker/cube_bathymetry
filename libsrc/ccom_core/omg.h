/*
 * $Id: omg.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:27  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.7  2001/09/23 18:56:06  brc
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
 * Revision 1.6  2001/08/28 16:04:06  brc
 * Added omg_new_from_mapsheet_header() to allow construction of just the
 * geo-referencing parts of the OMGRaster from a mapsheet structure.  The
 * result has no data stored, and any indication of data should not be
 * believed.
 *
 * Revision 1.5  2001/02/10 16:05:37  brc
 * Modified naming on raster checking function to ensure compatibility with
 * the rest of the routines in this module.
 *
 * Revision 1.4  2001/02/10 16:04:30  brc
 * Added prototype for R4 compatibility checking function.
 *
 * Revision 1.3  2000/12/03 20:33:11  brc
 * Added include for projection.h, and added prototype for omg_replace_data().
 *
 * Revision 1.2  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
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
 * File:	omg.h
 * Purpose:	Interface to OMG files for reading and writing
 * Date:	22 April 2000 / 06 July 2000 (incorporation)
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __OMG_H__
#define __OMG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "projection.h"

typedef enum {
	OMG1_NONE	= 0,	/* i.e., contains no data at all */
	OMG1_U8		= 1,	/* i.e., unsigned 8bpp image data */
	OMG1_U16	= 2,	/* i.e., unsigned 16bpp image data */
	OMG1_F32	= 3,	/* i.e., IEEE 32-bit (single) FP depths */
	OMG1_UNKNOWN		/* i.e., anything else (sentinel) */
} OMGType;

typedef struct _omg_raster *OMGRaster;

extern OMGType omg_get_data_type(OMGRaster raster);
extern void omg_get_sizes(OMGRaster raster, u32p width, u32p height);
extern Projection omg_get_projection(OMGRaster raster);
extern void *omg_get_data(OMGRaster raster);
extern void omg_get_spacings(OMGRaster raster, f64p dx, f64p dy);
extern void omg_get_bounds(OMGRaster raster, f64p left, f64p bottom,
						   f64p right, f64p top);
extern Bool omg_bounds_projected(OMGRaster r); /* True if so, False otherwise */

/* Routine:	omg_replace_data
 * Purpose:	Replace data into an OMGRaster structure
 * Inputs:	raster		Raster to work on
 *			type		Type of data being added
 *			rows, cols	Size of data being passed
 *			*data		Pointer to the data to copy
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
 */

extern Bool omg_replace_data(OMGRaster raster, OMGType type, u32 rows, u32 cols,
							 void *data);

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

extern Bool omg_replace_data_filt(OMGRaster raster, OMGType type, u32 rows,
								  u32 cols, void *data,
								  void *null_data, void *op_null);

#include "mapsheet.h"

extern OMGRaster omg_new_from_mapsheet(MapSheet sheet, MapSheetElem data);
extern OMGRaster omg_new_from_mapsheet_header(MapSheet sheet);
extern OMGRaster omg_new_from_file(const char *name, Bool header_only);
extern void omg_release(OMGRaster raster);

extern Bool omg_write(const char *name, OMGRaster raster);

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

extern Bool omg_check_surfaces(OMGRaster ras1, OMGRaster ras2);

#ifdef __cplusplus
}
#endif


#endif
