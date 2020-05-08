/*
 * $Id: sheetinfo.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.2.2.1  2002/12/15 02:03:06  brc
 * Added facility to specify a parameter file before reading back.
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
 * File:	sheetinfo.c
 * Purpose:	Provide header information from a mapsheet
 * Date:	22 July 2000
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
#include "ccom_general.h"
#include "mapsheet.h"

static char *modname = "sheetinfo";
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
	INPUT_FILE = 1,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("sheetinfo V%s [%s] - Provide header information from a mapsheet.\n",
			p, __DATE__);
	printf("Syntax: sheetinfo [opt] <input>\n");
	printf(" Mapsheet -----------------^\n");
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
	printf("%s: proj params: origin = (%lf, %lf) false origin = (%lf, %lf).\n",
			modname, cen_x, cen_y, x0, y0);
	projection_ll_to_en_deg(proj, cen_x, cen_y, &x0, &y0);
	printf("%s: proj: origin maps to (%lf, %lf) m.\n", modname, x0, y0);
}

void check_sheet(MapSheet sheet)
{
	f64	cen_x, cen_y, x0, y0;
	u32	width, height;
	
	mapsheet_get_center(sheet, &cen_x, &cen_y);
	printf("%s: sheet: center = (%lf, %lf) m.\n", modname, cen_x, cen_y);
	mapsheet_get_width_height(sheet, &width, &height);
	printf("%s: sheet: (w,h) = (%d, %d) pels.\n", modname, width, height);
	mapsheet_get_spacing(sheet, &x0, &y0);
	printf("%s: sheet: (de,dn) = (%lf, %lf) m.\n", modname, x0, y0);
	mapsheet_get_bl(sheet, &x0, &y0);
	printf("%s: sheet: llc = (%lf, %lf) m.\n", modname, x0, y0);
	mapsheet_get_br(sheet, &x0, &y0);
	printf("%s: sheet: lrc = (%lf, %lf) m.\n", modname, x0, y0);
	mapsheet_get_tr(sheet, &x0, &y0);
	printf("%s: sheet: urc = (%lf, %lf) m.\n", modname, x0, y0);
	mapsheet_get_tl(sheet, &x0, &y0);
	printf("%s: sheet: ulc = (%lf, %lf) m.\n", modname, x0, y0);
}

int main(int argc, char **argv)
{
	int		c;
	MapSheet		sheet;
	ParList			*parlist = NULL;
	Bool			params_reset = False;
	
	ccom_log_command_line(modname, argc, argv);

	opterr = 0;
	while ((c = getopt(argc, argv, "hf:")) != EOF) {
		switch(c) {
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
				params_reset = True;
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
	if ((sheet = mapsheet_load_sheet(argv[INPUT_FILE])) == NULL) {
		error_msgv(modname, "failed to load mapsheet from \"%s\".\n",
			argv[INPUT_FILE]);
		return(1);
	}
	/*
	if (params_reset)
		if (!mapsheet_reset_params(sheet)) {
			error_msg(modname, "error: failed to reset parameters to command"
				" line supplied file set.\n");
			return(1);
		}*/
	check_sheet(sheet);
	check_projection(mapsheet_get_projection(sheet));
	mapsheet_release(sheet);
	return(0);
}
