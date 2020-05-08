/*
 * $Id: ccom_general.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:12  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.9.2.3  2003/01/21 22:55:59  brc
 * Extended the BBox associated with fills to record the seed point used, and to
 * count the number of cells used when the blob is being filled.  Exposed the interface
 * for ccom_fill() and ccom_find_nearest_shoal() for use in coverage calculations,
 * and in edge-nibbling applications.
 *
 * Revision 1.9.2.2  2002/12/15 01:09:28  brc
 * Added:
 *  1. Prototypes for coverage tool support routines, enumeration of directories, GUTM writer, etc.
 *  2. Structures for filenames being returned by search code
 *  3. Default location of the command log
 *
 * Revision 1.9.2.1  2002/07/14 02:19:11  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.9  2002/05/10 19:07:40  brc
 * Added code for image processing type operations required by the blob-detection
 * features used in CUBE's data analysis phase.  This allows simple binary
 * errosion and dilation, thresholding, and making a list of blobs from a
 * binary image (uses a seed fill algorithm to detect and remove blobs).
 *
 * Revision 1.8  2002/03/14 04:20:47  brc
 * Modified CLUT writing routine to make it callable for GUTMs as well as GDPs.
 * Added de-spiking routine and control interfaces.  Modified GUTM writing code
 * to write a CLUT as well.  Added ccom_make_filename() to generate filenames
 * from a base name and extensions.
 *
 * Revision 1.7  2001/10/06 16:22:50  brc
 * Added facility to write to GeoZui3D formats: GUTM for grids with geo-ref data
 * and GDP for point data, with extra information for errors, file ID, beam
 * number, etc.  GDP interface isn't great, since it needs to see all of the
 * data before it can write the header output information.  Currently, this means
 * that it buffers everything in memory before writing it to disc, which could
 * cause difficulties if there are lots of data points.
 *
 * Revision 1.6  2001/08/28 23:24:43  brc
 * Added TIFF_RGB for colour 24bpp (3x8bpp) RGB images.
 *
 * Revision 1.5  2001/04/10 23:17:13  brc
 * Added prototype for TIFF reader.
 *
 * Revision 1.4  2001/02/16 23:27:24  brc
 * Added pass-through FIR filter code so that decimation filters can be
 * lower-limited at the input rate.
 *
 * Revision 1.3  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
 *
 * Revision 1.2  2000/09/29 20:33:23  brc
 * Moved filter design from sounding code to general (since it can be used for
 * more than one purpose), and updated sounding code to use it.  Changed filter
 * to windowed ideal filter time domain design for simplicity.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:25  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 * Revision 1.3  2000/04/24 02:06:26  brc
 * Added WriteTIFF and the TIFFFlags type to interface with fairly general
 * TIFF writing software (supports float, U16 and U8 inputs, and writes
 * float, U16 or U8 outputs; can also down-convert, e.g., from float->U16, or
 * U16->U8 as required).
 *
 * Revision 1.2  2000/04/22 19:44:50  brc
 * Added routine to allocate buffer space according to a text description of the
 * total space required, rather than a fixed number of elements.
 *
 * Revision 1.1  2000/04/22 18:08:29  brc
 * Added general.{c,h} for general routines used in the library.
 *
 *
 * File:	ccom_general.h
 * Purpose:	General routines used multiple times
 * Date:	22 April 2000 / 05 July 2000 (incorporation)
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __GENERAL_H__
#define __GENERAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <tiffio.h>
#include "stdtypes.h"
#include "sounding.h"

typedef enum {
	DATA_U8		= 0,
	DATA_U16	= 1,
	DATA_U32	= 2,
	DATA_F32	= 3,
	DATA_F64	= 4
} DataType;

typedef enum {
	TIFF_U8 = 0,
	TIFF_U16 = 1,
	TIFF_F32 = 2,
	TIFF_RGB = 3	/* i.e., triples of U8s {RGB}*N row major */
} TIFFType;	/* Types of input data to the TIFF writer */

typedef struct {
	Bool	verbose,	/* Write scaling information on stderr */
			writefloat,	/* Write output as F32 if possible (only if F32 in) */
			logtrans,	/* Implement log transform on data before scaling */
			force8;		/* Write output as U8 format for old readers */
} TIFFFlags;

typedef enum {
	COLOUR_BY_DEPTH = 0,
	COLOUR_BY_POSERR,
	COLOUR_BY_BEAM,
	COLOUR_BY_FILE
} GdpColour;			/* Colouring scheme for GDP file */

typedef struct _gdp_file GdpFILE;

typedef struct {		/* Structure for manipulating GUTMs */
	f32	*data;
	f32	*altdata;
	f32	cell_size;
	u32	rows, cols;
	f64	x0, y0, z0;
} GUTM;

/* FileEntry: a structure for linked lists of filenames */

typedef struct _file_ent {
	char				*filename;
	struct _file_ent	*next;
} FileEntry;

/* Summary statistics for the despiking algorithm code. */
typedef struct {
	u32	n_points,			/* Number of points in input data set */
		n_used_points,		/* Number of points being used in input data set */
		undefined_points,	/* Number of points undefined in input data set */
		isolated_points;	/* Points with no neighbours set to INVALID */
	u32	n_spikes_removed,	/* Total number of spikes removed */
		shoal_spikes,		/* Spikes that are shoaler than mean */
		deep_spikes;		/* Spikes that are deeper than mean */
	f32	pcnt_defined,		/* % points defined in input data */
		pcnt_spikes,		/* % spikes (prop. to number of defined points) */
		pcnt_shoal,			/* % shoal spikes in set removed */
		pcnt_deep;			/* % deep spikes in set removed */
} DespikeStat;

/* Bounding Boxes for Blob Detection.  Note that these are defined in a left-
 * handed coordinate system, in pixels, relative to the TL of the image passed
 * into the routine.  That is, the min_y value is the northern-most pixel in
 * the bounding box.
 */

typedef struct _blob_bbox {
	u32					min_x,			/* Western-most point */
						max_x,			/* Eastern-most point */
						min_y,			/* Northern-most point */
						max_y;			/* Southern-most point */
	u32					blob_pel_count;	/* Number of pels in the blob */
	u32					seed_row,
						seed_col;		/* Location of the first point in the blob */
	struct _blob_bbox	*next;			/* Singly-linked list */
} BlobBBox;

/* Name of the file where the logging output of command lines being run
 * is directed.
 */

#define CCOM_LOG_FILENAME	"libccom.log"

/* Routine:	ccom_permute_n
 * Purpose:	Carry out first N steps of Gentle's permutation algorithm
 * Inputs:	*data	Pointer to buffer
 *			size	Size of the buffer elements in bytes
 *			N		Number of elements to select
 *			T		Total number of elements in the array
 * Outputs:	- (first N samples of data[] are selected at random without
 *			replacement).
 * Comment:	This selects N elements from data[] without replacement but with
 *			equal probability.  Note that this routine uses random(3B), but
 *			does not set the random seed --- this *must* be set externally with
 *			srandom(3B) or a pseudo-random but repeatable sequence will ensue.
 */

extern void ccom_permute_n(void *data, size_t size, u32 N, u32 T);

/* Routine:	ccom_allocate_buffer
 * Purpose:	Allocate a buffer of fixed total bytecount, rather than elements
 * Inputs:	*desc	Description of how much memory to allocate
 *			esize	Size of the elements in the buffer
 * Outputs:	Returns pointer to buffer, or NULL for failure.
 *			*nelm	Number of elements in a full read of the buffer
 * Comment:	This allows the user to specify the buffer input size as a string,
 *			using 'k', 'M' or (inadvisably) 'G' as multipliers (default is
 *			bytes).  Anything else is reported as an error.
 */

extern void *ccom_allocate_buffer(char *desc, size_t esize, u32 *nelem);

/* Routine:	ccom_make_filename
 * Purpose:	Make a filename with new extension from one supplied
 * Inputs:	*name	Base filename
 *			*ext	Extension to add
 *			*buf	Buffer to write output into (see comment)
 *			nbuf	Number of data points in the buffer
 * Outputs:	Pointer to name on success (*buf when used), NULL on failure
 * Comment:	This may be called in two modes.  With *buf == NULL, the routine
 *			attempts to allocate a buffer and put the constructed name into it;
 *			otherwise it attempts to write into the buffer provided, and only
 *			fails if the buffer isn't big enough.
 */

extern char *ccom_make_filename(char *name, char *ext, char *buf, u32 nbuf);

/* Routine:	ccom_log_command_line
 * Purpose:	Write the command line for a command to a standard log file
 * Inputs:	*progname		Name to use for logging of the command
 *			argc, **argv	Standard C command line parameters
 * Outputs:	True on success, else False
 * Comment:	This sprang from not being able to trace the sequence of events in
 *			a run of _assimilate_ that appeared not to use a depth gate when
 *			it should have.  This opens 'cube.log' for appending in the current
 *			directory and then writes a timestamp, the command name and then
 *			the whole command line as it is passed to the utility.  Hence, this
 *			file becomes the audit trail for everything that is done to the data
 *			(at least by all commands that use this interface).
 */

extern Bool ccom_log_command_line(char *progname, u32 argc, char **argv);

/* Routine:	ccom_write_tiff
 * Purpose:	Write a generic TIFF output with processing options
 * Inputs:	*dirname	Offset directory, or NULL if none
 *			*name		Name of the file to write
 *			*data		Opaque pointer to the data to be written
 *			type		Type of the data elements
 *			rows, cols	Sizes of the data (row major format)
 *			*flg		Pointer to a TIFFFlag structure indicating processing
 *						(or NULL)
 * Outputs:	Success as a Bool
 * Comment:	This routine acts as an interface to the TIFF library for writing
 *			a number of alternative data formats.  It can write U8, U16 and F32
 *			style TIFF images (greyscale), and can convert from F32 to U16 or
 *			U8, and from U16 to U8.  It can also optionally construct a log
 *			scale transform (i.e., x <- log(x), with suitable constraints for
 *			scale and domain of log()), which can help in situations with very
 *			high dynamic range.  The conversion process and scales are written
 *			into the IMAGEDESCRIPTION TIFF tag.
 */

extern Bool ccom_write_tiff(char *dirname, char *name, void *data,
							TIFFType type, u32 rows, u32 cols, TIFFFlags *flg);

/* Routine:	ccom_read_tiff
 * Purpose:	Read TIFF into internal buffer, returning type and sizes
 * Inputs:	*name	Name of file to read
 * Outputs:	*type	TIFFType of the data in the file
 *			*rows,
 *			*cols	Size of the image
 *			Returns pointer to a uniform row major buffer of data (1D)
 * Comment:	This understands only monochrome images, but will accept U8, U16 or
 *			F32 images.
 */

extern void *ccom_read_tiff(char *name, TIFFType *type, u32 *rows, u32 *cols);

/* Routine:	ccom_write_gutm
 * Purpose:	Write a GeoZui3D style gridded UTM surface
 * Inputs:	*name	Name of file to write into
 *			*data	Pointer to (opaque) data
 *			type	Type of data being written (i.e., size of data)
 *			rows\	Size of the data grid being written
 *			cols/
 *			spacing	Size of node spacing in meters (must be 1:1 aspect)
 *			x0, y0	Origin of the north-west corner of the grid
 * Outputs:	Writes the file to (name); returns True if the write worked, or
 *			False otherwise
 * Comment:	This assumes that the data being passed is, in fact, a grid that
 *			is projected using UTM meters on WGS-84.  If not, the file will
 *			still be written, but you're responsible for the interpretation in
 *			GZ3D.  This routine understands the 'invalid' data elements from
 *			mapsheet.h, and translates these into GZ3D 'no-data' holes.
 */

extern Bool ccom_write_gutm(char *name, void *data, DataType type,
							u32 rows, u32 cols, f32 spacing, f64 x0, f64 y0);

/* Routine:	ccom_gutm_write
 * Purpose:	Write a GUTM straight from the structure
 * Inputs:	*gutm	Data to write
 *			*name	File in which to write it
 * Outputs:	True on success, otherwise False
 * Comment:	This is a simplified version of ccom_write_gutm() that
 *			only writes what's valid in the GUTM structure (i.e., won't
 *			translate data sizes, etc.).  However, it will write the
 *			GUTMs _altdata_ section.
 */

extern Bool ccom_gutm_write(GUTM *gutm, char *name);

/* Routine:	ccom_gutm_release
 * Purpose:	Release all memory associated with a GUTM structure
 * Inputs:	*gutm	Structure to work through
 * Outputs:	-
 * Comment:	This de-allocates all memory associated with the data, including
 *			the base GUTM structure.
 */

extern void ccom_gutm_release(GUTM *gutm);

/* Routine:	ccom_gutm_new_blank
 * Purpose:	Allocate memory for blank GUTM structure
 * Inputs:	-
 * Outputs:	Pointer to blank, or NULL on failure
 * Comment:	The structure is initialised to 0 on return.
 */

extern GUTM *ccom_gutm_new_blank(void);

/* Routine:	ccom_gutm_new
 * Purpose:	Allocate memory for a GUTM, including space for data
 * Inputs:	cols, rows	Size of the GUTM
 *			cell_size	Spacing of the cells in the GUTM
 *			x0, y0, z0	Georeferencing for the GUTM
 *			alt_data	Flag: True => allocate space for the altdata[] field
 * Outputs:	Pointer to the allocated array or NULL on failure
 * Comment:	Note that the data surfaces are not initialised.
 */

extern GUTM *ccom_gutm_new(u32 cols, u32 rows, f32 cell_size,
						   f64 x0, f64 y0, f64 z0, Bool alt_data);

/* Routine:	ccom_gutm_clone
 * Purpose:	Allocate another GUTM with same parameters as input
 * Inputs:	*gutm	GUTM to clone
 * Outputs:	Pointer to buffer of success, or NULL on failure
 * Comment:	This clones the parameters of the GUTM, but allocates more space
 *			for the data sections.
 */

extern GUTM *ccom_gutm_clone(GUTM *gutm);

/* Routine:	ccom_gutm_read_header
 * Purpose:	Read the header of an open GUTM file
 * Inputs:	*ip	Pointer to FILE to read from
 * Outputs:	*grid_full	Flag: True => a full grid is specified in FILE
 *			*alt_data	Flag: True => alternative data (second column) exists
 * Comment:	The routine loops until it finds a BEGIN GUTM statement, returning
 *			failed if EOF happens first.  The GUTM returned only contains the
 *			header information, rather than having data space allocated.
 */

extern GUTM *ccom_gutm_read_header(FILE *ip, Bool *grid_full, Bool *alt_data);

/* Routine:	ccom_gutm_read
 * Purpose:	Read a simple GUTM into memory.
 * Inputs:	*name	Name of the file to read and parse
 * Outputs:	Pointer to an appropriate GUTM structure on success, or NULL on
 *			on failure.
 * Comment:	This code reads V1.0 of the GUTM format, along with the
 *			USE_ALTERNATE_DATA extension (allows for a second column of data
 *			to be specified).  Note that PARTIAL grids are expanded out to
 *			full grids on read, with the remainder of the data areas being
 *			initialised to INVALID_DATA using the specified from
 *			mapsheet_get_invalid() (typ. FLT_MAX).  If no alternative data is
 *			specified, the g->altdata pointer is NULL.  Note that no vertical
 *			offset is applied to the alternate data, but the z0 offset is
 *			already applied to the primary data.
 */

extern GUTM *ccom_gutm_read(char *name);

/* Routine:	ccom_despike
 * Purpose:	Despike a DTM before further use
 * Inputs:	*data	Pointer to the (row major 1D) data array
 *			rows, cols	Size of the data buffer
 *			max_angle	Maximum slope angle that is acceptable (degrees)
 *			dx			Sample spacing (in same units as vertical)
 *			nwidth		Neighbourhood width for despiking operator
 *			deep_only	Flag: True => only deep spikes are removed
 *			*stats		Space for statistics on surface (or NULL)
 * Outputs:	Returns True if the operation completed, False otherwise
 * Comment:	This runs a variant of the familiar NASA Voyager despiking algorithm
 *			(i.e., replace central pixel if more than a threshold different than
 *			some weighted mean of the surrounding neighbourhood).  In this case,
 *			the threshold is determined by the angle that the central pixel
 *			makes to the background, assuming that the mean value of the
 *			neighbourhood is one sample from location of the potential spike.
 *			That is, we compute angle as atan((data-mean)/spacing) and replace
 *			the spike point with the mean if over the max_angle specified by the
 *			user.  Angle is specified in degrees.
 *			   Positive and negative spikes are both replaced, but are accounted
 *			separately; if *stats is non-null, these stats are returned to the
 *			caller for further processing and reporting to the user level code.
 */

extern Bool ccom_despike(f32 *data, u32 rows, u32 cols, f32 max_angle, f32 dx,
				  		 u32 nwidth, Bool deep_only, DespikeStat *stats);

/* Routine:	ccom_threshold
 * Purpose:	Threshold a 2D array at given limit
 * Inputs:	*data		Pointer to the array to process
 *			rows, cols	Size of the array
 *			thresh		Threshold (in units of the data)
 *			flip		Flag: TRUE => invert threshold logic
 *			*op			Output buffer, or NULL (see comment)
 * Outputs:	Returns pointer to binary coded U8 buffer containing the thresholded
 *			data.
 * Comment:	Everything above threshold is set to 1, everything below to 0
 *			(unless flip == TRUE).  Set _op_ valid (non-NULL) on input to
 *			provide the output space yourself (assumed to be the correct size)
 *			otherwise the code will generate the buffer.  A pointer to the
 *			buffer which was used (user or internally generated) is returned
 *			from the routine.  It is the user's responsibility to free the
 *			buffer after use, if appropriate.
 */

extern u8 *ccom_threshold(f32 *data, u32 rows, u32 cols, Bool flip, f32 thresh,
						  u8 *op);

/* Routine:	ccom_clip
 * Purpose:	Clip a 2D array at given limit
 * Inputs:	*data		Pointer to the array to process
 *			rows, cols	Size of the array
 *			limit		Clip level (in units of the data)
 *			flip		Flag: TRUE => invert threshold logic
 *			replace		Flag: TRUE => replace input with output (otherwise,
 *						generate and return a buffer).
 * Outputs:	Returns pointer to the buffer used to store the output, whether it's
 *			the same as the input, or was generated internally.
 * Comment:	Everything above limit is set to INVALID, everything below is retained
 *			(unless flip == TRUE).  Set _replace_ to _True_ to replace the
 *			input with the clipped output; otherwise, the code will allocate a
 *			new buffer and copy the data across with suitable modifications.
 *			A pointer to the buffer actually used is returned, whichever one that
 *			is.  It is the user's responsibility to release the buffer, if
 *			appropriate after use.
 */

extern f32 *ccom_clip(f32 *data, u32 rows, u32 cols, f32 limit, Bool flip, Bool replace);

/* Routine:	ccom_erode
 * Purpose:	Error a binary image using a specified structure element size
 * Inputs:	*data		Input data buffer
 *			rows, cols	Size of the input data buffer
 *			size		Size of the structuring element in pixels
 *			*op			Output data buffer (see comment)
 * Outputs:	Returns a pointer to the output data buffer, or NULL
 * Comment:	This does straight binary erosion of the input data buffer, writing
 *			the output into _op_.  The routine always uses a circular structure
 *			element, but of variable size.  If _op_ is NULL on input, a buffer
 *			will be generated internally; the value returned is always a pointer
 *			to whichever buffer was used.
 */

extern u8 *ccom_erode(u8 *data, u32 rows, u32 cols, u32 size, u8 *op);

/* Routine:	ccom_dilate
 * Purpose:	Dilate a binary image using a specified structure element size
 * Inputs:	*data		Input data buffer
 *			rows, cols	Size of the input data buffer
 *			size		Size of the structuring element in pixels
 *			*op			Output data buffer (see comment)
 * Outputs:	Returns a pointer to the output data buffer, or NULL
 * Comment:	This does straight binary dilation of the input data buffer, writing
 *			the output into _op_.  The routine always uses a circular structure
 *			element, but of variable size.  If _op_ is NULL on input, a buffer
 *			will be generated internally; the value returned is always a pointer
 *			to whichever buffer was used.
 */

extern u8 *ccom_dilate(u8 *data, u32 rows, u32 cols, u32 size, u8 *op);

/* Routine:	ccom_convolve
 * Purpose:	General purpose 2D convolution in the spatial domain using a
 *			user specified kernel
 * Inputs:	*data		Pointer to the data to filter
 *			rows, cols	Size of the data input
 *			*kernel		Pointer to the filtering kernel
 *			krows,kcols	Size of the kernel input
 * Outputs:	True if convolution was done, otherwise False
 * Comment:	This does in-place convolution by the supplied kernel in
 *			the spatial domain.  This is linear convolution in the sense
 *			that we don't wrap edges, but we don't extend the size of the
 *			input, nor do we assume that the edges are zero padded.  Instead,
 *			we set the unprocessed edges to INVALID.
 */

extern Bool ccom_convolve(f32 *data, u32 rows, u32 cols,
						  f32 *kernel, u32 krows, u32 kcols);

/* Routine:	ccom_patch_surface
 * Purpose:	Patch round *small* holes in the surface passed
 * Inputs:	*data		Surface to patch
 *			rows, cols	Size of the surface
 *			radius		Radius of window to search for patch neighbours
 *			dx			Sample spacing of the surface
 * Outputs:	True on success, otherwise False
 * Comment:	This attempts to fill in small holes in a surface by looking
 *			for holidays and computing the mean of the neighbours that
 *			are defined.  If there are a sufficient number of neighbours
 *			(currently defined as 80%), then the holiday is filled with
 *			the mean value of the neighbours.  Otherwise, the holiday
 *			is retained.
 *
 *          ****** Warning ****** Warning ****** Warning ******
 *
 *			This is, of course, fundamentally unsound.  A big geologist
 *			with a beard made me do it, honest 'guv.  And it didn't
 *			require a thousand lines of code, either, so there.
 *
 *          ****** Warning ****** Warning ****** Warning ******
 */

extern Bool ccom_patch_surface(f32 *data, u32 rows, u32 cols, f32 radius, f32 dx);

/* Routine:	ccom_gauss_filter
 * Purpose:	Filter the input data with a symmetric Gaussian kernel
 * Inputs:	*data		Data input to filter
 *			rows, cols	Size of the input data
 *			radius		Radius of the Gaussian filter to apply, in meters
 *			de			Sampling rate of surface in meters
 * Outputs:	True on success, False otherwise
 * Comment:	This simply generates a circularly symmetric Gaussian filter
 *			with 3\sigma limits equal to _radius_.  It then runs the kernel
 *			over the input data, replacing it as it goes.  The kernel is
 *			normalised so that there is no d.c. gain.
 */

extern Bool ccom_gauss_filter(f32 *data, u32 rows, u32 cols, f32 radius, f32 de);

/* Routine:	ccom_fill
 * Purpose:	Cut-down fill which just replaces all connected elements of the
 *			seed element (i.e., of the same colour) with another colour.
 * Inputs:	i		Pointer to the image to use (row major 1D)
 *			x0, y0	Initial seed.
 *			colour	Colour to replace seed value (and connected pels) with
 *			w, h	Width and height of the image.
 * Outputs:	Connected area is filled
 *			*bbox	Set to bounding box of the area filled
 * Comment:	This should take care of all of the problems of convexity, etc.,
 *			but has normal seed-fill problems with isolated interior segments.
 */

extern void ccom_fill(u8 *i, u32 x0, u32 y0, u8 colour, u32 w, u32 h, BlobBBox *bbox);

/* Routine:	ccom_release_bloblist
 * Purpose:	Release memory associated with a linked list of BlobBBoxes
 * Inputs:	*list	Head of the linked list
 * Outputs:	-
 * Comment:	-
 */

extern void ccom_release_bloblist(BlobBBox *root);

/* Routine:	ccom_blob_detect
 * Purpose:	Find all blobs in a binary image
 * Inputs:	*data	Pointer to a binary image to process
 *			rows,	Size of the image
 *			cols
 * Outputs:	Returns NULL terminated singly linked list of blob bounding boxes
 * Comment:	A blob is defined as the maximum simply connected region of pixels
 *			with value 1 in the image.
 */

extern BlobBBox *ccom_blob_detect(u8 *data, u32 rows, u32 cols);

/* Routine:	ccom_open_gdp
 * Purpose:	Open a new GeoZui3D GDP (Geographic Data Points) file for writing
 * Inputs:	*name	Name of the GDP to build
 *			colour	Colour scheme to use in file
 * Outputs:	Returns a pointer to a GdpFILE to use for working with the data
 * Comment:	The .gdp file format requires header information that it is not
 *			possible to compute without having seen all of the data (i.e.,
 *			minimum x, y, etc.)  so the code here caches all of the data in
 *			memory until the corresponding ccom_close_gdp() call.  At that
 *			point, the file is actually written.  However, the file is opened
 *			for writing in this routine.
 */

extern GdpFILE *ccom_open_gdp(char *name, GdpColour colour);

/* Routine:	ccom_write_gdp
 * Purpose:	Append entries to the GdpFILE structure specified
 * Inputs:	*file	A GdpFILE returned from ccom_open_gdp()
 *			*data	A set of Sounding.s
 *			n_snd	Number of Sounding.s in the structures
 * Outputs:	True if insertion worked, otherwise False.
 * Comment:	The only failure mode here is if the data space to cache the new
 *			soundings cannot be constructed.
 */

extern Bool ccom_write_gdp(GdpFILE *file, Sounding *data, u32 n_snd);

/* Routine:	ccom_close_gdp
 * Purpose:	Write the GDP file to disc, then flush & release memory buffers
 * Inputs:	*file	GdpFILE structure to work on
 * Outputs:	True if write worked, otherwise False
 * Comment:	Note that this call does the actual work of writing the file to disc
 *			but also cleans out buffers, including the GdpFILE itself.  It
 *			should therefore only be used after all data is in, since there is
 *			no way to append data afterwards.
 */

extern Bool ccom_close_gdp(GdpFILE *file);

/* Routine:	ccom_swap_*
 * Purpose:	Multi-byte swap routines for dealing with endian-ness
 * Inputs:	*elem	Element to be swapped
 * Outputs:	
 * Comment:	Use ccom_native_endian to determine the native endian-ness of the
 *			system dynamically.  It is more efficient to set the system up to
 *			either swap or not swap based on pre-processor defines, since you
 *			know at compile time whether the system is going to have to swap
 *			or not, based on the input files and target architecture (unless
 *			the input file is one where the designers thought `who cares', and
 *			just wrote it in native order, irrespective of machine, in which
 *			case you need to be able to do this dynamically).
 */

typedef enum {
	ENDIAN_LITTLE,
	ENDIAN_BIG
} CcomEndian;

extern void ccom_swap_2(void *elem);
extern void ccom_swap_4(void *elem);
extern void ccom_swap_8(void *elem);
extern CcomEndian ccom_native_endian(void);

/* ------------------------ FIR Filter design tools ------------------------- */

typedef struct {
	f32		ip_rate, op_rate;
	u32		step, len;
	f32p	fir;
} Filt;						/* Structure for data decimation filter */

/* Routine:	ccom_release_fir
 * Purpose:	Release dynamically allocated data associated with Filt structure
 * Inputs:	*filt	Filt structure to remove
 * Outputs:	-
 * Comment:	-
 */

extern void ccom_release_fir(Filt *filt);

/* Routine:	ccom_make_fir
 * Purpose:	Construct an FIR filter to downsample information
 * Inputs:	fs	Input sampling rate in Hz.
 *			fo	Output sampling rate in Hz.
 *			len	Length of the filter to construct
 * Outputs:	Pointer to a suitable Filt structure, or NULL on error
 * Comment:	This computes the required corner frequency for the anti-aliasing
 *			filter, and then builds the (a causal) FIR based on the specified
 *			length.  FIR coefficients are computed using the windowed inverse
 *			FFT approach using an ideal low-pass nominal response and a Von
 *			Hann window.  Note that we choose the corner frequency slightly
 *			lower than absolutely required to ensure that we slightly over-
 *			smooth the data, and then choose the sub-sampling step slightly
 *			smaller than we should in order to ensure that we are over-
 *			sampling the new rate data.  Hence, the output rate may not be
 *			exactly _fo_ (but is >= _fo_).
 */

extern Filt *ccom_make_fir(f32 fs, f32 fo, u32 len);

/* Routine:	ccom_make_fir_passthrough
 * Purpose:	Make a passthrough (identity) FIR filter for low-rate signals
 * Inputs:	-
 * Outputs:	Pointer to Filt structure, or NULL if failed
 * Comment:	This makes an FIR, length N=1 with h[n] = 1 (n=0) and h[n] = 0
 *			(n != 0).  As well as being the identity, this is used by, e.g.,
 *			the attitude decimation filtering code to deal with data when
 *			the input rate is lower than the nominal output rate (i.e., would
 *			need to *interpolate* rather than *decimate* to get to required
 *			rate.
 */

extern Filt* ccom_make_fir_passthrough(f32 ip_rate);

/* Routine:	ccom_find_nearest_shoal
 * Purpose:	Find the nearest node of the GUTM that has data shoaler than the
 *			node specified
 * Inputs:	*data		GUTM to search
 *			col, row	Location to start search
 *			limit		Maximum range to consider as 'close'
 * Outputs:	Returns the shoalest depth within range, or 0.0 if none can be found
 * Comment: This routine attempts to find the closest node to that specified (which
 *			is presumably undefined) that is shoalest of the neighbours at the same
 *			distance.  For simplicity, we look for the closest in square annuli
 *			rather than in circles.  If no data is found within range, 0.0 is returned.
 */

extern f32 ccom_find_nearest_shoal(GUTM *data, s32 col, s32 row, s32 limit);

/* Routine:	ccom_compute_coverage
 * Purpose:	Compute the coverage according to NOAA spec and deliverable.
 * Inputs:	*depth	GUTM for the estimated depth of the survey
 *			*cover	GUTM for the coverage depths being estimated
 *			*data	Sounding data to add to the coverage estimate
 *			nSnds	Number of soundings in the input
 * Outputs:	*cover is updated, and a pointer is returned to the same area, or
 *			NULL on failure.
 * Comment: This attempts to estimate the coverage pattern for a survey according
 *			to the S&D for NOAA surveys, i.e., that there should be an accepted
 *			sounding within a bin of no larger than 5.0m + 5% of depth over the whole
 *			area of the survey.  To compute this, we take an estimate of the depth
 *			over the whole area of the survey (e.g., a low-resolution grid), and
 *			thence compute the bin sizes (which are stored as the alternative data
 *			in the _cover_ GUTM).  Then, for each set of soundings to be added, we
 *			spread the sounding into the bins around it, and retain just the shoalest
 *			sounding in each bin.  This is stored in the data element of _cover_.
 *			Once all of the soundings have been inserted, the _cover_ GUTM is the
 *			coverage map.
 *			    Note that _cover_ should be set to NULL on first call, and this
 *			routine will allocate the required memory for the coverage map and the
 *			bin sizes alternative data.  It is valid to set nSnds == 0 on call; in this
 *			case the routine allocates _cover_ if required, and returns.
 */

extern GUTM *ccom_compute_coverage(GUTM *depth, GUTM *cover, Sounding *data, u32 nSnds);

/* Routine:	ccom_find_dirs
 * Purpose:	Recursive depth first decent of a directory tree
 * Inputs:	*base	Base to search from
 *			*target	Target file to find that identifies a directory
 *			*list	List to which matches are added
 * Outputs:	Pointer to (possibly extended) list of matches, or NULL on failure
 * Comment:	This routine recursively decends the directory tree to find all directories
 *			that contain a plain file of name 'target'.
 */

extern FileEntry *ccom_find_dirs(char *base, char *target, FileEntry *list);

/* Routine:	ccom_enumerate_hips_lines
 * Purpose:	Find all of the HIPS/HDCS lines in a directory
 * Inputs:	*base	Pointer to base of the directory tree to search
 * Outputs:	Returns pointer to a list of LineEntry structures, or NULL if
 *			no lines were found.
 * Comment:	This recursively searches the directory specified, and finds all of the
 *			directories that look as if they might be HIPS/HDCS directories (i.e., that
 *			contain a ProcessedDepths file).  Lines are specified as offsets from the
 *			base supplied.  This is a wrapper around ccom_find_dirs().
 */

extern FileEntry *ccom_enumerate_hips_lines(char *name);

#ifdef __cplusplus
}
#endif

#endif
