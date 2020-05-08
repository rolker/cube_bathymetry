/* Generated automatically by bfreader V1.10 [Jun 28 2002] at 1104 Eastern Daylight Time on 05 Jul 2002 */

/* $Id: simrad.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

static char *tabs[11] = {
	"", "\t", "\t\t", "\t\t\t", "\t\t\t\t", "\t\t\t\t\t",
	"\t\t\t\t\t\t", "\t\t\t\t\t\t\t", "\t\t\t\t\t\t\t\t",
	"\t\t\t\t\t\t\t\t\t", "\t\t\t\t\t\t\t\t\t\t"
};

#include "simrad.h"

#define __SWAP_BYTES__

static char *modname = "simrad";

static Bool read_emhdr(FILE *f, emhdr_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->datasize = *((u32*)buffer);
#else
	if (fread(&(data->datasize), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
	if (fread(&(data->stx), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->type), 1, 1, f) != 1) return(False);
	bytes_read++;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->modelnum = *((u16*)buffer);
#else
	if(fread(&(data->modelnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->date = *((u32*)buffer);
#else
	if (fread(&(data->date), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->time = *((u32*)buffer);
#else
	if (fread(&(data->time), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
	*tot_read += bytes_read;
	return(True);
}

static void print_emhdr(FILE *f, BfrPrintLevel level, emhdr_t *data, int indent)
{
	fprintf(f, "%semhdr {\n", tabs[indent]);
	fprintf(f, "%sdatasize = %u\n", tabs[indent+1], data->datasize);
	fprintf(f, "%sstx = %u\n", tabs[indent+1], (u32)data->stx);
	fprintf(f, "%stype = %u\n", tabs[indent+1], (u32)data->type);
	fprintf(f, "%smodelnum = %hu\n", tabs[indent+1], data->modelnum);
	fprintf(f, "%sdate = %u\n", tabs[indent+1], data->date);
	fprintf(f, "%stime = %u\n", tabs[indent+1], data->time);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emtail(FILE *f, emtail_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	if (fread(&(data->etx), 1, 1, f) != 1) return(False);
	bytes_read++;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->checksum = *((u16*)buffer);
#else
	if(fread(&(data->checksum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	*tot_read += bytes_read;
	return(True);
}

static void print_emtail(FILE *f, BfrPrintLevel level, emtail_t *data, int indent)
{
	fprintf(f, "%semtail {\n", tabs[indent]);
	fprintf(f, "%setx = %u\n", tabs[indent+1], (u32)data->etx);
	fprintf(f, "%schecksum = %hu\n", tabs[indent+1], data->checksum);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_eminstparam(eminstparam_t *data)
{
	free(data->params);
}

static Bool read_eminstparam(FILE *f, eminstparam_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->linenum = *((u16*)buffer);
#else
	if(fread(&(data->linenum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum2 = *((u16*)buffer);
#else
	if(fread(&(data->serialnum2), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if ((data->params = ReadTerminated(f, 0, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for eminstparam.params\n", modname);
		clean_eminstparam(data);
		return(False);
	}
	bytes_read += padn(f, bytes_read, 1);
	*tot_read += bytes_read;
	return(True);
}

static void print_eminstparam(FILE *f, BfrPrintLevel level, eminstparam_t *data, int indent)
{
	fprintf(f, "%seminstparam {\n", tabs[indent]);
	fprintf(f, "%slinenum = %hu\n", tabs[indent+1], data->linenum);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%sserialnum2 = %hu\n", tabs[indent+1], data->serialnum2);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->params, "params", f, indent+1);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emruntime(FILE *f, emruntime_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->status = *((u32*)buffer);
#else
	if (fread(&(data->status), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
	if (fread(&(data->mode), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->filters), 1, 1, f) != 1) return(False);
	bytes_read++;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->mindepth = *((u16*)buffer);
#else
	if(fread(&(data->mindepth), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->maxdepth = *((u16*)buffer);
#else
	if(fread(&(data->maxdepth), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->absorbcoef = *((u16*)buffer);
#else
	if(fread(&(data->absorbcoef), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->tx_pulselen = *((u16*)buffer);
#else
	if(fread(&(data->tx_pulselen), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->tx_beamw = *((u16*)buffer);
#else
	if(fread(&(data->tx_beamw), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if (fread(&(data->tx_power), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->rx_beamw), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->rx_bandw), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->rx_gain), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->TVG_xover), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->ss_source), 1, 1, f) != 1) return(False);
	bytes_read++;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->maxwidth = *((u16*)buffer);
#else
	if(fread(&(data->maxwidth), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if (fread(&(data->beamspacing), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->coveragesector), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->yaw_pitch_mode), 1, 1, f) != 1) return(False);
	bytes_read++;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->unused1 = *((u16*)buffer);
#else
	if(fread(&(data->unused1), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->unused2 = *((u32*)buffer);
#else
	if (fread(&(data->unused2), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
	*tot_read += bytes_read;
	return(True);
}

static void print_emruntime(FILE *f, BfrPrintLevel level, emruntime_t *data, int indent)
{
	fprintf(f, "%semruntime {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%sstatus = %u\n", tabs[indent+1], data->status);
	fprintf(f, "%smode = %u\n", tabs[indent+1], (u32)data->mode);
	fprintf(f, "%sfilters = %u\n", tabs[indent+1], (u32)data->filters);
	fprintf(f, "%smindepth = %hu\n", tabs[indent+1], data->mindepth);
	fprintf(f, "%smaxdepth = %hu\n", tabs[indent+1], data->maxdepth);
	fprintf(f, "%sabsorbcoef = %hu\n", tabs[indent+1], data->absorbcoef);
	fprintf(f, "%stx_pulselen = %hu\n", tabs[indent+1], data->tx_pulselen);
	fprintf(f, "%stx_beamw = %hu\n", tabs[indent+1], data->tx_beamw);
	fprintf(f, "%stx_power = %u\n", tabs[indent+1], (u32)data->tx_power);
	fprintf(f, "%srx_beamw = %u\n", tabs[indent+1], (u32)data->rx_beamw);
	fprintf(f, "%srx_bandw = %u\n", tabs[indent+1], (u32)data->rx_bandw);
	fprintf(f, "%srx_gain = %u\n", tabs[indent+1], (u32)data->rx_gain);
	fprintf(f, "%sTVG_xover = %u\n", tabs[indent+1], (u32)data->TVG_xover);
	fprintf(f, "%sss_source = %u\n", tabs[indent+1], (u32)data->ss_source);
	fprintf(f, "%smaxwidth = %hu\n", tabs[indent+1], data->maxwidth);
	fprintf(f, "%sbeamspacing = %u\n", tabs[indent+1], (u32)data->beamspacing);
	fprintf(f, "%scoveragesector = %u\n", tabs[indent+1], (u32)data->coveragesector);
	fprintf(f, "%syaw_pitch_mode = %u\n", tabs[indent+1], (u32)data->yaw_pitch_mode);
	fprintf(f, "%sunused1 = %hu\n", tabs[indent+1], data->unused1);
	fprintf(f, "%sunused2 = %u\n", tabs[indent+1], data->unused2);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emsbeam(FILE *f, emsbeam_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->date = *((u32*)buffer);
#else
	if (fread(&(data->date), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->time = *((u32*)buffer);
#else
	if (fread(&(data->time), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->depth = *((u32*)buffer);
#else
	if (fread(&(data->depth), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
	if (fread(&(data->source), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emsbeam(FILE *f, BfrPrintLevel level, emsbeam_t *data, int indent)
{
	fprintf(f, "%semsbeam {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%sdate = %u\n", tabs[indent+1], data->date);
	fprintf(f, "%stime = %u\n", tabs[indent+1], data->time);
	fprintf(f, "%sdepth = %u\n", tabs[indent+1], data->depth);
	fprintf(f, "%ssource = %u\n", tabs[indent+1], (u32)data->source);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emisvpr(emisvpr_t *data)
{
	free(data->depth);
	free(data->speed);
	free(data->temp);
	free(data->salinity);
	free(data->absorption);
}

static Bool read_emisvpr(FILE *f, emisvpr_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	if ((data->depth = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvpr.depth\n", modname);
		clean_emisvpr(data);
		return(False);
	}
	if ((data->speed = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvpr.speed\n", modname);
		clean_emisvpr(data);
		return(False);
	}
	if ((data->temp = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvpr.temp\n", modname);
		clean_emisvpr(data);
		return(False);
	}
	if ((data->salinity = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvpr.salinity\n", modname);
		clean_emisvpr(data);
		return(False);
	}
	if ((data->absorption = ReadTerminated(f, 10, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvpr.absorption\n", modname);
		clean_emisvpr(data);
		return(False);
	}
	if (fread(&(data->unused), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emisvpr(FILE *f, BfrPrintLevel level, emisvpr_t *data, int indent)
{
	fprintf(f, "%semisvpr {\n", tabs[indent]);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->depth, "depth", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->speed, "speed", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->temp, "temp", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->salinity, "salinity", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->absorption, "absorption", f, indent+1);
	fprintf(f, "%sunused = %u\n", tabs[indent+1], (u32)data->unused);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emisvp(emisvp_t *data)
{
	free(data->formater);
	free(data->profileid);
	free(data->numentries);
	free(data->time);
	free(data->day);
	free(data->month);
	free(data->year);
	free(data->isvp);
	free(data->lattitude);
	free(data->lat_hemisp);
	free(data->longitude);
	free(data->lon_hemisp);
	free(data->atmos_pres);
	free(data->comment);
}

static Bool read_emisvp(FILE *f, emisvp_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	int		i;
	int		dynamic1;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if ((data->formater = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.formater\n", modname);
		clean_emisvp(data);
		return(False);
	}
	if ((data->profileid = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.profileid\n", modname);
		clean_emisvp(data);
		return(False);
	}
	if ((data->numentries = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.numentries\n", modname);
		clean_emisvp(data);
		return(False);
	}
	if ((data->time = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.time\n", modname);
		clean_emisvp(data);
		return(False);
	}
	if ((data->day = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.day\n", modname);
		clean_emisvp(data);
		return(False);
	}
	if ((data->month = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.month\n", modname);
		clean_emisvp(data);
		return(False);
	}
	if ((data->year = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.year\n", modname);
		clean_emisvp(data);
		return(False);
	}
	dynamic1 = atoi((char*)data->numentries);
	if ((data->isvp = (emisvpr_t*)calloc(dynamic1, sizeof(emisvpr_t))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emisvp.isvp\n", modname);
		clean_emisvp(data);
		return(False);
	}
	for (i = 0; i < dynamic1; ++i) {
		if (!read_emisvpr(f, data->isvp+i, &bytes_read)) {
			bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading emisvp.isvp[%d].\n", modname, i);
			clean_emisvp(data);
			return(False);
		}
	}
	if ((data->lattitude = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.lattitude\n", modname);
		clean_emisvp(data);
		return(False);
	}
	if ((data->lat_hemisp = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.lat_hemisp\n", modname);
		clean_emisvp(data);
		return(False);
	}
	if ((data->longitude = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.longitude\n", modname);
		clean_emisvp(data);
		return(False);
	}
	if ((data->lon_hemisp = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.lon_hemisp\n", modname);
		clean_emisvp(data);
		return(False);
	}
	if ((data->atmos_pres = ReadTerminated(f, 44, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.atmos_pres\n", modname);
		clean_emisvp(data);
		return(False);
	}
	if ((data->comment = ReadTerminated(f, 0, &bytes_read)) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read terminated string for emisvp.comment\n", modname);
		clean_emisvp(data);
		return(False);
	}
	*tot_read += bytes_read;
	return(True);
}

static void print_emisvp(FILE *f, BfrPrintLevel level, emisvp_t *data, int indent)
{
	int	i, endstop;

	int		dynamic1;
	fprintf(f, "%semisvp {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->formater, "formater", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->profileid, "profileid", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->numentries, "numentries", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->time, "time", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->day, "day", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->month, "month", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->year, "year", f, indent+1);
	if (level >= BFR_PRINT_ARRAYSTART) {
		fprintf(f, "%sisvp = {", tabs[indent+1]);
	dynamic1 = atoi((char*)data->numentries);
		if (level >= BFR_PRINT_ALL) endstop = dynamic1; else endstop = BFR_PRINT_ARRAYLEN;
	endstop = (endstop > dynamic1) ? dynamic1 : endstop;
		fprintf(f, "\n");
		print_emisvpr(f, level, data->isvp, indent+2);
		for (i = 1; i < endstop; ++i)
			print_emisvpr(f, level, data->isvp + i, indent+2);
		if (level < BFR_PRINT_ALL)
			fprintf(f, "%s...\n%s}\n", tabs[indent+2], tabs[indent+1]);
	}
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->lattitude, "lattitude", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->lat_hemisp, "lat_hemisp", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->longitude, "longitude", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->lon_hemisp, "lon_hemisp", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->atmos_pres, "atmos_pres", f, indent+1);
	if (level >= BFR_PRINT_TERMARRAYS)
		PrintTerminated(data->comment, "comment", f, indent+1);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emnewsvpr(FILE *f, emnewsvpr_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->depth = *((u32*)buffer);
#else
	if (fread(&(data->depth), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->soundspeed = *((u32*)buffer);
#else
	if (fread(&(data->soundspeed), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
	*tot_read += bytes_read;
	return(True);
}

static void print_emnewsvpr(FILE *f, BfrPrintLevel level, emnewsvpr_t *data, int indent)
{
	fprintf(f, "%semnewsvpr {\n", tabs[indent]);
	fprintf(f, "%sdepth = %u\n", tabs[indent+1], data->depth);
	fprintf(f, "%ssoundspeed = %u\n", tabs[indent+1], data->soundspeed);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emnewsvp(emnewsvp_t *data)
{
	free(data->svp);
}

static Bool read_emnewsvp(FILE *f, emnewsvp_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	int		i;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->date = *((u32*)buffer);
#else
	if (fread(&(data->date), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->time = *((u32*)buffer);
#else
	if (fread(&(data->time), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->numentries = *((u16*)buffer);
#else
	if(fread(&(data->numentries), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->depthres = *((u16*)buffer);
#else
	if(fread(&(data->depthres), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if ((data->svp = (emnewsvpr_t*)calloc(data->numentries, sizeof(emnewsvpr_t))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emnewsvp.svp\n", modname);
		clean_emnewsvp(data);
		return(False);
	}
	for (i = 0; i < data->numentries; ++i) {
		if (!read_emnewsvpr(f, data->svp+i, &bytes_read)) {
			bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading emnewsvp.svp[%d].\n", modname, i);
			clean_emnewsvp(data);
			return(False);
		}
	}
	if (fread(&(data->unused), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emnewsvp(FILE *f, BfrPrintLevel level, emnewsvp_t *data, int indent)
{
	int	i, endstop;

	fprintf(f, "%semnewsvp {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%sdate = %u\n", tabs[indent+1], data->date);
	fprintf(f, "%stime = %u\n", tabs[indent+1], data->time);
	fprintf(f, "%snumentries = %hu\n", tabs[indent+1], data->numentries);
	fprintf(f, "%sdepthres = %hu\n", tabs[indent+1], data->depthres);
	if (level >= BFR_PRINT_ARRAYSTART) {
		fprintf(f, "%ssvp = {", tabs[indent+1]);
		if (level >= BFR_PRINT_ALL) endstop = data->numentries; else endstop = BFR_PRINT_ARRAYLEN;
	endstop = (endstop > data->numentries) ? data->numentries : endstop;
		fprintf(f, "\n");
		print_emnewsvpr(f, level, data->svp, indent+2);
		for (i = 1; i < endstop; ++i)
			print_emnewsvpr(f, level, data->svp + i, indent+2);
		if (level < BFR_PRINT_ALL)
			fprintf(f, "%s...\n%s}\n", tabs[indent+2], tabs[indent+1]);
	}
	fprintf(f, "%sunused = %u\n", tabs[indent+1], (u32)data->unused);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emsvpr(FILE *f, emsvpr_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->depth = *((u16*)buffer);
#else
	if(fread(&(data->depth), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->soundspeed = *((u16*)buffer);
#else
	if(fread(&(data->soundspeed), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	*tot_read += bytes_read;
	return(True);
}

static void print_emsvpr(FILE *f, BfrPrintLevel level, emsvpr_t *data, int indent)
{
	fprintf(f, "%semsvpr {\n", tabs[indent]);
	fprintf(f, "%sdepth = %hu\n", tabs[indent+1], data->depth);
	fprintf(f, "%ssoundspeed = %hu\n", tabs[indent+1], data->soundspeed);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emsvp(emsvp_t *data)
{
	free(data->svp);
}

static Bool read_emsvp(FILE *f, emsvp_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	int		i;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->date = *((u32*)buffer);
#else
	if (fread(&(data->date), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->time = *((u32*)buffer);
#else
	if (fread(&(data->time), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->numentries = *((u16*)buffer);
#else
	if(fread(&(data->numentries), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->depthres = *((u16*)buffer);
#else
	if(fread(&(data->depthres), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if ((data->svp = (emsvpr_t*)calloc(data->numentries, sizeof(emsvpr_t))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emsvp.svp\n", modname);
		clean_emsvp(data);
		return(False);
	}
	for (i = 0; i < data->numentries; ++i) {
		if (!read_emsvpr(f, data->svp+i, &bytes_read)) {
			bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading emsvp.svp[%d].\n", modname, i);
			clean_emsvp(data);
			return(False);
		}
	}
	if (fread(&(data->unused), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emsvp(FILE *f, BfrPrintLevel level, emsvp_t *data, int indent)
{
	int	i, endstop;

	fprintf(f, "%semsvp {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%sdate = %u\n", tabs[indent+1], data->date);
	fprintf(f, "%stime = %u\n", tabs[indent+1], data->time);
	fprintf(f, "%snumentries = %hu\n", tabs[indent+1], data->numentries);
	fprintf(f, "%sdepthres = %hu\n", tabs[indent+1], data->depthres);
	if (level >= BFR_PRINT_ARRAYSTART) {
		fprintf(f, "%ssvp = {", tabs[indent+1]);
		if (level >= BFR_PRINT_ALL) endstop = data->numentries; else endstop = BFR_PRINT_ARRAYLEN;
	endstop = (endstop > data->numentries) ? data->numentries : endstop;
		fprintf(f, "\n");
		print_emsvpr(f, level, data->svp, indent+2);
		for (i = 1; i < endstop; ++i)
			print_emsvpr(f, level, data->svp + i, indent+2);
		if (level < BFR_PRINT_ALL)
			fprintf(f, "%s...\n%s}\n", tabs[indent+2], tabs[indent+1]);
	}
	fprintf(f, "%sunused = %u\n", tabs[indent+1], (u32)data->unused);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emsss(FILE *f, emsss_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->latency = *((u16*)buffer);
#else
	if(fread(&(data->latency), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->soundspeed = *((u16*)buffer);
#else
	if(fread(&(data->soundspeed), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	*tot_read += bytes_read;
	return(True);
}

static void print_emsss(FILE *f, BfrPrintLevel level, emsss_t *data, int indent)
{
	fprintf(f, "%semsss {\n", tabs[indent]);
	fprintf(f, "%slatency = %hu\n", tabs[indent+1], data->latency);
	fprintf(f, "%ssoundspeed = %hu\n", tabs[indent+1], data->soundspeed);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emsurfacess(emsurfacess_t *data)
{
	free(data->soundspeed);
}

static Bool read_emsurfacess(FILE *f, emsurfacess_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	int		i;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->numentries = *((u16*)buffer);
#else
	if(fread(&(data->numentries), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if ((data->soundspeed = (emsss_t*)calloc(data->numentries, sizeof(emsss_t))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emsurfacess.soundspeed\n", modname);
		clean_emsurfacess(data);
		return(False);
	}
	for (i = 0; i < data->numentries; ++i) {
		if (!read_emsss(f, data->soundspeed+i, &bytes_read)) {
			bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading emsurfacess.soundspeed[%d].\n", modname, i);
			clean_emsurfacess(data);
			return(False);
		}
	}
	if (fread(&(data->unused), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emsurfacess(FILE *f, BfrPrintLevel level, emsurfacess_t *data, int indent)
{
	int	i, endstop;

	fprintf(f, "%semsurfacess {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%snumentries = %hu\n", tabs[indent+1], data->numentries);
	if (level >= BFR_PRINT_ARRAYSTART) {
		fprintf(f, "%ssoundspeed = {", tabs[indent+1]);
		if (level >= BFR_PRINT_ALL) endstop = data->numentries; else endstop = BFR_PRINT_ARRAYLEN;
	endstop = (endstop > data->numentries) ? data->numentries : endstop;
		fprintf(f, "\n");
		print_emsss(f, level, data->soundspeed, indent+2);
		for (i = 1; i < endstop; ++i)
			print_emsss(f, level, data->soundspeed + i, indent+2);
		if (level < BFR_PRINT_ALL)
			fprintf(f, "%s...\n%s}\n", tabs[indent+2], tabs[indent+1]);
	}
	fprintf(f, "%sunused = %u\n", tabs[indent+1], (u32)data->unused);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emmtr(FILE *f, emmtr_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->latency = *((u16*)buffer);
#else
	if(fread(&(data->latency), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->mechtilt = *((s16*)buffer);
#else
	if(fread(&(data->mechtilt), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	*tot_read += bytes_read;
	return(True);
}

static void print_emmtr(FILE *f, BfrPrintLevel level, emmtr_t *data, int indent)
{
	fprintf(f, "%semmtr {\n", tabs[indent]);
	fprintf(f, "%slatency = %hu\n", tabs[indent+1], data->latency);
	fprintf(f, "%smechtilt = %hd\n", tabs[indent+1], data->mechtilt);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emmechtilt(emmechtilt_t *data)
{
	free(data->mechtilt);
}

static Bool read_emmechtilt(FILE *f, emmechtilt_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	int		i;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->numentries = *((u16*)buffer);
#else
	if(fread(&(data->numentries), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if ((data->mechtilt = (emmtr_t*)calloc(data->numentries, sizeof(emmtr_t))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emmechtilt.mechtilt\n", modname);
		clean_emmechtilt(data);
		return(False);
	}
	for (i = 0; i < data->numentries; ++i) {
		if (!read_emmtr(f, data->mechtilt+i, &bytes_read)) {
			bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading emmechtilt.mechtilt[%d].\n", modname, i);
			clean_emmechtilt(data);
			return(False);
		}
	}
	if (fread(&(data->unused), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emmechtilt(FILE *f, BfrPrintLevel level, emmechtilt_t *data, int indent)
{
	int	i, endstop;

	fprintf(f, "%semmechtilt {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%snumentries = %hu\n", tabs[indent+1], data->numentries);
	if (level >= BFR_PRINT_ARRAYSTART) {
		fprintf(f, "%smechtilt = {", tabs[indent+1]);
		if (level >= BFR_PRINT_ALL) endstop = data->numentries; else endstop = BFR_PRINT_ARRAYLEN;
	endstop = (endstop > data->numentries) ? data->numentries : endstop;
		fprintf(f, "\n");
		print_emmtr(f, level, data->mechtilt, indent+2);
		for (i = 1; i < endstop; ++i)
			print_emmtr(f, level, data->mechtilt + i, indent+2);
		if (level < BFR_PRINT_ALL)
			fprintf(f, "%s...\n%s}\n", tabs[indent+2], tabs[indent+1]);
	}
	fprintf(f, "%sunused = %u\n", tabs[indent+1], (u32)data->unused);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emclock(FILE *f, emclock_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->date = *((u32*)buffer);
#else
	if (fread(&(data->date), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->time = *((u32*)buffer);
#else
	if (fread(&(data->time), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
	if (fread(&(data->pps), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emclock(FILE *f, BfrPrintLevel level, emclock_t *data, int indent)
{
	fprintf(f, "%semclock {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%sdate = %u\n", tabs[indent+1], data->date);
	fprintf(f, "%stime = %u\n", tabs[indent+1], data->time);
	fprintf(f, "%spps = %u\n", tabs[indent+1], (u32)data->pps);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emtide(FILE *f, emtide_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->date = *((u32*)buffer);
#else
	if (fread(&(data->date), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->time = *((u32*)buffer);
#else
	if (fread(&(data->time), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->tideoffset = *((s16*)buffer);
#else
	if(fread(&(data->tideoffset), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if (fread(&(data->unused), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emtide(FILE *f, BfrPrintLevel level, emtide_t *data, int indent)
{
	fprintf(f, "%semtide {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%sdate = %u\n", tabs[indent+1], data->date);
	fprintf(f, "%stime = %u\n", tabs[indent+1], data->time);
	fprintf(f, "%stideoffset = %hd\n", tabs[indent+1], data->tideoffset);
	fprintf(f, "%sunused = %u\n", tabs[indent+1], (u32)data->unused);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emheight(FILE *f, emheight_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->height = *((s32*)buffer);
#else
	if (fread(&(data->height), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
	if (fread(&(data->type), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emheight(FILE *f, BfrPrintLevel level, emheight_t *data, int indent)
{
	fprintf(f, "%semheight {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%sheight = %d\n", tabs[indent+1], data->height);
	fprintf(f, "%stype = %u\n", tabs[indent+1], (u32)data->type);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emhd(FILE *f, emhd_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->latency = *((u16*)buffer);
#else
	if(fread(&(data->latency), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->heading = *((u16*)buffer);
#else
	if(fread(&(data->heading), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	*tot_read += bytes_read;
	return(True);
}

static void print_emhd(FILE *f, BfrPrintLevel level, emhd_t *data, int indent)
{
	fprintf(f, "%semhd {\n", tabs[indent]);
	fprintf(f, "%slatency = %hu\n", tabs[indent+1], data->latency);
	fprintf(f, "%sheading = %hu\n", tabs[indent+1], data->heading);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emheading(emheading_t *data)
{
	free(data->heading);
}

static Bool read_emheading(FILE *f, emheading_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	int		i;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->numentries = *((u16*)buffer);
#else
	if(fread(&(data->numentries), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if ((data->heading = (emhd_t*)calloc(data->numentries, sizeof(emhd_t))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emheading.heading\n", modname);
		clean_emheading(data);
		return(False);
	}
	for (i = 0; i < data->numentries; ++i) {
		if (!read_emhd(f, data->heading+i, &bytes_read)) {
			bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading emheading.heading[%d].\n", modname, i);
			clean_emheading(data);
			return(False);
		}
	}
	if (fread(&(data->active), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emheading(FILE *f, BfrPrintLevel level, emheading_t *data, int indent)
{
	int	i, endstop;

	fprintf(f, "%semheading {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%snumentries = %hu\n", tabs[indent+1], data->numentries);
	if (level >= BFR_PRINT_ARRAYSTART) {
		fprintf(f, "%sheading = {", tabs[indent+1]);
		if (level >= BFR_PRINT_ALL) endstop = data->numentries; else endstop = BFR_PRINT_ARRAYLEN;
	endstop = (endstop > data->numentries) ? data->numentries : endstop;
		fprintf(f, "\n");
		print_emhd(f, level, data->heading, indent+2);
		for (i = 1; i < endstop; ++i)
			print_emhd(f, level, data->heading + i, indent+2);
		if (level < BFR_PRINT_ALL)
			fprintf(f, "%s...\n%s}\n", tabs[indent+2], tabs[indent+1]);
	}
	fprintf(f, "%sactive = %u\n", tabs[indent+1], (u32)data->active);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_ematt(FILE *f, ematt_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->latency = *((u16*)buffer);
#else
	if(fread(&(data->latency), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->status = *((u16*)buffer);
#else
	if(fread(&(data->status), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->roll = *((s16*)buffer);
#else
	if(fread(&(data->roll), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->pitch = *((s16*)buffer);
#else
	if(fread(&(data->pitch), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->heave = *((s16*)buffer);
#else
	if(fread(&(data->heave), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->heading = *((u16*)buffer);
#else
	if(fread(&(data->heading), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	*tot_read += bytes_read;
	return(True);
}

static void print_ematt(FILE *f, BfrPrintLevel level, ematt_t *data, int indent)
{
	fprintf(f, "%sematt {\n", tabs[indent]);
	fprintf(f, "%slatency = %hu\n", tabs[indent+1], data->latency);
	fprintf(f, "%sstatus = %hu\n", tabs[indent+1], data->status);
	fprintf(f, "%sroll = %hd\n", tabs[indent+1], data->roll);
	fprintf(f, "%spitch = %hd\n", tabs[indent+1], data->pitch);
	fprintf(f, "%sheave = %hd\n", tabs[indent+1], data->heave);
	fprintf(f, "%sheading = %hu\n", tabs[indent+1], data->heading);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emattitude(emattitude_t *data)
{
	free(data->attitude);
}

static Bool read_emattitude(FILE *f, emattitude_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	int		i;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->numentries = *((u16*)buffer);
#else
	if(fread(&(data->numentries), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if ((data->attitude = (ematt_t*)calloc(data->numentries, sizeof(ematt_t))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emattitude.attitude\n", modname);
		clean_emattitude(data);
		return(False);
	}
	for (i = 0; i < data->numentries; ++i) {
		if (!read_ematt(f, data->attitude+i, &bytes_read)) {
			bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading emattitude.attitude[%d].\n", modname, i);
			clean_emattitude(data);
			return(False);
		}
	}
	if (fread(&(data->motiondesc), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emattitude(FILE *f, BfrPrintLevel level, emattitude_t *data, int indent)
{
	int	i, endstop;

	fprintf(f, "%semattitude {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%snumentries = %hu\n", tabs[indent+1], data->numentries);
	if (level >= BFR_PRINT_ARRAYSTART) {
		fprintf(f, "%sattitude = {", tabs[indent+1]);
		if (level >= BFR_PRINT_ALL) endstop = data->numentries; else endstop = BFR_PRINT_ARRAYLEN;
	endstop = (endstop > data->numentries) ? data->numentries : endstop;
		fprintf(f, "\n");
		print_ematt(f, level, data->attitude, indent+2);
		for (i = 1; i < endstop; ++i)
			print_ematt(f, level, data->attitude + i, indent+2);
		if (level < BFR_PRINT_ALL)
			fprintf(f, "%s...\n%s}\n", tabs[indent+2], tabs[indent+1]);
	}
	fprintf(f, "%smotiondesc = %u\n", tabs[indent+1], (u32)data->motiondesc);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emposition(emposition_t *data)
{
	free(data->ip_dgram);
}

static Bool read_emposition(FILE *f, emposition_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->counter = *((u16*)buffer);
#else
	if(fread(&(data->counter), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->latitude = *((s32*)buffer);
#else
	if (fread(&(data->latitude), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 4, 1, f) != 1) return(False);
	bytes_read += 4;
	swap = buffer[0]; buffer[0] = buffer[3]; buffer[3] = swap;
	swap = buffer[1]; buffer[1] = buffer[2]; buffer[2] = swap;
	data->longitude = *((s32*)buffer);
#else
	if (fread(&(data->longitude), 4, 1, f) != 1) return(False);
	bytes_read += 4;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->fix_quality = *((u16*)buffer);
#else
	if(fread(&(data->fix_quality), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->speed_og = *((u16*)buffer);
#else
	if(fread(&(data->speed_og), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->course_og = *((u16*)buffer);
#else
	if(fread(&(data->course_og), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->heading = *((u16*)buffer);
#else
	if(fread(&(data->heading), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if (fread(&(data->possys), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->ip_dgram_len), 1, 1, f) != 1) return(False);
	bytes_read++;
	if ((data->ip_dgram = (u8*)calloc(data->ip_dgram_len, sizeof(u8))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emposition.ip_dgram\n", modname);
		clean_emposition(data);
		return(False);
	}
	if (fread(data->ip_dgram, sizeof(u8), data->ip_dgram_len, f) != data->ip_dgram_len) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading array of %d u8's for emposition.ip_dgram.\n", modname, data->ip_dgram_len);
		clean_emposition(data);
		return(False);
	}
	bytes_read += data->ip_dgram_len*sizeof(u8);
	if (fread(&(data->unused), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emposition(FILE *f, BfrPrintLevel level, emposition_t *data, int indent)
{
	int	i, endstop;

	fprintf(f, "%semposition {\n", tabs[indent]);
	fprintf(f, "%scounter = %hu\n", tabs[indent+1], data->counter);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%slatitude = %d\n", tabs[indent+1], data->latitude);
	fprintf(f, "%slongitude = %d\n", tabs[indent+1], data->longitude);
	fprintf(f, "%sfix_quality = %hu\n", tabs[indent+1], data->fix_quality);
	fprintf(f, "%sspeed_og = %hu\n", tabs[indent+1], data->speed_og);
	fprintf(f, "%scourse_og = %hu\n", tabs[indent+1], data->course_og);
	fprintf(f, "%sheading = %hu\n", tabs[indent+1], data->heading);
	fprintf(f, "%spossys = %u\n", tabs[indent+1], (u32)data->possys);
	fprintf(f, "%sip_dgram_len = %u\n", tabs[indent+1], (u32)data->ip_dgram_len);
	if (level >= BFR_PRINT_ARRAYSTART) {
		fprintf(f, "%sip_dgram = {", tabs[indent+1]);
		if (level >= BFR_PRINT_ALL) endstop = data->ip_dgram_len; else endstop = BFR_PRINT_ARRAYLEN;
	endstop = (endstop > data->ip_dgram_len) ? data->ip_dgram_len : endstop;
		fprintf(f, " %u", (u32)data->ip_dgram[0]);
		for (i = 1; i < endstop; ++i)
			fprintf(f, ", %u", (u32)data->ip_dgram[i]);
		if (level < BFR_PRINT_ALL)
			fprintf(f, " }\n");
		else
			fprintf(f, ", ... }\n");
	}
	fprintf(f, "%sunused = %u\n", tabs[indent+1], (u32)data->unused);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emcbed(FILE *f, emcbed_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	if (fread(&(data->beamindex), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->unused), 1, 1, f) != 1) return(False);
	bytes_read++;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->numsamp = *((u16*)buffer);
#else
	if(fread(&(data->numsamp), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->detrange = *((u16*)buffer);
#else
	if(fread(&(data->detrange), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	*tot_read += bytes_read;
	return(True);
}

static void print_emcbed(FILE *f, BfrPrintLevel level, emcbed_t *data, int indent)
{
	fprintf(f, "%semcbed {\n", tabs[indent]);
	fprintf(f, "%sbeamindex = %u\n", tabs[indent+1], (u32)data->beamindex);
	fprintf(f, "%sunused = %d\n", tabs[indent+1], (s32)data->unused);
	fprintf(f, "%snumsamp = %hu\n", tabs[indent+1], data->numsamp);
	fprintf(f, "%sdetrange = %hu\n", tabs[indent+1], data->detrange);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emcenbeam(emcenbeam_t *data)
{
	if (data->beamdata != NULL)
		free(data->beamdata[0]);
	free(data->beamdata);
	free(data->beamhdr);
}

static Bool read_emcenbeam(FILE *f, emcenbeam_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	int		i;
	int		total;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->ping = *((u16*)buffer);
#else
	if(fread(&(data->ping), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->maxrange = *((u16*)buffer);
#else
	if(fread(&(data->maxrange), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->tx_pulselen = *((u16*)buffer);
#else
	if(fread(&(data->tx_pulselen), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->R0_used = *((u16*)buffer);
#else
	if(fread(&(data->R0_used), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->TVG_start = *((u16*)buffer);
#else
	if(fread(&(data->TVG_start), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->TVG_stop = *((u16*)buffer);
#else
	if(fread(&(data->TVG_stop), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if (fread(&(data->ni_bs), 1, 1, f) != 1) return(False);
	bytes_read++;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->tx_beamwd = *((u16*)buffer);
#else
	if(fread(&(data->tx_beamwd), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if (fread(&(data->TVG_xover), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->numbeams), 1, 1, f) != 1) return(False);
	bytes_read++;
	if ((data->beamhdr = (emcbed_t*)calloc(data->numbeams, sizeof(emcbed_t))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emcenbeam.beamhdr\n", modname);
		clean_emcenbeam(data);
		return(False);
	}
	for (i = 0; i < data->numbeams; ++i) {
		if (!read_emcbed(f, data->beamhdr+i, &bytes_read)) {
			bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading emcenbeam.beamhdr[%d].\n", modname, i);
			clean_emcenbeam(data);
			return(False);
		}
	}
	for (i = 0, total = 0; i < data->numbeams; ++i) total += data->beamhdr[i].numsamp;
	if ((data->beamdata = (s8**)calloc(data->numbeams, sizeof(s8*))) == NULL ||
		(data->beamdata[0] = (s8*)calloc(total, sizeof(s8))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emcenbeam.beamdata\n", modname);
		clean_emcenbeam(data);
		return(False);
	}
	for (i = 0; i < data->numbeams-1; ++i) data->beamdata[i+1] = data->beamdata[i] + data->beamhdr[i].numsamp;
	if (fread(data->beamdata[0], sizeof(s8), total, f) != total) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading array of %d s8's for emcenbeam.beamdata.\n", modname, total);
		clean_emcenbeam(data);
		return(False);
	}
	bytes_read += total*sizeof(s8);
	if (fread(&(data->unused), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emcenbeam(FILE *f, BfrPrintLevel level, emcenbeam_t *data, int indent)
{
	int	i, j, endstop, endstopj;

	fprintf(f, "%semcenbeam {\n", tabs[indent]);
	fprintf(f, "%sping = %hu\n", tabs[indent+1], data->ping);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%smaxrange = %hu\n", tabs[indent+1], data->maxrange);
	fprintf(f, "%stx_pulselen = %hu\n", tabs[indent+1], data->tx_pulselen);
	fprintf(f, "%sR0_used = %hu\n", tabs[indent+1], data->R0_used);
	fprintf(f, "%sTVG_start = %hu\n", tabs[indent+1], data->TVG_start);
	fprintf(f, "%sTVG_stop = %hu\n", tabs[indent+1], data->TVG_stop);
	fprintf(f, "%sni_bs = %d\n", tabs[indent+1], (s32)data->ni_bs);
	fprintf(f, "%stx_beamwd = %hu\n", tabs[indent+1], data->tx_beamwd);
	fprintf(f, "%sTVG_xover = %u\n", tabs[indent+1], (u32)data->TVG_xover);
	fprintf(f, "%snumbeams = %u\n", tabs[indent+1], (u32)data->numbeams);
	if (level >= BFR_PRINT_ARRAYSTART) {
		fprintf(f, "%sbeamhdr = {", tabs[indent+1]);
		if (level >= BFR_PRINT_ALL) endstop = data->numbeams; else endstop = BFR_PRINT_ARRAYLEN;
	endstop = (endstop > data->numbeams) ? data->numbeams : endstop;
		fprintf(f, "\n");
		print_emcbed(f, level, data->beamhdr, indent+2);
		for (i = 1; i < endstop; ++i)
			print_emcbed(f, level, data->beamhdr + i, indent+2);
		if (level < BFR_PRINT_ALL)
			fprintf(f, "%s...\n%s}\n", tabs[indent+2], tabs[indent+1]);
	}
	fprintf(f, "%sbeamdata = {\n", tabs[indent+1]);
	
	
	if (level < BFR_PRINT_ALL)
		endstop = BFR_PRINT_ARRAYLEN;
	else
		endstop = data->numbeams;
	endstop = (endstop > data->numbeams) ? data->numbeams : endstop;
	for (i = 0; i < endstop; ++i) {
		if (level < BFR_PRINT_ALL)
			endstopj = BFR_PRINT_ARRAYLEN;
		else
			endstopj = data->beamhdr[i].numsamp;
		endstopj = (endstopj > data->beamhdr[i].numsamp) ? data->beamhdr[i].numsamp : endstopj;
		fprintf(f, "%s[%d] = {", tabs[indent+2], i);
		fprintf(f, " %d", data->beamdata[i][0]);
		for (j = 0; j < endstopj; ++j)
			fprintf(f, ", %d", (s32)data->beamdata[i][j]);
		if (level < BFR_PRINT_ALL)
			fprintf(f, ", ... }\n");
		else
			fprintf(f, "}\n");
	}
	if (level < BFR_PRINT_ALL) {
		fprintf(f, "%s...\n", tabs[indent+2]);
		fprintf(f, "%s}\n", tabs[indent+1]);
	}
	fprintf(f, "%sunused = %u\n", tabs[indent+1], (u32)data->unused);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emseaim(FILE *f, emseaim_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	if (fread(&(data->beamindex), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->sortdir), 1, 1, f) != 1) return(False);
	bytes_read++;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->numsamp = *((u16*)buffer);
#else
	if(fread(&(data->numsamp), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->detrange = *((u16*)buffer);
#else
	if(fread(&(data->detrange), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	*tot_read += bytes_read;
	return(True);
}

static void print_emseaim(FILE *f, BfrPrintLevel level, emseaim_t *data, int indent)
{
	fprintf(f, "%semseaim {\n", tabs[indent]);
	fprintf(f, "%sbeamindex = %u\n", tabs[indent+1], (u32)data->beamindex);
	fprintf(f, "%ssortdir = %d\n", tabs[indent+1], (s32)data->sortdir);
	fprintf(f, "%snumsamp = %hu\n", tabs[indent+1], data->numsamp);
	fprintf(f, "%sdetrange = %hu\n", tabs[indent+1], data->detrange);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emseabed(emseabed_t *data)
{
	if (data->beamdata != NULL)
		free(data->beamdata[0]);
	free(data->beamdata);
	free(data->beamhdr);
}

static Bool read_emseabed(FILE *f, emseabed_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	int		i;
	int		total;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->ping = *((u16*)buffer);
#else
	if(fread(&(data->ping), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->maxrange = *((u16*)buffer);
#else
	if(fread(&(data->maxrange), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->R0_pred = *((u16*)buffer);
#else
	if(fread(&(data->R0_pred), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->R0_used = *((u16*)buffer);
#else
	if(fread(&(data->R0_used), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->TVG_start = *((u16*)buffer);
#else
	if(fread(&(data->TVG_start), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->TVG_stop = *((u16*)buffer);
#else
	if(fread(&(data->TVG_stop), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if (fread(&(data->ni_bs), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->ob_bs), 1, 1, f) != 1) return(False);
	bytes_read++;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->tx_beamwd = *((u16*)buffer);
#else
	if(fread(&(data->tx_beamwd), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if (fread(&(data->TVG_xover), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->validbeams), 1, 1, f) != 1) return(False);
	bytes_read++;
	if ((data->beamhdr = (emseaim_t*)calloc(data->validbeams, sizeof(emseaim_t))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emseabed.beamhdr\n", modname);
		clean_emseabed(data);
		return(False);
	}
	for (i = 0; i < data->validbeams; ++i) {
		if (!read_emseaim(f, data->beamhdr+i, &bytes_read)) {
			bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading emseabed.beamhdr[%d].\n", modname, i);
			clean_emseabed(data);
			return(False);
		}
	}
	for (i = 0, total = 0; i < data->validbeams; ++i) total += data->beamhdr[i].numsamp;
	if ((data->beamdata = (s8**)calloc(data->validbeams, sizeof(s8*))) == NULL ||
		(data->beamdata[0] = (s8*)calloc(total, sizeof(s8))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emseabed.beamdata\n", modname);
		clean_emseabed(data);
		return(False);
	}
	for (i = 0; i < data->validbeams-1; ++i) data->beamdata[i+1] = data->beamdata[i] + data->beamhdr[i].numsamp;
	if (fread(data->beamdata[0], sizeof(s8), total, f) != total) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading array of %d s8's for emseabed.beamdata.\n", modname, total);
		clean_emseabed(data);
		return(False);
	}
	bytes_read += total*sizeof(s8);
	*tot_read += bytes_read;
	return(True);
}

static void print_emseabed(FILE *f, BfrPrintLevel level, emseabed_t *data, int indent)
{
	int	i, j, endstop, endstopj;

	fprintf(f, "%semseabed {\n", tabs[indent]);
	fprintf(f, "%sping = %hu\n", tabs[indent+1], data->ping);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%smaxrange = %hu\n", tabs[indent+1], data->maxrange);
	fprintf(f, "%sR0_pred = %hu\n", tabs[indent+1], data->R0_pred);
	fprintf(f, "%sR0_used = %hu\n", tabs[indent+1], data->R0_used);
	fprintf(f, "%sTVG_start = %hu\n", tabs[indent+1], data->TVG_start);
	fprintf(f, "%sTVG_stop = %hu\n", tabs[indent+1], data->TVG_stop);
	fprintf(f, "%sni_bs = %d\n", tabs[indent+1], (s32)data->ni_bs);
	fprintf(f, "%sob_bs = %d\n", tabs[indent+1], (s32)data->ob_bs);
	fprintf(f, "%stx_beamwd = %hu\n", tabs[indent+1], data->tx_beamwd);
	fprintf(f, "%sTVG_xover = %u\n", tabs[indent+1], (u32)data->TVG_xover);
	fprintf(f, "%svalidbeams = %u\n", tabs[indent+1], (u32)data->validbeams);
	if (level >= BFR_PRINT_ARRAYSTART) {
		fprintf(f, "%sbeamhdr = {", tabs[indent+1]);
		if (level >= BFR_PRINT_ALL) endstop = data->validbeams; else endstop = BFR_PRINT_ARRAYLEN;
	endstop = (endstop > data->validbeams) ? data->validbeams : endstop;
		fprintf(f, "\n");
		print_emseaim(f, level, data->beamhdr, indent+2);
		for (i = 1; i < endstop; ++i)
			print_emseaim(f, level, data->beamhdr + i, indent+2);
		if (level < BFR_PRINT_ALL)
			fprintf(f, "%s...\n%s}\n", tabs[indent+2], tabs[indent+1]);
	}
	fprintf(f, "%sbeamdata = {\n", tabs[indent+1]);
	
	
	if (level < BFR_PRINT_ALL)
		endstop = BFR_PRINT_ARRAYLEN;
	else
		endstop = data->validbeams;
	endstop = (endstop > data->validbeams) ? data->validbeams : endstop;
	for (i = 0; i < endstop; ++i) {
		if (level < BFR_PRINT_ALL)
			endstopj = BFR_PRINT_ARRAYLEN;
		else
			endstopj = data->beamhdr[i].numsamp;
		endstopj = (endstopj > data->beamhdr[i].numsamp) ? data->beamhdr[i].numsamp : endstopj;
		fprintf(f, "%s[%d] = {", tabs[indent+2], i);
		fprintf(f, " %d", data->beamdata[i][0]);
		for (j = 0; j < endstopj; ++j)
			fprintf(f, ", %d", (s32)data->beamdata[i][j]);
		if (level < BFR_PRINT_ALL)
			fprintf(f, ", ... }\n");
		else
			fprintf(f, "}\n");
	}
	if (level < BFR_PRINT_ALL) {
		fprintf(f, "%s...\n", tabs[indent+2]);
		fprintf(f, "%s}\n", tabs[indent+1]);
	}
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emrt(FILE *f, emrt_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->rx_pointangle = *((s16*)buffer);
#else
	if(fread(&(data->rx_pointangle), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->tx_tilt = *((s16*)buffer);
#else
	if(fread(&(data->tx_tilt), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->rx_twt = *((u16*)buffer);
#else
	if(fread(&(data->rx_twt), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if (fread(&(data->backscatter), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->beamnum), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emrt(FILE *f, BfrPrintLevel level, emrt_t *data, int indent)
{
	fprintf(f, "%semrt {\n", tabs[indent]);
	fprintf(f, "%srx_pointangle = %hd\n", tabs[indent+1], data->rx_pointangle);
	fprintf(f, "%stx_tilt = %hd\n", tabs[indent+1], data->tx_tilt);
	fprintf(f, "%srx_twt = %hu\n", tabs[indent+1], data->rx_twt);
	fprintf(f, "%sbackscatter = %d\n", tabs[indent+1], (s32)data->backscatter);
	fprintf(f, "%sbeamnum = %u\n", tabs[indent+1], (u32)data->beamnum);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emrawrt(emrawrt_t *data)
{
	free(data->beams);
}

static Bool read_emrawrt(FILE *f, emrawrt_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	int		i;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->ping = *((u16*)buffer);
#else
	if(fread(&(data->ping), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if (fread(&(data->maxbeams), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->validbeams), 1, 1, f) != 1) return(False);
	bytes_read++;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->tx_soundspeed = *((u16*)buffer);
#else
	if(fread(&(data->tx_soundspeed), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if ((data->beams = (emrt_t*)calloc(data->validbeams, sizeof(emrt_t))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emrawrt.beams\n", modname);
		clean_emrawrt(data);
		return(False);
	}
	for (i = 0; i < data->validbeams; ++i) {
		if (!read_emrt(f, data->beams+i, &bytes_read)) {
			bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading emrawrt.beams[%d].\n", modname, i);
			clean_emrawrt(data);
			return(False);
		}
	}
	if (fread(&(data->unused), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emrawrt(FILE *f, BfrPrintLevel level, emrawrt_t *data, int indent)
{
	int	i, endstop;

	fprintf(f, "%semrawrt {\n", tabs[indent]);
	fprintf(f, "%sping = %hu\n", tabs[indent+1], data->ping);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%smaxbeams = %u\n", tabs[indent+1], (u32)data->maxbeams);
	fprintf(f, "%svalidbeams = %u\n", tabs[indent+1], (u32)data->validbeams);
	fprintf(f, "%stx_soundspeed = %hu\n", tabs[indent+1], data->tx_soundspeed);
	if (level >= BFR_PRINT_ARRAYSTART) {
		fprintf(f, "%sbeams = {", tabs[indent+1]);
		if (level >= BFR_PRINT_ALL) endstop = data->validbeams; else endstop = BFR_PRINT_ARRAYLEN;
	endstop = (endstop > data->validbeams) ? data->validbeams : endstop;
		fprintf(f, "\n");
		print_emrt(f, level, data->beams, indent+2);
		for (i = 1; i < endstop; ++i)
			print_emrt(f, level, data->beams + i, indent+2);
		if (level < BFR_PRINT_ALL)
			fprintf(f, "%s...\n%s}\n", tabs[indent+2], tabs[indent+1]);
	}
	fprintf(f, "%sunused = %u\n", tabs[indent+1], (u32)data->unused);
	fprintf(f, "%s}\n", tabs[indent]);
}

static Bool read_emdb(FILE *f, emdb_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->depth = *((s16*)buffer);
#else
	if(fread(&(data->depth), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->y = *((s16*)buffer);
#else
	if(fread(&(data->y), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->x = *((s16*)buffer);
#else
	if(fread(&(data->x), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->rx_angle = *((s16*)buffer);
#else
	if(fread(&(data->rx_angle), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->rx_azim_tx_tilt = *((u16*)buffer);
#else
	if(fread(&(data->rx_azim_tx_tilt), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->rx_range_time = *((u16*)buffer);
#else
	if(fread(&(data->rx_range_time), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if (fread(&(data->quality), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->detwin_len), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->backscatter), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->beamnum), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emdb(FILE *f, BfrPrintLevel level, emdb_t *data, int indent)
{
	fprintf(f, "%semdb {\n", tabs[indent]);
	fprintf(f, "%sdepth = %hd\n", tabs[indent+1], data->depth);
	fprintf(f, "%sy = %hd\n", tabs[indent+1], data->y);
	fprintf(f, "%sx = %hd\n", tabs[indent+1], data->x);
	fprintf(f, "%srx_angle = %hd\n", tabs[indent+1], data->rx_angle);
	fprintf(f, "%srx_azim_tx_tilt = %hu\n", tabs[indent+1], data->rx_azim_tx_tilt);
	fprintf(f, "%srx_range_time = %hu\n", tabs[indent+1], data->rx_range_time);
	fprintf(f, "%squality = %u\n", tabs[indent+1], (u32)data->quality);
	fprintf(f, "%sdetwin_len = %u\n", tabs[indent+1], (u32)data->detwin_len);
	fprintf(f, "%sbackscatter = %d\n", tabs[indent+1], (s32)data->backscatter);
	fprintf(f, "%sbeamnum = %u\n", tabs[indent+1], (u32)data->beamnum);
	fprintf(f, "%s}\n", tabs[indent]);
}

static void clean_emdepth(emdepth_t *data)
{
	free(data->beams);
}

static Bool read_emdepth(FILE *f, emdepth_t *data, int *tot_read)
{
#ifdef __SWAP_BYTES__
	u8		buffer[8], swap, *swap_p;
#endif
	int		bytes_read = 0;

	int		i;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->ping = *((u16*)buffer);
#else
	if(fread(&(data->ping), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->serialnum = *((u16*)buffer);
#else
	if(fread(&(data->serialnum), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->heading = *((u16*)buffer);
#else
	if(fread(&(data->heading), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->tx_soundspeed = *((u16*)buffer);
#else
	if(fread(&(data->tx_soundspeed), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->tx_depth = *((u16*)buffer);
#else
	if(fread(&(data->tx_depth), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if (fread(&(data->maxbeams), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->validbeams), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->zres), 1, 1, f) != 1) return(False);
	bytes_read++;
	if (fread(&(data->xyres), 1, 1, f) != 1) return(False);
	bytes_read++;
#ifdef __SWAP_BYTES__
	if (fread(buffer, 2, 1, f) != 1) return(False);
	bytes_read += 2;
	swap = buffer[0]; buffer[0] = buffer[1]; buffer[1] = swap;
	data->samprate = *((u16*)buffer);
#else
	if(fread(&(data->samprate), 2, 1, f) != 1) return(False);
	bytes_read += 2;
#endif
	if ((data->beams = (emdb_t*)calloc(data->validbeams, sizeof(emdb_t))) == NULL) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: out of memory allocating emdepth.beams\n", modname);
		clean_emdepth(data);
		return(False);
	}
	for (i = 0; i < data->validbeams; ++i) {
		if (!read_emdb(f, data->beams+i, &bytes_read)) {
			bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed reading emdepth.beams[%d].\n", modname, i);
			clean_emdepth(data);
			return(False);
		}
	}
	if (fread(&(data->tx_depthoff_mult), 1, 1, f) != 1) return(False);
	bytes_read++;
	*tot_read += bytes_read;
	return(True);
}

static void print_emdepth(FILE *f, BfrPrintLevel level, emdepth_t *data, int indent)
{
	int	i, endstop;

	fprintf(f, "%semdepth {\n", tabs[indent]);
	fprintf(f, "%sping = %hu\n", tabs[indent+1], data->ping);
	fprintf(f, "%sserialnum = %hu\n", tabs[indent+1], data->serialnum);
	fprintf(f, "%sheading = %hu\n", tabs[indent+1], data->heading);
	fprintf(f, "%stx_soundspeed = %hu\n", tabs[indent+1], data->tx_soundspeed);
	fprintf(f, "%stx_depth = %hu\n", tabs[indent+1], data->tx_depth);
	fprintf(f, "%smaxbeams = %u\n", tabs[indent+1], (u32)data->maxbeams);
	fprintf(f, "%svalidbeams = %u\n", tabs[indent+1], (u32)data->validbeams);
	fprintf(f, "%szres = %u\n", tabs[indent+1], (u32)data->zres);
	fprintf(f, "%sxyres = %u\n", tabs[indent+1], (u32)data->xyres);
	fprintf(f, "%ssamprate = %hu\n", tabs[indent+1], data->samprate);
	if (level >= BFR_PRINT_ARRAYSTART) {
		fprintf(f, "%sbeams = {", tabs[indent+1]);
		if (level >= BFR_PRINT_ALL) endstop = data->validbeams; else endstop = BFR_PRINT_ARRAYLEN;
	endstop = (endstop > data->validbeams) ? data->validbeams : endstop;
		fprintf(f, "\n");
		print_emdb(f, level, data->beams, indent+2);
		for (i = 1; i < endstop; ++i)
			print_emdb(f, level, data->beams + i, indent+2);
		if (level < BFR_PRINT_ALL)
			fprintf(f, "%s...\n%s}\n", tabs[indent+2], tabs[indent+1]);
	}
	fprintf(f, "%stx_depthoff_mult = %d\n", tabs[indent+1], (s32)data->tx_depthoff_mult);
	fprintf(f, "%s}\n", tabs[indent]);
}

#ifdef ALLOW_SIMRAD
BFRtn read_simrad(FILE *f, simrad_t *data, u32 *n_read)
{
	Bool	synch = False;
	int		bytes_read, resynch_distance = 0;
	BFRtn	rc;
	long	position = ftell(f);

	while (!synch && resynch_distance < 1024) {
		bytes_read = 0;

		if (!read_emhdr(f, &(data->header), &bytes_read)) {
			if (feof(f)) {
				bfrFiltText(stderr, BFR_FILT_WARN, "%s: end of file while seeking datagram.\n", modname);
				rc = BFR_FILE_END;
			} else {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: error: failed reading header\n", modname);
				rc = BFR_READ_ERROR;
			}
			return(rc);
		}
		if (!(synch = validate_simrad(&(data->header), 40960))) {
			fseek(f, -bytes_read+1, SEEK_CUR);
			++resynch_distance;
		} else if (resynch_distance != 0)
			bfrFiltText(stderr, BFR_FILT_WARN, "%s: warning: skipped %d bytes in resynchronising at file offset %ld.\n", modname, resynch_distance, position);
	}
	*n_read = bytes_read; /* Make sure we're up to date! */
	if (resynch_distance >= 1024) {
		bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to resynchronise after 1024 bytes - file corrupt?\n", modname);
		return(BFR_NO_HEADER);
	}
	switch(data->header.type) {
		case 0x70:
			if (!read_eminstparam(f, &(data->datagram.remote_iparam), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a eminstparam datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x70;
			break;
		case 0x69:
			if (!read_eminstparam(f, &(data->datagram.stop_iparam), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a eminstparam datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x69;
			break;
		case 0x49:
			if (!read_eminstparam(f, &(data->datagram.start_iparam), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a eminstparam datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x49;
			break;
		case 0x52:
			if (!read_emruntime(f, &(data->datagram.runtime), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emruntime datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x52;
			break;
		case 0x45:
			if (!read_emsbeam(f, &(data->datagram.singlebeam), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emsbeam datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x45;
			break;
		case 0x76:
			if (!read_emisvp(f, &(data->datagram.isvp), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emisvp datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x76;
			break;
		case 0x55:
			if (!read_emnewsvp(f, &(data->datagram.newsvp), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emnewsvp datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x55;
			break;
		case 0x56:
			if (!read_emsvp(f, &(data->datagram.svp), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emsvp datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x56;
			break;
		case 0x47:
			if (!read_emsurfacess(f, &(data->datagram.surfss), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emsurfacess datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x47;
			break;
		case 0x4a:
			if (!read_emmechtilt(f, &(data->datagram.mechtilt), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emmechtilt datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x4a;
			break;
		case 0x43:
			if (!read_emclock(f, &(data->datagram.clock), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emclock datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x43;
			break;
		case 0x54:
			if (!read_emtide(f, &(data->datagram.tide), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emtide datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x54;
			break;
		case 0x68:
			if (!read_emheight(f, &(data->datagram.height), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emheight datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x68;
			break;
		case 0x48:
			if (!read_emheading(f, &(data->datagram.heading), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emheading datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x48;
			break;
		case 0x41:
			if (!read_emattitude(f, &(data->datagram.attitude), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emattitude datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x41;
			break;
		case 0x50:
			if (!read_emposition(f, &(data->datagram.position), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emposition datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x50;
			break;
		case 0x4b:
			if (!read_emcenbeam(f, &(data->datagram.echograms), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emcenbeam datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x4b;
			break;
		case 0x53:
			if (!read_emseabed(f, &(data->datagram.imagery), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emseabed datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x53;
			break;
		case 0x46:
			if (!read_emrawrt(f, &(data->datagram.rawrt), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emrawrt datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x46;
			break;
		case 0x44:
			if (!read_emdepth(f, &(data->datagram.depth), &bytes_read)) {
				bfrFiltText(stderr, BFR_FILT_CRIT, "%s: failed to read a emdepth datagram.\n", modname);
				*n_read = bytes_read;
				return(BFR_DGRAM_READ_FAIL);
			}
			data->id = (simrad_id)0x44;
			break;
		default:
			bfrFiltText(stderr, BFR_FILT_WARN, "%s: unknown datagram (type %d)\n", modname, (int)data->header.type);
			return(BFR_DGRAM_UNKNOWN);
	}
	if (!read_emtail(f, &(data->tail), &bytes_read)) {
		bfrFiltText(stderr, BFR_FILT_WARN, "%s: failed to read tail.\n", modname);
		return(BFR_INVALID_TAIL);
	}
	*n_read = bytes_read;
	return(BFR_DGRAM_OK);
}

void clean_simrad(simrad_t *data)
{
	switch(data->id) {
		case 0x70:
			clean_eminstparam(&(data->datagram.remote_iparam));
			break;
		case 0x69:
			clean_eminstparam(&(data->datagram.stop_iparam));
			break;
		case 0x49:
			clean_eminstparam(&(data->datagram.start_iparam));
			break;
		case 0x52: /* No item required */ break;
		case 0x45: /* No item required */ break;
		case 0x76:
			clean_emisvp(&(data->datagram.isvp));
			break;
		case 0x55:
			clean_emnewsvp(&(data->datagram.newsvp));
			break;
		case 0x56:
			clean_emsvp(&(data->datagram.svp));
			break;
		case 0x47:
			clean_emsurfacess(&(data->datagram.surfss));
			break;
		case 0x4a:
			clean_emmechtilt(&(data->datagram.mechtilt));
			break;
		case 0x43: /* No item required */ break;
		case 0x54: /* No item required */ break;
		case 0x68: /* No item required */ break;
		case 0x48:
			clean_emheading(&(data->datagram.heading));
			break;
		case 0x41:
			clean_emattitude(&(data->datagram.attitude));
			break;
		case 0x50:
			clean_emposition(&(data->datagram.position));
			break;
		case 0x4b:
			clean_emcenbeam(&(data->datagram.echograms));
			break;
		case 0x53:
			clean_emseabed(&(data->datagram.imagery));
			break;
		case 0x46:
			clean_emrawrt(&(data->datagram.rawrt));
			break;
		case 0x44:
			clean_emdepth(&(data->datagram.depth));
			break;
		default:
			bfrFiltText(stderr, BFR_FILT_WARN, "%s: unrecognised datagram type (%d=0x%x).\n", modname, data->id, data->id);
			return;
	}
}

void print_simrad(FILE *f, BfrPrintLevel level, simrad_t *data)
{
	fprintf(f, "simrad {\n");
	fprintf(f, "\tident = 0x%x\n", data->id);
	if (level <= BFR_PRINT_IDS) {
		fprintf(f, "}\n");
		return;
	}
	print_emhdr(f, level, &(data->header), 1);
	if (level <= BFR_PRINT_HEADERS) {
		fprintf(f, "}\n");
		return;
	}
	switch(data->id) {
		case 0x70:
			print_eminstparam(f, level, &(data->datagram.remote_iparam), 1);
			break;
		case 0x69:
			print_eminstparam(f, level, &(data->datagram.stop_iparam), 1);
			break;
		case 0x49:
			print_eminstparam(f, level, &(data->datagram.start_iparam), 1);
			break;
		case 0x52:
			print_emruntime(f, level, &(data->datagram.runtime), 1);
			break;
		case 0x45:
			print_emsbeam(f, level, &(data->datagram.singlebeam), 1);
			break;
		case 0x76:
			print_emisvp(f, level, &(data->datagram.isvp), 1);
			break;
		case 0x55:
			print_emnewsvp(f, level, &(data->datagram.newsvp), 1);
			break;
		case 0x56:
			print_emsvp(f, level, &(data->datagram.svp), 1);
			break;
		case 0x47:
			print_emsurfacess(f, level, &(data->datagram.surfss), 1);
			break;
		case 0x4a:
			print_emmechtilt(f, level, &(data->datagram.mechtilt), 1);
			break;
		case 0x43:
			print_emclock(f, level, &(data->datagram.clock), 1);
			break;
		case 0x54:
			print_emtide(f, level, &(data->datagram.tide), 1);
			break;
		case 0x68:
			print_emheight(f, level, &(data->datagram.height), 1);
			break;
		case 0x48:
			print_emheading(f, level, &(data->datagram.heading), 1);
			break;
		case 0x41:
			print_emattitude(f, level, &(data->datagram.attitude), 1);
			break;
		case 0x50:
			print_emposition(f, level, &(data->datagram.position), 1);
			break;
		case 0x4b:
			print_emcenbeam(f, level, &(data->datagram.echograms), 1);
			break;
		case 0x53:
			print_emseabed(f, level, &(data->datagram.imagery), 1);
			break;
		case 0x46:
			print_emrawrt(f, level, &(data->datagram.rawrt), 1);
			break;
		case 0x44:
			print_emdepth(f, level, &(data->datagram.depth), 1);
			break;
		default:
			bfrFiltText(stderr, BFR_FILT_WARN, "%s: unrecognised datagram type (%d=0x%x).\n", modname, data->id, data->id);
			return;
	}
	fprintf(f, "}\n");
}

#endif /* ALLOW_SIMRAD */

