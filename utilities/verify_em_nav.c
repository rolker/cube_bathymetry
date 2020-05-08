/*
 * $Id: verify_em_nav.c 20 2005-06-22 15:20:16Z brc $
 * $Log$
 * Revision 1.1  2005/06/22 15:19:54  brc
 * Added to complete the contents of the repository.
 *
 * Revision 1.2  2002/12/15 02:04:24  brc
 * Removed endian-ness checks, since they are now not required.  Added
 * command-line loging command.
 *
 * Revision 1.1  2002/07/14 02:20:48  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 *
 * File:	verify_em_nav.c
 * Purpose:	Read Simrad EM1000 series (a.k.a. Simrad 91) datagrams and verify
 *			that the navigation packets make sense, and remove those that don't
 *			so that the file can be read into CARIS without it failing.
 * Date:	28 June 2002
 * Note:	This code was written to patch round a problem found during the
 *			De Soto Canyon 2002 USGS Mapping Cruise.  The EM1000 on board was
 *			having occasional navigation dropouts which manifested as poor
 *			packets (with corrupted timestamp).  These caused CARIS/HIPS to
 *			believe that time had gone backwards, and hence that the navigation
 *			for the remainder of the line could not be interpolated and/or used.
 *			The up-shot is that many chunks of data were lost due to one poor
 *			navigation packet.  Of course, this is a pretty poor show on the
 *			part of CARIS's reader software, but that doesn't help in getting
 *			at the data on-board.  Sigh.
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
#include <limits.h>
#include <float.h>
#include <time.h>
#include "libbfr.h"
#include "error.h"
#include "ccom_general.h"

#define ALLOW_SIMRAD91
#include "simrad91.h"

#define DEFAULT_PRINTLEVEL		BFR_PRINT_IDS
#define DEFAULT_MAXRESYNCH		1000
#define MAX_EXPECTED_SAMPLES	10000	/* Maximum number of samples to be
										 * expects in one channel of SS
										 */

static char *modname = "verify_em_nav";
static char *modrev = "$Revision: 20 $";

u8	*buffer = NULL;	/* Buffer for copy data from ip to op */
u32	buf_len = 0;	/* Size of buffer currently allocated */

static char *get_rcs_rev(const char *rev)
{
	char *p = strchr(rev, ' ');
	
	if (p != NULL) {
		char *b = strdup(++p);
		if ((p = b) != NULL && (b = strchr(p, ' ')) != NULL) *b = '\0';
	} else p = strdup("1.0");
	return(p);
}

#define SIMRAD_STX	2		/* recognition flag in simrad headers */

/* Routine:	validate_simrad9`
 * Purpose: Check validity of Simrad-91 EM format header block
 * Inputs:	*hdr	Pointer to header block
 *			maxlen	Maximum expected length for any header block
 * Outputs:	True if block looks OK, False otherwise
 * Comment:	This checks that the STX byte is right, and that the declared size
 *			of block is reasonable.  Believe it or not, the '91 version of the
 *			datagrams has big endian sizes and ASCII data, but little endian
 *			binary data!  Consequently, we swap the data size here to fool the
 *			reader afterwards.
 */

Bool validate_simrad91(em91hdr_t *d, int maxlen)
{
	u8		buffer[4], swap;

	if (d->stx != SIMRAD_STX) {
		fprintf(stderr, "%s: did not recognise STX byte (%d)\n", modname,
			(int)d->stx);
		return(False);
	}

	/* Swap the declared data size, since this is big endian ... */
	*((u32*)buffer) = d->datasize;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	d->datasize = *((u32*)buffer);

	if (d->datasize > (u32)maxlen) {
		fprintf(stderr, "%s: maximum size bigger than expected.\n", modname);
		return(False);
	}
	return(True);
}

typedef enum {
	INPUT_RAW = 1,
	OUTPUT_RAW,
	ARGC_EXPECTED
} Cmd;

void Syntax(void)
{
	char	*p = get_rcs_rev(modrev);
	
	printf("verify_em_nav V%s [%s] - Verify EM91 position datagrams, removing"
		" bad packets.\n", p, __DATE__);
	printf("Syntax: verify_em_nav [opt] <input><output>\n");
	printf(" Input raw EM91 datagrams -----^      ^\n");
	printf(" Output repaired EM91 datagrams ------'\n");
	printf(" Options:\n");
	printf("  -r n          Set number of allowable resynch events before\n");
	printf("                rejecting line as bogus (default: %d).\n",
								DEFAULT_MAXRESYNCH);
	printf("  -d            Turn on debugging mode (lots of information).\n");
}

/* Routine:	copy_block
 * Purpose:	Copies bytes_read bytes from ip->op
 * Inputs:	bytes_read	Number of bytes just read and validated for copy
 *			*ip			Source file
 *			*op			Destination file
 * Outputs:	True if copy worked, otherwise False
 * Comment:	Note that this routine assumes that _ip_ is at the end of the block
 *			of _bytes_read_ bytes which are to be copied to the output file;
 *			_ip_ is rewound, read into buffer, and then copied into output to
 *			ensure integrity of data without reinterpretation in this code.
 *			This leaves the _ip_ file in the state it was before the call, and
 *			advances the _op_ file by the new data.  This routine also depends
 *			on the _buffer_ and _buf_len_ globals, and may reallocate _buffer_
 *			to expand it to the correct size.  Note that this is always upwards,
 *			until demands for bigger blocks stop ...
 */

Bool copy_block(u32 bytes_read, FILE *ip, FILE *op)
{
	if (buf_len < bytes_read) {
		free(buffer);
		if ((buffer = (u8*)malloc(bytes_read)) == NULL) {
			bfrFiltText(stderr, BFR_FILT_CRIT, "%s: fatal: failed resizing "
				"input buffer to %d bytes.\n", modname, bytes_read);
			buf_len = 0;
			return(False);
		}
		buf_len = bytes_read;
	}
	fseek(ip, -(s32)bytes_read, SEEK_CUR);
	fread(buffer, 1, bytes_read, ip);
	fwrite(buffer, 1, bytes_read, op);
	return(True);
}

Bool validate_nav(simrad91_t *data)
{
	if (strchr(data->datagram.position.time, '.') != NULL)
		return(False);
	return(True);
}

int main(int argc, char **argv)
{
	int				c;
	simrad91_t		data;
	FILE			*ip, *op;
	BFRtn			rc;
	Bool			abort = False, handled;
	int				resynch_event = 0, max_resynchs = DEFAULT_MAXRESYNCH;
	u32				n_read, ndgrams = 0, nculled = 0;
	long			position;

	ccom_log_command_line(modname, argc, argv);

	bfrSetFiltLvl(BFR_FILT_CRIT);
	opterr = 0;
	while ((c = getopt(argc, argv, "hr:d")) != EOF) {
		switch(c) {
			case 'd':
				bfrSetFiltLvl(BFR_FILT_DEBUG);	/* Print everything! */
				break;
			case 'r':
				max_resynchs = atoi(optarg);
				if (max_resynchs < 0) {
					fprintf(stderr, "%s: maximum resynchs must be positive.\n",
						modname);
					return(1);
				}
				break;
			case '?':
				fprintf(stderr, "%s: flag '%c' unknown.\n", modname, optopt);
			case 'h':
			default:
				Syntax();
				return(1);
		}
	}
	argc -= optind-1; argv += optind-1;
	if (argc != ARGC_EXPECTED) {
		Syntax();
		return(1);
	}
	if ((ip = fopen(argv[INPUT_RAW], "rb")) == NULL) {
		fprintf(stderr, "%s: failed to open \"%s\" for input.\n",
			modname, argv[INPUT_RAW]);
		return(1);
	}
	if ((op = fopen(argv[OUTPUT_RAW], "wb")) == NULL) {
		fprintf(stderr, "%s: failed to open \"%s\" for output.\n",
			modname, argv[OUTPUT_RAW]);
		return(1);
	}
	
	memset(&data, 0, sizeof(simrad91_t));
	position = ftell(ip);
	while (!abort && resynch_event < max_resynchs &&
			(rc = read_simrad91(ip, &data, &n_read)) != BFR_FILE_END) {
		switch(rc) {
			case BFR_DGRAM_OK:
				handled = False;
					/* Mark that datagram needs to be checked. */
				if (data.id == SIMRAD91_POSITION && !validate_nav(&data)) {
					handled = True;
					nculled++;
				}
				if (!handled) {
					/* Move to the declared length of the datagram.  The EM91
					 * datagrams are all of fixed length, but the code can't
					 * readily determine what that is at the start.  Therefore,
					 * we check the number of bytes read, and correct to the
					 * declared length (minus the length word that isn't included
					 * in the length) if required.
					 */
					if (n_read != data.header.datasize+4)
						fseek(ip, data.header.datasize+4-n_read, SEEK_CUR);

					/* Copy across the block, adding 4 bytes for the length
					 * word that the Simrad telegrams don't include in their
					 * own length, but which C & C add to make reading easier.
					 */
					copy_block(data.header.datasize+4, ip, op);
				}
				++ndgrams;
				break;
			case BFR_DGRAM_UNKNOWN:
				error_msgv(modname, "error: unknown datagram (type %d).\n",
					data.id);
				exit(1);
				break;
			case BFR_NO_HEADER:
			case BFR_DGRAM_READ_FAIL:
			case BFR_INVALID_TAIL:
				fprintf(stderr, "%s: error seeking header at offset %ld.\n",
					modname, position);
				fprintf(stderr, "%s: attempting resynch %d.\n", modname,
					resynch_event);
				++resynch_event;
				break;
			case BFR_READ_ERROR:
				fprintf(stderr, "%s: unexpected hard read error.  Aborting.\n",
					modname);
				abort = True;
				break;
			default:
				fprintf(stderr, "%s: internal error: unknown read return.  "
					"Aborting.\n", modname);
				abort = True;
				break;
		}
		clean_simrad91(&data);
		memset(&data, 0, sizeof(simrad91_t));
		position = ftell(ip);
		printf("\r%s: %d datagrams read, %d nav points culled.", modname, ndgrams, nculled);
		fflush(stdout);
	}
	fclose(ip);
	fclose(op);
	printf("\n");
	if (abort)
		return(1);
	else
		return(0);
}
