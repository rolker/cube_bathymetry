/*
 * $Id: ccom_general.c 10 2003-07-22 22:47:58Z brc $
 * $Log$
 * Revision 1.2  2003/07/22 22:47:58  brc
 * Modified GUTM writer to use 2 dp rather than 6 (default) --- this can
 * save a great deal of space in large GUTMs (and make them faster
 * to load), and we can't really justify micron accuracy in GUTMs
 * (yet).
 *
 * Revision 1.1.1.1  2003/02/03 20:18:44  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2.4.1  2003/01/28 14:29:12  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.13.2.3  2003/01/21 22:53:57  brc
 * Extended ccom_fill()'s stack to allow for larger more complex areas.  Also made
 * the stacklength a compile-time variable, and added checks to make sure that it
 * was not exceeded without the code knowing about it.  Also modified the fill code
 * to count the number of pels being filled during the process.
 *
 * Revision 1.13.2.2  2002/12/15 01:07:49  brc
 * Added:
 *  1.  Support for coverage assessment according to NOAA S&D
 *  2. Enumeration of directories (lists all lines in a hierarchy)
 *  3. GDP output of flags from data, if available
 *  4. GUTM writer using a GUTM structure as input
 *  5. Command line loging so that a trace of commands is kept.
 *
 * Revision 1.13.2.1  2002/07/14 02:19:11  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.13  2002/05/10 19:07:40  brc
 * Added code for image processing type operations required by the blob-detection
 * features used in CUBE's data analysis phase.  This allows simple binary
 * errosion and dilation, thresholding, and making a list of blobs from a
 * binary image (uses a seed fill algorithm to detect and remove blobs).
 *
 * Revision 1.12  2002/03/14 04:20:47  brc
 * Modified CLUT writing routine to make it callable for GUTMs as well as GDPs.
 * Added de-spiking routine and control interfaces.  Modified GUTM writing code
 * to write a CLUT as well.  Added ccom_make_filename() to generate filenames
 * from a base name and extensions.
 *
 * Revision 1.11  2001/12/07 20:23:23  brc
 * Removed restriction of size of GDP files being made.
 *
 * Revision 1.10  2001/10/15 23:03:35  brc
 * Added facility to generate a colour map file, coordinate grid axis, scene
 * file, etc. in addition to the basic GDP file.  This allows everything to be
 * georeferenced in context just by loading the scene file.
 *
 * Revision 1.9  2001/10/06 16:22:50  brc
 * Added facility to write to GeoZui3D formats: GUTM for grids with geo-ref data
 * and GDP for point data, with extra information for errors, file ID, beam
 * number, etc.  GDP interface isn't great, since it needs to see all of the
 * data before it can write the header output information.  Currently, this means
 * that it buffers everything in memory before writing it to disc, which could
 * cause difficulties if there are lots of data points.
 *
 * Revision 1.8  2001/08/28 23:24:43  brc
 * Added TIFF_RGB for colour 24bpp (3x8bpp) RGB images.
 *
 * Revision 1.7  2001/05/16 21:03:44  brc
 * Added float.h for DBL_MAX, FLT_MAX etc. on Linux
 *
 * Revision 1.6  2001/04/10 23:16:58  brc
 * Added a basic TIFF reader to support reinitialisation code in mapsheet.
 *
 * Revision 1.5  2001/02/16 23:27:24  brc
 * Added pass-through FIR filter code so that decimation filters can be
 * lower-limited at the input rate.
 *
 * Revision 1.4  2000/09/29 20:33:23  brc
 * Moved filter design from sounding code to general (since it can be used for
 * more than one purpose), and updated sounding code to use it.  Changed filter
 * to windowed ideal filter time domain design for simplicity.
 *
 * Revision 1.3  2000/09/24 01:22:24  brc
 * Fixed bug in auto-rescale of TIFF images (using -ve numbers with u32).
 *
 * Revision 1.2  2000/09/07 21:07:05  brc
 * *** empty log message ***
 *
 * Revision 1.1.1.1  2000/08/10 15:53:25  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 * Revision 1.3  2000/04/24 02:05:01  brc
 * Added TIFF writer code based on ../utilities/array2tif.c original spec, with
 * some enhancements to make this callable from the outside.
 *
 * Revision 1.2  2000/04/22 19:44:50  brc
 * Added routine to allocate buffer space according to a text description of the
 * total space required, rather than a fixed number of elements.
 *
 * Revision 1.1  2000/04/22 18:08:29  brc
 * Added general.{c,h} for general routines used in the library.
 *
 *
 * File:	general.c
 * Purpose:	General routines used multiple times
 * Date:	22 April 2000 / 05 July 2000 (incorporation)
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <tiffio.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef WIN32
	#include <direct.h>
	#include <io.h>
	#define S_ISDIR(x) (((x) & _S_IFDIR) != 0)
#else
	#include <sys/dir.h>
#endif
#include "stdtypes.h"
#include "ccom_general.h"
#include "error.h"
#include "mapsheet.h"
#include "sounding.h"
#include "stime.h"

static char *modname = "general";

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))
#define DEG2RAD(x) ((x)*M_PI/180.0)
#define RAD2DEG(x) ((x)*180.0/M_PI)

#define COVERAGE_SEARCH_LIMIT	10
#ifdef WIN32
	#define DIRECTORY_SEPARATOR	'\\'
#else
	#define DIRECTORY_SEPARATOR '/'
#endif

struct _gdp_file {
	FILE		*gdp_fp;		/* FILE we're writing points to */
	FILE		*cga_fp;		/* FILE we're writing the grid spec to */
	FILE		*clut_fp;		/* FILE we're writing the CLUT spec to */
	u32			n_records;		/* Number of points writen into the file */
	u32			n_bad_nav;		/* Number of snds with bad navigation data */
	f64			min_x, max_x,
				min_y, max_y,
				min_z, max_z;	/* Extrema values for LOCAL_ORIGIN structure */
	GdpColour	colour_scheme;	/* Colouring mode to use */
	Sounding	*data;			/* Pointer to the data being used in the file */
};

/* WW: A WalkingWindow for running low-memory convolutions over images/grids */

typedef struct {
	void	**buffer;	/* The WalkingWindow rotating buffer */
	void	*anchor;	/* Pointer to the start of the buffer */
	u32		n_rows;		/* Rows in the buffer */
	u32		n_bytes;	/* Length of the rows */
} WW;

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

void ccom_permute_n(void *data, size_t size, u32 N, u32 T)
{
	char	*buffer, *D = (char*)data;
	u32		n, r;
	
	if ((buffer = (char *)malloc(size)) == NULL) {
		fprintf(stderr, "%s: permutation failed!\n", modname);
		return;
	}
	for (n = 0; n < N; ++n, D += size, --T) {
#ifdef WIN32
		r = (u32)floor(T*((f64)rand()/RAND_MAX));
#else
		r = random() % T;	/* N.B.: we can only do this because *all* bits
							 * generated by random(3B) are equally `random'.
							 * Beware! Not all PRNGs are the same!
							 */
#endif
		if (r == 0) continue; /* i.e., swapping element with itself */
		memcpy(buffer, D, size);
		memcpy(D, D+r*size, size);
		memcpy(D+r*size, buffer, size);
	}
	free(buffer);
}

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

void *ccom_allocate_buffer(char *desc, size_t esize, u32 *nelem)
{
	u32		len;
#ifdef WIN32
	__int64		size = 0;
#else
	long long	size = 0LL;	/* ! Beware: not all environments support LL */
#endif

	len = strlen(desc);
#ifdef WIN32
	size = _atoi64(desc);
#else
	size = atoll(desc);
#endif
	if (!isdigit(desc[len-1])) {
		/* Attempt to match a multiplier */
		switch(desc[len-1]) {
			case 'k':
				size *= 1<<10;
				break;
			case 'M':
				size *= 1<<20;
				break;
			case 'G':
				size *= 1<<30;
				break;
			default:
				error_msgv(modname, "multiplier '%c' unknown.\n", desc[len-1]);
				return(NULL);
		}
	}
	*nelem = (u32)(size/esize);
	return(malloc(esize*(*nelem)));
}

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

char *ccom_make_filename(char *name, char *ext, char *buf, u32 nbuf)
{
	char	*rtn, *ptr;
	u32		len;
	
	len = strlen(name) + strlen(ext) + 2;
	if (buf == NULL) {
		/* Attempt to make a buffer. */
		if ((rtn = (char*)calloc(len, sizeof(char))) == NULL) {
			error_msgv(modname, "error: failed to allocate buffer for extended"
				" filename (base \"%s\", extension \"%s\").\n", name, ext);
			return(NULL);
		}
	} else {
		if (len > nbuf) {
			error_msgv(modname, "error: input buffer is not big enough for"
				" extended filename (base \"%s\", extension \"%s\").\n",
				name, ext);
			return(NULL);
		}
		rtn = buf;
	}
	strcpy(rtn, name);
	if ((ptr = strrchr(rtn, '.')) != NULL)
		*++ptr = '\0';
	else
		strcat(rtn, ".");
	strcat(rtn, ext);
	return(rtn);
}

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

Bool ccom_log_command_line(char *progname, u32 argc, char **argv)
{
	FILE	*f;
	u32		par;

	if ((f = fopen(CCOM_LOG_FILENAME, "a")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" in the current"
			" directory for command line logging.\n", CCOM_LOG_FILENAME);
		return(False);
	}
	fprintf(f, "%s: %s:", stime_timestamp(), progname);
	for (par = 0; par < argc; ++par)
		fprintf(f, " %s", argv[par]);
	fprintf(f, "\n");
	fclose(f);
	return(True);
}

/* Routine:	ccom_proc_u8
 * Purpose:	Process U8 image into output file
 * Inputs:	*data	Pointer to the data array
 *			rows, cols	Size of the data array
 *			*tiff	Pointer to the TIFF data structure
 *			*flg	Flags structure
 * Outputs:	Converts data into TIFF structure, ready to be written out.
 *			Returns success as a Bool.
 * Comment:	This routine understands the verbose and logtrans flags, and ignores
 *			the writefloat and force8 flags.
 */

static Bool ccom_proc_u8(u8 *data, u32 rows, u32 cols, TIFF *tiff,
						TIFFFlags *flg)
{
	u32		pel, npels = rows*cols, row, min, max, text_count = 0;
	f32		scale, offset;
	char	buffer[1024];
	
	if (flg->writefloat)
		error_msg(modname, "warning: -f ignored with U8 inputs.\n");
	if (flg->force8)
		error_msg(modname, "warning: -8 superfluous with U8 inputs.\n");

	TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8);
	text_count += sprintf(buffer+text_count, "Converted from 8bpp");
	if (flg->logtrans) {
		min = 256; max = -1;
		for (pel = 0; pel < npels; ++pel) {
			min = (data[pel] < min) ? data[pel] : min;
			max = (data[pel] > max) ? data[pel] : max;
		}
		if (min == 0) offset = (f32)log(0.5); else offset = (f32)log(min);
		scale = 255.f/((f32)log(max) - offset);
		if (flg->verbose)
			error_msgv(modname,
				"U8 logscale: min = %d max = %d offset = %f scale = %f\n",
				min, max, offset, scale);
		text_count += sprintf(buffer+text_count,
						", with logscale on domain (%d,%d), range (%f,%f), "
						"(offset,scale) = (%f, %f)\n", min, max, offset,
						log(max), offset, scale);
		for (pel = 0; pel < npels; ++pel)
			if (data[pel] == 0) data[pel] = (u8)log(0.5);
			else
				data[pel] = (u8)(scale*((f32)log(data[pel]) - offset));
	}
	TIFFSetField(tiff, TIFFTAG_IMAGEDESCRIPTION, buffer);
	for (row = 0; row < rows; ++row, data += cols)
		TIFFWriteScanline(tiff, data, row, 0);
	return(True);
}

/* Routine:	ccom_proc_u16
 * Purpose:	Process U16 image into output file
 * Inputs:	*data	Pointer to the data array
 *			rows, cols	Size of the data array
 *			*tiff	Pointer to the TIFF data structure
 *			*flg	Flags structure
 * Outputs:	Converts data into TIFF structure, ready to be written out.
 *			Returns success as Bool
 * Comment:	This routine understands the verbose, logtrans, and force8 flags,
 *			but ignores the writefloat flag.
 */

static Bool ccom_proc_u16(u16 *data, u32 rows, u32 cols, TIFF *tiff,
							TIFFFlags *flg)
{
	u32		pel, npels = rows*cols, row, col, min, max, text_count = 0;
	f32		scale, offset;
	char	buffer[1024];
	u8		*line;
	
	if (flg->writefloat)
		error_msg(modname, "warning: -f ignored with U16 inputs.\n");

	text_count += sprintf(buffer+text_count, "Converted from 16bpp");
	if (flg->force8)
		text_count += sprintf(buffer+text_count, ", compressed to 8bpp");
		
	if (flg->logtrans) {
		min = 65536; max = 0;
		for (pel = 0; pel < npels; ++pel) {
			min = (data[pel] < min) ? data[pel] : min;
			max = (data[pel] > max) ? data[pel] : max;
		}
		if (min == 0) offset = (f32)log(0.5); else offset = (f32)log(min);
		scale = 65535.f/((f32)log(max) - offset);
		if (flg->verbose)
			error_msgv(modname,
				"U16 logscale: min = %d max = %d offset = %f scale = %f\n",
				min, max, offset, scale);
		text_count += sprintf(buffer+text_count,
						", with logscale on domain (%d,%d), range (%f,%f), "
						"(offset,scale) = (%f, %f)", min, max, offset,
						log(max), offset, scale);
		for (pel = 0; pel < npels; ++pel)
			if (data[pel] == 0) data[pel] = (u16)log(0.5);
			else
				data[pel] = (u16)(scale*(log(data[pel]) - offset));
	}
	if (flg->force8) {
		TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8);
		if ((line = (u8*)malloc(cols)) == NULL) {
			error_msg(modname, "failed to allocate U8 conversion buffer.\n");
			return(False);
		}
		min = 65536; max = 0;
		for (pel = 0; pel < npels; ++pel) {
			min = (data[pel] < min) ? data[pel] : min;
			max = (data[pel] > max) ? data[pel] : max;
		}
		offset = (f32)min; scale = 255.f/(max - min);
		if (flg->verbose)
			error_msgv(modname,
				"U16->U8 min = %d max = %d offset = %f scale = %f\n",
				min, max, offset, scale);
		text_count += sprintf(buffer+text_count,
						", with U8 re-scale (min,max) = (%d,%d), (offset,scale)=(%f,%f)",
						min, max, offset, scale);
		for (row = 0; row < rows; ++row) {
			for (col = 0; col < cols; ++col)
				line[col] = (u8)(scale*(*data++ - offset));
			TIFFWriteScanline(tiff, line, row, 0);
		}
		free(line);
	} else {
		TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16);
		for (row = 0; row < rows; ++row, data += cols)
			TIFFWriteScanline(tiff, data, row, 0);
	}
	TIFFSetField(tiff, TIFFTAG_IMAGEDESCRIPTION, buffer);
	return(True);
}

/* Routine:	ccom_proc_f32
 * Purpose:	Transform input R4 into TIFF
 * Inputs:	*data	Pointer to the data array
 *			rows, cols	Size of the data array
 *			*tiff	Pointer to the TIFF structure
 *			*flg	Pointer to flags structure
 * Outputs:	Success as a Bool
 *			Converts data into TIFF structure ready for writing out.
 * Comment:	This routine accepts the verbose, writefloat, force8 and logtrans
 *			flags, applying the log transform before converting to output format
 *			(if requested).  Default output is 16bpp.
 */

static Bool ccom_proc_f32(f32p data, u32 rows, u32 cols, TIFF *tiff,
							TIFFFlags *flg)
{
	u32		row, col, pel, npels = rows*cols, text_count = 0;
	u8		*line8;
	u16		*line16;
	f32		min, max, scale, offset, maxop;
	char	buffer[1024];
	
	text_count += sprintf(buffer+text_count, "Converted from float");
	if (flg->force8)
		text_count += sprintf(buffer+text_count, ", compressed to 8bpp");
	else
		text_count += sprintf(buffer+text_count, ", compressed to 16bpp");
	
	if (flg->logtrans) {
		min = FLT_MAX;
		for (pel = 0; pel < npels; ++pel)
			min = (data[pel] < min) ? data[pel] : min;
		min -= 0.5; /* Avoid log(0) */
		if (flg->verbose)
			error_msgv(modname, "F32 logscale offset = %f\n", min);
		text_count += sprintf(buffer+text_count, ", with F32 logscale offset %f",
							min);
		for (pel = 0; pel < npels; ++pel)
			data[pel] = (f32)log(data[pel] - min);
	}
	if (flg->writefloat) {
		TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
		TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 32);
		for (row = 0; row < rows; ++row, data += cols)
			TIFFWriteScanline(tiff, data, row, 0);
	} else {
		if (flg->force8) {
			maxop = 255.f;
			TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8);
			if ((line8 = (u8*)malloc(cols)) == NULL) {
				error_msg(modname,
					"failed allocating F32->U8 conversion array.\n");
				return(False);
			}
		} else {
			maxop = 65535.f;
			TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 16);
			if ((line16 = (u16*)malloc(2*cols)) == NULL) {
				error_msg(modname,
					"failed allocating F32->U16 conversion array.\n");
				return(False);
			}
		}
		min = FLT_MAX; max = -FLT_MAX;
		for (pel = 0; pel < npels; ++pel) {
			min = (data[pel] < min) ? data[pel] : min;
			max = (data[pel] > max) ? data[pel] : max;
		}
		offset = min; scale = maxop/(max-min);
		if (flg->verbose)
			error_msgv(modname,
				"F32->U%d scale: min = %f max = %f offset = %f scale = %f\n",
				flg->force8?8:16, min, max, offset, scale);
		text_count += sprintf(buffer+text_count,
						", with U%d re-scale (min,max) = (%f,%f), "
						"(offset,scale)=(%f,%f)",
						flg->force8?8:16,min, max, offset, scale);
		if (flg->force8) {
			for (row = 0; row < rows; ++row) {
				for (col = 0; col < cols; ++col)
					line8[col] = (u8)(scale*(*data++ - offset));
				TIFFWriteScanline(tiff, line8, row, 0);
			}
			free(line8);
		} else {
			for (row = 0; row < rows; ++row) {
				for (col = 0; col < cols; ++col)
					line16[col] = (u16)(scale*(*data++ - offset));
				TIFFWriteScanline(tiff, line16, row, 0);
			}
			free(line16);
		}
	}
	TIFFSetField(tiff, TIFFTAG_IMAGEDESCRIPTION, buffer);
	return(True);
}

/* Routine: ccom_proc_rgb
 * Purpose:	Write out an RGB image to TIFF file
 * Inputs:	*data	Pointer to the RGB triples
 *			rows	Height of image
 *			cols	Width of image
 *			*tiff	TIFF file currently open for writing
 * Outputs:	True if file was written, else False
 * Comment:	This ignores all flags, and just writes the data in row major
 *			(PLANARCONFIG_CONTIG) order.
 */

static Bool ccom_proc_rgb(u8p data, u32 rows, u32 cols, TIFF *tiff)
{
	u32		row;
	
	TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 3);
	TIFFSetField(tiff, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField(tiff, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
	TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
	
	for (row = 0; row < rows; ++row, data += 3*cols)
		TIFFWriteScanline(tiff, data, row, 0);
	return(True);
}

/* Routine:	ccom_write_tiff
 * Purpose:	Write a generic TIFF output with processing options
 * Inputs:	*dirname	Offset directory, or NULL if none
 *			*name		Name of the file to write
 *			*data		Opaque pointer to the data to be written
 *			type		Type of the data elements
 *			rows, cols	Sizes of the data (row major format)
 *			*flg		Pointer to a TIFFFlag structure indicating processing
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

Bool ccom_write_tiff(char *dirname, char *name, void *data, TIFFType type,
			   		 u32 rows, u32 cols, TIFFFlags *flg)
{
	char	*buffer;
	Bool	rc;
	TIFF	*tiff;
	TIFFFlags	defflg = { True, False, False, False };
	
	if (flg == NULL) flg = &defflg;
	if (dirname != NULL) {
		if ((buffer = (char*)malloc(strlen(dirname)+strlen(name)+2)) == NULL) {
			error_msg(modname, "failed to allocate buffer for names.\n");
			return(False);
		}
		sprintf(buffer, "%s/%s", dirname, name);
	} else
		buffer = name;

	if ((tiff = TIFFOpen(buffer, "wb")) == NULL) {
		error_msgv(modname, "could not open \"%s\" for output.\n", buffer);
		if (dirname != NULL) free(buffer);
		return(False);
	}
	TIFFSetField(tiff, TIFFTAG_IMAGEWIDTH, cols);
	TIFFSetField(tiff, TIFFTAG_IMAGELENGTH, rows);
	TIFFSetField(tiff, TIFFTAG_SAMPLESPERPIXEL, 1);
	TIFFSetField(tiff, TIFFTAG_DOCUMENTNAME, name);
	TIFFSetField(tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField(tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK);
	if (type == TIFF_U8)	
		rc = ccom_proc_u8((u8p)data, rows, cols, tiff, flg);
	else if (type == TIFF_U16)
		rc = ccom_proc_u16((u16p)data, rows, cols, tiff, flg);
	else if (type == TIFF_F32)
		rc = ccom_proc_f32((f32p)data, rows, cols, tiff, flg);
	else if (type == TIFF_RGB)
		rc = ccom_proc_rgb((u8p)data, rows, cols, tiff);
	else {
		error_msgv(modname, "TIFF data type %d unknown.\n", type);
		return(False);
	}
	if (dirname != NULL) free(buffer);
	if (rc) TIFFClose(tiff);
	return(rc);
}

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

void *ccom_read_tiff(char *name, TIFFType *type, u32 *rows, u32 *cols)
{
	TIFF	*ip;
	u16		spp, bps, planar_config, photo_interp, samp_form;
	u8		*buffer, *insert;
	u32		reqd_size, row, offset;
	
	if ((ip = TIFFOpen(name, "r")) == NULL) {
		error_msgv(modname, "failed opening \"%s\" as TIFF input.\n", name);
		return(NULL);
	}
	TIFFGetField(ip, TIFFTAG_IMAGEWIDTH, cols);
	TIFFGetField(ip, TIFFTAG_IMAGELENGTH, rows);
	TIFFGetField(ip, TIFFTAG_SAMPLESPERPIXEL, &spp);
	TIFFGetField(ip, TIFFTAG_BITSPERSAMPLE, &bps);
	TIFFGetField(ip, TIFFTAG_PLANARCONFIG, &planar_config);
	TIFFGetField(ip, TIFFTAG_PHOTOMETRIC, &photo_interp);
	
	/* Check that the TIFF is something that we understand */
	if (spp != 1 || planar_config != PLANARCONFIG_CONTIG ||
		photo_interp != PHOTOMETRIC_MINISBLACK) {
		error_msgv(modname, "TIFF file \"%s\" if not monochrome (spp=%d), "
			"contiguous (pc=%d (%d)) and min=black (pi=%d (%d)).  "
			"Failing open.\n", name, spp, planar_config, PLANARCONFIG_CONTIG,
			photo_interp, PHOTOMETRIC_MINISBLACK);
		TIFFClose(ip);
		return(NULL);
	}
	
	switch (bps) {
		case 8:
			*type = TIFF_U8;
			reqd_size = (u32)(*rows) * (*cols);
			offset = *cols;
			break;
		case 16:
			*type = TIFF_U16;
			reqd_size = ((u32)(*rows) * (*cols))*sizeof(u16);
			offset = (u32)(*cols) * sizeof(u16);
			break;
		case 32:
			TIFFGetField(ip, TIFFTAG_SAMPLEFORMAT, &samp_form);
			if (samp_form != SAMPLEFORMAT_IEEEFP) {
				error_msg(modname, "samples are 32-bit, but not floating point"
					" --- unknown interpretation.\n");
				TIFFClose(ip);
				return(NULL);
			}
			*type = TIFF_F32;
			reqd_size = ((u32)(*rows) * (*cols))*sizeof(f32);
			offset = (u32)(*cols) * sizeof(f32);
			break;
		default:
			error_msgv(modname, "TIFF bits per sample = %d (not 8, 16, or 32)"
				"\n", bps);
			TIFFClose(ip);
			return(NULL);
	}
	
	if ((buffer = (u8p)malloc(reqd_size)) == NULL) {
		error_msgv(modname, "failed allocating buffer (%d bytes) for TIFF.\n",
			reqd_size);
		TIFFClose(ip);
		return(NULL);
	}
	
	for (row = 0, insert = buffer; row < *rows; ++row, insert += offset) {
		if (TIFFReadScanline(ip, insert, row, 0) < 0) {
			error_msgv(modname, "failed reading row %d of \"%s\".\n",
				row, name);
			TIFFClose(ip);
			free(buffer);
			return(NULL);
		}
	}
	
	TIFFClose(ip);
	return((void*)buffer);
}

static void ccom_min_max_u8(u8 *data, u32 npels, f64p min, f64p max)
{
	u8	umin = 255, umax = 0, no_data;
	u32	pel;
	
	mapsheet_get_invalid(MAP_DATA_U8, &no_data);
	for (pel = 0; pel < npels; ++pel) {
		if (data[pel] == no_data) continue;
		umin = MIN(umin, data[pel]);
		umax = MAX(umax, data[pel]);
	}
	*min = umin; *max = umax;
}

static void ccom_min_max_u16(u16 *data, u32 npels, f64p min, f64p max)
{
	u16	umin = 65535, umax = 0, no_data;
	u32	pel;
	
	mapsheet_get_invalid(MAP_DATA_U16, &no_data);
	for (pel = 0; pel < npels; ++pel) {
		if (data[pel] == no_data) continue;
		umin = MIN(umin, data[pel]);
		umax = MAX(umax, data[pel]);
	}
	*min = umin; *max = umax;
}

static void ccom_min_max_u32(u32 *data, u32 npels, f64p min, f64p max)
{
	u32	umin = INT_MAX, umax = 0, no_data;
	u32	pel;
	
	mapsheet_get_invalid(MAP_DATA_U32, &no_data);
	for (pel = 0; pel < npels; ++pel) {
		if (data[pel] == no_data) continue;
		umin = MIN(umin, data[pel]);
		umax = MAX(umax, data[pel]);
	}
	*min = umin; *max = umax;
}

static void ccom_min_max_f32(f32 *data, u32 npels, f64p min, f64p max)
{
	f32	umin = FLT_MAX, umax = -FLT_MAX, no_data;
	u32	pel;
	
	mapsheet_get_invalid(MAP_DATA_F32, &no_data);
	for (pel = 0; pel < npels; ++pel) {
		if (data[pel] == no_data) continue;
		umin = MIN(umin, data[pel]);
		umax = MAX(umax, data[pel]);
	}
	*min = umin; *max = umax;
}

static void ccom_min_max_f64(f64 *data, u32 npels, f64p min, f64p max)
{
	f64	umin = DBL_MAX, umax = -DBL_MAX, no_data;
	u32	pel;
	
	mapsheet_get_invalid(MAP_DATA_F64, &no_data);
	for (pel = 0; pel < npels; ++pel) {
		if (data[pel] == no_data) continue;
		umin = MIN(umin, data[pel]);
		umax = MAX(umax, data[pel]);
	}
	*min = umin; *max = umax;
}

static void ccom_write_gutm_u8(FILE *f, u8 *data, u32 rows, u32 cols, f64 base)
{
	u8	no_data;
	s32	row, col;
	s32	first, last;
	
	mapsheet_get_invalid(MAP_DATA_U8, &no_data);
	data += (rows-1)*cols;
	for (row = rows-1; row >= 0; --row, data -= cols) {
		/* Find the first valid data point */
		first = 0;
		while (first < (s32)cols && data[first] == no_data) ++first;
		
		/* Find the last valid data point */
		last = cols-1;
		while (last >= 0 && data[last] == no_data) --last;
		
		if (last < first) first = last = 0; /* No valid data in line */
		
		/* Write headers and data points between limits */
		fprintf(f, "%d %d\n", first, last+1);
			/* Note that we have to have last+1 here because the GZ3D input
			 * routine for V1.0 GUTMs has for (row = start; row < last; ++row)
			 * but needs a point on the end of the data row to terminate the
			 * read.  Hence, if you want to read five points, you need '1 6'
			 * in the file, not '1 5'.  Wierd.
			 */
		for (col = first; col <= last; ++col)
			if (data[col] == no_data)
				fprintf(f, "0.0\n");
			else
				fprintf(f, "%lf\n", (f64)data[col] - base);
	}
}

static void ccom_write_gutm_u16(FILE *f, u16 *data, u32 rows, u32 cols, f64 base)
{
	u16	no_data;
	s32	row, col;
	s32	first, last;
	
	mapsheet_get_invalid(MAP_DATA_U16, &no_data);
	data += (rows-1)*cols;
	for (row = rows-1; row >= 0; --row, data -= cols) {
		/* Find the first valid data point */
		first = 0;
		while (first < (s32)cols && data[first] == no_data) ++first;
		
		/* Find the last valid data point */
		last = cols-1;
		while (last >= 0 && data[last] == no_data) --last;
		
		if (last < first) first = last = 0; /* No valid data in line */
		
		/* Write headers and data points between limits */
		fprintf(f, "%d %d\n", first, last+1);
			/* Note that we have to have last+1 here because the GZ3D input
			 * routine for V1.0 GUTMs has for (row = start; row < last; ++row)
			 * but needs a point on the end of the data row to terminate the
			 * read.  Hence, if you want to read five points, you need '1 6'
			 * in the file, not '1 5'.  Wierd.
			 */
		for (col = first; col <= last; ++col)
			if (data[col] == no_data)
				fprintf(f, "0.0\n");
			else
				fprintf(f, "%lf\n", (f64)data[col] - base);
	}
}

static void ccom_write_gutm_u32(FILE *f, u32 *data, u32 rows, u32 cols, f64 base)
{
	u32	no_data;
	s32	row, col;
	s32	first, last;
	
	mapsheet_get_invalid(MAP_DATA_U32, &no_data);
	data += (rows-1)*cols;
	for (row = rows-1; row >= 0; --row, data -= cols) {
		/* Find the first valid data point */
		first = 0;
		while (first < (s32)cols && data[first] == no_data) ++first;
		
		/* Find the last valid data point */
		last = cols-1;
		while (last >= 0 && data[last] == no_data) --last;
		
		if (last < first) first = last = 0; /* No valid data in line */
		
		/* Write headers and data points between limits */
		fprintf(f, "%d %d\n", first, last+1);
			/* Note that we have to have last+1 here because the GZ3D input
			 * routine for V1.0 GUTMs has for (row = start; row < last; ++row)
			 * but needs a point on the end of the data row to terminate the
			 * read.  Hence, if you want to read five points, you need '1 6'
			 * in the file, not '1 5'.  Wierd.
			 */
		for (col = first; col <= last; ++col)
			if (data[col] == no_data)
				fprintf(f, "0.0\n");
			else
				fprintf(f, "%lf\n", (f64)data[col] - base);
	}
}

static void ccom_write_gutm_f32(FILE *f, f32 *data, u32 rows, u32 cols, f64 base)
{
	f32	no_data;
	s32	row, col;
	s32	first, last;
	
	mapsheet_get_invalid(MAP_DATA_F32, &no_data);
	data += (rows-1)*cols;
	for (row = rows-1; row >= 0; --row, data -= cols) {
		/* Find the first valid data point */
		first = 0;
		while (first < (s32)cols && data[first] == no_data) ++first;
		
		/* Find the last valid data point */
		last = cols-1;
		while (last >= 0 && data[last] == no_data) --last;
		
		if (last < first) first = last = 0; /* No valid data in line */
		
		/* Write headers and data points between limits */
		fprintf(f, "%d %d\n", first, last+1);
			/* Note that we have to have last+1 here because the GZ3D input
			 * routine for V1.0 GUTMs has for (row = start; row < last; ++row)
			 * but needs a point on the end of the data row to terminate the
			 * read.  Hence, if you want to read five points, you need '1 6'
			 * in the file, not '1 5'.  Wierd.
			 */
		for (col = first; col <= last; ++col)
			if (data[col] == no_data)
				fprintf(f, "0.0\n");
			else
				fprintf(f, "%lf\n", data[col] - base);
	}
}

static void ccom_write_gutm_f64(FILE *f, f64 *data, u32 rows, u32 cols, f64 base)
{
	f64	no_data;
	s32	row, col;
	s32	first, last;
	
	mapsheet_get_invalid(MAP_DATA_F64, &no_data);
	data += (rows-1)*cols;
	for (row = rows-1; row >= 0; --row, data -= cols) {
		/* Find the first valid data point */
		first = 0;
		while (first < (s32)cols && data[first] == no_data) ++first;
		
		/* Find the last valid data point */
		last = cols-1;
		while (last >= 0 && data[last] == no_data) --last;
		
		if (last < first) first = last = 0; /* No valid data in line */
		
		/* Write headers and data points between limits */
		fprintf(f, "%d %d\n", first, last+1);
			/* Note that we have to have last+1 here because the GZ3D input
			 * routine for V1.0 GUTMs has for (row = start; row < last; ++row)
			 * but needs a point on the end of the data row to terminate the
			 * read.  Hence, if you want to read five points, you need '1 6'
			 * in the file, not '1 5'.  Wierd.
			 */
		for (col = first; col <= last; ++col)
			if (data[col] == no_data)
				fprintf(f, "0.0\n");
			else
				fprintf(f, "%lf\n", data[col] - base);
	}
}
static void ccom_hsv_to_rgb(f32 h, f32 s, f32 v, u8 *r, u8 *g, u8 *b)
{
	s32		i;
	f64		f, p, q, t, rr, gg, bb;
	
	if (s == 0.0)
		*r = *g = *b = (u8)floor(255.999 * v);
	else {
		while (h >= 360.0) h -= 360.0;
		h /= 60.0;
		i = (s32)h;
		f = h - i;
		p = v*(1.0 - s);
		q = v*(1.0 - (s*f));
		t = v*(1.0 - (s * (1.0 - f)));
		switch(i) {
			case 0:
				rr = v; gg = t; bb = p;
				break;
			case 1:
				rr = q; gg = v; bb = p;
				break;
			case 2:
				rr = p; gg = v; bb = t;
				break;
			case 3:
				rr = p; gg = q; bb = v;
				break;
			case 4:
				rr = t; gg = p; bb = v;
				break;
			case 5:
				rr = v; gg = p; bb = q;
				break;
		}
		*r = (rr < 0.0) ? 0 : (u8)floor(rr * 255.999);
		*g = (gg < 0.0) ? 0 : (u8)floor(gg * 255.999);
		*b = (bb < 0.0) ? 0 : (u8)floor(bb * 255.999);
	}
}

/* Routine:	ccom_write_clut
 * Purpose:	Write a Colour-LookUp Table (CLUT) for GeoZui3D
 * Inputs:	*file 			GdpFILE to work with
 *			min, max		Minimum and maximum values in data range
 *			clut_entries	Number of CLUT entries to write for range
 * Outputs:	True unless write fails
 * Comment:	This builds a standard rainbow pattern from cools to hots and scales
 *			according to the dynamic range of the data observed.
 */

static Bool ccom_write_clut(FILE *file, f64 min, f64 max, u32 clut_entries)
{
	u8	r, g, b;
	f32	hue, depth, range;
	u32	entry;
	
	range = (f32)(max - min);
	
	fprintf(file, "clut1.0\n");
	for (entry = 0; entry < clut_entries; ++entry) {
		hue = 300.0f - 300.0f*entry/(clut_entries - 1.0f);
		depth = (f32)min + range*entry/(clut_entries - 1.0f);
		ccom_hsv_to_rgb(hue, 1.f, 1.f, &r, &g, &b);
		fprintf(file, "%f %d %d %d 255\n", depth, (u32)r, (u32)g, (u32)b);
	}
	fprintf(file, "done\n");
	return(True);
}

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

Bool ccom_write_gutm(char *name, void *data, DataType type, u32 rows, u32 cols,
					 f32 spacing, f64 x0, f64 y0)
{
	f64		min, max;
	FILE	*f;
	time_t	stamp;
	char	*clut_name;
	
	if ((f = fopen(name, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			name);
		return(False);
	}
	
	switch(type) {
		case DATA_U8:
			ccom_min_max_u8(data, rows*cols, &min, &max);
			break;
		case DATA_U16:
			ccom_min_max_u16(data, rows*cols, &min, &max);
			break;
		case DATA_U32:
			ccom_min_max_u32(data, rows*cols, &min, &max);
			break;
		case DATA_F32:
			ccom_min_max_f32(data, rows*cols, &min, &max);
			break;
		case DATA_F64:
			ccom_min_max_f64(data, rows*cols, &min, &max);
			break;
		default:
			error_msgv(modname, "error: data type (%d) not known.\n",
				(u32)type);
			fclose(f);
			return(False);
			break;
	}

	min -= 1.0;	/* Offset so that data at the minimum value isn't 0.0 (which
				 * is interpreted as 'no-data'.
				 */
	time(&stamp);
	fprintf(f, "# Generated by libccom at %s", ctime(&stamp));
	fprintf(f, "BEGIN GUTM 1.0\nCOLS_ROWS %d %d\nCELLSIZE %f\nSHAPE PARTIAL\n",
		cols, rows, spacing);
	fprintf(f, "LOCAL_ORIGIN %f %f %f\n", x0, y0 - (rows-1)*spacing, min);
	
	switch(type) {
		case DATA_U8:
			ccom_write_gutm_u8(f, data, rows, cols, min);
			break;
		case DATA_U16:
			ccom_write_gutm_u16(f, data, rows, cols, min);
			break;
		case DATA_U32:
			ccom_write_gutm_u32(f, data, rows, cols, min);
			break;
		case DATA_F32:
			ccom_write_gutm_f32(f, data, rows, cols, min);
			break;
		case DATA_F64:
			ccom_write_gutm_f64(f, data, rows, cols, min);
			break;
		default:
			error_msgv(modname, "error: data type (%d) not known.\n",
				(u32)type);
			fclose(f);
			return(False);
			break;
	}
	fclose(f);
	
	/* Make a CLUT to go with the GUTM.  Note that we don't make a scene file,
	 * since the user may want to integrate this with point data, etc.  However,
	 * making the CLUT allows the user to readily colour the surface if needed.
	 */
	if ((clut_name = ccom_make_filename(name, "clut", NULL, 0)) == NULL) {
		error_msgv(modname, "error: failed to make name for GUTM's associated"
			" CLUT (based on \"%s\").  Out of memory.\n", name);
		return(False);
	}
	if ((f = fopen(clut_name, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for GUTM's associated"
			" CLUT file.\n", clut_name);
		free(clut_name);
		return(False);
	}
	if (!ccom_write_clut(f, min, max, 100)) {
		error_msgv(modname, "error: failed to write CLUT associated with GUTM"
			" (as \"%s\").\n", clut_name);
		fclose(f);
		free(clut_name);
		return(False);
	}
	return(True);
}

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

Bool ccom_gutm_write(GUTM *gutm, char *name)
{
	f64		min, max;
	FILE	*f;
	char	*clut_name;
	f32		invalid, *data, *altdata;
	s32		row, col, first, last;
	
	if ((f = fopen(name, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			name);
		return(False);
	}
	
	ccom_min_max_f32(gutm->data, gutm->rows*gutm->cols, &min, &max);

	min -= 1.0;	/* Offset so that data at the minimum value isn't 0.0 (which
				 * is interpreted as 'no-data'.
				 */
	fprintf(f, "# Generated by libccom at %s\n", stime_timestamp());
	fprintf(f, "BEGIN GUTM 1.0\nCOLS_ROWS %d %d\nCELLSIZE %f\nSHAPE PARTIAL\n",
		gutm->cols, gutm->rows, gutm->cell_size);
	if (gutm->altdata != NULL)
		fprintf(f, "USE_ALTERNATE_DATA\n");
	fprintf(f, "LOCAL_ORIGIN %lf %lf %lf\n", gutm->x0, gutm->y0, min);
	
	mapsheet_get_invalid(MAP_DATA_F32, &invalid);
	data = gutm->data + (gutm->rows-1)*gutm->cols;
	if (gutm->altdata != NULL)
			altdata = gutm->altdata + (gutm->rows-1)*gutm->cols;
	else
		altdata = data;	/* This isn't used but makes the rest of the code simpler */

	for (row = gutm->rows-1; row >= 0; --row, data -= gutm->cols, altdata -= gutm->cols) {
		/* Find the first valid data point */
		first = 0;
		while (first < (s32)gutm->cols && data[first] == invalid) ++first;
		
		/* Find the last valid data point */
		last = gutm->cols-1;
		while (last >= 0 && data[last] == invalid) --last;
		
		if (last < first) first = last = 0; /* No valid data in line */
		
		/* Write headers and data points between limits */
		fprintf(f, "%d %d\n", first, last+1);
			/* Note that we have to have last+1 here because the GZ3D input
			 * routine for V1.0 GUTMs has for (row = start; row < last; ++row)
			 * but needs a point on the end of the data row to terminate the
			 * read.  Hence, if you want to read five points, you need '1 6'
			 * in the file, not '1 5'.  Wierd.
			 */
		for (col = first; col <= last; ++col) {
			if (data[col] == invalid)
				fprintf(f, "0.0");
			else
				fprintf(f, "%.2lf", data[col] - min);
			if (gutm->altdata != NULL) {
				if (altdata[col] == invalid)
					fprintf(f, " 0.0\n");
				else
					fprintf(f, " %.2lf\n", altdata[col]);
			} else
				fprintf(f, "\n");
		}
	}
	
	fclose(f);
	
	/* Make a CLUT to go with the GUTM.  Note that we don't make a scene file,
	 * since the user may want to integrate this with point data, etc.  However,
	 * making the CLUT allows the user to readily colour the surface if needed.
	 */
	if ((clut_name = ccom_make_filename(name, "clut", NULL, 0)) == NULL) {
		error_msgv(modname, "error: failed to make name for GUTM's associated"
			" CLUT (based on \"%s\").  Out of memory.\n", name);
		return(False);
	}
	if ((f = fopen(clut_name, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for GUTM's associated"
			" CLUT file.\n", clut_name);
		free(clut_name);
		return(False);
	}
	if (!ccom_write_clut(f, min, max, 100)) {
		error_msgv(modname, "error: failed to write CLUT associated with GUTM"
			" (as \"%s\").\n", clut_name);
		fclose(f);
		free(clut_name);
		return(False);
	}
	return(True);
}

/* Routine:	ccom_gutm_release
 * Purpose:	Release all memory associated with a GUTM structure
 * Inputs:	*gutm	Structure to work through
 * Outputs:	-
 * Comment:	This de-allocates all memory associated with the data, including
 *			the base GUTM structure.
 */

void ccom_gutm_release(GUTM *gutm)
{
	if (gutm == NULL) return;
	free(gutm->data);
	free(gutm->altdata);	/* Set to NULL if not used, so safe. */
	free(gutm);
}

/* Routine:	ccom_gutm_new_blank
 * Purpose:	Allocate memory for blank GUTM structure
 * Inputs:	-
 * Outputs:	Pointer to blank, or NULL on failure
 * Comment:	The structure is initialised to 0 on return.
 */

GUTM *ccom_gutm_new_blank(void)
{
	GUTM	*rtn;
	
	if ((rtn = (GUTM*)calloc(1, sizeof(GUTM))) == NULL) {
		error_msgv(modname, "error: failed to allocate base buffer for"
			" a GUTM (%d bytes).\n", sizeof(GUTM));
		return(NULL);
	}
	return(rtn);
}

/* Routine:	ccom_gutm_new
 * Purpose:	Allocate memory for a GUTM, including space for data
 * Inputs:	cols, rows	Size of the GUTM
 *			cell_size	Spacing of the cells in the GUTM
 *			x0, y0, z0	Georeferencing for the GUTM
 *			alt_data	Flag: True => allocate space for the altdata[] field
 * Outputs:	Pointer to the allocated array or NULL on failure
 * Comment:	Note that the data surfaces are not initialised.
 */

GUTM *ccom_gutm_new(u32 cols, u32 rows, f32 cell_size, f64 x0, f64 y0, f64 z0,
					Bool alt_data)
{
	GUTM	*rtn;
	u32		n_pels = rows * cols;
	
	if ((rtn = ccom_gutm_new_blank()) == NULL ||
		(rtn->data = (f32*)malloc(sizeof(f32)*n_pels)) == NULL ||
		(alt_data && (rtn->altdata = (f32*)malloc(sizeof(f32)*n_pels)) == NULL)){
		error_msgv(modname, "error: failed to allocate space for data surfaces"
			" in GUTM (%dx%dx%d bytes).\n", rows, cols, sizeof(f32));
		ccom_gutm_release(rtn);
		return(NULL);
	}
	rtn->rows = rows; rtn->cols = cols; rtn->cell_size = cell_size;
	rtn->x0 = x0; rtn->y0 = y0; rtn->z0 = z0;
	
	return(rtn);
}

/* Routine:	ccom_gutm_clone
 * Purpose:	Allocate another GUTM with same parameters as input
 * Inputs:	*gutm	GUTM to clone
 * Outputs:	Pointer to buffer of success, or NULL on failure
 * Comment:	This clones the parameters of the GUTM, but allocates more space
 *			for the data sections.
 */

GUTM *ccom_gutm_clone(GUTM *gutm)
{
	return(ccom_gutm_new(gutm->cols, gutm->rows, gutm->cell_size, gutm->x0,
					gutm->y0, gutm->z0, gutm->altdata != NULL ? True : False));
}

/* Routine:	ccom_gutm_read_header
 * Purpose:	Read the header of an open GUTM file
 * Inputs:	*ip	Pointer to FILE to read from
 * Outputs:	*grid_full	Flag: True => a full grid is specified in FILE
 *			*alt_data	Flag: True => alternative data (second column) exists
 * Comment:	The routine loops until it finds a BEGIN GUTM statement, returning
 *			failed if EOF happens first.  The GUTM returned only contains the
 *			header information, rather than having data space allocated.
 */

GUTM *ccom_gutm_read_header(FILE *ip, Bool *grid_full, Bool *alt_data)
{
	char	buffer[1024];
	Bool	header = False;
	GUTM	*rtn;

	if ((rtn = ccom_gutm_new_blank()) == NULL) {
		error_msgv(modname, "error: failed to allocate memory for GUTM header"
			" block (%d bytes).\n", sizeof(GUTM));
		return(NULL);
	}
	while (!header && !feof(ip)) {
		fgets(buffer, 1024, ip);
		if (strncmp(buffer, "BEGIN GUTM 1.0", 14) == 0)
			header = True;
	}
	if (!header) {
		error_msg(modname, "error: read through whole of file without"
			" finding a valid V1.0 GUTM header!.\n");
		ccom_gutm_release(rtn);
		return(NULL);
	}
	fgets(buffer, 1024, ip);
	if (sscanf(buffer, "COLS_ROWS %d %d", &rtn->cols, &rtn->rows) != 2) {
		error_msg(modname, "error: failed to read dimensions for GUTM.\n");
		ccom_gutm_release(rtn);
		return(NULL);
	}
	fgets(buffer, 1024, ip);
	if (sscanf(buffer, "CELLSIZE %f", &rtn->cell_size) != 1) {
		error_msg(modname, "error: failed to read cell-size for GUTM.\n");
		ccom_gutm_release(rtn);
		return(NULL);
	}
	fgets(buffer, 1024, ip);
	if (strncmp(buffer, "SHAPE", 5) != 0) {
		error_msg(modname, "error: failed reading SHAPE line for GUTM.\n");
		ccom_gutm_release(rtn);
		return(NULL);
	}
	if (strstr(buffer, "PARTIAL") != NULL)
		*grid_full = False;
	else if (strstr(buffer, "FULL") != NULL)
		*grid_full = True;
	else {
		error_msgv(modname, "error: failed to parse \"%s\" for SHAPE clause"
			" in GUTM.\n", buffer);
		ccom_gutm_release(rtn);
		return(NULL);
	}
	
	fgets(buffer, 1024, ip);
	if (strncmp(buffer, "USE_ALTERNATE_DATA", 18) == 0) {
		*alt_data = True;
		fgets(buffer, 1024, ip);
	} else
		*alt_data = False;

	if (sscanf(buffer, "LOCAL_ORIGIN %lf %lf %lf",
										&rtn->x0, &rtn->y0, &rtn->z0) != 3) {
		error_msg(modname, "error: failed to read LOCAL_ORIGIN for GUTM.\n");
		ccom_gutm_release(rtn);
		return(NULL);
	}
	return(rtn);
}

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

GUTM *ccom_gutm_read(char *name)
{
	s32		row, col, start, end, pel;
	Bool	grid_full, alt_data;
	FILE	*ip;
	f32		invalid_f32;
	GUTM	*rtn, *hdr;
	
	error_msgv(modname, "info: start reading GUTM \"%s\" at %s.\n",
		name, stime_timestamp());

	if ((ip = fopen(name, "r")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for input GUTM.\n",
			name);
		return(NULL);
	}
	if ((hdr = ccom_gutm_read_header(ip, &grid_full, &alt_data)) == NULL) {
		error_msgv(modname, "error: failed to read header of GUTM \"%s\".\n",
			name);
		fclose(ip);
		return(NULL);
	}
	if ((rtn = ccom_gutm_new(hdr->cols, hdr->rows, hdr->cell_size,
							hdr->x0, hdr->y0, hdr->z0, alt_data)) == NULL) {
		error_msgv(modname, "error: failed to get memory for data"
			" buffer for GUTM (%dx%dx%d bytes).\n",
			hdr->rows, hdr->cols, sizeof(f32));
		fclose(ip);
		ccom_gutm_release(hdr);
		return(NULL);
	}
	ccom_gutm_release(hdr);
	
	mapsheet_get_invalid(MAP_DATA_F32, &invalid_f32);
	for (pel = 0; pel < (s32)(rtn->cols*rtn->rows); ++pel) {
		rtn->data[pel] = invalid_f32;
		if (alt_data) rtn->altdata[pel] = invalid_f32;
	}
	
	for (row = rtn->rows-1; row >= 0; --row) {
		if (grid_full) {
			start = 0;
			end = rtn->cols;
		} else {
			if (fscanf(ip, "%d %d", &start, &end) != 2) {
				error_msgv(modname, "error: failed to read start/end for row %d"
					" of GUTM \"%s\".\n", row, name);
				fclose(ip);
				ccom_gutm_release(rtn);
				return(NULL);
			}
		}
		for (col = start; col < end; ++col) {
			if (alt_data) {
				if (fscanf(ip, "%f %f",
						rtn->data+row*rtn->cols+col,
						rtn->altdata+row*rtn->cols+col) != 2) {
					error_msgv(modname, "error: failed reading element (%d,%d)"
						" of GUTM \"%s\".\n", row, col, name);
					fclose(ip);
					ccom_gutm_release(rtn);
					return(NULL);
				}
			} else {
				if (fscanf(ip, "%f", rtn->data+row*rtn->cols + col) != 1) {
					error_msgv(modname, "error: failed reading element (%d,%d)"
						" of GUTM \"%s\".\n", row, col, name);
					fclose(ip);
					ccom_gutm_release(rtn);
					return(NULL);
				}
			}
			if (rtn->data[row*rtn->cols+col] == 0.0f) {
				rtn->data[row*rtn->cols+col] = invalid_f32;
				if (alt_data)
					rtn->altdata[row*rtn->cols+col] = invalid_f32;
			} else
				rtn->data[row*rtn->cols+col] += (f32)rtn->z0;
		}
	}
	fclose(ip);
	
	error_msgv(modname, "info: end reading GUTM \"%s\" at %s.\n",
				name, stime_timestamp());

	return(rtn);
}

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

Bool ccom_despike(f32 *data, u32 rows, u32 cols, f32 max_angle, f32 dx,
				  u32 nwidth, Bool deep_only, DespikeStat *stats)
{
	DespikeStat	s;
	s32	row, col, n_neighbours, buffer_height, half_wdth;
	s32	dr, dc;
	f32	invalid_data, mean, **buffer, *op, *tmp, *buffer_base, grad, grad_limit;

#ifdef __DEBUG__
	error_msgv(modname, "info: starting to de-spike data at %s.\n",
		stime_timestamp());
#endif

	memset(&s, 0, sizeof(DespikeStat));	/* Zero de-spiking statistics */
	
	grad_limit = (f32)tan(DEG2RAD(max_angle));
	
	/* This allocates the rolling-buffer for output.  We keep just enough
	 * memory to hold the output required to avoid over-writing the input
	 * context required for processing --- in this case, as wide as the DTM,
	 * but half a neighbourhood high.  This means that we drastically reduce
	 * the memory requirements, but pay a little extra in copying the data
	 * back from the buffer to the input once the processing point has moved
	 * forward enough.  Probably a reasonable trade-off since this might be done
	 * with a full map-sheet in memory ... a non-trivial memory burden.
	 */
	half_wdth = nwidth/2;
	buffer_height = half_wdth + 1;
	if ((buffer = (f32**)malloc(sizeof(f32*)*buffer_height)) == NULL ||
		(buffer_base = (f32*)malloc(sizeof(f32)*buffer_height*cols)) == NULL) {
		error_msgv(modname, "error: failed to allocate buffer for despiking"
			" (need %d rows x %d cols).\n", buffer_height, cols);
		if (buffer != NULL) free(buffer);
		return(False);
	}
	buffer[0] = buffer_base;
		/* Note that we need to keep hold of the buffer base location separately
		 * since, in shuffling the pointers during the rolling-buffer portion
		 * of the code, we loose track of what the base pointer was.
		 */

	for (row = 1; row < buffer_height; ++row)
		buffer[row] = buffer[row-1] + cols;
	op = data; /* For write-back --- after we roll the processed data out of
				* the buffer into the input section. */
	
	mapsheet_get_invalid(MAP_DATA_F32, &invalid_data);
	
	/* Initialise the first half_wdth lines of the buffer, since we can't
	 * get context there.  Of course, we usually have a mapsheet well outside
	 * the active region, so this isn't a major loss.
	 */
	for (row = 0; row < half_wdth; ++row)
		for (col = 0; col < (s32)cols; ++col)
			buffer[row][col] = invalid_data;
	
	for (row = half_wdth; row < (s32)rows-half_wdth; ++row) {
		/* Initialise start and end of row to invalid_data */
		for (col = 0; col < half_wdth; ++col) {
			buffer[buffer_height-1][col] = invalid_data;
			buffer[buffer_height-1][cols-1-col] = invalid_data;
		}
		for (col = half_wdth; col < (s32)cols-half_wdth; ++col) {
			if (data[row*cols + col] == invalid_data) {
				buffer[buffer_height-1][col] = invalid_data;
				++s.undefined_points;
				continue;
			}
			mean = 0.0f;
			n_neighbours = 0;
			for (dr = -half_wdth; dr <= half_wdth; ++dr) {
				for (dc = -half_wdth; dc <= half_wdth; ++dc) {
					if (dc == 0 && dr == 0) continue;	/* Don't use center */
					if (data[(row+dr)*cols+(col+dc)] == invalid_data) continue;
					mean += data[(row+dr)*cols+(col+dc)];
					++n_neighbours;
				}
			}
			if (n_neighbours == 0) {
				/* No neighbours --- so probably a stray sounding. */
#ifdef __DEBUG__
				error_msgv(modname, "info: isolated point at (%d, %d)."
					" Setting output to INVALID.\n", row, col);
#endif
				++s.isolated_points;
				buffer[buffer_height-1][col] = invalid_data;
			} else {
				/* Compute gradient and clip */
				mean /= n_neighbours;
				grad = (f32)fabs((data[row*cols + col] - mean)/dx);
				if (grad > grad_limit) {
#ifdef __DEBUG__
					error_msgv(modname, "info: removing spike at (%d, %d)"
						" with mean %.1f m, data %.1f m and gradient %.2f"
						" (angle %.2f deg.).\n",
						row, col, mean, data[row*cols+col], grad,
						RAD2DEG(atan(grad)));
#endif
					/* We remove the spike but leave a gap --- we don't really know what
					 * the true value is, just that it isn't this.  Leaving a gap makes
					 * this clear, although it is a bit of a cop-out.
					 */
					if (deep_only) {
						if (data[row*cols+col] < mean) { /* Remember, depths are -ve */
							buffer[buffer_height-1][col] = invalid_data;
							++s.deep_spikes;
						} else
							buffer[buffer_height-1][col] = data[row*cols+col];
					} else {
						buffer[buffer_height-1][col] = invalid_data;
						if (data[row*cols + col] >= mean)
							++s.shoal_spikes;
						else
							++s.deep_spikes;
					}
				} else
					buffer[buffer_height-1][col] = data[row*cols+col];
			}
		}
		/* Copy top of output buffer into the image at *op, then shuffle ptrs
		 * to roll the buffer forward.
		 */
		memcpy(op, buffer[0], sizeof(f32)*cols); op += cols;
		tmp = buffer[0];
		for (dr = 1; dr < buffer_height; ++dr)
			buffer[dr-1] = buffer[dr];
		buffer[buffer_height-1] = tmp;
	}
	/* Fill in the end of the buffer with invalid_data */
	for (row = (s32)rows-half_wdth; row < (s32)rows; ++row)
		for (col = 0; col < (s32)cols; ++col)
			data[row*cols + col] = invalid_data;
	
	free(buffer_base);
	free(buffer);
	
	/* Finalise computation of statistics */
	s.n_points = rows * cols;
	s.n_used_points = s.n_points - s.undefined_points;
	s.n_spikes_removed = s.shoal_spikes + s.deep_spikes;
	s.pcnt_defined = (100.0f*s.n_used_points)/s.n_points;
	s.pcnt_spikes = (100.0f*s.n_spikes_removed)/s.n_used_points;
	s.pcnt_shoal = (100.0f*s.shoal_spikes)/s.n_spikes_removed;
	s.pcnt_deep = (100.0f*s.deep_spikes)/s.n_spikes_removed;
	
#ifdef __DEBUG__
	error_msgv(modname, "info: finished de-spiking data at %s.\n",
		stime_timestamp());
	error_msgv(modname, "info: summary: %d nodes total, %d unused (%d used, "
		"%.2f%%).\n", s.n_points, s.undefined_points, s.n_used_points,
		s.pcnt_defined);
	error_msgv(modname, "info: summary: %d isolated points set to INVALID.\n",
		s.isolated_points);
	error_msgv(modname, "info: summary: %d spikes total (%.2f%% of used pts),"
		" %d shoal (%.2f%%), %d deep (%.2f%%).\n",
		s.n_spikes_removed, s.pcnt_spikes,
		s.shoal_spikes, s.pcnt_shoal, s.deep_spikes, s.pcnt_deep);
#endif

	if (stats != NULL) memcpy(stats, &s, sizeof(DespikeStat));

	return(True);
}

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

u8 *ccom_threshold(f32 *data, u32 rows, u32 cols, Bool flip, f32 thresh, u8 *op)
{
	u8	*rtn, above, below;
	u32	pel, npels = rows * cols;
	
	if (op == NULL) {
		if ((rtn = (u8*)malloc(sizeof(u8)*npels)) == NULL) {
			error_msgv(modname, "error: failed to allocate buffer (%d bytes)"
				" for thresholding data.\n", npels);
			return(NULL);
		}
	} else rtn = op;
	
	if (flip) {
		above = 0; below = 1;
	} else {
		above = 1; below = 0;
	}
	
	for (pel = 0; pel < npels; ++pel)
		if (data[pel] > thresh) rtn[pel] = above; else rtn[pel] = below;
	
	return(rtn);
}

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

f32 *ccom_clip(f32 *data, u32 rows, u32 cols, f32 limit, Bool flip, Bool replace)
{
	f32	*rtn,invalid;
	u32	pel, npels = rows * cols;
	
	if (!replace) {
		if ((rtn = (f32*)malloc(sizeof(f32)*npels)) == NULL) {
			error_msgv(modname, "error: failed to allocate buffer (%d bytes)"
				" for clipping data.\n", npels);
			return(NULL);
		}
	} else rtn = data;
	
	mapsheet_get_invalid(MAP_DATA_F32, &invalid);
	if (flip) {
		for (pel = 0; pel < npels; ++pel)
			if (data[pel] < limit) rtn[pel] = invalid; else rtn[pel] = data[pel];
	} else {
		for (pel = 0; pel < npels; ++pel)
			if (data[pel] > limit) rtn[pel] = invalid; else rtn[pel] = data[pel];
	}
	return(rtn);
}

/* Routine:	ccom_erode
 * Purpose:	Erode a binary image using a specified structure element size
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

u8 *ccom_erode(u8 *data, u32 rows, u32 cols, u32 size, u8 *op)
{
	u8		*rtn, **elem;
	u32		r, c, dr, dc;
	f32		dist;
	Bool	done;
	
	if (op == NULL) {
		if ((rtn = (u8*)malloc(rows*cols*sizeof(u8))) == NULL) {
			error_msgv(modname, "error: failed to allocate buffer (%d bytes)"
				" for erroding data.\n", rows*cols);
			return(NULL);
		}
	} else rtn = op;
	
	memset(rtn, 0, sizeof(u8)*rows*cols);
	
	/* Allocate space for, and then digitise the structure element */
	if ((size % 2) == 0) ++size;	
	if ((elem = (u8**)calloc(size, sizeof(u8*))) == NULL ||
		(elem[0] = (u8*)calloc(size*size, sizeof(u8))) == NULL) {
		error_msgv(modname, "error: failed to generate structuring element"
			" workspace (%dx%d elements).\n", size, size);
		free(elem);
		return(NULL);
	}
	for (r = 1; r < size; ++r) elem[r] = elem[r-1] + size;
	for (r = 0; r < size; ++r) {
		for (c = 0; c < size; ++c) {
			dist = (f32)sqrt((r-size/2)*(r-size/2)+(c-size/2)*(c-size/2));
			if (dist <= size/2)
				elem[r][c] = 1;
			else
				elem[r][c] = 0;
		}
	}
	
	for (r = 0; r < rows - size; ++r) {
		op = rtn + (r+size/2)*cols + size/2;
		for (c = 0; c < cols; ++c) {
			done = False;
			for (dr = 0; dr < size && !done; ++dr) {
				for (dc = 0; dc < size && !done; ++dc) {
					if (elem[dr][dc] == 1 && data[(r+dr)*cols + c + dc] != 1)
						done = True;
				}
			}
			if (done) *op++ = 0; else *op++ = 1;
		}
	}
	
	free(elem[0]); free(elem);
	return(rtn);
}

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

u8 *ccom_dilate(u8 *data, u32 rows, u32 cols, u32 size, u8 *op)
{
	u8		*rtn, **elem;
	u32		r, c, dr, dc;
	f32		dist;
	Bool	done;
	
	if (op == NULL) {
		if ((rtn = (u8*)malloc(rows*cols*sizeof(u8))) == NULL) {
			error_msgv(modname, "error: failed to allocate buffer (%d bytes)"
				" for erroding data.\n", rows*cols);
			return(NULL);
		}
	} else rtn = op;
	
	memset(rtn, 0, sizeof(u8)*rows*cols);
	
	/* Allocate space for, and then digitise the structure element */
	if ((size % 2) == 0) ++size;	
	if ((elem = (u8**)calloc(size, sizeof(u8*))) == NULL ||
		(elem[0] = (u8*)calloc(size*size, sizeof(u8))) == NULL) {
		error_msgv(modname, "error: failed to generate structuring element"
			" workspace (%dx%d elements).\n", size, size);
		free(elem);
		return(NULL);
	}
	for (r = 1; r < size; ++r) elem[r] = elem[r-1] + size;
	for (r = 0; r < size; ++r) {
		for (c = 0; c < size; ++c) {
			dist = (f32)sqrt((r-size/2)*(r-size/2)+(c-size/2)*(c-size/2));
			if (dist <= size/2)
				elem[r][c] = 1;
			else
				elem[r][c] = 0;
		}
	}
	
	for (r = 0; r < rows - size; ++r) {
		op = rtn + (r+size/2)*cols + size/2;
		for (c = 0; c < cols - size; ++c) {
			done = False;
			for (dr = 0; dr < size && !done; ++dr) {
				for (dc = 0; dc < size && !done; ++dc) {
					if (elem[dr][dc] == 1 && data[(r+dr)*cols + c + dc] == 1)
						done = True;
				}
			}
			if (done) *op++ = 1; else *op++ = 0;
		}
	}
	
	free(elem[0]); free(elem);
	return(rtn);
}

/* Routine: ccom_ww_release
 * Purpose:	Release all memory associated with a Walking Window structure
 * Inputs:	*ww	Pointer to the WW structure to release
 * Outputs:	-
 * Comment:	-
 */

static void ccom_ww_release(WW *ww)
{
	if (ww != NULL) {
		if (ww->anchor != NULL) free(ww->anchor);
		if (ww->buffer != NULL) free(ww->buffer);
		free(ww);
	}
}

/* Routine:	ccom_ww_init
 * Purpose: Initialise a Walking Window structure
 * Inputs:	depth	Depth of the window in nodes
 *			width	Width of the window in nodes
 *			size	Size of each element in the row
 * Outputs:	Pointer to a suitable WW structure, or NULL on error
 * Comment:	This builds a structure suitable for running an IP walking window
 *			over an image/grid of _width_ cells wide using a kernel of _depth_
 *			cells with data elements of _size_ bytes.
 */

static WW *ccom_ww_init(u32 depth, u32 width, size_t size)
{
	WW	*rtn;
	u32	row;

	if ((depth%2) == 0) ++depth;
	depth = depth/2 + 1;
				/* We actually only need half the depth of the convolution
				 * kernel due to the causality principle.
				 */
	if ((rtn = (WW*)calloc(1, sizeof(WW))) == NULL) {
		error_msgv(modname, "error: failed to allocate space for the Walking"
			"Window (%dx%dx%d bytes).\n", depth, width, size);
		return(NULL);
	}
	if ((rtn->anchor = calloc(depth*width, size)) == NULL ||
		(rtn->buffer = (void**)calloc(depth, sizeof(void*))) == NULL) {
		error_msgv(modname, "error: failed to allocate space for the components"
			" of the WalkingWindow (%dx%dx%d bytes).\n", depth, width, size);
		ccom_ww_release(rtn);
		return(NULL);
	}
	rtn->n_rows = depth;
	rtn->n_bytes = width * size;
	rtn->buffer[0] = rtn->anchor;
	for (row = 1; row < depth; ++row)
		rtn->buffer[row] = (void*)((u8*)(rtn->buffer[row-1])+rtn->n_bytes);
	return(rtn);
}

/* Routine:	ccom_ww_get_insert
 * Purpose:	Return the point in the WalkingWindow in which to put data
 * Inputs:	*ww	Pointer to the WalkingWindow to use
 * Outputs:	Pointer to the buffer row into which to write
 * Comment:	The user should write into the returned buffer (cast as approprirate)
 *			before the next call to ccom_ww_rotate().  Note, of course, that
 *			if you over-run the buffer (e.g., by casting it to another type
 *			than you built the WalkingWindow for), then Very Bad Things (TM)
 *			will occur.
 */

static void *ccom_ww_get_insert(WW *ww)
{
	return(ww->buffer[ww->n_rows-1]);
}

/* Routine:	ccom_ww_rotate
 * Purpose:	Rotate the WalkingWindow buffer, returning a pointer to the output
 * Inputs:	*ww	Pointer to the WalkingWindow to use
 * Outputs:	Pointer to the current output slot of the WalkingWindow
 * Comment:	This rotates the WalkingWindow so that another input slot comes
 *			into view (call ccom_ww_get_insert() to find out where), and returns
 *			a pointer to the output slot that just became valid.  The user should
 *			ensure that this output is used before the next call to
 *			ccom_ww_rotate().
 */

static void *ccom_ww_rotate(WW *ww)
{
	u32		row;
	void	*temp;

	temp = ww->buffer[0];
	for (row = 1; row < ww->n_rows; ++row)
		ww->buffer[row-1] = ww->buffer[row];
	ww->buffer[ww->n_rows-1] = temp;
	return(temp);
}

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

Bool ccom_convolve(f32 *data, u32 rows, u32 cols,
				   f32 *kernel, u32 krows, u32 kcols)
{
	s32		row, col, dr, dc;
	f32		invalid, *oprow, *output = data, *k;
	WW		*buffer;

	if ((krows%2)==0 || (kcols%2)==0) {
		error_msgv(modname, "error: convolution kernel must be an odd"
			" number of cells wide/deep (not (%d,%d)).\n",
			krows, kcols);
		return(False);
	}
	if ((k = (f32*)malloc(sizeof(f32)*krows*kcols)) == NULL) {
		error_msgv(modname, "error: failed to get space for space-"
			"reversed convolution filter kernel (%dx%d).\n",
			krows, kcols);
		return(False);
	}
	for (row = 0; row < (s32)krows; ++row)
		for (col = 0; col < (s32)kcols; ++col)
			k[row*krows+col] = kernel[(krows-row-1)*kcols+(kcols-col-1)];
	if ((buffer = ccom_ww_init(krows, cols, sizeof(f32))) == NULL) {
		error_msg(modname, "error: failed to get space for"
			" rolling convolution buffer.\n");
		free(k);
		return(False);
	}
	mapsheet_get_invalid(MAP_DATA_F32, &invalid);
	for (row = 0; row < (s32)krows/2; ++row) {
		oprow = ccom_ww_get_insert(buffer);
		for (col = 0; col < (s32)cols; ++col) oprow[col] = invalid;
		(void)ccom_ww_rotate(buffer);
	}
	for (row = krows/2; row < (s32)(rows - krows/2); ++row) {
		oprow = ccom_ww_get_insert(buffer);
		for (col = 0; col < (s32)kcols/2; ++col)
			oprow[col] = oprow[cols-1-col] = invalid;
		for (col = kcols/2; col < (s32)(cols - kcols/2); ++col) {
			oprow[col] = 0.0f;
			for (dr = -(s32)krows/2; dr <= (s32)krows/2; ++dr)
				for (dc = -(s32)kcols/2; dc <= (s32)kcols/2; ++dc)
					oprow[col] += data[(row+dr)*cols+(col+dc)]*k[(dr+krows/2)*kcols+(dc+kcols/2)];
		}
		oprow = ccom_ww_rotate(buffer);
		memcpy(output, oprow, sizeof(f32)*cols);
		output += cols;
	}
	for (row = 0; row < (s32)krows/2; ++row, output += cols) {
		oprow = ccom_ww_rotate(buffer);
		memcpy(output, oprow, sizeof(f32)*cols);
	}
	for (row = 0; row < (s32)krows/2; ++row, output += cols) {
		for (col = 0; col < (s32)cols; ++col)
			output[col] = invalid;
	}
	ccom_ww_release(buffer);
	free(k);
	return(True);
}

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

Bool ccom_patch_surface(f32 *data, u32 rows, u32 cols, f32 radius, f32 dx)
{
	s32 row, col, dr, dc;
	s32	kernel_radius, kernel_width, valid, validity_level;
	f32	invalid, mean, *output, *oprow;
	WW	*buffer;

	kernel_radius = (u32)floor(radius/dx + 0.51f);
	kernel_width = 2*kernel_radius+1;
	validity_level = (u32)floor(0.80f*kernel_width*kernel_width);
	
	if ((buffer = ccom_ww_init(kernel_width, cols, sizeof(f32))) == NULL) {
		error_msgv(modname, "error: could not build buffer for surface"
			" patching (%dx%d cells).\n", kernel_width, kernel_width);
		return(False);
	}
	mapsheet_get_invalid(MAP_DATA_F32, &invalid);

	/* Prime walking window for first kernel_radius rows */
	for (row = 0; row < kernel_radius; ++row) {
		oprow = ccom_ww_get_insert(buffer);
		for (col = 0; col < (s32)cols; ++col) oprow[col] = data[row*cols+col];
		(void)ccom_ww_rotate(buffer);
	}

	output = data;
	for (row = kernel_radius; row < (s32)rows-kernel_radius; ++row, output += cols) {
		oprow = ccom_ww_get_insert(buffer);
		for (col = 0; col < kernel_radius; ++col) {
			oprow[col] = data[row*cols+col];
			oprow[cols-1-col] = data[row*cols+(cols-1-col)];
		}
		for (col = kernel_radius; col < (s32)cols-kernel_radius; ++col) {
			/* Check for holidays, copying across data otherwise */
			if (data[row*cols+col] == invalid) {
				/* Compute mean and number of valid points in window */
				mean = 0.0f; valid = 0;
				for (dr = -kernel_radius; dr <= kernel_radius; ++dr) {
					for (dc = -kernel_radius; dc <= kernel_radius; ++dc) {
						if (data[(row+dr)*cols+(col+dc)] != invalid) {
							mean += data[(row+dr)*cols+(col+dc)];
							++valid;
						}
					}
				}
				/* Only use patch if correct number of neighbours exist */
				if (valid > validity_level)
					oprow[col] = mean/valid;
				else
					oprow[col] = invalid;
			} else
				oprow[col] = data[row*cols+col];
		}
		oprow = ccom_ww_rotate(buffer);
		memcpy(output, oprow, sizeof(f32)*cols);
	}
	/* Flush the remaining components of the buffer into the output */
	for (row = 0; row < kernel_radius; ++row, output += cols) {
		oprow = ccom_ww_rotate(buffer);
		memcpy(output, oprow, sizeof(f32)*cols);
	}
	/* Note that we don't need to do anything about the bottom kernel_radius-1
	 * lines of cells, since we'ld only be copying them from the original
	 * back to the original in any case.
	 */
	ccom_ww_release(buffer);
	return(True);
}

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

Bool ccom_gauss_filter(f32 *data, u32 rows, u32 cols, f32 radius, f32 de)
{
	s32		kernel_width;
	f32		*k;
	f64		x, norm;
	s32		row, col;
	Bool	rc;

	kernel_width = (u32)ceil(2.0*radius/de);
	if ((kernel_width%2) == 0) ++kernel_width;

	if ((k = (f32*)malloc(sizeof(f32)*kernel_width*kernel_width)) == NULL) {
		error_msgv(modname, "error: failed to get memory for Gaussian"
			" convolution kernel (%dx%d cells).\n", kernel_width,
			kernel_width);
		return(False);
	}
	
	radius /= de;

	for (row = 0; row < kernel_width; ++row)
		for (col = 0; col < kernel_width; ++col) {
			x = sqrt((row - kernel_width/2)*(row - kernel_width/2) +
					(col - kernel_width/2)*(col - kernel_width/2));
			k[row*kernel_width+col] = (f32)exp(-4.5*x*x/(radius*radius));
		}
	norm = 0.0f;
	for (row = 0; row < kernel_width; ++row)
		for (col = 0; col < kernel_width; ++col)
			norm += k[row*kernel_width+col];

	for (row = 0; row < kernel_width; ++row)
		for (col = 0; col < kernel_width; ++col)
			k[row*kernel_width+col] /= (f32)norm;

	rc = ccom_convolve(data, rows, cols, k, kernel_width, kernel_width);

	free(k);
	
	return(rc);
}

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

#define MAX_FILL_STACK	32768

void ccom_fill(u8 *i, u32 x0, u32 y0, u8 colour, u32 w, u32 h, BlobBBox *bbox)
{
	s32		x, y, saveX, saveY, xLeft, xRight;
	u32		stackLen = 0, stack[MAX_FILL_STACK];
	u8		col;
	
	stack[stackLen++] = x0; stack[stackLen++] = y0;
	
	bbox->min_x = w-1; bbox->max_x = 0;
	bbox->min_y = h-1; bbox->max_y = 0;
	bbox->blob_pel_count = 0;
	bbox->seed_row = y0; bbox->seed_col = x0;
	
	while (stackLen) {
		/* Process this row, by poping the seed off the stack ... */
		y = stack[--stackLen]; x = stack[--stackLen];
		i[y*w+x] = colour; ++bbox->blob_pel_count;
		saveX = x++; col = i[y*w+x];
		/* ... and then filling right ... */
		while (col != 0 && x < (s32)w) {
			i[y*w+x] = colour; ++x; ++bbox->blob_pel_count;
			col = i[y*w+x];
		}
		xRight = x-1; x = saveX-1; col = i[y*w+x];
		/* ... and then left. */
		while (col != 0 && x >= 0) {
			i[y*w+x] = colour; --x; ++bbox->blob_pel_count;
			col = i[y*w+x];
		}
		xLeft = ++x;
		saveY = y;
		
		bbox->min_x = MIN((s32)(bbox->min_x), xLeft);
		bbox->max_x = MAX((s32)(bbox->max_x), xRight);
		bbox->min_y = MIN((s32)(bbox->min_y), y);
		bbox->max_y = MAX((s32)(bbox->max_y), y);
		
		/* Now find all segments of the seed colour on the next line down */
		if (y < (s32)h-1) {
			++y; col = i[y*w+x];
			do {
				while ( ((col == 0)||(col == colour))
						&& (x < (s32)w-1) && (x<xRight)) {
					++x; col = i[y*w+x];
				}
				if (col != colour && col != 0) {
					if ((x == xRight || x == (s32)w-1) && xLeft != xRight) {
						stack[stackLen++] = x; stack[stackLen++] = y;
						if (stackLen == MAX_FILL_STACK) {
							error_msgv(modname, "error: maximum stack length (%d pairs)"
								" exceeded in ccom_fill().\n", MAX_FILL_STACK/2);
							bbox->min_x = bbox->max_x = bbox->min_y = bbox->max_y = 0;
							bbox->blob_pel_count = 0;
							return;
						}
						break;
					} else {
						while ((col != 0)
								&& (x <= xRight) && (x < (s32)w)) {
							++x; col = i[y*w+x];
						}
						stack[stackLen++] = x-1; stack[stackLen++] = y;
						if (stackLen == MAX_FILL_STACK) {
							error_msgv(modname, "error: maximum stack length (%d pairs)"
								" exceeded in ccom_fill().\n", MAX_FILL_STACK/2);
							bbox->min_x = bbox->max_x = bbox->min_y = bbox->max_y = 0;
							bbox->blob_pel_count = 0;
							return;
						}
					}
				}
			} while (x < xRight);
			x = saveX; y = saveY;
		}
		y = saveY; x = xLeft;
		/* Finally, find all segments of the seed colour on the line above */
		if (y > 0) {
			--y; col = i[y*w+x];
			do {
				while ( ((col == colour) || (col == 0))
						 && (x < (s32)w-1) && (x < xRight)) {
					++x; col = i[y*w+x];
				}
				if (col != colour && col != 0) {
					if (x == xRight || x == (s32)w-1) {
						stack[stackLen++] = x; stack[stackLen++] = y;
						if (stackLen == MAX_FILL_STACK) {
							error_msgv(modname, "error: maximum stack length (%d pairs)"
								" exceeded in ccom_fill().\n", MAX_FILL_STACK/2);
							bbox->min_x = bbox->max_x = bbox->min_y = bbox->max_y = 0;
							bbox->blob_pel_count = 0;
							return;
						}
						break;
					} else {
						while ((col != 0) && (x <= xRight) && (x < (s32)w)) {
							++x; col = i[y*w+x];
						}
						stack[stackLen++] = x-1; stack[stackLen++] = y;
						if (stackLen == MAX_FILL_STACK) {
							error_msgv(modname, "error: maximum stack length (%d pairs)"
								" exceeded in ccom_fill().\n", MAX_FILL_STACK/2);
							bbox->min_x = bbox->max_x = bbox->min_y = bbox->max_y = 0;
							bbox->blob_pel_count = 0;
							return;
						}
					}
				}
			} while (x < xRight);
			x = saveX; y = saveY;
		}
	}
}

/* Routine:	ccom_release_bloblist
 * Purpose:	Release memory associated with a linked list of BlobBBoxes
 * Inputs:	*list	Head of the linked list
 * Outputs:	-
 * Comment:	-
 */

void ccom_release_bloblist(BlobBBox *root)
{
	BlobBBox	*next;
	
	while (root != NULL) {
		next = root->next;
		free(root);
		root = next;
	}
}

/* Routine:	ccom_blob_detect
 * Purpose:	Find all blobs in a binary image
 * Inputs:	*data	Pointer to a binary image to process
 *			rows,	Size of the image
 *			cols
 * Outputs:	Returns NULL terminated singly linked list of blob bounding boxes
 * Comment:	A blob is defined as the maximum simply connected region of pixels
 *			with value 1 in the image.
 */

BlobBBox *ccom_blob_detect(u8 *data, u32 rows, u32 cols)
{
	BlobBBox *rtn = NULL, *tmp;
	u32		row, col;
	
	for (row = 0; row < rows; ++row) {
		for (col = 0; col < cols; ++col) {
			if (data[row*cols + col] == 1) {
				/* Compute blob statistics for this blob */
				if ((tmp = (BlobBBox*)calloc(1, sizeof(BlobBBox))) == NULL) {
					error_msgv(modname, "error: failed to allocate space for"
						" a blob bounding-box (%d bytes).\n", sizeof(BlobBBox));
					ccom_release_bloblist(rtn);
					return(NULL);
				}
				ccom_fill(data, col, row, 2, cols, rows, tmp);
				tmp->next = rtn;
				rtn = tmp;
			}
		}
	}
	
	/* Restore the image to its original levels */
	for (row = 0; row < rows; ++row)
		for (col = 0; col < cols; ++col)
			if (data[row*cols + col] == 2) data[row*cols+col] = 1;
	
	return(rtn);
}

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

GdpFILE *ccom_open_gdp(char *name, GdpColour colour)
{
	GdpFILE	*rtn;
	char	*buffer = NULL, *prefix, *ptr;
	FILE	*sdv_fp;
	
	if ((buffer = (char*)malloc(strlen(name)+10)) == NULL) {
		error_msg(modname, "error: failed to get name space for outputs.\n");
		return(NULL);
	}
	if ((prefix = strdup(name)) == NULL) {
		error_msg(modname, "error: failed to get prefix buffer space for"
			" name construction!\n");
		return(NULL);
	}
	if ((ptr = strrchr(prefix, '.')) != NULL) *ptr = '\0';
	if ((rtn = (GdpFILE *)calloc(1, sizeof(struct _gdp_file))) == NULL) {
		error_msgv(modname, "error: failed to allocate GdpFILE structure.\n");
		return(NULL);
	}
	sprintf(buffer, "%s.gdp", prefix);
	if ((rtn->gdp_fp = fopen(buffer, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			name);
		free(rtn);
		free(prefix);
		return(NULL);
	}
	sprintf(buffer, "%s.cga", prefix);
	if ((rtn->cga_fp = fopen(buffer, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			name);
		free(rtn);
		free(prefix);
		return(NULL);
	}
	sprintf(buffer, "%s.clut", prefix);
	if ((rtn->clut_fp = fopen(buffer, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			name);
		free(rtn);
		free(prefix);
		return(NULL);
	}
	
	sprintf(buffer, "%s.sdv", prefix);
	if ((sdv_fp = fopen(buffer, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output.\n",
			name);
		free(rtn);
		free(prefix);
		return(NULL);
	}
	sprintf(buffer, "%s.clut", prefix);
	fprintf(sdv_fp, "textures\n"
					"%s default*\n"
					"objects\n", buffer);
	sprintf(buffer, "%s.gdp", prefix);
	fprintf(sdv_fp, "%s %s default\n", buffer, buffer);
	sprintf(buffer, "%s.cga", prefix);
	fprintf(sdv_fp, "%s %s default\n"
					"end\n", buffer, buffer);
	fclose(sdv_fp);
	
	rtn->min_x = rtn->min_y = rtn->min_z = DBL_MAX;
	rtn->max_x = rtn->max_y = rtn->max_z = -DBL_MAX;
	rtn->colour_scheme = colour;
	free(prefix);
	return(rtn);
}

/* Routine:	ccom_write_gdp
 * Purpose:	Append entries to the GdpFILE structure specified
 * Inputs:	*file	A GdpFILE returned from ccom_open_gdp()
 *			*data	A set of Sounding.s
 *			n_snd	Number of Sounding.s in the structures
 * Outputs:	True if insertion worked, otherwise False.
 * Comment:	The only failure mode here is if the data space to cache the new
 *			soundings cannot be constructed.
 */

Bool ccom_write_gdp(GdpFILE *file, Sounding *data, u32 n_snd)
{
	u32			target, snd;
	Sounding	*tmp;

#ifdef __DEBUG__	
	if (file->n_records >= 2048) return(True);
#endif

	target = sizeof(Sounding)*(file->n_records + n_snd);
	if ((tmp = (Sounding *)realloc(file->data, target)) == NULL) {
		error_msgv(modname, "error: no space for new data in GdpFILE.\n");
		return(False);
	}
	
	file->data = tmp;
	memcpy(file->data + file->n_records, data, sizeof(Sounding)*n_snd);
	file->n_records += n_snd;
	
	/* Update the minimum x/y/z values */
	for (snd = 0; snd < n_snd; ++snd) {
		if (data[snd].east == 0.0 && data[snd].north == 0.0) {
			++file->n_bad_nav;
			continue;
		}
		file->min_x = MIN(file->min_x, data[snd].east);
		file->max_x = MAX(file->max_x, data[snd].east);
		file->min_y = MIN(file->min_y, data[snd].north);
		file->max_y = MAX(file->max_y, data[snd].north);
		file->min_z = MIN(file->min_z, data[snd].depth);
		file->max_z = MAX(file->max_z, data[snd].depth);
	}
	
	return(True);
}

/* Routine:	ccom_write_cga
 * Purpose:	Write a coordinate grid axis for the GDP file
 * Inputs:	*file						GdpFILE to work with
 *			min_x, max_x, min_y, max_y	Coordinate extremes
 *			z0							Minimum Z value used in LOCAL_ORIGIN
 * Outputs:	Writes the <name>.gca file using the pointer provided
 *			Returns True if write suceeded, otherwise False
 * Comment:	This writes a coordinate axis description to match the .gdp file
 *			in that it is an XY plane plate just below the data, with labels on
 *			UTM norm grids.
 */

static Bool ccom_write_cga(GdpFILE *file)
{
	f64		width, height;
	time_t	stamp;
	
	width = file->max_x - file->min_x;
	height = file->max_y - file->min_y;
	
	time(&stamp);
	fprintf(file->cga_fp, "# Created by libccom at %s", ctime(&stamp));
	fprintf(file->cga_fp,
		"BEGIN COORD 1.0\n"
		"DIMENSIONS XY %lf %lf\n"
		"REFERENCE GLOBAL\n"
		"BACKGROUND 1 XYMIN\n"
		"BGCOLOR 0.6 0.6 1.0\n"
		"LINECOLOR 1.0 1.0 1.0\n"
		"BACKGROUND_OFFSET 1.0\n"
		"LOCAL_ORIGIN %lf %lf %lf\n"
		"GRID XY MIN SPACING_DYNAMIC\n"
		"TICKS_LABELS X MIN_MAX MIN_MAX 1.0 SPACING_DYNAMIC\n"
		"TICKS_LABELS Y MIN_MAX MIN_MAX 1.0 SPACING_DYNAMIC\n",
		width, height,
		file->min_x, file->min_y, file->min_z);
	return(True);
}


/* Routine:	ccom_close_gdp
 * Purpose:	Write the GDP file to disc, then flush & release memory buffers
 * Inputs:	*file	GdpFILE structure to work on
 * Outputs:	True if write worked, otherwise False
 * Comment:	Note that this call does the actual work of writing the file to disc
 *			but also cleans out buffers, including the GdpFILE itself.  It
 *			should therefore only be used after all data is in, since there is
 *			no way to append data afterwards.
 */

Bool ccom_close_gdp(GdpFILE *file)
{
	u32		snd;
	time_t	stamp;
	u32		colours[4] = { 3, 5, 6, 7};
	f64		x0, y0, z0;

#ifdef __DEBUG__
	error_msgv(modname, "debug: min_x = %lg max_x = %lg min_y = %lg max_y = %lg\n",
		file->min_x, file->max_x, file->min_y, file->max_y);
#endif

	x0 = (file->max_x + file->min_x)/2.0;
	y0 = (file->max_y + file->min_y)/2.0;
	z0 = file->min_z - 1.0;
	
	time(&stamp);
	fprintf(file->gdp_fp, "# Generated by libccom at %s", ctime(&stamp));
	fprintf(file->gdp_fp,
			"BEGIN DATA_POINTS 1.0\n"
			"N_RECORDS %d\n"
			"N_COLUMNS 8\n"
			"XYZ\n"
			"COLOR ATTRIBUTE %d%s\n"
			"GLYPH UCUBE\n"
			"DROP_LINE OFF\n"
			"SIZE COLUMNS XYZK 4 4 4 1.96\n"
			"TEXT NONE\n"
			"DEGRADATION NTH\n"
			"LOCAL_ORIGIN %lf %lf %lf\n",
			file->n_records - file->n_bad_nav,
			colours[(u32)(file->colour_scheme)],
			file->colour_scheme == COLOUR_BY_DEPTH ?
			" AS_HEIGHT" : "",
			x0, y0, z0);
	for (snd = 0; snd < file->n_records; ++snd) {
		if (file->data[snd].east == 0.0) continue;
		fprintf(file->gdp_fp, "%lf %lf %lf %f %f %d %x %hd\n",
				file->data[snd].east - x0,
				file->data[snd].north - y0,
				file->data[snd].depth - z0,
				(f32)sqrt(file->data[snd].dz),
				(f32)sqrt(file->data[snd].dr),
				(u32)(file->data[snd].beam_number),
				(u32)(file->data[snd].flags),
				file->data[snd].file_id);
	}
	
	fclose(file->gdp_fp);
	if (!ccom_write_cga(file)) {
		error_msgv(modname, "error: failed to write CGA grid file.\n");
		return(False);
	}
	fclose(file->cga_fp);
	if (!ccom_write_clut(file->clut_fp, file->min_z, file->max_z, 100)) {
		error_msgv(modname, "error: failed to write CLUT colour file.\n");
		return(False);
	}
	fclose(file->clut_fp);
	free(file->data);
	free(file);
	return(True);
}

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

void ccom_swap_2(void *elem)
{
	u8	swap, *buffer = (u8p)elem;
	
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
}

void ccom_swap_4(void *elem)
{
	u8	swap, *buffer = (u8p)elem;
	
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
}

void ccom_swap_8(void *elem)
{
	u8	swap, *buffer = (u8p)elem;
	
	swap = buffer[0]; buffer[0] = buffer[7]; buffer[7] = swap;
	swap = buffer[1]; buffer[1] = buffer[6]; buffer[6] = swap;
	swap = buffer[2]; buffer[2] = buffer[5]; buffer[5] = swap;
	swap = buffer[3]; buffer[3] = buffer[4]; buffer[4] = swap;
}

CcomEndian ccom_native_endian(void)
{
	char	buffer[4];

	*((int*)buffer) = 0x01020304;
	if (buffer[0] == 0x01) return(ENDIAN_BIG);
	else return(ENDIAN_LITTLE);
}

/* Routine:	ccom_release_fir
 * Purpose:	Release dynamically allocated data associated with Filt structure
 * Inputs:	*filt	Filt structure to remove
 * Outputs:	-
 * Comment:	-
 */

void ccom_release_fir(Filt *filt)
{
	if (filt != NULL) {
		free(filt->fir);
		free(filt);
	}
}

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

Filt *ccom_make_fir(f32 fs, f32 fo, u32 len)
{
	Filt	*rtn;
	f32		corner_f, sum = 0.0f;
	s32		n, center;
	
	if ((rtn = (Filt*)calloc(1, sizeof(Filt))) == NULL ||
		(rtn->fir = (f32*)malloc(sizeof(f32)*len)) == NULL) {
		error_msgv(modname, "failed to allocate FIR filter coefficient"
			" structure.\n");
		ccom_release_fir(rtn);
		return(NULL);
	}
	corner_f = (f32)(2.0*M_PI*(fo/2.0)/fs);	/* Corner f. in normalised freq. */
	center = len/2;	/* Note integer division */
	rtn->ip_rate = fs;
	rtn->op_rate = fo;
	rtn->step = (int)(fs/fo);	/* Output step for sub-sampling */
	rtn->len = len;
	for (n = 0; n < (s32)len; ++n) {
		if (n != center)
			rtn->fir[n] = (f32)((0.5+0.5*cos(2.0*M_PI*(n-center)/len)) *
							sin((n-center)*corner_f)/(M_PI*(n-center)));
		else
			rtn->fir[n] = (f32)(corner_f/M_PI);
		sum += rtn->fir[n];
	}
	for (n = 0; n < (s32)len; ++n)
		rtn->fir[n] /= sum;		/* Correct d.c. artifact */

#ifdef __DEBUG__
printf("%s: debug: designed length %d FIR, ip_rate = %f Hz, op_rate = %f Hz, "
	"f_c = %f (norm.), step = %d samples.\n", modname, len, fs, fo, corner_f,
	rtn->step);
printf("%s: fir[] = { %f", modname, rtn->fir[0]);
for (n = 1; n < len; ++n)
	printf(", %f", rtn->fir[n]);
printf(" }\n");
printf("%s: d.c. gain before correction %f (%fdB).\n", modname, sum,
	20*log10(sum));
#endif
	return(rtn);
}

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

Filt* ccom_make_fir_passthrough(f32 ip_rate)
{
	Filt	*rtn;
	
	if ((rtn = (Filt*)calloc(1, sizeof(Filt))) == NULL ||
		(rtn->fir = (f32*)malloc(sizeof(f32))) == NULL) {
		error_msg(modname, "failed to allocate FIR passthrough structure.\n");
		ccom_release_fir(rtn);
		return(NULL);
	}
	rtn->fir[0] = 1.0;
	rtn->ip_rate = ip_rate;
	rtn->op_rate = ip_rate;
	rtn->step = 1;
	rtn->len = 1;
	return(rtn);
}

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

f32 ccom_find_nearest_shoal(GUTM *data, s32 col, s32 row, s32 limit)
{
	s32		r, c, offset;
	f32		invalid, shoalest;

	mapsheet_get_invalid(MAP_DATA_F32, &invalid);
	for (offset = 1; offset < limit; ++offset) {
		shoalest = -FLT_MAX;
		for (r = MAX(0, row - offset); r <= MIN((s32)data->rows-1, row + offset); ++r) {
			if (col - offset >= 0 && data->data[r*data->cols + col - offset] != invalid)
				shoalest = MAX(data->data[r*data->cols + col - offset], shoalest);
			if (col + offset < (s32)data->cols && data->data[r*data->cols + col + offset] != invalid)
				shoalest = MAX(data->data[r*data->cols + col + offset], shoalest);
		}
		for (c = MAX(0, col - offset); c <= MIN((s32)data->cols-1, col + offset); ++c) {
			if (row - offset >= 0 && data->data[(row-offset)*data->cols + c] != invalid)
				shoalest = MAX(data->data[(row-offset)*data->cols + c], shoalest);
			if (row + offset < (s32)data->rows && data->data[(row+offset)*data->cols + c] != invalid)
				shoalest = MAX(data->data[(row+offset)*data->cols + c], shoalest);
		}
		if (shoalest > -FLT_MAX) return(shoalest);
	}
	return(0.0f);
}

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

GUTM *ccom_compute_coverage(GUTM *depth, GUTM *cover, Sounding *data, u32 nSnds)
{
	GUTM	*r;
	u32		snd, spread_width;
	s32		row, col, top, bottom, left, right, snd_r, snd_c, d_row;
	f32		est_depth, target_range, invalid;
	f64		node_east, node_north, dist_sq;

	if (cover == NULL) {
		/* First time caller ... need to allocate the coverage map */
		if ((r = ccom_gutm_clone(depth)) == NULL) {
			error_msgv(modname, "error: failed to clone depth grid for coverage map.\n");
			return(NULL);
		}
		if ((r->altdata = (f32*)malloc(sizeof(f32)*r->rows*r->cols)) == NULL) {
			error_msgv(modname, "error: failed to allocate bin size grid for coverage map.\n");
			ccom_gutm_release(r);
			return(NULL);
		}
		/* Allocated data ... now we need to compute the bin sizes and initialise coverage */
		error_msgv(modname, "info: computing coverage bin capture radii ... hold on ...");
		error_flush_output();
		mapsheet_get_invalid(MAP_DATA_F32, &invalid);
		for (row = 0; row < (s32)depth->rows; ++row) {
			for (col = 0; col < (s32)depth->cols; ++col) {
				if (depth->data[row*depth->cols + col] != invalid) {
					est_depth = -depth->data[row*depth->cols + col];
				} else {
					/* Need to determine the nearest shoal depth in the grid in order to
					 * determine the bin size.
					 */
					est_depth = -1.0f * ccom_find_nearest_shoal(depth, col, row, COVERAGE_SEARCH_LIMIT);
				}
				if (est_depth <= 0.0f)
					r->altdata[row*r->cols + col] = 0.0f;
				else
					r->altdata[row*r->cols + col] = (2.5f+ 0.025f * est_depth)*(2.5f + 0.025f * est_depth);
				r->data[row*r->cols + col] = -FLT_MAX;
			}
		}
		error_msgv(NULL, " done.\n");
	} else
		r = cover;

	if (nSnds == 0) return(r);	/* Nothing to do, so do no more ... */

	if (r->altdata == NULL) {
		error_msgv(modname, "error: user supplied coverage map does not have range"
			" estimates available (internal error: this should not happen!).\n");
		return(NULL);
	}

	for (snd = 0; snd < nSnds; ++snd) {
		target_range = (f32)(5.0f*ceil((2.5f - 0.025f*data[snd].depth)/5.0f + 1.0f));
			/* Note 2.5 - 0.025f*depth because depths are negative here */
		spread_width = (u32)ceil(target_range/r->cell_size);
		snd_r = (s32)floor((data[snd].north - (r->y0 - r->cell_size*0.5f))/r->cell_size);
		snd_c = (s32)floor((data[snd].east - (r->x0 - r->cell_size*0.5f))/r->cell_size);
		
		if (snd_r < 0 || snd_c < 0 || snd_r > (s32)r->rows || snd_c > (s32)r->cols) continue;
		
		left = MAX((s32)0, snd_c - (s32)spread_width); right = MIN((s32)r->cols-1, snd_c + (s32)spread_width);
		bottom = MAX((s32)0, snd_r - (s32)spread_width); top = MIN((s32)r->rows-1, snd_r + (s32)spread_width);

		/* Scan through spread area, using the sounding to update anything that's
		 * within range.  Note that (a) we assume that the capture range *squared* is
		 * stored in the altdata[] element of _r_, and (b) we have to run both increasing
		 * down and increasing up row counts since the GUTM is stored with the northern-
		 * most nodes first, but the computations for spreading are easier if we count
		 * rows from the bottom ...
		 */
		for (row = top, d_row = r->rows-1-top; row >= bottom; --row, ++d_row) {
			for (col = left; col <= right; ++col) {
				node_east = r->x0 + r->cell_size*col;
				node_north = r->y0 + r->cell_size*row;
				dist_sq = (data[snd].east - node_east)*(data[snd].east - node_east) +
						  (data[snd].north - node_north)*(data[snd].north - node_north);
				if (dist_sq < r->altdata[d_row*r->cols+col]) {
					/* Within range, so update element in data */
					r->data[d_row*r->cols+col] = MAX(r->data[d_row*r->cols+col], data[snd].depth);
				}
			}
		}
	}

	return(r);
}

/* Routine:	ccom_find_dirs
 * Purpose:	Recursive depth first decent of a directory tree
 * Inputs:	*base	Base to search from
 *			*target	Target file to find that identifies a directory
 *			*list	List to which matches are added
 * Outputs:	Pointer to (possibly extended) list of matches, or NULL on failure
 * Comment:	This routine recursively decends the directory tree to find all directories
 *			that contain a plain file of name 'target'.
 */

FileEntry *ccom_find_dirs(char *base, char *target, FileEntry *list)
{
	char			*new_base, *name;
	u32				base_len = strlen(base), len;
	FileEntry		*added;
	struct stat	file_state;
#ifdef WIN32
	char				search[_MAX_FNAME];
	long				s_handle;
	struct _finddata_t	find;
#else
	DIR	*dir;
	struct direct *entry;
#endif
	
	len = base_len + strlen(target) + 2;
	if ((new_base = (char*)malloc(len)) == NULL) {
		error_msgv(modname, "error: failed to get memory for %d characters to search"
			" directory \"%s\" for targets \"%s\".\n", len, base, target);
		return(list);
	}
	sprintf(new_base, "%s%c%s", base, DIRECTORY_SEPARATOR, target);
	if (stat(new_base, &file_state) == 0) {
		if (!S_ISDIR(file_state.st_mode)) {
			/* File exists and is a regular file --- add a new list entry */
			if ((added = (FileEntry*)malloc(sizeof(FileEntry))) == NULL ||
				(added->filename = strdup(base)) == NULL) {
				error_msgv(modname, "error: failed to allocate memory for new list entry"
					" (%d bytes) for directory \"%s\".\n", sizeof(FileEntry)+strlen(base), base);
				if (added != NULL) free(added);
				return(list);
			}
			added->next = list; list = added;
		} else {
			error_msgv(modname, "error: target file \"%s\" exists in \"%s\" but is not a"
				" plain file!\n", target, base);
			return(list);
		}
	} else {
#ifdef WIN32
		sprintf(search, "%s/*", base);
		if ((s_handle = _findfirst(search, &find)) < 0) {
#else
		if ((dir = opendir(base)) == NULL ||
			(entry = readir(dir)) == NULL) {
#endif
			error_msgv(modname, "error: failed to read first file in \"%s\".\n", base);
			return(list);
		}
		do {
#ifdef WIN32
			name = find.name;
#else
			name = entry->d_name;
#endif
			if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
			len = strlen(name) + base_len + 2;
			free(new_base);
			if ((new_base = (char*)malloc(len)) == NULL) {
				error_msgv(modname, "error: failed to get memory for recursive search"
					" from directory \"%s\" (%d bytes).\n", base, len);
				return(list);
			}
			sprintf(new_base, "%s%c%s", base, DIRECTORY_SEPARATOR, name);
			if (stat(new_base, &file_state) < 0) {
				error_msgv(modname, "error: failed to stat(2) file \"%s\".\n", new_base);
				/* This is a fatal error, since the directory listing says that it exists! */
				return(list);
			}
			if (S_ISDIR(file_state.st_mode))
				list = ccom_find_dirs(new_base, target, list);
#ifdef WIN32
		} while (_findnext(s_handle, &find) == 0);
#else
		} while ((entry = readdir(dir)) != NULL);
#endif

		free(new_base);

#ifdef WIN32
		_findclose(s_handle);
#else
		closedir(dir);
#endif
	}

	return(list);
}

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

FileEntry *ccom_enumerate_hips_lines(char *name)
{
	FileEntry *rtn = ccom_find_dirs(name, "ProcessedDepths", NULL);
	return(rtn);
}