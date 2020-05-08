/*
 * $Id: estdensity.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.3.2.1  2002/12/15 01:53:02  brc
 * Updated code to generate a MapSheet as output, rather than just creating one
 * in memory and then ditching it later.  This preserves the correct geo-referencing
 * and makes the output more easily manipulated by other tools.  Added flags to
 * allow all known data types to be read, to access configuration files, etc. as with
 * the other utilities, and made the output a GUTM (optionally).
 *
 * Revision 1.3  2001/08/21 00:12:48  brc
 * Moved from local mapsheet ASCII parser to mapsheet_new_from_ascii().
 *
 * Revision 1.2  2001/02/10 17:20:12  brc
 * Modified to use a vessel descriptor (new interface to sounding structures).
 *
 * Revision 1.1  2000/12/03 04:29:54  brc
 * Compute the sounding density for a dataset.
 *
 *
 * ----- Previous log from estdepth.c, used as basis for estdensity.c ------
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
 * File:	estdensity.c
 * Purpose:	Estimate data density
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
#include "stdtypes.h"
#include "device.h"
#include "vessel.h"
#include "projection.h"
#include "sounding.h"
#include "mapsheet.h"
#include "error.h"
#include "omg.h"
#include "ccom_general.h"

static char *modname = "estdepth";
static char *modrev = "$Revision: 20 $";

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
	FILELIST = 1,
	GEN_DEVICE,
	MAPSHEET_DESC,
	OUTPUT_SHEET,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("estdensity V%s [%s] - Estimate data density.\n",
			p, __DATE__);
	printf("Syntax: estdensity [opt] <iplist><device><mapdesc><outsheet>\n");
	printf(" Input file list ------------^      ^        ^        ^\n");
	printf(" Device generating data ------------'        |        |\n");
	printf(" Mapsheet (or ASCII description) ------------'        |\n");
	printf(" Output Mapsheet file --------------------------------'\n");
	printf(" Options:\n");
	printf("  -p dir     Specify prefix directory for all input merged files.\n");
	printf("  -a         Load ASCII description from <mapdesc> instead of mapsheet.\n");
	printf("  -r         Load OMG/R4 description from <mapdesc> instead of mapsheet.\n");
	printf("  -v         Verbose mode (prints checks and more time ticks).\n");
	printf("  -f params   Specify parameters file for library.\n");
	printf("  -d <type>  Input files are <type>:\n");
	printf("               omg:  OMG/HDCS variant.\n");
	printf("               gsf:  GSF V1.10 or later.\n");
	printf("               raw:  Manufacturer's raw datagrams (default).\n");
	printf("               nat:  Pre-digested native file.\n");
	printf("               hips: CARIS/HIPS ProcessedDepths.\n");
	printf("  -g <gutm>  Write density at the end of the process as <gutm>.\n");
	free(p);
}

void check_projection(Projection proj)
{
	f64	cen_x, cen_y, x0, y0;
	
	error_msg(NULL, "projection type: ");
	switch (projection_get_type(proj)) {
		case PROJECTION_UTM:
			error_msg(NULL, "UTM\n");
			break;
		case PROJECTION_MERCATOR:
			error_msg(NULL, "Mercator\n");
			break;
		case PROJECTION_POLAR_STEREO:
			error_msg(NULL, "Polar Sterographic\n");
			break;
		default:
			error_msg(NULL, "Unknown\n");
			break;
	}
	projection_get_params(proj, &cen_x, &cen_y, &x0, &y0);
	error_msgv(modname, "proj params: origin = (%lf, %lf) false origin = "
		"(%lf, %lf).\n", cen_x, cen_y, x0, y0);
	projection_ll_to_en_deg(proj, cen_x, cen_y, &x0, &y0);
	error_msgv(modname, "proj test: origin maps to (%lf, %lf).\n", x0, y0);
	cen_x = cen_y = 0.0;
	projection_en_to_ll_deg(proj, x0, y0, &cen_x, &cen_y);
	error_msgv(modname, "proj test: and back again to (%lf, %lf).\n",
		cen_x, cen_y);
}

void check_sheet(MapSheet sheet)
{
	f64	cen_x, cen_y, x0, y0;
	u32	width, height;
	
	mapsheet_get_center(sheet, &cen_x, &cen_y);
	error_msgv(modname, "sheet test: center = (%lf, %lf) m.\n", cen_x, cen_y);
	mapsheet_get_width_height(sheet, &width, &height);
	error_msgv(modname, "sheet test: (w,h) = (%d, %d) pels.\n", width, height);
	mapsheet_get_spacing(sheet, &x0, &y0);
	error_msgv(modname, "sheet test: (de,dn) = (%lf, %lf) m.\n", x0, y0);
	mapsheet_get_bl(sheet, &x0, &y0);
	error_msgv(modname, "sheet test: llc = (%lf, %lf) m.\n", x0, y0);
	mapsheet_get_br(sheet, &x0, &y0);
	error_msgv(modname, "sheet test: lrc = (%lf, %lf) m.\n", x0, y0);
	mapsheet_get_tr(sheet, &x0, &y0);
	error_msgv(modname, "sheet test: urc = (%lf, %lf) m.\n", x0, y0);
	mapsheet_get_tl(sheet, &x0, &y0);
	error_msgv(modname, "sheet test: ulc = (%lf, %lf) m.\n", x0, y0);
}

void check_device(Device dev)
{
	f32	across_wd, along_wd, opf1, opf2;
	
	device_get_beamwidths(dev, &across_wd, &along_wd);
	device_get_frequencies(dev, &opf1, &opf2);
	error_msgv(modname, "soundings from %s, max_beams = %d, beamwidths = "
		"(%lf, %lf) deg,", device_get_name(dev), device_get_nbeams(dev),
		across_wd, along_wd);
	if ((device_get_properties(dev) & DEVICE_IS_DUALFREQ) != 0)
		error_msgv(NULL, " op.freq = (%lf, %lf) kHz.\n", opf1, opf2);
	else
		error_msgv(NULL, " op.freq = %lf kHz.\n", opf1);
}

Bool get_next_name(FILE *list, char *prefix_dir, char *buffer, u32 buf_len)
{
	char	filename[256];
	u32		length;
	
	if (fgets(filename, 256, list) == NULL) {
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

f32 *generate_density(MapSheet sheet)
{
	u32	width, height, target, pel;
	u32	*hits;
	f32	*rtn, area;
	f64	east_sz, north_sz;
	
	if (mapsheet_get_data_type(sheet, MAP_NODE_HITS) != MAP_DATA_U32) {
		error_msgv(modname, "internal: hits sheet not u32!\n");
		return(NULL);
	}
	if ((hits = (u32*)mapsheet_get_data(sheet, MAP_NODE_HITS)) == NULL) {
		error_msg(modname, "failed to extract hits counts.\n");
		return(NULL);
	}
	mapsheet_get_width_height(sheet, &width, &height);
	target = sizeof(f32)*width*height;
	if ((rtn = (f32p)malloc(target)) == NULL) {
		error_msgv(modname, "couldn't allocate density grid memory (%d bytes)"
			".\n", target);
		free(hits);
		return(NULL);
	}
	
	mapsheet_get_spacing(sheet, &east_sz, &north_sz);
	area = (f32)(east_sz * north_sz);
	for (pel = 0; pel < width*height; ++pel)
		rtn[pel] = hits[pel] / area;
		
	free(hits);
	return(rtn);
}

Bool save_data_r4(MapSheet sheet, f32 *data, char *name)
{
	OMGRaster	r4;
	u32			rows, cols;
	
	mapsheet_get_width_height(sheet, &cols, &rows);
	if ((r4 = omg_new_from_mapsheet(sheet, MAP_NODE_HITS)) == NULL) {
		error_msg(modname, "failed to convert mapsheet to R4.\n");
		return(False);
	}
	if (!omg_replace_data(r4, OMG1_F32, rows, cols, data)) {
		error_msg(modname, "failed to swap raster data for densities.\n");
		return(False);
	}
	if (!omg_write(name, r4)) {
		error_msgv(modname, "failed to write \"%s\" as R4 output.\n", name);
		return(False);
	}
	omg_release(r4);
	return(True);
}

Bool real_save_data_r4(MapSheet sheet, char *name, MapSheetElem type)
{
	OMGRaster	r4;
	
	if ((r4 = omg_new_from_mapsheet(sheet, type)) == NULL) {
		error_msg(modname, "failed to convert mapsheet to R4.\n");
		return(False);
	}
	if (!omg_write(name, r4)) {
		error_msgv(modname, "failed to write \"%s\" as R4 output.\n", name);
		return(False);
	}
	omg_release(r4);
	return(True);
}

#define BUFFER_LENGTH 1024

int main(int argc, char **argv)
{
	int				c;
	FILE			*list;
	Bool			descfile = False, verbose = False, r4file = False;
	char			filename[BUFFER_LENGTH];
	MapSheet		sheet;
	SoundingStream	stream;
	Platform		orient;
	Sounding		*data;
	Vessel			vessel;
	u32				n_samples, ping_num, width, height;
	char			*prefix_dir = NULL, *gutm_file = NULL;
	f32				*density;
	f64				de, dn, x0, y0;
	SndIpType		ipFiles = SND_IP_RAW;
	Device			device;
	ParList			*parlist = NULL;
	FileError		sgn_rc;

	ccom_log_command_line(modname, argc, argv);

	opterr = 0;
	while ((c = getopt(argc, argv, "hp:ad:rvf:g:")) != EOF) {
		switch(c) {
			case 'g':
				if ((gutm_file = strdup(optarg)) == NULL) {
					error_msg(modname, "error: no memory for name of output file!?\n");
					return(1);
				}
				break;
			case 'f':
				if (!params_new_from_file(optarg, &parlist)) {
					error_msgv(modname, "error: failed reading \"%s\" for"
						" parameters.\n", optarg);
					return(1);
				}
				if (!params_execute(parlist)) {
					error_msg(modname, "error: failed executing parameter"
						" list.\n");
					return(1);
				}
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
			case 'v':
				verbose = True;
				break;
			case 'r':
				r4file = True;
				break;
			case 'a':
				descfile = True;
				break;
			case 'p':
				prefix_dir = strdup(optarg);
				break;
			case '?':
				fprintf(stderr, "%s: unknown flag '%c'.\n", modname, optopt);
			case 'h':
				Syntax();
				return(1);
		}
	}
	argc -= optind-1; argv += optind-1;
	if (argc != ARGC_EXPECTED) {
		Syntax();
		return(1);
	}
	if ((device = device_new_by_name(argv[GEN_DEVICE])) == NULL) {
		error_msgv(modname, "can't convert \"%s\" to device specification.\n",
			argv[GEN_DEVICE]);
		return(1);
	}
	if (descfile && r4file) {
		error_msg(modname, "can't have both ASCII and R4 descriptions!\n");
		return(1);
	}
	if (descfile) {
		/* We're initialising a mapsheet, so read ASCII description */
		if ((sheet = mapsheet_new_from_ascii(argv[MAPSHEET_DESC])) == NULL) {
			error_msgv(modname, "failed to load \"%s\" for mapsheet.\n",
				argv[MAPSHEET_DESC]);
			return(1);
		}
		/* Add a hit count surface to the mapsheet */
		if (verbose) {
			error_msg(modname, "adding hitcount surface to mapsheet ... ");
			error_flush_output();
		}
		if (!mapsheet_add_hits(sheet)) {
			if (verbose) error_msg(NULL, "failed!\n");
			else
				error_msg(modname, "failed adding hit count surface.\n");
			return(1);
		} else error_msg(NULL, "succeeded!\n");
	} else if (r4file) {
		/* We're initialising from an R4, so read OMG/R4 header and convert */
		OMGRaster	raster;
		if ((raster = omg_new_from_file(argv[MAPSHEET_DESC], True)) == NULL) {
			error_msgv(modname, "failed to load description from \"%s\".\n",
				argv[MAPSHEET_DESC]);
			return(1);
		}
		if ((sheet = mapsheet_new_from_omg(raster)) == NULL) {
			error_msgv(modname, "failed to convert mapsheet from \"%s\".\n",
				argv[MAPSHEET_DESC]);
			return(1);
		}
		omg_release(raster);
		/* Add a hit count surface to the mapsheet */
		if (verbose) {
			error_msg(modname, "adding hitcount surface to mapsheet ... ");
			error_flush_output();
		}
		if (!mapsheet_add_hits(sheet)) {
			if (verbose) error_msg(NULL, "failed!\n");
			else
				error_msg(modname, "failed adding hit count surface.\n");
			return(1);
		} else error_msg(NULL, "succeeded!\n");
	} else {
		/* We're loading a previous mapsheet for update */
		if ((sheet = mapsheet_load_sheet(argv[MAPSHEET_DESC])) == NULL) {
			error_msgv(modname, "failed to load mapsheet from \"%s\".\n",
				argv[MAPSHEET_DESC]);
			return(1);
		}
		/* Check that it has a hit count surface */
		if (!mapsheet_contains(sheet, MAP_NODE_HITS)) {
			error_msgv(modname, "error: mapsheet \"%s\" does not contain"
				" a hit count surface.\n", argv[MAPSHEET_DESC]);
			return(1);
		}
	}
	if (verbose) {
		check_sheet(sheet);
		check_projection(mapsheet_get_projection(sheet));
	}
	
	/* Make default vessel configuration */
	if ((vessel = vessel_default()) == NULL) {
		error_msg(modname, "failed generating default vessel configuration.\n");
		return(1);
	}
	
	/* Open list of files to be incorporated into the mapsheet */
	if ((list = fopen(argv[FILELIST], "r")) == NULL) {
		error_msgv(modname, "can't open \"%s\" for list of filenames.\n",
			argv[FILELIST]);
		return(1);
	}
	if (!get_next_name(list, prefix_dir, filename, BUFFER_LENGTH)) {
		error_msgv(modname, "failed reading list file.\n");
		return(1);
	}

	if (verbose) {
		error_msgv(modname, "opening sounding stream \"%s\" ... ", filename);
		error_flush_output();
	}

	if ((stream = sounding_new_from(ipFiles, filename, mapsheet_get_projection(sheet),
										device, vessel, ERRMOD_FULL, SOUNDING_BATHY)) == NULL) {
		if (verbose) error_msg(NULL, " failed!\n");
		else
			error_msgv(modname, "failed reading file \"%s\" for filenames.\n",
				filename);
		return(1);
	} else {
		if (verbose) error_msg(NULL, " succeeded!\n");
	}
	
	/* Determine device and report characteristics */
	if (verbose) check_device(device);

	/*
	 * Main processing loop: read all pings from stream, and insert; then
	 * jump to next sounding stream; until all streams have been done.
	 *
	 */
	ping_num = 0;
	if (!verbose) {
		error_msgv(modname, "Ping: %08d", 0);
		error_flush_output();
	}
	do {
		/*
		 * Sub-loop: get soundings from current stream until exhausted.
		 *
		 */
		while ((sgn_rc = sounding_get_next(stream, &orient, &data, &n_samples)) == FILE_OK) {
			if (verbose) {
				error_msgv(modname, "ping %d, %d soundings (mid-depth %f m).\n",
					ping_num, n_samples, data[n_samples/2].depth);
			} else {
				if ((ping_num%500) == 0) {
					error_msgv(NULL, "\b\b\b\b\b\b\b\b%08d", ping_num);
					error_flush_output();
				}
			}
			if (!mapsheet_count_hits(sheet, stream, data, n_samples)) {
				error_msgv(modname, "failed to insert ping %d soundings.\n",
					ping_num);
				return(1);
			}
			++ping_num;
		}
		if (sgn_rc != FILE_OK && sgn_rc != FILE_EOF) {
			error_msgv(modname, "error: emergency stop!\n");
			return(1);
		}
		/*
		 * Switch to next sounding stream
		 *
		 */
		if (get_next_name(list, prefix_dir, filename, BUFFER_LENGTH)) {
			sounding_detach_file(stream);
			if (verbose)
				error_msgv(modname, "switching to stream \"%s\" ...\n",
					filename);
			if (!sounding_attach_file(stream, filename, SOUNDING_BATHY)) {
				error_msgv(modname,
					"failed jumping to new soundings file \"%s\".\n", filename);
				return(1);
			}
		}
	} while (!feof(list));
	sounding_release(stream);
	vessel_release(vessel);
	device_release(device);
	
	/* Save the mapsheet to output as required */
	if (!mapsheet_save_sheet(sheet, argv[OUTPUT_SHEET])) {
		error_msgv(modname, "error: failed to save sheet to \"%s\".\n", argv[OUTPUT_SHEET]);
		return(1);
	}

	/* Save density surface to GUTM file */
	if (gutm_file != NULL) {
		/* Extract information and convert to density of soundings */
		if ((density = generate_density(sheet)) == NULL) {
			error_msg(modname, "error: failed to generate density surface.\n");
			return(1);
		}
		mapsheet_get_width_height(sheet, &width, &height);
		mapsheet_get_spacing(sheet, &de, &dn);
		mapsheet_get_tl(sheet, &x0, &y0);
		if (!ccom_write_gutm(gutm_file, density, DATA_F32, height, width, (f32)de, x0, y0)) {
			error_msgv(modname, "error: failed saving to \"%s\".\n", gutm_file);
			return(1);
		}
	}
	return(0);
}
