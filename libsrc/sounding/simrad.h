/* Generated automatically by bfreader V1.10 [Jun 28 2002] at 1104 Eastern Daylight Time on 05 Jul 2002 */

/* $Id: simrad.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 */

#ifndef __SIMRAD_H__
#define __SIMRAD_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "libbfr.h"

typedef struct {
	u32	datasize;
	u8	stx;
	u8	type;
	u16	modelnum;
	u32	date;
	u32	time;
} emhdr_t;

typedef struct {
	u8	etx;
	u16	checksum;
} emtail_t;

typedef struct {
	u16	linenum;
	u16	serialnum;
	u16	serialnum2;
	u8	*params;
} eminstparam_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	u32	status;
	u8	mode;
	u8	filters;
	u16	mindepth;
	u16	maxdepth;
	u16	absorbcoef;
	u16	tx_pulselen;
	u16	tx_beamw;
	u8	tx_power;
	u8	rx_beamw;
	u8	rx_bandw;
	u8	rx_gain;
	u8	TVG_xover;
	u8	ss_source;
	u16	maxwidth;
	u8	beamspacing;
	u8	coveragesector;
	u8	yaw_pitch_mode;
	u16	unused1;
	u32	unused2;
} emruntime_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	u32	date;
	u32	time;
	u32	depth;
	u8	source;
} emsbeam_t;

typedef struct {
	u8	*depth;
	u8	*speed;
	u8	*temp;
	u8	*salinity;
	u8	*absorption;
	u8	unused;
} emisvpr_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	u8	*formater;
	u8	*profileid;
	u8	*numentries;
	u8	*time;
	u8	*day;
	u8	*month;
	u8	*year;
	emisvpr_t	*isvp;
	u8	*lattitude;
	u8	*lat_hemisp;
	u8	*longitude;
	u8	*lon_hemisp;
	u8	*atmos_pres;
	u8	*comment;
} emisvp_t;

typedef struct {
	u32	depth;
	u32	soundspeed;
} emnewsvpr_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	u32	date;
	u32	time;
	u16	numentries;
	u16	depthres;
	emnewsvpr_t	*svp;
	u8	unused;
} emnewsvp_t;

typedef struct {
	u16	depth;
	u16	soundspeed;
} emsvpr_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	u32	date;
	u32	time;
	u16	numentries;
	u16	depthres;
	emsvpr_t	*svp;
	u8	unused;
} emsvp_t;

typedef struct {
	u16	latency;
	u16	soundspeed;
} emsss_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	u16	numentries;
	emsss_t	*soundspeed;
	u8	unused;
} emsurfacess_t;

typedef struct {
	u16	latency;
	s16	mechtilt;
} emmtr_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	u16	numentries;
	emmtr_t	*mechtilt;
	u8	unused;
} emmechtilt_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	u32	date;
	u32	time;
	u8	pps;
} emclock_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	u32	date;
	u32	time;
	s16	tideoffset;
	u8	unused;
} emtide_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	s32	height;
	u8	type;
} emheight_t;

typedef struct {
	u16	latency;
	u16	heading;
} emhd_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	u16	numentries;
	emhd_t	*heading;
	u8	active;
} emheading_t;

typedef struct {
	u16	latency;
	u16	status;
	s16	roll;
	s16	pitch;
	s16	heave;
	u16	heading;
} ematt_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	u16	numentries;
	ematt_t	*attitude;
	u8	motiondesc;
} emattitude_t;

typedef struct {
	u16	counter;
	u16	serialnum;
	s32	latitude;
	s32	longitude;
	u16	fix_quality;
	u16	speed_og;
	u16	course_og;
	u16	heading;
	u8	possys;
	u8	ip_dgram_len;
	u8	*ip_dgram;
	u8	unused;
} emposition_t;

typedef struct {
	u8	beamindex;
	s8	unused;
	u16	numsamp;
	u16	detrange;
} emcbed_t;

typedef struct {
	u16	ping;
	u16	serialnum;
	u16	maxrange;
	u16	tx_pulselen;
	u16	R0_used;
	u16	TVG_start;
	u16	TVG_stop;
	s8	ni_bs;
	u16	tx_beamwd;
	u8	TVG_xover;
	u8	numbeams;
	emcbed_t	*beamhdr;
	s8	**beamdata;
	u8	unused;
} emcenbeam_t;

typedef struct {
	u8	beamindex;
	s8	sortdir;
	u16	numsamp;
	u16	detrange;
} emseaim_t;

typedef struct {
	u16	ping;
	u16	serialnum;
	u16	maxrange;
	u16	R0_pred;
	u16	R0_used;
	u16	TVG_start;
	u16	TVG_stop;
	s8	ni_bs;
	s8	ob_bs;
	u16	tx_beamwd;
	u8	TVG_xover;
	u8	validbeams;
	emseaim_t	*beamhdr;
	s8	**beamdata;
} emseabed_t;

typedef struct {
	s16	rx_pointangle;
	s16	tx_tilt;
	u16	rx_twt;
	s8	backscatter;
	u8	beamnum;
} emrt_t;

typedef struct {
	u16	ping;
	u16	serialnum;
	u8	maxbeams;
	u8	validbeams;
	u16	tx_soundspeed;
	emrt_t	*beams;
	u8	unused;
} emrawrt_t;

typedef struct {
	s16	depth;
	s16	y;
	s16	x;
	s16	rx_angle;
	u16	rx_azim_tx_tilt;
	u16	rx_range_time;
	u8	quality;
	u8	detwin_len;
	s8	backscatter;
	u8	beamnum;
} emdb_t;

typedef struct {
	u16	ping;
	u16	serialnum;
	u16	heading;
	u16	tx_soundspeed;
	u16	tx_depth;
	u8	maxbeams;
	u8	validbeams;
	u8	zres;
	u8	xyres;
	u16	samprate;
	emdb_t	*beams;
	s8	tx_depthoff_mult;
} emdepth_t;

#ifdef ALLOW_SIMRAD
typedef union {
	eminstparam_t	remote_iparam;
	eminstparam_t	stop_iparam;
	eminstparam_t	start_iparam;
	emruntime_t	runtime;
	emsbeam_t	singlebeam;
	emisvp_t	isvp;
	emnewsvp_t	newsvp;
	emsvp_t	svp;
	emsurfacess_t	surfss;
	emmechtilt_t	mechtilt;
	emclock_t	clock;
	emtide_t	tide;
	emheight_t	height;
	emheading_t	heading;
	emattitude_t	attitude;
	emposition_t	position;
	emcenbeam_t	echograms;
	emseabed_t	imagery;
	emrawrt_t	rawrt;
	emdepth_t	depth;
} simrad_data;

typedef enum {
	SIMRAD_REMOTE_IPARAM = 0x70,
	SIMRAD_STOP_IPARAM = 0x69,
	SIMRAD_START_IPARAM = 0x49,
	SIMRAD_RUNTIME = 0x52,
	SIMRAD_SINGLEBEAM = 0x45,
	SIMRAD_ISVP = 0x76,
	SIMRAD_NEWSVP = 0x55,
	SIMRAD_SVP = 0x56,
	SIMRAD_SURFSS = 0x47,
	SIMRAD_MECHTILT = 0x4a,
	SIMRAD_CLOCK = 0x43,
	SIMRAD_TIDE = 0x54,
	SIMRAD_HEIGHT = 0x68,
	SIMRAD_HEADING = 0x48,
	SIMRAD_ATTITUDE = 0x41,
	SIMRAD_POSITION = 0x50,
	SIMRAD_ECHOGRAMS = 0x4b,
	SIMRAD_IMAGERY = 0x53,
	SIMRAD_RAWRT = 0x46,
	SIMRAD_DEPTH = 0x44
} simrad_id;

typedef struct {
	simrad_id	id;
	emhdr_t	header;
	simrad_data	datagram;
	emtail_t	tail;
} simrad_t;

extern BFRtn read_simrad(FILE *f, simrad_t *data, u32 *n_read);
extern void clean_simrad(simrad_t *data);
extern void print_simrad(FILE *f, BfrPrintLevel level, simrad_t *data);

extern Bool validate_simrad(emhdr_t *hdr, int maxreclen);

#endif /* ALLOW_SIMRAD */
#ifdef __cplusplus
}
#endif
#endif
