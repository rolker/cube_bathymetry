/* $Id: plotsheets.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.1.2.1  2002/12/15 02:17:27  brc
 * A utility to overlay the bounds of all of the MapSheets found in a directory onto
 * some image of the area, so that a cartoon (in the original sense) is formed to guide
 * users as to whether makesheets got it right or not (and/or whether the user screwed
 * it up when the bounds were being adjusted).
 *
 *
 * File:	plotsheets.c
 * Purpose:	Draw a cartoon of the extents of the sheets in a directory
 * Date:	16 September 2002
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>
#include <limits.h>
#include <float.h>
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
#include "mapsheet.h"
#include "error.h"

#define MAX_BUFFER_LENGTH	1024
#define BASE_OFFSET			0.10	/* Offset used between sheet outlines, in meters */

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

static char *modname = "plotsheets";
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
	IP_DIRECTORY = 1,
	OUTPUT_GDL,
	ARGC_EXPECTED
} Cmd;

static void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	printf("plotsheets V%s [%s] - Plot extents of MapSheets as GDLs.\n",
			p, __DATE__);
	printf("Syntax: plotsheets [opt] <ipdir><output>\n");
	printf(" Input directory -----------^      ^\n");
	printf(" Output GDL -----------------------'\n");
	free(p);
}

void append_mapsheet(MapSheet sheet, FILE *gdl, f64 x0, f64 y0, f64 z)
{
	f64	west, south, east, north;

	mapsheet_get_bl(sheet, &west, &south);
	mapsheet_get_tr(sheet, &east, &north);

	fprintf(gdl, "NEW 5\n");
	fprintf(gdl, "%.2lf %.2lf %.2lf\n", west - x0, south - y0, z);
	fprintf(gdl, "%.2lf %.2lf %.2lf\n", west - x0, north - y0, z);
	fprintf(gdl, "%.2lf %.2lf %.2lf\n", east - x0, north - y0, z);
	fprintf(gdl, "%.2lf %.2lf %.2lf\n", east - x0, south - y0, z);
	fprintf(gdl, "%.2lf %.2lf %.2lf\n", west - x0, south - y0, z);
}

char **ccom_enumerate_files(char *dirname, char *extension, u32 *n_found)
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

Bool write_gdl(char *name, MapSheet *sheet, u32 n_sheets)
{
	FILE	*gdl;
	u32		sht;
	f64		west, south, x, y;

	if ((gdl = fopen(name, "w")) == NULL) {
		error_msgv(modname, "error: failed to open \"%s\" for output GDL.\n",
			name);
		return(False);
	}
	fprintf(gdl, "BEGIN DATA_LINES 1.0\nN_RECORDS %d\nN_COLUMNS 3\nXYZ\nCOLOR CONSTANT CODE R\n",
		n_sheets);
	fprintf(gdl, "LINE ON\nTUBE OFF\nSIZE CONSTANT 2\nTEXT NONE\nDEGRADATION FACETS\n");

	west = DBL_MAX; south = DBL_MAX;
	for (sht = 0; sht < n_sheets; ++sht) {
		mapsheet_get_bl(sheet[sht], &x, &y);
		west = MIN(west, x); south = MIN(south, y);
		mapsheet_get_tr(sheet[sht], &x, &y);
		west = MIN(west, x); south = MIN(south, y);
	}
	fprintf(gdl, "LOCAL_ORIGIN %.2lf %.2lf 0.0\n", west, south);

	for (sht = 0; sht < n_sheets; ++sht)
		append_mapsheet(sheet[sht], gdl, west, south, BASE_OFFSET * sht);
	fclose(gdl);
	return(True);
}

int main(int argc, char **argv)
{
	int				c;
	char			**sdfs, **shts, buffer[MAX_BUFFER_LENGTH];
	u32				n_sdfs, n_shts, n_sheets, sht, n_loaded;
	MapSheet		*sheet;
	
	ccom_log_command_line(modname, argc, argv);

	opterr = 0;
	while ((c = getopt(argc, argv, "h")) != EOF) {
		switch(c) {
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
	if ((sdfs = ccom_enumerate_files(argv[IP_DIRECTORY], "sdf", &n_sdfs)) == NULL &&
		(shts = ccom_enumerate_files(argv[IP_DIRECTORY], "sht", &n_shts)) == NULL) {
		error_msgv(modname, "error: failed to enumerate any active MapSheets in \"%s\".\n",
			argv[IP_DIRECTORY]);
		return(1);
	}
	n_sheets = n_sdfs + n_shts;
	if ((sheet = (MapSheet*)malloc(sizeof(MapSheet)*n_sheets)) == NULL) {
		error_msgv(modname, "error: failed to get memory for %d MapSheet structures.\n", n_sheets);
		return(1);
	}
	n_loaded = 0;
	for (sht = 0; sht < n_sdfs; ++sht) {
		sprintf(buffer, "%s/%s", argv[IP_DIRECTORY], sdfs[sht]);
		if ((sheet[n_loaded] = mapsheet_new_from_ascii(buffer)) == NULL) {
			error_msgv(modname, "error: failed to load \"%s\" for ASCII MapSheet.\n", buffer);
			return(1);
		}
		++n_loaded;
	}
	for (sht = 0; sht < n_shts; ++sht) {
		sprintf(buffer, "%s/%s", argv[IP_DIRECTORY], shts[sht]);
		if ((sheet[n_loaded] = mapsheet_load_sheet(buffer)) == NULL) {
			error_msgv(modname, "error: failed to load \"%s\" for binary MapSheet.\n", buffer);
			return(1);
		}
		++n_loaded;
	}
	if (n_loaded < n_sheets)
		error_msgv(modname, "warning: found %d sheets by scan, but only loaded %d.\n",
			n_sheets, n_loaded);

	if (!write_gdl(argv[OUTPUT_GDL], sheet, n_loaded)) {
		error_msgv(modname, "error: failed to write GDL to \"%s\".\n", argv[OUTPUT_GDL]);
		return(1);
	}
	
	for (sht = 0; sht < n_loaded; ++sht)
		mapsheet_release(sheet[sht]);

	return(0);
}
