/*
 * $Id: estdepth.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.14.2.2  2002/12/15 01:53:18  brc
 * Added command-line loging command.
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
#include <math.h>
#include "stdtypes.h"
#include "device.h"
#include "vessel.h"
#include "projection.h"
#include "sounding.h"
#include "tides.h"
#include "mapsheet.h"
#include "error.h"
#include "omg.h"
#include "ccom_general.h"
#include "errmod.h"
#include "params.h"
#include "stime.h"

static char *modname = "estdepth";
static char *modrev = "$Revision: 20 $";

typedef struct _target {
	GdpFILE	*f;
	char	*filename;
	f64		x0, y0, delta;
	struct _target *next;
} Target;

#define DEFAULT_IHO_SURVEY_ORDER ERRMOD_IHO_ORDER_1

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
	MERGED_FILELIST = 1,
	GEN_DEVICE,
	MAPSHEET_DESC,
	OUTPUT_SHEET,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("estdepth V%s [%s] - Estimate depths using various algorithms.\n",
			p, __DATE__);
	printf("Syntax: estdepth [opt] <raw-list><device><mapdesc><output>\n");
	printf(" Raw data file list -------^         ^       ^        ^\n");
	printf(" Device used to generate data -------'       |        |\n");
	printf(" Mapsheet (or ASCII description) ------------'        |\n");
	printf(" Output mapsheet file --------------------------------'\n");
	printf(" Options:\n");
	printf("  Computational Environment:\n");
	printf("  -v          Verbose mode (prints checks and more time ticks).\n");
	printf("  -p dir      Specify prefix directory for all input merged files.\n");
	printf("  -f params   Specify parameters file for library.\n");
	printf("\n  Input File Options:\n");
	printf("  -d <type>   Input files are <type>:\n");
	printf("                  omg:  OMG/HDCS variant.\n");
	printf("                  gsf:  GSF V1.10 or later.\n");
	printf("                  raw:  Manufacturer's raw datagrams (default).\n");
	printf("                  nat:  Pre-digested native file.\n");
	printf("                  hips: CARIS/HIPS ProcessedDepths.\n");
/*	printf("  -h          Input files are OMG/HDCS format.\n");
	printf("  -g          Input files are GSF V1.10 format (or later).\n");
	printf("  -n          Input files are manufacturer's native format (default).\n");
	printf("  -x          Input files are pre-digested native format.\n");*/
	printf("  -a          Load ASCII description from <mapdesc> instead of mapsheet.\n");
	printf("  -r          Load OMG/R4 description from <mapdesc> instead of mapsheet.\n");
	printf("\n  Depth Estimation Algorithms:\n");
	printf("  -z min,max  Set depth gate on input soundings (m +ve down).\n");
	printf("  -q min,max  Set angle gate on input soundings (deg. +ve to stbd).\n");
	printf("  -m          Compute depths with mean bining algorithm\n");
	printf("  -c          Compute depths with median bining algorithm\n");
	printf("  -l lim      Limit maximum bin soundings to _lim_ in memory.\n");
	printf("  -k          Compute depths with Kalman filtering algorithm\n");
	printf("  -b          Compute depths using HyperCUBE (default).\n");
	printf("  -i order    Set IHO survey order for estimation accuracy "
						  "(default: %d).\n", 1+(u32)DEFAULT_IHO_SURVEY_ORDER);
	printf("\n  Hydrographic corrections:\n");
	printf("  -t file     Load OMG ASCII tide descriptions from <file>.\n");\
	printf("  -y year     Specify year (YYYY) for tide file since OMG isn't Y2K.\n");
	printf("\n  Output File Options:\n");
	printf("  -o          Write output surfaces (-d,-u,-s) in OMG R4 format.\n");
	printf("  -x file     Write depth surface to <file> as a TIFF image.\n");
	printf("  -u file     Write depth uncertainty to <file> as a TIFF image.\n");
	printf("  -s file     Write depth std. dev. to <file> as TIFF image.\n");
	printf("  -e dir      Write summary information into directory <dir>.\n");
	printf("  -w file     Write to file a list of input files actually used.\n");
	printf("  -j file     Write raw depths within \\pm d m. of (x0,y0)\n");
	printf("              to (GeoZui3D) GDP <file>.\n");
	printf(" Notes:\n");
	printf(" 1. Mean/Median filtering algorithms operate in memory, and will\n");
	printf("    fail if they can't fit all of the data into core.\n");
	printf(" 2. Depth uncertainty surface is only available with the Kalman\n");
	printf("    filtering algorithm, and HyperCUBE.\n");
	printf(" 3. TIFF images are not geo-referenced, but the output mapsheet is.\n");
	printf(" 4. TIFF images are 16bpp greyscale with linear contrast enhancement.\n");
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

void check_tide(Tide tide, char *name)
{
	FILE	*op;
	u32		n_points, point, year, month, day, hr, min, sec;
	f64		timetag, msec;
	f32		mtide;
	
	if ((op = fopen(name, "w")) == NULL) {
		error_msgv(modname, "failed to open \"%s\" for writing tide output.\n",
			name);
		return;
	}
	n_points = tide_get_npoints(tide);
	for (point = 0; point < n_points; ++point) {
		if (!tide_get_point_n(tide, point, &timetag, &mtide)) {
			error_msgv(modname, "failed reading point %d from tide LUT.\n",
				point);
			return;
		}
		stime_break_time(timetag, &year, &month, &day, &hr, &min, &sec, &msec);
		fprintf(op, "%04d/%02d/%02d %02d:%02d:%02d:%03d %f\n",
			year, month, day, hr, min, sec, (u32)msec, mtide);
	}
	fclose(op);
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

Bool save_data_tif(MapSheet sheet, char *name, MapSheetElem type)
{
	u32		width, height;
	void	*data;
	TIFFFlags flags = { True, False, False, False };

	if (mapsheet_get_data_type(sheet, type) != MAP_DATA_F32) {
		error_msg(modname, "surface is not f32.  Can't convert.\n");
		return(False);
	}
	if ((data = mapsheet_get_data(sheet, type)) == NULL) {
		error_msg(modname, "failed to get data from sheet.\n");
		return(False);
	}
	mapsheet_get_width_height(sheet, &width, &height);
	if (!ccom_write_tiff(NULL, name, data, TIFF_F32, height, width, &flags)) {
		error_msg(modname, "failed to write TIFF.\n");
		return(False);
	}
	free(data);
	return(True);
}

Bool save_data_r4(MapSheet sheet, char *name, MapSheetElem type)
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

Bool filt_write_gdp(GdpFILE *f, Sounding *data, u32 nsnd,
					f64 x0, f64 y0, f64 delta)
{
	Sounding	*buffer;
	u32			snd, n_writeable;
	Bool		rc;
	f64			x, y;
#ifdef __DEBUG__
	static u32	n_total = 0;
#endif
	
	n_writeable = 0;
	for (snd = 0; snd < nsnd; ++snd) {
		x = data[snd].east - x0;
		y = data[snd].north - y0;
		if ((fabs(x) <= delta) && (fabs(y) <= delta)) ++n_writeable;
	}
	
	if (n_writeable == 0) return(True);
	
	if ((buffer = (Sounding*)malloc(sizeof(Sounding)*n_writeable)) == NULL) {
		error_msgv(modname, "error: out of memory for Sounding buffer in"
			" GDP output filter (%d soundings, %d bytes).\n", n_writeable,
			n_writeable*sizeof(Sounding));
		return(False);
	}
	n_writeable = 0;
	for (snd = 0; snd < nsnd; ++snd) {
		x = data[snd].east - x0;
		y = data[snd].north - y0;
		if ((fabs(x) <= delta) && (fabs(y) <= delta)) {
			memcpy(buffer + n_writeable, data + snd, sizeof(Sounding));
			++n_writeable;
		}
	}
	rc = ccom_write_gdp(f, buffer, n_writeable);
	free(buffer);
#ifdef __DEBUG__
	n_total += n_writeable;
	error_msgv(modname, "info: wrote %d elements to GDP file (%d total since"
		" start of processing).\n", n_writeable, n_total);
#endif
	return(rc);
}

Bool list_filt_write_gdp(Target *list, Sounding *data, u32 nsnd)
{
	Bool	rc = True;
	
	while (rc && list != NULL) {
		rc |= filt_write_gdp(list->f, data, nsnd,
							 list->x0, list->y0, list->delta);
		list = list->next;
	}
	return(rc);
}

void free_gdp_list(Target *list)
{
	Target *next;
	
	while (list != NULL) {
		next = list->next;
		if (list->f != NULL) ccom_close_gdp(list->f);
		free(list->filename);
		free(list);
		list = next;
	}
}

Target *read_gdp_filespec(char *name)
{
	Target	*rtn = NULL, *tgt;
	u32		n_specs;
	char	buffer[1024], *ptr;
	FILE	*ip;
	
	if ((ip = fopen(name, "r")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for GDP point"
			" file specifications input.\n", name);
		return(NULL);
	}
	n_specs = 0;
	while (fgets(buffer, 1024, ip) != NULL) {
		if ((tgt = (Target *)malloc(sizeof(Target))) == NULL) {
			error_msgv(modname, "error: out of memory while allocating space"
				" for GDP point specification %d.\n", n_specs);
			free_gdp_list(rtn);
			return(NULL);
		}
		memset(tgt, 0, sizeof(Target));
		if ((ptr = strchr(buffer, ' ')) == NULL) {
			error_msgv(modname, "error: malformed GDP point spec \"%s\" at"
				" specification %d.\n", buffer, n_specs);
			free_gdp_list(rtn);
			return(NULL);
		}
		*ptr++ = '\0';
		if (sscanf(ptr, "%lf %lf %lf", &(tgt->x0), &(tgt->y0), &(tgt->delta))
				!= 3 || (tgt->filename = strdup(buffer)) == NULL) {
			error_msgv(modname, "error: failed parsing GDP point spec %d.\n",
				n_specs);
			free_gdp_list(rtn);
			return(NULL);
		}
		if ((tgt->f = ccom_open_gdp(tgt->filename, COLOUR_BY_DEPTH)) == NULL) {
			error_msgv(modname, "error: failed to open \"%s\" for GDP point"
				" spec %d.\n", tgt->filename, n_specs);
			free_gdp_list(rtn);
			return(NULL);
		}
		tgt->next = rtn;
		rtn = tgt;
	}
	fclose(ip);
	return(rtn);
}

Bool close_gdp_list(Target *list)
{
	Bool	rc = True;
	
	while (rc && list != NULL) {
		rc |= ccom_close_gdp(list->f);
		list->f = NULL;
		list = list->next;
	}
	return(rc);
}

#define BUFFER_LENGTH 1024

int main(int argc, char **argv)
{
	int				c;
	FILE			*list, *relist_fp = NULL;
	Target			*gdp_list = NULL;
	SndIpType		ipFiles = SND_IP_RAW;
	ParList			*parlist = NULL;
	Bool			descfile = False, verbose = False, output_r4 = False, rc,
					r4file = False, switched, write_name;
	char			filename[BUFFER_LENGTH];
	MapSheet		sheet;
	SoundingStream	stream;
	Platform		orient;
	Device			device;
	Vessel			vessel;
	Sounding		*data;
	Tide			tide;
	f32				min_depth = -1.0f, max_depth = -1.0f;
	f32				min_angle = -180.0, max_angle = 180.0;
	u32				n_samples, ping_num, bin_limit = 0, tide_year = 0,
					n_used;
	char			*depth_name = NULL, *uncert_name = NULL, *prefix_dir = NULL,
					*tide_name = NULL, *sdev_name = NULL, *summary_dir = NULL;
	MapSheetDepthAlg	alg = MAP_DEPTH_HYPERCUBE;	/* Default to HyperCUBE */
	ErrModIHOOrder	order = DEFAULT_IHO_SURVEY_ORDER;
	
	ccom_log_command_line(modname, argc, argv);

	opterr = 0;
	while ((c = getopt(argc, argv, "abcd:e:f:i:j:kl:mop:q:rs:t:u:vw:x:y:z:")) != EOF) {
		switch(c) {
			case 'j':
				if ((gdp_list = read_gdp_filespec(optarg)) == NULL) {
					error_msgv(modname, "error: failed to read filespec from"
						" \"%s\" for output GDP files.\n", optarg);
					return(1);
				}
				break;
			case 'w':
				if ((relist_fp = fopen(optarg, "w")) == NULL) {
					error_msgv(modname, "error: failed opening \"%s\" for"
						" rewritten input list output file.\n", optarg);
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
			case 'e':
				summary_dir = strdup(optarg);
				break;
			case 'q':
				if (sscanf(optarg, "%f,%f", &min_angle, &max_angle) != 2) {
					error_msgv(modname, "failed to convert \"%s\" for angle"
						" gate.\n", optarg);
					return(1);
				}
				if (min_angle <= -90.0f || max_angle >= 90.0f) {
					error_msgv(modname, "angles are not acceptable for gate"
						" (need -90 deg. < min=%f < max=%f < 90 deg.).\n",
						min_angle, max_angle);
					return(1);
				}
				break;
			case 'z':
				if (sscanf(optarg, "%f,%f", &min_depth, &max_depth) != 2) {
					error_msgv(modname, "failed to convert \"%s\" for depth"
						" gate.\n", optarg);
					return(1);
				}
				if (min_depth < 0 || max_depth < 0) {
					error_msg(modname, "depth gate depths are +ve down.\n");
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
			case 'r':
				r4file = True;
				break;
			case 'l':
				bin_limit = atoi(optarg);
				break;
			case 'o':
				output_r4 = True;
				break;
			case 'v':
				verbose = True;
				break;
			case 'a':
				descfile = True;
				break;
			case 'p':
				prefix_dir = strdup(optarg);
				break;
			case 'x':
				depth_name = strdup(optarg);
				break;
			case 'u':
				uncert_name = strdup(optarg);
				break;
			case 's':
				sdev_name = strdup(optarg);
				break;
			case 'm':
				alg = MAP_DEPTH_BINED_MEAN;
				break;
			case 'c':
				alg = MAP_DEPTH_BINED_MEDIAN;
				break;
			case 'k':
				alg = MAP_DEPTH_NODAL;
				break;
			case 'b':
				alg = MAP_DEPTH_HYPERCUBE;
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
			error_msgv(modname, "failed to load \"%s\" as mapsheet description"
						" in ASCII format.\n", argv[MAPSHEET_DESC]);
			return(1);
		}
		/* Add at least a depth surface to the mapsheet with chosen algorithm */
		if (verbose) {
			error_msg(modname, "adding depth surface to mapsheet ... ");
			error_flush_output();
		}
		if (!mapsheet_add_depth_surface(sheet, alg, order)) {
			if (verbose) error_msg(NULL, "failed!\n");
			else
				error_msg(modname, "failed adding depth surface.\n");
			return(1);
		} else error_msg(NULL, "succeeded!\n");
	} else if (r4file) {
		/* We're initialising from an R4, so read OMG/R4 header and convert */
		OMGRaster	raster;
		if ((raster = omg_new_from_file(argv[MAPSHEET_DESC], True)) == NULL) {
			error_msgv(modname, "failed to load mapsheet from \"%s\".\n",
				argv[MAPSHEET_DESC]);
			return(1);
		}
		if ((sheet = mapsheet_new_from_omg(raster)) == NULL) {
			error_msgv(modname, "failed to convert mapsheet from \"%s\".\n",
				argv[MAPSHEET_DESC]);
			return(1);
		}
		omg_release(raster);
		/* Add at least a depth surface to the mapsheet with chosen algorithm */
		if (verbose) {
			error_msg(modname, "adding depth surface to mapsheet ... ");
			error_flush_output();
		}
		if (!mapsheet_add_depth_surface(sheet, alg, order)) {
			if (verbose) error_msg(NULL, "failed!\n");
			else
				error_msg(modname, "failed adding depth surface.\n");
			return(1);
		} else error_msg(NULL, "succeeded!\n");		
	} else {
		/* We're loading a previous mapsheet for update */
		error_msgv(modname, "info: loading \"%s\" for update ... ",
			argv[MAPSHEET_DESC]);
		error_flush_output();
		if ((sheet = mapsheet_load_sheet(argv[MAPSHEET_DESC])) == NULL) {
			error_msgv(modname, "failed to load mapsheet from \"%s\".\n",
				argv[MAPSHEET_DESC]);
			return(1);
		}
		error_msg(NULL, "done.\n");
	}

	if (verbose) {
		check_sheet(sheet);
		check_projection(mapsheet_get_projection(sheet));
	}
	/* Check that the user's bin_limit is only used if we have a binned
	 * surface.
	 */
	if (bin_limit != 0) {
		/* Determine the algorithm being used by the mapsheet */
		alg = mapsheet_get_depth_alg(sheet);
		if (alg == MAP_DEPTH_BINED_MEAN || alg == MAP_DEPTH_BINED_MEDIAN) {
			if (!mapsheet_limit_bin_size(sheet, bin_limit)) {
				error_msgv(modname,
					"failed to set maximum bin buffer to %d slots.\n",
					bin_limit);
				return(1);
			}
		} else {
			error_msg(modname, "-l ignored: depth surface is not binned.\n");
		}
	}

	/* Make a default Vessel configuration so that we can initialise the
	 * sounding stream structures correctly.
	 */
	if ((vessel = vessel_default()) == NULL) {
		error_msg(modname, "failed to generate default vessel specification.\n");
		return(1);
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
	
	/* Open list of files to be incorporated into the mapsheet */
	if ((list = fopen(argv[MERGED_FILELIST], "r")) == NULL) {
		error_msgv(modname, "can't open \"%s\" for list of filenames.\n",
			argv[MERGED_FILELIST]);
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
	
	/* Open the input stream assuming the format that the user told us about */
	stream = sounding_new_from(ipFiles, filename,
							   mapsheet_get_projection(sheet), device, vessel,
							   ERRMOD_FULL, SOUNDING_BATHY);

	if (stream == NULL) {
		if (verbose) error_msg(NULL, " failed!\n");
		else
			error_msgv(modname, "failed reading file \"%s\" for input.\n",
				filename);
		return(1);
	} else {
		if (verbose) error_msg(NULL, " succeeded!\n");
	}

	/* Add depth gate to sounding stream */
	if (min_depth >= 0.0) {
		/* Note, only need to check one, because these have to be set as a
		 * pair in the command line.
		 */
		if (!sounding_add_depthgate(stream, min_depth, max_depth)) {
			error_msg(modname, "failed to set depth gate on sounding stream.\n");
			return(1);
		}
	}
	
	/* Add angle gate to sounding stream */
	if (min_angle > -90.0) {
		if (!sounding_add_anglegate(stream, min_angle, max_angle)) {
			error_msg(modname, "failed to set angle gate on sounding stream.\n");
			return(1);
		}
	}

	/* Determine device and report characteristics */
	if (verbose) check_device(sounding_get_device(stream));

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
	write_name = False;

	do {
		/*
		 * Sub-loop: get soundings from current stream until exhausted.
		 *
		 */
		while (sounding_get_next(stream, &orient, &data, &n_samples) == FILE_OK) {
			if (verbose) {
				error_msgv(modname, "ping %d, %d soundings (mid-depth %f m).\n",
					ping_num, n_samples, data[n_samples/2].depth);
			} else {
				if ((ping_num%500) == 0) {
					error_msgv(NULL, "\b\b\b\b\b\b\b\b%08d", ping_num);
					error_flush_output();
				}
			}
			if (tide != NULL &&
				!tide_soundings(tide, orient.timestamp, data, n_samples)) {
				error_msgv(modname, "failed to tide ping %d soundings.\n",
					ping_num);
				continue;	/* ... and don't insert them */
			}
			if (gdp_list != NULL &&
				!list_filt_write_gdp(gdp_list, data, n_samples)) {
				error_msgv(modname, "error: failed to write GDP output file(s)"
					" for ping %d.\n", ping_num);
				return(1);
			}
			n_used = 0;
			if (!mapsheet_add_soundings(sheet, stream, data, n_samples, &n_used)) {
				error_msgv(modname, "warning: failed to insert ping %d"
					" soundings.\n", ping_num);
				return(1);
			}
			if (n_used > 0) write_name = True;
			++ping_num;
		}
		
		/* Write name of input file, if it contained data which was incorporated
		 * into the mapsheet, and the user requested a re-written input list.
		 */
		if (relist_fp != NULL && write_name) {
			fprintf(relist_fp, "%s\n", filename);
			fflush(relist_fp);
			write_name = False;	/* Reset for next file switched in */
		}
		
		/*
		 * Switch to next sounding stream
		 *
		 */
		switched = False;
		while (!switched &&
					get_next_name(list, prefix_dir, filename, BUFFER_LENGTH)) {
			sounding_detach_file(stream);
/*			if (verbose)*/
				error_msgv(modname, "switching to stream \"%s\" ...\n",
					filename);
			if (!sounding_attach_file(stream, filename, SOUNDING_BATHY)) {
				error_msgv(modname,
					"failed jumping to new soundings file \"%s\".\n", filename);
			} else
				switched = True;
		}
	} while (!feof(list));
	sounding_release(stream);
	vessel_release(vessel);
	if (gdp_list != NULL && !close_gdp_list(gdp_list)) {
		error_msg(modname, "warning: unable to close GDP output file(s).  Data"
			" in file(s) may be incomplete/absent.\n");
	}
	free_gdp_list(gdp_list);
	
	/* Save mapsheet */
	if (!mapsheet_save_sheet(sheet, argv[OUTPUT_SHEET])) {
		error_msgv(modname, "failed to save mapsheet to \"%s\".\n",
			argv[OUTPUT_SHEET]);
		return(1);
	}
	
	/* Attempt to save data elements as TIFF if required */
	if (depth_name != NULL) {
		if (output_r4)
			rc = save_data_r4(sheet, depth_name, MAP_DEPTH_ESTIMATE);
		else
			rc = save_data_tif(sheet, depth_name, MAP_DEPTH_ESTIMATE);
		if (!rc) {
			error_msgv(modname, "failed to write depth surface as \"%s\".\n",
						depth_name);
		}
	}
	if (uncert_name != NULL) {
		if (output_r4)
			rc = save_data_r4(sheet, uncert_name, MAP_DEPTH_UNCERTAINTY);
		else
			rc = save_data_tif(sheet, uncert_name, MAP_DEPTH_UNCERTAINTY);
		if (!rc) {
			error_msgv(modname, "failed to write depth uncertainty surface as"
				" \"%s\".\n", uncert_name);
		}
	}
	if (sdev_name != NULL) {
		if (output_r4)
			rc = save_data_r4(sheet, sdev_name, MAP_DEPTH_SDEV);
		else
			rc = save_data_tif(sheet, sdev_name, MAP_DEPTH_SDEV);
		if (!rc) {
			error_msgv(modname, "failed to write depth std. dev. surface as"
				" \"%s\".\n", sdev_name);
		}
	}
	
	if (summary_dir != NULL) {
		if (!mapsheet_make_summary(sheet, summary_dir)) {
			error_msgv(modname, "failed to write mapsheet summary to \"%s\".\n",
				summary_dir);
		}
	}
	
	/* Release memory to simplify purify's leakage count */
	free(prefix_dir); free(depth_name); free(uncert_name);
	
	return(0);
}
