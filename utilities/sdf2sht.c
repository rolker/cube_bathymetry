/*
 * $Id: sdf2sht.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.1.2.2  2002/12/15 01:58:54  brc
 * Added facility to use parameter files in initialisation.
 *
 * Revision 1.1.2.1  2002/07/14 02:20:48  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.1  2002/07/13 23:43:33  brc
 * This is an alternative to initsheet, in the sense that it will make binary
 * format MapSheets from .sdf descriptions, but it doesn't make any initialisation
 * requests and hence can be used in real-time mode survey operations where the
 * data is being assimilated as soon as it is collected.  This code is not
 * quite as flexible as initsheet, and in particular will only use HyperCUBE
 * for the depth surface that gets added.
 *
 *
 * File:	sdf2sht.c
 * Purpose:	Make a binary (blank) mapsheet from an ASCII sdf.
 * Date:	23 June 2002
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2002.
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
#include "error.h"
#include "mapsheet.h"

static char *modname = "sdf2sht";
static char *modrev = "$Revision: 20 $";

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
	SDF_FILE = 1,
	SHT_FILE,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("sdf2sht V%s [%s] - Make a binary (blank) mapsheet from an ASCII sdf.\n",
			p, __DATE__);
	printf("Syntax: sdf2sht [opt] <sdf><sht>\n");
	printf(" Input ASCII sdf file --^    ^\n");
	printf(" Output MapSheet directory --'\n");
	printf(" Options:\n");
	printf("  -i order    Set IHO survey order for estimation accuracy "
						  "(default: %d).\n", 1+(u32)DEFAULT_IHO_SURVEY_ORDER);
	printf("  -f params   Specify parameters file for library.\n");
	free(p);
}

int main(int argc, char **argv)
{
	int			c;
	MapSheet	sheet;
	ErrModIHOOrder	order = DEFAULT_IHO_SURVEY_ORDER;
	ParList		*parlist = NULL;

	ccom_log_command_line(modname, argc, argv);

	opterr = 0;
	while ((c = getopt(argc, argv, "hi:f:")) != EOF) {
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
	
	if ((sheet = mapsheet_new_from_ascii(argv[SDF_FILE])) == NULL) {
		error_msgv(modname, "error: can't load mapsheet from SDF file \"%s\".\n",
			argv[SDF_FILE]);
		return(1);
	}
	if (!mapsheet_add_depth_surface(sheet, MAP_DEPTH_HYPERCUBE, order)) {
		error_msgv(modname, "error: failed to add HyperCUBE layer.\n");
		return(1);
	}
	if (!mapsheet_save_sheet(sheet, argv[SHT_FILE])) {
		error_msgv(modname, "error: failed to save mapsheet in binary"
			" format to \"%s\".\n", argv[SHT_FILE]);
		return(1);
	}
	mapsheet_release(sheet);
	
	return(0);
}
