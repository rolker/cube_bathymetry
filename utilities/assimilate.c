/*
 * $Id: assimilate.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.1.2.1  2002/12/15 02:08:28  brc
 * Data driver used where there are multiple survey platforms and multiple
 * MapSheets to load.  This keeps track of which platform comes from which
 * configuration file, and swaps the vessel descriptions and error models
 * as appropriate when it matches their name in the input stream name.  This
 * also manages the caching of the MapSheets a little more actively, so that
 * it periodically attempts to flush all tiles in a MapSheet in order to minimise the
 * memory useage.  This is important when there are many MapSheets in memory
 * at the same time.
 *
 *
 * --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE --- NOTE ---
 *
 * This code was cloned from estdepth 1.14.2.1 on the libccom_win32 branch
 * and then modified to allow for multiple MapSheets in memory at the same
 * time.  Since this is a more specific task, lots of the accreted options
 * have been removed from the estdepth source, leaving a lean-n-mean source
 * base from which to work ...  This means that the MapSheets have to be in
 * binary format, and they can't be initialised from here, nor can this
 * code do any extraction of GUTMs or summary information, and, most
 * importantly, only HyperCUBE is supported as an assimilation method.
 *
 * ------------- Log from estdepth before clone and munge ----------------
 *
 * Revision 1.14.2.1  2002/07/14 02:20:48  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.14  2002/03/14 04:47:58  brc
 * Rationalised command line arguments (needed to add another flag and there
 * wasn't a letter left!)  The data format is now specified through a string
 * appended to the -d flag, which in turn sets a format variable that is then
 * passed to the generic stream open routine, rather than a format specific
 * stream opener.  Makes things a fair bit cleaner.
 *
 * Revision 1.13  2001/12/07 20:20:09  brc
 * Added facility to allow the specification of a series of target areas
 * within the area of interest, for which the code writes a set of GeoZui3D
 * style point files with the raw data.  Areas are specified by centre
 * location in projected coordinates and a delta, which is half the side
 * length of the bounding square (OK, so it's meant to be a radius, but we're
 * too cheap to compute proper Euclidean distance, and we implemented it as
 * a square, alright?).
 *
 * Revision 1.12  2001/09/23 19:24:33  brc
 * Added HyperCUBE as an estimation method, and Native format files as inputs;
 * updated syntax information to suit.  Made HyperCUBE the default estimator.
 *
 * Revision 1.11  2001/08/21 01:38:12  brc
 * Added facility to write out a list of files which actually contributed some
 * soundings to the area where the estimation is taking place.  This can be
 * used to sub-set the data for a particular area.
 *
 * Revision 1.10  2001/08/21 00:12:48  brc
 * Moved from local mapsheet ASCII parser to mapsheet_new_from_ascii().
 *
 * Revision 1.9  2001/05/15 01:47:34  brc
 * Added facility to load parameter file and initialise the library with it.
 *
 * Revision 1.8  2001/05/13 02:38:39  brc
 * Added facility to specify the order of IHO survey being conducted at the
 * command line, and to write out a summary directory if required.
 *
 * Revision 1.7  2001/04/10 23:35:03  brc
 * Reorganised flags so that they make a little more sense (running out of
 * letters!).  Added support for depth and angle gates.  Added facility to
 * select which data format is being used (OMG/HDCS, GSF or RAW) rather than
 * having to recompile.  Added facility to read back standard deviation
 * estimates from the nodal mapsheet.
 *
 * Revision 1.6  2001/02/26 02:35:01  brc
 * Changed some flags so that the acronyms make a little more sense.  Added the
 * facility to interface to an OMG tide file through the tides module, and to
 * apply them whenever possible.  In order to avoid precipitate failure modes
 * when the tides run out before the data does, the code simply drops packets
 * when it can't add the tide corrections.  This may not be the correct solution
 * in all cases.
 *
 * Revision 1.5  2001/02/10 17:10:13  brc
 * Modified to use raw data, rather than merged files.  This allows the code
 * to get at sound-speed and ship-speed recorded in the data files, which don't
 * seem to make it across in OMG/HDCS files, although the code indicates that
 * they should ...  This has been mostly tested using the SAX'99 set, and
 * appears to work relatively reliably.
 *
 * Revision 1.4  2000/12/03 04:31:06  brc
 * Added facility to get location and bounds from an R4 file so that we can
 * attempt to match stuff done with the UNB suite.
 *
 * Revision 1.3  2000/09/24 01:21:32  brc
 * Modifications to deal with new call sequence with sounding interface.
 *
 * Revision 1.2  2000/08/24 15:11:35  brc
 * Modified numerous files to allow the code to compile cleanly under Linux.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	estdepth.c
 * Purpose:	Estimate depths using various algorithms
 * Date:	18 July 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <time.h>
#include <math.h>
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
#include "device.h"
#include "vessel.h"
#include "projection.h"
#include "sounding.h"
#include "tides.h"
#include "mapsheet.h"
#include "error.h"
#include "ccom_general.h"
#include "errmod.h"
#include "params.h"
#include "stime.h"

static char *modname = "assimilate";
static char *modrev = "$Revision: 20 $";

/* Structure to define the platform specification for a particular
 * survey platform.  Having a list of these, and a way to recognise
 * them from the filenames in the input list using the recog_string
 * allows us to feed all of the data for a particular data into the
 * system in one go, rather than having the load/save overhead of
 * multiple mapsheets several times
 */

typedef struct _plat_spec {
	char	*recog_string;
	Device	device;
	Vessel	vessel_port,
			vessel_stbd;
	f32		min_depth, max_depth;
	f32		min_angle, max_angle;
} PlatSpec;

typedef struct _bbox {
	f64	x0, y0, x1, y1;
} BBox;

#define DEFAULT_IHO_SURVEY_ORDER ERRMOD_IHO_ORDER_1

/* The flush-time macros define a time limit on how long the code should
 * run without attempting to minimise the memory that it is holding in
 * core.  This is important when you have a large number of MapSheets in
 * memory, because they don't cooperate on the maximum amount of memory
 * that they use --- only how much each one uses individually.  This
 * ensures that tiles older than the threshold in the configuration file
 * (10 min. by default) are flushed back to disc, freeing up a fair chunk
 * of memory.  The benefit that you'll get from doing this depends on the
 * degree of spatial locality of the data ... if you bounce around, then
 * you'll keep hitting up the different components of the MapSheets, and
 * they'll all be in time; if you run all of the lines for one boat-day
 * one after the other, then (a) they should be mostly in the same place,
 * and (b) you won't have to keep remaking the error models for each input
 * file, which will be a little faster.
 */

#define DEFAULT_FLUSH_TIME_THRESHOLD	600
#define DEFAULT_MINIMUM_FLUSH_TIME		60
#define DEFAULT_MAXIMUM_FLUSH_TIME		3600

#define BUFFER_LENGTH 1024

static char *get_rcs_rev(const char *rev)
{
	char *p = strchr(rev, ' ');
	
	if (p != NULL) {
		char *b = strdup(++p);
		if ((p = b) != NULL && (b = strchr(p, ' ')) != NULL) *b = '\0';
	} else p = strdup("1.0");
	return(p);
}

typedef enum {
	FILE_LIST = 1,
	PLATFORM_LIST,
	MAPSHEET_DIR,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("assimilate V%s [%s] - Assimilate depth data using HyperCUBE.\n",
			p, __DATE__);
	printf("Syntax: assimilate [opt] <raw-list><platlist><srcdir>\n");
	printf(" Raw data file list -------^           ^         ^\n");
	printf(" Platform setup list ------------------'         |\n");
	printf(" MapSheet directory to use for inputs -----------'\n");
	printf(" Options:\n");
	printf("  Computational Environment:\n");
	printf("  -p dir      Specify prefix directory for all input merged files.\n");
	printf("  -f params   Specify parameters file for library.\n");
	printf("  -m time     Specify time between attempts to minimise memory use\n");
	printf("              (default: %d s).\n", DEFAULT_FLUSH_TIME_THRESHOLD);
	printf("  -s dir,gutm Use stencils for sheets from directory <dir>/*.tif and georef\n");
	printf("              from GUTM <dir>/<gutm>.  Obviously, these should match ...\n");
	printf("\n  Input File Options:\n");
	printf("  -d <type>   Input files are <type>:\n");
	printf("                  omg:  OMG/HDCS variant.\n");
	printf("                  gsf:  GSF V1.10 or later.\n");
	printf("                  raw:  Manufacturer's raw datagrams (default).\n");
	printf("                  nat:  Pre-digested native file.\n");
	printf("                  hips: CARIS/HIPS ProcessedDepths.\n");
	printf("\n  Depth Estimation Algorithms:\n");
	printf("  -i order    Set IHO survey order for estimation accuracy "
						  "(default: %d).\n", 1+(u32)DEFAULT_IHO_SURVEY_ORDER);
	printf("\n  Hydrographic corrections:\n");
	printf("  -t file     Load OMG ASCII tide descriptions from <file>.\n");\
	printf("  -y year     Specify year (YYYY) for tide file since OMG isn't Y2K.\n");
	printf(" Notes:\n");
	printf("  1. The raw data list can be a list of input files in whatever\n");
	printf("     format is suitable for the type of data input file selected\n");
	printf("     with the -d option.\n");
	printf("  2. The platform list must be a sequence of one or more lines\n");
	printf("     with format:\n");
	printf("        <recog-string> <device> <depth_gate> <angle_gate> <config-file>\n");
	printf("     where <recog-string> is a recognition string that will always\n");
	printf("     appear in filenames destined for that platform (at any point\n");
	printf("     in the filename), <device> is a libccom device string for\n");
	printf("     the generating system (e.g., \"sb8101\", \"em300\"),\n");
	printf("     <depth_gate> = <min_depth>,<max_depth> (both in meters, positive\n");
	printf("     down), <angle_gate> = <min_angle>,<max_angle> (in degress, with\n");
	printf("     negative angles to port, positive angles to starboard), and\n");
	printf("     <config-file> is an absolute or relative path to the library\n");
	printf("     configuration file to use for that platform.  Note that the\n");
	printf("     library configuration file can set a great deal more than the\n");
	printf("     vessel configurations, but it is intended in this case that\n");
	printf("     the contents of the file should be primarily just the vessel{}\n");
	printf("     clause structure.  Note that any other parameters in the file\n");
	printf("     will be ignored as long as there is a configuration file on the\n");
	printf("     command line, since it is read and executed last.\n");
	printf("  3. The -m <time> element allows you to control the time ticker\n");
	printf("     used to flush seldom used MapSheet components back to disc,\n");
	printf("     freeing up memory in the process to ensure that with multiple\n");
	printf("     mapsheets in memory, we don't run out of core store.  This time\n");
	printf("     (in seconds) is a threshold for how often this driver code asks\n");
	printf("     the underlying library to attempt a memory reduction; you should\n");
	printf("     control tile survival times using the mapsheet { hcube {\n");
	printf("     tile_expiry_delta <delay>; } } clause of the standard library\n");
	printf("     configuration file (i.e., how long the tile can go without being\n");
	printf("     used before it is flushed to disc and removed from core).\n");
	free(p);
}

Bool get_next_name(FILE *list, char *prefix_dir, char *buffer, u32 buf_len)
{
	char	filename[256];
	u32		length;
	
	if (fgets(filename, 256, list) == NULL) {
		if (!feof(list))
			error_msg(modname, "failed reading filename from list.\n");
		return(False);
	}
	length = strlen(filename);
	filename[length-1] = '\0';
	if (prefix_dir != NULL) length += strlen(prefix_dir);
	++length;
	if (length > buf_len) {
		error_msg(modname, "full filename is longer than available buffer.\n");
		return(False);
	}
	if (prefix_dir != NULL)
		sprintf(buffer, "%s/%s", prefix_dir, filename);
	else
		strcpy(buffer, filename);
	return(True);
}

char **assimilate_enumerate_files(char *dirname, char *extension, u32 *n_found)
{
#ifdef WIN32
	struct stat			s_stat;
	char				search[_MAX_FNAME];
	long				s_handle;
	struct _finddata_t	find;
#else
	DIR	*dir;
	struct direct *entry;
#endif
	
	u32		len, n, match_length;
	char	*name, **name_list = NULL, **tmp;

	*n_found = 0;
	match_length = strlen(extension);

#ifdef WIN32
	if (stat(dirname, &s_stat) < 0 || !S_ISDIR(s_stat.st_mode)) {
#else
	if ((dir = opendir(dirname)) == NULL) {
#endif
		error_msgv(modname, "error: file \"%s\" does not exist, or is"
			" not readable, or is not a directory.\n", dirname);
		return(NULL);
	}

#ifdef WIN32
	sprintf(search, "%s/*", dirname);
	if ((s_handle = _findfirst(search, &find)) < 0) {
#else
	if ((entry = readir(dir)) == NULL) {
#endif
		error_msgv(modname, "error: failed to read first file in \"%s\".\n", dirname);
		return(NULL);
	}

	do {
#ifdef WIN32
		name = find.name;
#else
		name = entry->d_name;
#endif
		len = strlen(name);
		if (len > 4 && strncmp(name+len-match_length, extension, match_length) == 0) {
			++(*n_found);
			if ((tmp = (char**)realloc(name_list, sizeof(char*)*(*n_found))) == NULL) {
				error_msgv(modname, "error: failed to allocate space for %d names"
					" from \"%s\".\n", *n_found, dirname);
				if (name_list != NULL)
					for (n = 0; n < *n_found-1; ++n)
						free(name_list[n]);
				free(name_list);
				return(NULL);
			}
			name_list = tmp;
			if ((name_list[*n_found-1] = strdup(name)) == NULL) {
				error_msgv(modname, "error: failed to get memory for name %d (%s) from"
					" directory \"%s\".\n", *n_found, name, dirname);
				for (n = 0; n < *n_found-1; ++n)
					free(name_list[n]);
				free(name_list);
				return(NULL);
			}
		}
#ifdef WIN32
	} while (_findnext(s_handle, &find) == 0);
#else
	} while ((entry = readdir(dir)) != NULL);
#endif

#ifdef WIN32
	_findclose(s_handle);
#else
	closedir(dir);
#endif

	return(name_list);
}

void platspec_release(PlatSpec *r, u32 n)
{
	u32	p;

	for (p = 0; p < n; ++p, ++r) {
		device_release(r->device);
		vessel_release(r->vessel_port);
		if (r->vessel_stbd != NULL) vessel_release(r->vessel_stbd);
		free(r->recog_string);
	}
}

PlatSpec *get_platform_list(char *name, u32 *n_found)
{
	FILE		*ip;
	PlatSpec	*rtn = NULL, *tmp;
	ParList		*parlist = NULL;
	char		buffer[BUFFER_LENGTH], string[BUFFER_LENGTH],
				device[BUFFER_LENGTH], config[BUFFER_LENGTH],
				depth_gate[BUFFER_LENGTH], angle_gate[BUFFER_LENGTH];

	*n_found = 0;
	if ((ip = fopen(name, "r")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for platform list.\n",
			name);
		return(NULL);
	}
	while (fgets(buffer, BUFFER_LENGTH, ip) != NULL) {
		if (sscanf(buffer, "%s %s %s %s %s", string, device, depth_gate, angle_gate, config) != 5) {
			error_msgv(modname, "error: failed to parse line %d of \"%s\""
				" for a platform description.\n", *n_found+1, name);
			platspec_release(rtn, *n_found);
			free(rtn); fclose(ip);
			return(NULL);
		}
		if ((tmp = (PlatSpec*)realloc(rtn, sizeof(PlatSpec)*(*n_found+1))) == NULL) {
			error_msgv(modname, "error: failed to get memory for platform"
				" specification %d from \"%s\".\n", *n_found+1, name);
			platspec_release(rtn, *n_found);
			free(rtn); fclose(ip);
			return(NULL);
		}
		rtn = tmp;

		/* Parse out and store angle and depth gates */
		if (sscanf(angle_gate, "%f,%f", &(rtn[*n_found].min_angle), &(rtn[*n_found].max_angle)) != 2) {
			error_msgv(modname, "error: failed to convert \"%s\" for angle"
				" gate.\n", angle_gate);
			platspec_release(rtn, *n_found);
			free(rtn); fclose(ip);
			return(NULL);
		}
		if (rtn[*n_found].min_angle <= -90.0f || rtn[*n_found].max_angle >= 90.0f) {
			error_msgv(modname, "error: angles are not acceptable for gate"
				" (need -90 deg. < min=%f < max=%f < 90 deg.).\n",
				rtn[*n_found].min_angle, rtn[*n_found].max_angle);
			platspec_release(rtn, *n_found);
			free(rtn); fclose(ip);
			return(NULL);
		}
		if (sscanf(depth_gate, "%f,%f", &(rtn[*n_found].min_depth), &(rtn[*n_found].max_depth)) != 2) {
			error_msgv(modname, "error: failed to convert \"%s\" for depth gate.\n", depth_gate);
			platspec_release(rtn, *n_found);
			free(rtn); fclose(ip);
			return(NULL);
		}
		if (rtn[*n_found].min_depth < 0 || rtn[*n_found].max_depth < 0) {
			error_msg(modname, "error: depth gate depths are +ve down.\n");
			platspec_release(rtn, *n_found);
			free(rtn); fclose(ip);
			return(NULL);
		}

		/* Attempt to load and execute the parameters list for the vessel */
		if (!params_new_from_file(config, &parlist) ||
			!vessel_execute_params(parlist)) {
			error_msgv(modname, "error: failed reading \"%s\" for"
				" vessel parameters.\n", config);
			platspec_release(rtn, *n_found);
			free(rtn); fclose(ip);
			return(NULL);
		}
		if (vessel_get_config() == VESSEL_DUAL_HEAD) {
			if ((rtn[*n_found].vessel_port = vessel_get_head(VESSEL_HEAD_PORT)) == NULL ||
				(rtn[*n_found].vessel_stbd = vessel_get_head(VESSEL_HEAD_STBD)) == NULL) {
				error_msgv(modname, "error: failed to get vessel descriptions for dual-"
					"head configuration from \"%s\".\n", config);
				platspec_release(rtn, *n_found);
				free(rtn); fclose(ip);
				return(NULL);
			}
		} else {
			if ((rtn[*n_found].vessel_port = vessel_get_head(VESSEL_HEAD_ANY)) == NULL) {
				error_msgv(modname, "error: failed to get vessel description from \"%s\".\n",
					config);
				platspec_release(rtn, *n_found);
				free(rtn); fclose(ip);
				return(NULL);
			}
			rtn[*n_found].vessel_stbd = NULL;
		}

		/* Convert the device description into a Device object */
		if ((rtn[*n_found].device = device_new_by_name(device)) == NULL) {
			error_msgv(modname, "error: failed to convert \"%s\" to a device"
				" specification.\n", device);
			platspec_release(rtn, *n_found);
			free(rtn); fclose(ip);
			return(NULL);
		}
		/* Finally, copy the recognition string */
		if ((rtn[*n_found].recog_string = strdup(string)) == NULL) {
			error_msgv(modname, "error: failed to get memory for recognition"
				" string \"%s\" from \"%s\".\n", string, name);
			platspec_release(rtn, *n_found);
			free(rtn); fclose(ip);
			return(NULL);
		}
		++(*n_found);
		ungetc(fgetc(ip), ip);
	}
	fclose(ip);
	return(rtn);
}

PlatSpec *match_platform(char *filename, PlatSpec *spec, u32 n_platforms)
{
	u32	plat;

	for (plat = 0; plat < n_platforms; ++plat)
		if (strstr(filename, spec[plat].recog_string) != NULL) return(spec + plat);
	return(NULL);
}

Bool assimilate_load_masks(char *stencil, char **sheetname, u32 n_sheets,
						   GUTM **georef, u8 ***masks)
{
	char	**masknames, *ptr;
	u32		n_masks, mask, sheet;
	u8		**mask_ims, **mask_map;
	GUTM	*geo;
	char	buffer[BUFFER_LENGTH], dirname[BUFFER_LENGTH], gutm[BUFFER_LENGTH];
	FILE	*geo_ip;
	Bool	full_grid, alt_data;
	u32			rows, cols;
	TIFFType	im_type;

	*georef = NULL;
	*masks = NULL;

	if ((ptr = strchr(stencil, ',')) == NULL) {
		error_msgv(modname, "error: \"%s\" is mal-formed.\n", stencil);
		return(False);
	}
	strcpy(gutm, ptr+1);
	*ptr = '\0';
	strcpy(dirname, stencil);
	*ptr = ',';

	sprintf(buffer, "%s/%s", dirname, gutm);
	if ((geo_ip = fopen(buffer, "r")) == NULL) {
		error_msgv(modname, "error: can't open \"%s\" for georeferencing.\n",
			buffer);
		return(False);
	}
	if ((geo = ccom_gutm_read_header(geo_ip, &full_grid, &alt_data)) == NULL) {
		error_msgv(modname, "error: failed to read header of \"%s\" for georeferencing.\n",
			buffer);
		fclose(geo_ip);
		return(False);
	}
	fclose(geo_ip);
	*georef = geo;	/* Palm off release to user ... */
	if ((masknames = assimilate_enumerate_files(dirname, ".tif", &n_masks)) == NULL) {
		error_msgv(modname, "error: failed to read names for mask images from \"%s\".\n",
			dirname);
		return(False);
	}
	if ((mask_ims = (u8**)calloc(n_masks, sizeof(u8*))) == NULL ||
		(mask_map = (u8**)calloc(n_sheets, sizeof(u8*))) == NULL) {
		error_msg(modname, "error: failed to allocate space for mask image array.\n");
		for (mask = 0; mask < n_masks; ++mask) free(masknames[mask]);
		free(masknames);
		return(False);
	}
	for (mask = 0; mask < n_masks; ++mask) {
		sprintf(buffer, "%s/%s", dirname, masknames[mask]);
		if ((mask_ims[mask] = ccom_read_tiff(buffer, &im_type, &rows, &cols)) == NULL ||
			im_type != TIFF_U8 || rows != geo->rows || cols != geo->cols) {
			error_msgv(modname, "error: failed to read \"%s\" for image mask %d,"
				" or image not U8 and the right size.\n",
				buffer, mask+1);
			for (mask = 0; mask < n_masks; ++mask) {
				free(masknames[mask]);
				free(mask_ims[mask]);
			}
			free(masknames); free(mask_ims);
			return(False);
		}
	}

	/* Match up names with the MapSheets, and make the output array */
	for (mask = 0; mask < n_masks; ++mask) {
		ptr = strrchr(masknames[mask], '.');
		*ptr = '\0'; /* Trim '.tif' extension, which must exist due to load sequence */
		for (sheet = 0; sheet < n_sheets; ++sheet)
			if (strstr(sheetname[sheet], masknames[mask]) != NULL) {
				/* This sheet name contains the mask name, so it should use this mask */
				mask_map[sheet] = mask_ims[mask];
			}
	}

	*masks = mask_map;
	free(mask_ims);	/* But *not* mask_ims[x], since we need these outside here */
	for (mask = 0; mask < n_masks; ++mask) free(masknames[mask]);
	free(masknames);
	return(True);
}

void release_mask_map(u8 **mask_map, u32 n_sheets)
{
	u32		mask, target;

	for (mask = 0; mask < n_sheets; ++mask) {
		if (mask_map[mask] == NULL) continue;
		for (target = mask+1; target < n_sheets; ++target)
			if (mask_map[target] == mask_map[mask]) mask_map[target] = NULL;
		free(mask_map[mask]);
	}
	free(mask_map);
}

BBox *compute_sheet_bounds(MapSheet *sheet, u32 n_sheets)
{
	u32	s;
	BBox	*rtn;

	if ((rtn = (BBox*)malloc(sizeof(BBox)*n_sheets)) == NULL) {
		error_msgv(modname, "error: failed to allocate memory for %d MapSheet"
			" bounding box structures.\n", n_sheets);
		return(NULL);
	}
	for (s = 0; s < n_sheets; ++s) {
		mapsheet_get_bl(sheet[s], &(rtn[s].x0), &(rtn[s].y0));
		mapsheet_get_tr(sheet[s], &(rtn[s].x1), &(rtn[s].y1));
	}
	return(rtn);
}

int main(int argc, char **argv)
{
	int				c;

	FILE			*list;
	SndIpType		ipFiles = SND_IP_RAW;
	char			*prefix_dir = NULL;

	ParList			*parlist = NULL;

	ErrModIHOOrder	order = DEFAULT_IHO_SURVEY_ORDER;

	Bool			switched;
	char			filename[BUFFER_LENGTH];

	MapSheet		*sheet = NULL;
	char			**sheet_name = NULL;
	BBox			*sheet_bounds;
	u32				n_sheets = 0, sht;
	time_t			last_flush, now, start;
	s32				flush_time_threshold = DEFAULT_FLUSH_TIME_THRESHOLD;
	GUTM			*georef;
	BBox			mask_bounds;
	char			*stencil = NULL;
	u8				**mask_map = NULL;
	s32				mask_row, mask_col;
	
	SoundingStream	stream;
	Platform		orient;
		
	PlatSpec		*plat_list = NULL, *plat_defn, *next_plat;
	u32				n_platforms, plat;

	Sounding		*data, *valid_pts;
	Bool			valid;
	u32				snd, n_samples, ping_num, n_soundings, tide_year = 0, n_used, n_valid;

	Tide			tide;
	char			*tide_name = NULL;

	ccom_log_command_line(modname, argc, argv);

	opterr = 0;
	while ((c = getopt(argc, argv, "d:f:i:p:t:y:m:s:")) != EOF) {
		switch(c) {
			case 's':
				if ((stencil = strdup(optarg)) == NULL) {
					error_msgv(modname, "error: failed to get memory for"
						" stencil string \"%s\".\n", stencil);
					return(1);
				}
				break;
			case 'm':
				flush_time_threshold = atoi(optarg);
				if (flush_time_threshold < DEFAULT_MINIMUM_FLUSH_TIME ||
					flush_time_threshold > DEFAULT_MAXIMUM_FLUSH_TIME) {
					error_msgv(modname, "error: memory timeout threshold must"
						" be in range [%d,%d]s (not %d s).\n",
						DEFAULT_MINIMUM_FLUSH_TIME, DEFAULT_MAXIMUM_FLUSH_TIME,
						flush_time_threshold);
					return(1);
				}
				break;
			case 'f':
				if (!params_new_from_file(optarg, &parlist)) {
					error_msgv(modname, "error: failed reading \"%s\" for"
						" parameters.\n", optarg);
					return(1);
				}
				break;
			case 'i':
				switch (atoi(optarg)) {
					case 1: order = ERRMOD_IHO_ORDER_1; break;
					case 2: order = ERRMOD_IHO_ORDER_2; break;
					case 3: order = ERRMOD_IHO_ORDER_3; break;
					case 4: order = ERRMOD_IHO_ORDER_4; break;
					default:
						error_msg(modname, "error: IHO survey orders are"
							" [1,4].\n");
						return(1);
				}
				break;
			case 't':
				tide_name = strdup(optarg);
				break;
			case 'y':
				tide_year = atoi(optarg);
				if (tide_year < 1970 || tide_year >= 2037) {
					error_msg(modname, "tide year must be between 1970 and"
						" 2037 (valid time() years).\n");
					return(1);
				}
				break;
			case 'p':
				prefix_dir = strdup(optarg);
				break;
			case 'd':
				if (strcmp(optarg, "omg") == 0)
					ipFiles = SND_IP_MERGED;
				else if (strcmp(optarg, "gsf") == 0)
					ipFiles = SND_IP_GSF;
				else if (strcmp(optarg, "raw") == 0)
					ipFiles = SND_IP_RAW;
				else if (strcmp(optarg, "nat") == 0)
					ipFiles = SND_IP_NATIVE;
				else if (strcmp(optarg, "hips") == 0)
					ipFiles = SND_IP_HIPS;
				else {
					error_msgv(modname, "error: input type \"%s\" not known.\n",
						optarg);
					return(1);
				}
				break;
			case '?':
			default:
				fprintf(stderr, "%s: unknown flag '%c'.\n", modname, optopt);
				Syntax();
				return(1);
		}
	}
	argc -= optind-1; argv += optind-1;
	if (argc != ARGC_EXPECTED) {
		Syntax();
		return(1);
	}

	/*
	 * Get a list of the platforms that will be used in the estimation.  The file
	 * consists of a line per platform in format:
	 *  <rstring> <device> <depth_gate> <angle_gate> <config>
	 *      ^         ^        ^			^			^
	 *		|		  |		   |			|			`- Config file to read
	 *		|		  |		   |			|			   vessel {} clause
	 *		|		  |		   |			`- Angle gate, format <min>,<max> degress,
	 *		|		  |		   |			   -ve to port.
	 *		|		  |		   `- Depth gate, format <min>,<max>, meters, +ve down.
	 *      |         `- Device name for the active element
	 *      `- Platform recognition string (must appear in the filename somewhere as
	 *		   an indicator).
	 *
	 */
	if ((plat_list = get_platform_list(argv[PLATFORM_LIST], &n_platforms)) == NULL) {
		error_msgv(modname, "error: failed to load platform list from \"%s\".\n",
			argv[PLATFORM_LIST]);
		return(1);
	}
	error_msgv(modname, "info: loaded %d platform specifications from \"%s\":\n",
		n_platforms, argv[PLATFORM_LIST]);
	for (plat = 0; plat < n_platforms; ++plat) {
		error_msgv(modname, "info:  %d \"%s\", device \"%s\" (%s head).\n", plat,
			plat_list[plat].recog_string, device_get_name(plat_list[plat].device),
			plat_list[plat].vessel_stbd == NULL ? "single" : "dual");
	}
	/* Check for maximum number of beams on any platform, and build valid beam buffer */
	n_valid = 0;
	for (plat = 0; plat < n_platforms; ++plat)
		if (device_get_nbeams(plat_list[plat].device) > n_valid)
			n_valid = device_get_nbeams(plat_list[plat].device);
	if ((valid_pts = (Sounding*)malloc(sizeof(Sounding)*n_valid)) == NULL) {
		error_msgv(modname, "error: failed to allocate space for %d valid beams.\n",
			n_valid);
		return(1);
	}

	/* Note that we have to delay this parameters list execution until now because
	 * the vessel {} setups above may cause problems, and we want the active parameters
	 * to be those that the user specified on the command line.
	 */
	if (parlist != NULL && !params_execute(parlist)) {
		error_msg(modname, "error: failed executing parameter list.\n");
		return(1);
	}
	
	/*
	 * Find all of the MapSheets in the directory that the user specified, and then
	 * load all of them into memory.  Note that this doesn't load any of the data for
	 * the MapSheets, since they are required to be HyperCUBE enabled, and this system
	 * delays loading the CubeGrid tiles until they are actually referenced.
	 *
	 */
	if ((sheet_name = assimilate_enumerate_files(argv[MAPSHEET_DIR], ".sht", &n_sheets)) == NULL) {
		error_msgv(modname, "error: failed to count MapSheets in \"%s\".\n",
			argv[MAPSHEET_DIR]);
		return(1);
	}
	error_msgv(modname, "info: loaded %d sheets from \"%s\":\n", n_sheets, argv[MAPSHEET_DIR]);
	for (sht = 0; sht < n_sheets; ++sht) {
		error_msgv(modname, "info:  %d: %s\n", sht, sheet_name[sht]);
	}

	if ((sheet = (MapSheet*)calloc(sizeof(MapSheet), n_sheets)) == NULL) {
		error_msgv(modname, "error: failed to allocate memory for MapSheet list"
			" (%d sheets from \"%s\").\n", n_sheets, argv[MAPSHEET_DIR]);
		return(1);
	}
	for (sht = 0; sht < n_sheets; ++sht) {
		sprintf(filename, "%s/%s", argv[MAPSHEET_DIR], sheet_name[sht]);
		if ((sheet[sht] = mapsheet_load_sheet(filename)) == NULL) {
			error_msgv(modname, "failed to load mapsheet from \"%s\".\n", filename);
			return(1);
		}
		if (mapsheet_get_depth_alg(sheet[sht]) != MAP_DEPTH_HYPERCUBE) {
			error_msgv(modname, "error: MapSheet %d (%s) does not have a HyperCUBE"
				" depth estimation surface (this is mandatory).\n", sht, filename);
			return(1);
		}
	}
	if ((sheet_bounds = compute_sheet_bounds(sheet, n_sheets)) == NULL) {
		error_msg(modname, "error: failed to get memory for MapSheet bounding boxes.\n");
		return(1);
	}
	if (stencil != NULL) {
		if (!assimilate_load_masks(stencil, sheet_name, n_sheets, &georef, &mask_map)) {
			error_msg(modname, "error: failed to load MapSheet activity masks.\n");
			return(1);
		}
		mask_bounds.x0 = georef->x0; mask_bounds.y0 = georef->y0;
		mask_bounds.x1 = georef->x0 + (georef->cols - 1)*georef->cell_size;
		mask_bounds.y1 = georef->y0 + (georef->rows - 1)*georef->cell_size;
	}
	
	/* If the user specified a tide file, attempt to load it */
	if (tide_name != NULL && tide_year == 0) {
		error_msg(modname, "you must specify a tide year (-y) with the tide"
			" file in order to correct properly.\n");
		return(1);
	}
	if (tide_name != NULL) {
		if ((tide = tide_new_from_omg(tide_name, tide_year)) == NULL) {
			error_msgv(modname, "failed loading \"%s\" for OMG ASCII Tides.\n",
				tide_name);
			return(1);
		}
	} else {
		tide = NULL;
	}
	
	/* Open list of files to be incorporated into the mapsheets */
	if ((list = fopen(argv[FILE_LIST], "r")) == NULL) {
		error_msgv(modname, "can't open \"%s\" for list of filenames.\n",
			argv[FILE_LIST]);
		return(1);
	}
	if (!get_next_name(list, prefix_dir, filename, BUFFER_LENGTH)) {
		error_msgv(modname, "failed reading list file.\n");
		return(1);
	}

	error_msgv(modname, "opening sounding stream \"%s\" ... ", filename);
	error_flush_output();

	/* Open the input stream assuming the format that the user told us about */
	if ((plat_defn = match_platform(filename, plat_list, n_platforms)) == NULL) {
		error_msgv(modname, "error: could not determine platform for filename \"%s\".\n",
			filename);
		return(1);
	}
	projection_make_current(mapsheet_get_projection(sheet[0]));
	stream = sounding_new_from_dual(ipFiles, filename,
							   mapsheet_get_projection(sheet[0]),
							   plat_defn->device, plat_defn->vessel_port, plat_defn->vessel_stbd,
							   ERRMOD_FULL, SOUNDING_BATHY);

	if (stream == NULL) {
		error_msg(NULL, " failed!\n");
		return(1);
	} else {
		error_msg(NULL, " succeeded!\n");
	}

	/* Add depth gate to sounding stream */
	if (!sounding_add_depthgate(stream, plat_defn->min_depth, plat_defn->max_depth)) {
		error_msg(modname, "error: failed to set depth gate on sounding stream.\n");
		return(1);
	}
	/* Add angle gate to sounding stream */
	if (!sounding_add_anglegate(stream, plat_defn->min_angle, plat_defn->max_angle)) {
		error_msg(modname, "error: failed to set angle gate on sounding stream.\n");
		return(1);
	}

	/*
	 * Main processing loop: read all pings from stream, and insert; then
	 * jump to next sounding stream; until all streams have been done.
	 *
	 */
	ping_num = 0; n_soundings = 0;
	error_msgv(modname, "Ping: %08d (%05d snd/s)", 0);
	error_flush_output();
	
	time(&last_flush);	/* Initialise our idea of how long we've been working */
	start = last_flush;	/* Record real-time stamp when we started */
	do {
		/*
		 * Sub-loop: get soundings from current stream until exhausted.
		 *
		 */
		while (sounding_get_next(stream, &orient, &data, &n_samples) == FILE_OK) {
			if ((ping_num%50) == 0) {
				time(&now);
				error_msgv(NULL, "\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b%08d (%05d snd/s)",
					ping_num, (u32)((f32)n_soundings/(now - start)));
				error_flush_output();
			}
			if (tide != NULL &&
				!tide_soundings(tide, orient.timestamp, data, n_samples)) {
				error_msgv(modname, "failed to tide ping %d soundings.\n",
					ping_num);
				continue;	/* ... and don't insert them */
			}
			for (sht = 0; sht < n_sheets; ++sht) {
				/* Check that there are any valid points for this sheet */
				n_valid = 0;
				for (snd = 0; snd < n_samples; ++snd) {
					valid = True;
					/* 1.  Is the point inside the MapSheet at all? */
					if (data[snd].east < sheet_bounds[sht].x0 || data[snd].east > sheet_bounds[sht].x1 ||
						data[snd].north < sheet_bounds[sht].y0 || data[snd].north > sheet_bounds[sht].y1)
						valid = False;
					/* 2.  If a mask is available, is it in an active region? */
					if (valid && mask_map != NULL) {
						mask_row = (s32)((mask_bounds.y1 - data[snd].north)/georef->cell_size);
						mask_col = (s32)((data[snd].east - mask_bounds.x0)/georef->cell_size);
						if (mask_row < 0 || mask_row >= (s32)georef->rows ||
							mask_col < 0 || mask_col >= (s32)georef->cols)
							valid = False;
						if (valid && mask_map[sht][mask_row*georef->cols + mask_col] != 0)
							valid = False;
					}
					if (!valid) continue;
					memcpy(valid_pts+n_valid, data+snd, sizeof(Sounding));
					++n_valid;
				}
				if (n_valid == 0) continue;
				projection_make_current(mapsheet_get_projection(sheet[sht]));
				if (!mapsheet_add_soundings(sheet[sht], stream, valid_pts, n_valid, &n_used)) {
					error_msgv(modname, "warning: failed to insert ping %d"
						" soundings into mapsheet %d (%s).\n", ping_num, sht, sheet_name[sht]);
					return(1);
				}
			}
			++ping_num; n_soundings += n_samples;
		}
				
		/*
		 * Switch to next sounding stream
		 *
		 */
		switched = False;
		while (!switched &&
					get_next_name(list, prefix_dir, filename, BUFFER_LENGTH)) {
			error_msgv(NULL, "\n");
			error_msgv(modname, "switching to stream \"%s\" ...\n", filename);
			if ((next_plat = match_platform(filename, plat_list, n_platforms)) == NULL) {
				error_msgv(modname, "error: could not determine platform for filename \"%s\".\n",
					filename);
				return(1);
			}
			if (next_plat == plat_defn) {
				sounding_detach_file(stream);
				if (!sounding_attach_file(stream, filename, SOUNDING_BATHY)) {
					error_msgv(modname, "error: failed jumping to new soundings files \"%s\".\n",
						filename);
				} else
					switched = True;
			} else {
				sounding_release(stream); stream = NULL;
				if ((stream = sounding_new_from_dual(ipFiles, filename,
								   mapsheet_get_projection(sheet[0]), next_plat->device,
								   next_plat->vessel_port, next_plat->vessel_stbd,
								   ERRMOD_FULL, SOUNDING_BATHY)) == NULL) {
					error_msgv(modname, "error: failed jumping to new soundings file \"%s\".\n",
						filename);
				} else {
					switched = True;
					plat_defn = next_plat;
					if (!sounding_add_depthgate(stream, plat_defn->min_depth, plat_defn->max_depth)) {
						error_msg(modname, "failed to set depth gate on sounding stream.\n");
						return(1);
					}
					if (!sounding_add_anglegate(stream, plat_defn->min_angle, plat_defn->max_angle)) {
						error_msg(modname, "failed to set angle gate on sounding stream.\n");
						return(1);
					}
				}
			}
		}
		time(&now);
		error_msgv(modname, "Ping: %08d (%05d snd/s)", ping_num, (u32)((f32)n_soundings/(now-start)));
		error_flush_output();
		if ((now - last_flush) > flush_time_threshold) {
			for (sht = 0; sht < n_sheets; ++sht) mapsheet_minimise_memory(sheet[sht]);
			last_flush = now;
		}
	} while (!feof(list));
	
	/* Save mapsheet */
	for (sht = 0; sht < n_sheets; ++sht) {
		sprintf(filename, "%s/%s", argv[MAPSHEET_DIR], sheet_name[sht]);
		if (!mapsheet_save_sheet(sheet[sht], filename)) {
			error_msgv(modname, "failed to save mapsheet %d (%s) to \"%s\".\n",
				sht, sheet_name[sht], filename);
			return(1);
		}
	}
		
	/* Release memory to simplify purify's leakage count */
	free(prefix_dir);
	
	return(0);
}
