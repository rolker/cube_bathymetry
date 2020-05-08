/*
 * $Id: read_simrad.c 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1  2002/08/30 21:42:59  dneville
 * Adding some more code needed for the CUBE raw simrad reader
 *
 * Revision 1.13  2001/10/15 23:09:35  brc
 * Added SoG default value, plus parameter, and a check to make sure that the
 * SoG from the raw datagram is valid before it is actually used.  This can
 * cause not-so-subtle problems, since the default value is 65536 = 655.36 m/s
 * speed-over-ground.  Oops.  Also fixed beam numbering in the bathy beam
 * translation section (and re-fixed that in the imagery beam numbering to
 * match), so that the beams are no always 0..max-1 internally.  This means
 * that they match up with the error module code, so that they do actually
 * get the correct errors computed.  Double-oops with a side order of damn.
 *
 * Revision 1.12  2001/09/23 19:04:40  brc
 * Fixed bug in determining the phase/amplitude detection status of the beams
 * in a raw datagram, and added the code to ensure that data window length does
 * get propagated into the data flags.
 *
 * Revision 1.11  2001/05/16 21:08:53  brc
 * Added float.h for FLT_MAX, DBL_MAX, etc. in Linux
 *
 * Revision 1.10  2001/05/14 04:23:27  brc
 * Updated to make modules 'params'-aware.
 *
 * Revision 1.9  2001/04/10 23:24:28  brc
 * Removed sounding location code since this now needs to be used by other
 * modules, and really aught to be in the sounding module.
 *
 * Revision 1.8  2001/02/21 04:28:08  brc
 * Modified simrad_timestamp() to use stime_make_time() to construct the time
 * rather than working internally.  Turns out that mktime() doesn't correctly
 * use the GMT/UTC timezone as expected, and converts in the local timezone.
 * Consequently, the code mis-interprets timestamps, and code that uses the
 * Simrad reader and stime gets very confused because of the time offsets.  This
 * is better done all in stime anyway (code here is just a hang-over from before
 * stime was available), and it makes sure everything is consistent.
 *
 * Revision 1.7  2001/02/17 20:23:14  brc
 * Significantly improved robustness of attitude decimation and filtering
 * algorithm.  This now checks that the input rate is reasonable rather than
 * just assuming that it is, and only computes an FIR filter if the rate is
 * sufficiently high (compile time limits only at present).  Packets with very
 * low mean rates are rejected out of hand; those with rate above the absolute
 * minimum, but below the expected input rate have a pass-through (identity)
 * filter applied.  This should deal better with problems as experienced with
 * the Pinnacles and SAX-99 data sets where attitude packets occasionally have
 * poor latency figures in them, which would lead a naive algorithm (:-) to
 * generate bad filters and code.  Note that we still don't do culling of bad
 * latency points (except in as much as they are removed with the rest of a
 * bad packet), so we may still see some problems with interpolation.
 *
 * Revision 1.6  2001/02/11 17:56:14  brc
 * Added facility to gather imagery data into the input stream.  This means that
 * raw streams with imagery components now honour the SOUNDING_IMAGERY component.
 * Currently, only the Simrad reader is defined directly, and as a function of
 * this, imagery packets don't get geo-referenced until they are matched with a
 * suitable bathy packet.  The code in read_simrad.c/simrad_translate_x() makes
 * sure that (a) space is set aside for this to happen, and (b) that whenever a
 * bathy packet is read, the imagery packet is tagged (if it exists) and
 * whenever an imagery packet is read, the beams are tagged if the associated
 * bathy packet exists.  In practice, you need to request and read both
 * SOUNDING_IMAGERY and SOUNDING_BATHY packets if this is going to work, and
 * read the bathy stream every time you read an imagery packet, either until
 * you hit a match, or run out of bathy.  See utilities/dumpimagery.c for
 * an example of this in action.
 *
 * Revision 1.5  2001/02/10 18:59:42  brc
 * Added code to crack out speed-over-ground and add to the navigational
 * structure.  Also improved the attitude decimation filter construction
 * somewhat to deal with latency error problems in some files.
 *
 * Revision 1.4  2000/09/29 20:33:23  brc
 * Moved filter design from sounding code to general (since it can be used for
 * more than one purpose), and updated sounding code to use it.  Changed filter
 * to windowed ideal filter time domain design for simplicity.
 *
 * Revision 1.3  2000/09/25 20:19:05  brc
 * Major modification to raw file reading structure, and corresponding changes
 * to the remainder of the library to deal with the changes that this caused
 * at the sounding stream level.
 *
 * Revision 1.2  2000/08/31 21:33:50  brc
 * Modified test_rawrd.c to resolve a problem with uninitialised memory reading,
 * and fixed a memory leak in read_simrad.c which resulted in all of the memory
 * being allocated by simrad.c being held until termination (oops!).
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	read_simrad.c
 * Purpose:	Driver for Simrad EM series multi-beams
 * Date:	25 July 2000 (based on example read_simrad.c in BFR project)
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <limits.h>
#include <float.h>
#include "stdtypes.h"
#include "error.h"
#include "ccom_general.h"
#include "nav.h"
#include "sounding.h"
#include "sounding_private.h"
#include "sounding_raw.h"
#include "read_simrad.h"
#include "stime.h"
#include "params.h"

#undef __DEBUG__

#define ALLOW_SIMRAD
#include "simrad.h"

#define DEFAULT_MAXRESYNCH	1000
#define MIN_MAX_RESYNCH		1
#define MAX_MAX_RESYNCH		10000
#define ATT_OUTPUT_RATE		10.f	/* Output rate for attitude in Hz.*/
#define ATT_FAILURE_RATE	1.0f	/* Rate at which we suspect failure, Hz. */
#define ATT_MINIMUM_RATE	10.f	/* Rate which is only acceptable, Hz. */
#define ATT_FIR_LENGTH		51		/* Length of attitude decimation filter */
#define MAX_FIR_LEN			99
#define MIN_FIR_LEN			11
#define ATT_RATE_SLOP		0.05	/* Fractional change in rate which is
									 * acceptable between runs of the decimation
									 * FIR filter before we have to recompute
									 * the filter coefficients.
									 */
#define DEFAULT_SOG			4.116	/* Default speed over ground, if not
									 * included in position packet.  This is
									 * about 8 kts, which is reasonable under
									 * most circumstances.  Overestimation is
									 * better than underestimation.
									 */

static u32 default_maxresynch = DEFAULT_MAXRESYNCH;
static f32 att_output_rate = ATT_OUTPUT_RATE;
static f32 att_failure_rate = ATT_FAILURE_RATE;
static f32 att_minimum_rate = ATT_MINIMUM_RATE;
static u32 att_fir_length = ATT_FIR_LENGTH;
static f32 att_rate_slop = (f32)ATT_RATE_SLOP;
static f32 default_sog = (f32)DEFAULT_SOG;

#define EM_DEG2RAD(x)	((x)*M_PI/180.0)	/* Degrees to radians */
#define EM_RAD2DEG(x)	((x)*180.0/M_PI)	/* Radians to degrees */
#define EM_INT2LAT(x)	((x)/20000000.0)	/* Scaled int to dec. deg. */
#define EM_INT2LON(x)	((x)/10000000.0)	/* Scaled int to dec. deg. */
#define EM_INT2SOG(x)	((x)/100.0)			/* Speed Over Ground in cm/s to m/s */
#define EM_INT2SCALE(x)	((x)/100.0)			/* Scaled int x/y/z scale to m. */
#define EM_INT2HDNG(x)	((x)/100.0)			/* 0.01 deg. heading int to deg. */
#define EM_INT2ROLL(x)	((x)/100.0)			/* 0.01 deg. roll int to deg. */
#define EM_INT2PITCH(x)	((x)/100.0)			/* 0.01 deg. pitch int to deg. */
#define EM_INT2HEAVE(x)	((x)/100.0)			/* 0.01m heave to m. */
#define EM_INT2BKSC(x)	((x)/2.0)			/* 0.5dB backscatter int to dB */
#define EM_INT2SURFSP(x) ((x)/10.0)			/* Surface Sound Sp. in dm/s->m/s */
#define EM_ISPHASEDET(x) (((x)&0x80) != 0)	/* Phase/Amplitude detection flag */

#define SIMRAD_STX	2		/* recognition flag in simrad headers */

static char *modname = "read_simrad";
static char *modrev = "$Revision: 2 $";

/* Adapt positioning calls to compilation platform */
#ifdef sgi
#	define FTELL	ftell64
#	define FSEEK	fseek64
#else
#	define FTELL	ftell
#	define FSEEK	fseek
#endif

/* Routine:	simrad_cleanup
 * Purpose:	Release all memory associated with a simrad_t structure
 * Inputs:	*data	Pointer to the data packet
 * Outputs:	-
 * Comment:	This assumes that the simrad_t structure is itself dynamically
 *			allocated, and free()s it too!
 */

static void simrad_cleanup(simrad_t *data)
{
	//clean_simrad(data);
	free(data);
}

/* Routine:	validate_simrad
 * Purpose: Check validity of current EM format header block
 * Inputs:	*hdr	Pointer to header block
 *			maxlen	Maximum expected length for any header block
 * Outputs:	True if block looks OK, False otherwise
 * Comment:	This checks that the STX byte is right, and that the declared size
 *			of block is reasonable.
 */

Bool validate_simrad(emhdr_t *d, int maxlen)
{
	if (d->stx != SIMRAD_STX) {
		error_msgv(modname, "did not recognise STX byte (%d)\n", (u32)d->stx);
		return(False);
	}
	if (d->datasize > (u32)maxlen) {
		error_msg(modname, "warning: maximum size bigger than expected.\n");
		return(False);
	}
	return(True);
}

/* Routine:	simrad_release
 * Purpose:	Release memory associated with a simrad_t datagram
 * Inputs:	*data	Pointer to data element
 * Outputs:	-
 * Comment:	This assumes that the pointer passed *is* a simrad_t*.  If it isn't
 *			many very bad things may happen ...
 */

void simrad_release(void *data)
{
	simrad_cleanup((simrad_t*)data);
}

/* Routine:	simrad_register_releasers
 * Purpose:	Set up pointers to suitable release functions in buffer structures
 * Inputs:	*p	Pointer to RPrivate structure to fill in
 * Outputs:	-
 * Comment:	Note that this only fills in the elements that it knows about,
 *			leaving the rest NULL.  This will cause visible debug warnings
 *			elsewhere, giving a security check.
 */

void simrad_register_releasers(RPrivate *p)
{
	p->data[RAW_BATHY].release = simrad_release;
	p->data[RAW_ATTITUDE].release = free;
	p->data[RAW_IMAGERY].release = simrad_release;
}

/* Routine:	simrad_timestamp
 * Purpose:	Construct milliseconds since epoch timestamp from datagram stamps
 * Inputs:	date	Coded date on datagram
 *			ms		Milliseconds since midnight
 * Outputs:	Seconds since epoch, or <0 for error
 * Comment:	Epoch in this case means the standard Unix time epoch: 0000hrs,
 *			1 January 1970.  Note that times *must* be kept as doubles, since
 *			float has insufficient accuracy to represent millisecond times of
 *			the magnitude required.
 *				Times are always represented with respect to UTC, rather than
 *			any individual timezone.
 *				This setup ensures that the times are monotonic over midnight
 *			boundaries, etc. but exposes the code to a bug similar to the Y2K
 *			problem: eventually, the magnitude of the times will increase to
 *			such a level that there will be an insufficient number of bits in
 *			the double's mantissa to provide millisecond accuracy.  However,
 *			given that we'll need about 16 bits to give accuracy to about 10^-5
 *			this still leaves 37 bits for the integer seconds part of the
 *			time, so we're good through 6325AD (or there abouts ...)
 */

static f64 simrad_timestamp(u32 date, u32 milliseconds)
{
	f64	t;
	u32	year, month, day;
	
	/* date is YYYYMMDD */
	year = date/10000; date -= year*10000;
	month = date/100; date -= month*100;
	day = date;
	
	t = stime_make_time(year, month, day, 0, 0, 0, 0.0);
	return(t + milliseconds/1000.0);
}

/* Routine:	simrad_insert_attitude
 * Purpose:	Sub-sample high resolution attitude data, and insert into LUT
 * Inputs:	*att	Pointer to emattitude_t buffer
 *			fir		Filter coefficients to use to sub-sample data.
 *			stamp	Timestamp in ms for packet header
 *			lut		Att structure for LUT insertion
 * Outputs:	-
 * Comment:	This converts internal format attitude data into external format,
 *			runs a FIR anti-aliasing filter over each channel, and then inserts
 *			data into the attitude buffer at a rate of 10Hz.  Note that because
 *			we don't know a priori the sampling rate of the attitude data, we
 *			have to fudge a little.  That is, we use a smoothing-by-n, n-odd
 *			filter, applied a-causally, with edge reflection to provide out-of-
 *			buffer data.  The size of the filter can then be computed fairly
 *			simply in order to over-smooth the data (slightly), and then the
 *			sub-sample step is chosen to provide slightly higher than 10Hz, thus
 *			ensuring that we never have aliasing (although, admittedly, aliasing
 *			is unlikely anyway, even at 25Hz sampling of attitude ...)
 */

static void simrad_insert_attitude(emattitude_t *att, Filt *fir, f64 stamp,
								   Att lut)
{
	f32		roll[128], pitch[128], heading[128], heave[128],
			s_roll, s_pitch, s_heading, s_heave;
	s32		pos, i, j, lim;
	ematt_t	*ptr = att->attitude;

#ifdef __DEBUG__
printf("%s: attitude with %d entries (base timestamp = %lf).\n", modname,
		att->numentries, stamp);
#endif

	for (i = 0; i < att->numentries && i < 128; ++i, ++ptr) {
		roll[i] = (f32)EM_INT2ROLL(ptr->roll);
		pitch[i] = (f32)EM_INT2PITCH(ptr->pitch);
		heading[i] = (f32)EM_INT2HDNG(ptr->heading);
		heave[i] = (f32)EM_INT2HEAVE(ptr->heave);
	}
		
	/* FIR filter input, using a-causal support */
	lim = fir->len/2;
	for (i = 0; i < att->numentries; i += fir->step) {
		s_roll = s_pitch = s_heading = s_heave = 0.f;
		for (j = -lim; j <= lim; ++j) {
			pos = i+j;
			if (pos < 0) pos = -pos;
			else if (pos >= att->numentries) pos = 2*(att->numentries-1)-pos;
			s_roll += fir->fir[j+fir->len/2]*roll[pos];
			s_pitch += fir->fir[j+fir->len/2]*pitch[pos];
			s_heading += fir->fir[j+fir->len/2]*heading[pos];
			s_heave += fir->fir[j+fir->len/2]*heave[pos];
		}
		att_insert(lut, stamp+att->attitude[i].latency/1000.0,
										s_roll, s_pitch, s_heading, s_heave);
	}
}

/* Routine:	simrad_att_to_plat
 * Purpose:	Translate simrad internal format attitude to Platform structure
 * Inputs:	*att	ematt_t structure with internal attitude
 *			time	Timestamp from overall emattitude_t packet
 *			*plat	Pointer to space for output data
 * Outputs:	Returns timestamp for *this* attitude packet, offset from base.
 * Comment:	This simply rescales the various integer components in the input
 *			data packet, and writes the result into the Platform packet.
 */

static f64 simrad_att_to_plat(ematt_t *att, f64 time, Platform *plat)
{
	plat->timestamp = time + att->latency/1000.0;
	plat->roll = (f32)EM_INT2ROLL(att->roll);
	plat->pitch = (f32)EM_INT2PITCH(att->pitch);
	plat->heading = (f32)EM_INT2HDNG(att->heading);
	plat->heave = (f32)EM_INT2HEAVE(att->heave);
	return(time + att->latency/1000.0);
}

/* Routine:	simrad_breakout_attitude
 * Purpose:	Break attitude structure into a sequence of Platform elements, and
 *			store in buffer for high-res attitude data
 * Inputs:	stamp	Timestamp for the datagram packet
 *			att		Pointer to emattitude_t structure
 *			*buf	Pointer to the buffer in which to insert the data
 * Outputs:	True on success, otherwise False (not enough memory)
 * Comment:	This assumes that there is enough space in the buffer for the data
 *			in the _att_ structure.  Expect very bad things to happen if this is
 *			not the case.
 */

static Bool simrad_breakout_attitude(f64 stamp, emattitude_t *att, RBuffer *buf)
{
	u32			i, pos = buf->n_used;
	Platform	*op;
	ematt_t		*ptr = att->attitude;

	for (i = 0; i < att->numentries; ++i, ++pos, ++ptr, ++buf->n_used) {
		if ((op = (Platform*)calloc(1, sizeof(Platform))) == NULL) return(False);
		buf->epoch[pos] = simrad_att_to_plat(ptr, stamp, op);
		buf->dgram[pos] = (void*)op;
	}

#ifdef __DEBUG__
printf("%s: debug: added %d entries to attitude buffer (%d entries total, "
	"first %lf last %lf).\n", modname, att->numentries, buf->n_used,
	buf->epoch[0], buf->epoch[buf->n_used-1]);
#endif

	return(True);
}

/* Routine:	simrad_trim_buffers
 * Purpose:	Remove data before a given timestamp
 * Inputs:	*p	Pointer to RPrivate structure
 *			now	Timestamp to clip against.
 * Outputs:	-
 * Comment:	This ensures that there is nothing in the buffers before the
 *			timestamp given.  This can be used to ensure that the buffers only
 *			have data which we can actually use (e.g., if now is the timestamp
 *			on the first nav information which we receive, since anything before
 *			that time can't be interpolated).
 */

static void simrad_trim_buffers(RPrivate *p, f64 now)
{
	u32		buf, n = 0;

#ifdef __DEBUG__
printf("%s: debug: triming buffer before %lf\n", modname, now);
#endif

	for (buf = 0; buf < RAW_NBUFS; ++buf)
		if (p->data[buf].epoch != NULL) {
			n = 0;
			while (n < p->data[buf].n_used && p->data[buf].epoch[n] <= now) ++n;
			if (n != 0) sounding_raw_discard_n(p, (RBufKn)buf, n);
		}
}

/* Routine:	simrad_fix_attfir
 * Purpose:	Compute FIR filter for attitude decimation
 * Inputs:	basetime	Base datagram timestamp (s)
 *			*att		Attitude data (raw)
 *			*param		Pointer to RPrivate parameter structure
 * Outputs:	-
 * Comment:	This computes the input data sample rate, and compares it against
 *			that of the extant FIR filter; if they differ by more than one
 *			sample spacing over the length of the data stream, the FIR filter
 *			is recomputed and replaced.  It is also safe to call with a NULL
 *			pointer for the FIR filter, since the code traps this and computes
 *			a default filter.
 */

static Bool simrad_fix_attfir(f64 basetime, emattitude_t *att, RPrivate *param)
{
	u32		samp;
	f64		rate;
	Filt	*fir;
	
	/* We need to check that the latency values actually make sense, so that
	 * when we compute the input rate, we aren't fooled by wacko outliers.  This
	 * sometimes happens in Simrad datastreams, where the last value in the
	 * sequence will have a recorded latency several orders of magnitude higher
	 * than it should be.  This makes the sequence look as if it's longer than
	 * it is, and hence at lower rates.
	 *   In order to make this relatively simple, we compute the mean input
	 * rate as the average of the first differences of the latencies, and then
	 * assume that all of the points are at the same rate.  When there is error,
	 * the rate (inverse of difference) will be very small, and hence there
	 * should be very little impact on the rest of the data.  We therefore patch
	 * over the problem, but take into account all of the data, rather than
	 * assuming that the data is at fixed rate.
	 */
	rate = 0.0;
	for (samp = 1; samp < att->numentries; ++samp)
		rate += 1000.0 /* latency is in ms, so we renormalise here */ /
				(att->attitude[samp].latency - att->attitude[samp-1].latency);
	rate = rate/(att->numentries-1);

	if (rate < att_failure_rate) {
		error_msgv(modname, "attitude rate seems to be %f Hz.  Probably"
			" sensor failure.  Aborting packet.\n", rate);
		return(False);
	} else if (rate < att_minimum_rate) {
		error_msgv(modname, "attitude input rate %f Hz < %f Hz (min).  Aborting"
			" packet.\n", rate, att_minimum_rate);
		return(False);
	}
	
	/* If we don't have a filter, or the rate is significantly different,
	 * re-make the FIR to the correct specification.
	 */
	if (param->attfir == NULL || (param->attfir->ip_rate - rate)/rate > att_rate_slop) {
		if (rate < att_output_rate) {
			error_msgv(modname, "warning: attitude input rate %f Hz is lower"
				"than nominal output rate %f Hz.  Setting pass-through mode.\n",
				rate, att_output_rate);
			if ((fir = ccom_make_fir_passthrough((f32)rate)) == NULL) {
				error_msg(modname, "failed to (re)make attitude passthrough"
					" FIR filter.\n");
				if (param->attfir != NULL)
					return(True);	/* ... and keep the old one. */
				else {
					error_msg(modname, "failed making first attitude"
						" passthrough FIR filter.  Aborting packet.\n");
					return(False);
				}
			}
		} else if ((fir = ccom_make_fir((f32)rate, att_output_rate, att_fir_length))
																	== NULL) {
			error_msgv(modname, "failed to (re)make attitude FIR filter.\n");
			if (param->attfir != NULL)
				return(True);	/* ... and keep the old one. */
			else {
				error_msg(modname, "failed making first attitude decimation"
					" FIR.  Aborting packet.\n");
				return(False);
			}
		}
		ccom_release_fir(param->attfir);
		param->attfir = fir;
		error_msgv(modname, "making attitude decimation FIR at %.2f Hz input"
			" rate, output %f Hz (%d pt).\n", rate, att_output_rate,
			att_fir_length);
	}
	return(True);
}

/* Routine:	simrad_get_next
 * Purpose:	Read the next datagram out of the raw file, coping with errors and
 *			inserting nav into the buffer provided
 * Inputs:	*ip		File to read from
 *			*param	Parameter structure containing data buffers
 *			nav		Nav sequence to insert packets into
 *			att		Att sequence to insert packets into
 * Outputs:	FILE_OK if next item read, otherwise something appropriate
 * Comment:	This reads the input file until it terminates in an error (or EOF)
 *			with the proviso that the next element read is something that the
 *			user is interested in.  Any packets for which a buffer element is
 *			not available (either because there is no buffer at all, or because
 *			the user has not provided buffer space for a packet which is
 *			handled by the system) are ignored, and any nav packets are
 *			interpreted and inserted into the nav stream directly.
 */


FileError simrad_get_next(FILE *ip, RPrivate *param, Nav nav, Att att)
{
	simrad_t	*data;
	BFRtn		readcode;
	FileError	rtncode = FILE_INVALID;
	RBuffer		*buf;
	Bool		done = False;
	u32			resynch_event = 0, max_resynchs = default_maxresynch;
	u32			n_read;
	f64			time_stamp, lat, lon, sog, nav_first, nav_last, att_first,
				att_last, first;
	long		position /* in file ... not in world! */;

	if ((data = (simrad_t*)calloc(1, sizeof(simrad_t))) == NULL) {
		error_msg(modname, "failed to allocate simrad datagram input space.\n");
		return(FILE_NOMEM);
	}
	position = ftell(ip);

    // DN --- > HACK --> WILL NOT WORK

    while( 1 ) {
    /*
	while (!done && resynch_event < max_resynchs &&
			(readcode = read_simrad(ip, data, &n_read)) != BFR_FILE_END) {*/
		switch(readcode) {
			case BFR_DGRAM_OK:
				if (n_read != data->header.datasize+4) {
/*					error_msgv(modname, "datagram size (%d) doesn't agree with"
							" size declared in file header (%d).  "
							"Re-positioning.\n",
							n_read, data->header.datasize+4);*/
					/* Really ought to do this by offset from current position
					 * in one step.
					 */
					fseek(ip, position, SEEK_SET);
					fseek(ip, data->header.datasize+4, SEEK_CUR);
				}
				/* Check whether this is a datagram type that the user wants */
				if (data->id == SIMRAD_POSITION) {
					/* Extract nav and location, and insert into sequence */
					time_stamp = simrad_timestamp(data->header.date,
												  data->header.time);
					lat = EM_INT2LAT(data->datagram.position.latitude);
					lon = EM_INT2LON(data->datagram.position.longitude);
					if (data->datagram.position.speed_og == 65535) {
						/* Indicates that information is not available */
						sog = default_sog;
					} else
						sog = EM_INT2SOG(data->datagram.position.speed_og);
					if (!nav_insert_v(nav, time_stamp, lon, lat, sog)) {
						error_msg(modname, "failed to insert navigation point.\n");
						done = True;
						rtncode = FILE_INVALID;
					}
					if (nav_get_npoints(nav) == 1) {
						/* If this is the first item we've put into the buffer,
						 * we need to check that there isn't already anything in
						 * in the data buffers which pre-date the timestamp on
						 * this navigation data, since it cannot be interpolated.
						 */
						simrad_trim_buffers(param, time_stamp);
					}
				} else if (data->id == SIMRAD_ATTITUDE) {
					/* Sub-sample attitude information and insert into LUT */
					time_stamp = simrad_timestamp(data->header.date,
												  data->header.time);
					/* Compute the input navigation rate and redefine filter
					 * if required
					 */
					if (simrad_fix_attfir(time_stamp,
										  &(data->datagram.attitude), param)) {
						simrad_insert_attitude(&(data->datagram.attitude),
												param->attfir, time_stamp, att);
					} else {
						error_msgv(modname, "failed inserting attitude packet"
							" at %lf.\n", time_stamp);
					}
				}
				if ((data->id == SIMRAD_DEPTH && param->data[RAW_BATHY].dgram != NULL) ||
					(data->id == SIMRAD_IMAGERY && param->data[RAW_IMAGERY].dgram != NULL) ||
					(data->id == SIMRAD_ATTITUDE && param->data[RAW_ATTITUDE].dgram != NULL)) {
					/* Compute the bound times for the interpolation sequences
					 * so that we are sure that the data is going to be able to
					 * be interpolated when it is finally real back out of the
					 * buffer.  This resolves the problem of trying to trim the
					 * buffers when the first nav point is read (which can fail
					 * because the order of nav points, attitude points and raw
					 * data is not guaranteed).
					 */
					if (nav_get_npoints(nav) != 0 && att_get_npoints(att) != 0) {
						/* There is some data, so check timestamps */
						nav_get_bound_times(nav, &nav_first, &nav_last);
						att_get_bound_times(att, &att_first, &att_last);
						first = nav_first > att_first ? nav_first : att_first;
						time_stamp = simrad_timestamp(data->header.date,
													  data->header.time);
						if (time_stamp > first) {
							/* i.e., maximum first epoch is less than the
							 * timestamp, so we'll bound the timestamp
							 * eventually.  Mark this as good and carry on.
							 */
							rtncode = FILE_OK;
							done = True;
						} else {
							error_msgv(modname, "debug: failing data point"
								" at epoch %lf because it can't be bounded"
								" at current state of interpolation buffers.\n",
								time_stamp);
						}
					} else {
						error_msgv(modname, "debug: failing data point at"
							" epoch %lf because there is no data in the"
							" interpolation buffers.\n", time_stamp);
					}
				}
				break;
			case BFR_NO_HEADER:
			case BFR_DGRAM_READ_FAIL:
			case BFR_INVALID_TAIL:
				error_msgv(modname, "error seeking header at offset %ld.\n",
					position);
				error_msgv(modname, "attempting resynch %d.\n", modname,
					resynch_event);
				++resynch_event;
				break;
			case BFR_DGRAM_UNKNOWN:
				error_msgv(modname, "unknown datagram at offset %ld --- ignoring.\n",
					position);
				/* Return to position before read */
				fseek(ip, position, SEEK_SET);
				/* Skip over datagram (with +4 because Simrad DG's don't include
				 * the length word in the datagram length.)
				 **** Should really do this as offset from current.
				 */
				fseek(ip, data->header.datasize+4, SEEK_CUR);
				break;
			case BFR_READ_ERROR:
				error_msg(modname, "unexpected hard read error.  Aborting.\n");
				rtncode = FILE_IOERR;
				done = True;
				break;
			default:
				error_msg(modname, "internal error: unknown read return.  "
					"Aborting.\n");
				rtncode = FILE_INTERNAL;
				done = True;
				break;
		}
		if (!done) {
			//clean_simrad(data);
			memset(data, 0, sizeof(simrad_t));
			position = ftell(ip);
		}
	}
	if (readcode == BFR_FILE_END) {
		rtncode = FILE_EOF;
		free(data);
	} else if (rtncode == FILE_OK) {
		/* Got a good'un.  Extract the data and insert into buffers. */
		time_stamp = simrad_timestamp(data->header.date, data->header.time);
		switch(data->id) {
			case SIMRAD_DEPTH:
				buf = param->data + RAW_BATHY;
				if (buf->n_used+buf->minimum >= buf->n_space) {
					error_msg(modname, "internal: read with no buffer space.\n");
					rtncode = FILE_INTERNAL;
				} else {
#ifdef __DEBUG__
printf("%s: debug: inserting depth packet %d, epoch %lf.\n",
	modname, buf->n_used, time_stamp);
#endif
					buf->dgram[buf->n_used] = (void*)data;
					buf->epoch[buf->n_used++] = time_stamp;
				}
				break;
			case SIMRAD_IMAGERY:
				buf = param->data + RAW_IMAGERY;
				if (buf->n_used+buf->minimum >= buf->n_space) {
					error_msg(modname, "internal: read with no buffer space.\n");
					rtncode = FILE_INTERNAL;
				} else {
					buf->dgram[buf->n_used] = (void*)data;
					buf->epoch[buf->n_used++] = time_stamp;
				}
				break;
			case SIMRAD_ATTITUDE:
				buf = param->data + RAW_ATTITUDE;
				if (buf->n_used+buf->minimum >= buf->n_space) {
					error_msg(modname, "internal: read with no buffer space.\n");
					rtncode = FILE_INTERNAL;
				} else
					simrad_breakout_attitude(time_stamp,
												&(data->datagram.attitude), buf);
				break;
			default:
				error_msgv(modname, "internal: datagram at insertion with"
					" no corresponding array in buffers.\n");
				rtncode = FILE_INTERNAL;
				break;
		}
	}
	if (rtncode != FILE_OK && rtncode != FILE_EOF) {
		//clean_simrad(data);
		free(data);
	}
	return(rtncode);
}

/* Routine:	simrad_translate_bathy
 * Purpose:	Translate Simrad proprietry bathy data format to internal
 * Inputs:	dev		Device used in the translation
 *			proj	Projection in use on the mapsheet
 *			data	Pointer to internal private data structure
 *			orient	Orientation of the survey platform at reception
 *			ping	Internal data structure to fill in
 * Outputs:	FILE_OK if translation went OK, otherwise suitable error
 * Comment:	This translates the data structure into the internal format
 *			(assuming that the transducer locations are set), and then releases
 *			the internal format structure.  Note that error checking is done
 *			for the device and type of packet, etc.
 */

FileError simrad_translate_bathy(Device dev, Projection proj, void *data,
								 Platform *orient, Ping ping)
{
	simrad_t	*pkt = (simrad_t *)data;
	u32			beam, sbeam;
	emdepth_t	*d;
	f64			heading, xyscale, zscale, zoffset;
	Bool		found;

	if (pkt->id != SIMRAD_DEPTH) {
		error_msg(modname, "internal: translate_bathy with non-bathy packet.\n");
		simrad_cleanup(pkt);
		return(FILE_INVALID);
	}
	
	/* Check that there is enough space in the buffer for the beams */
	d = &(pkt->datagram.depth);
	if (ping->buffer_space < d->validbeams) {
		error_msgv(modname, "translate: buffer space (%d) < valid beams"
			" (%d).\n", ping->buffer_space, d->validbeams);
		simrad_cleanup(pkt);
		return(FILE_INVALID);
	}
	ping->n_beams = d->validbeams;
	ping->bathy_pingnum = d->ping;
	
	/* Translate the scaled integer values into usable numbers */
	xyscale = EM_INT2SCALE(d->xyres); zscale = -EM_INT2SCALE(d->zres);
	heading = EM_INT2HDNG(d->heading);
	zoffset = EM_INT2SCALE(d->tx_depth)+d->tx_depthoff_mult*EM_INT2SCALE(65536);
	
	/* Map surface sound speed into the orientation packet */
	orient->surf_sspeed = (f32)EM_INT2SURFSP(d->tx_soundspeed);
	orient->mean_speed = orient->surf_sspeed; /* for now .. until SVP included */
	
	/* Generate beam information */
	for (beam = 0; beam < d->validbeams; ++beam) {
		ping->beams[beam].beam_number = d->beams[beam].beamnum - 1;
			/* Note that we subtract 1 from the beam number since the beams
			 * held internally are always indexed from 0 so that they fit in
			 * with the error model beam number tables, etc.  This also makes
			 * the depth packets match with the imagery packets.
			 */
		ping->beams[beam].backscatter = (u8)EM_INT2BKSC(d->beams[beam].backscatter);
		sounding_locate_beam((f32)(d->beams[beam].x*xyscale), (f32)(d->beams[beam].y*xyscale),
						   (f32)heading, orient->longitude, orient->latitude, proj,
						   &(ping->beams[beam].east), &(ping->beams[beam].north));
		if (device_get_device(dev) == DEVICE_EM300)
			/* Depths are interpreted unsigned */
			ping->beams[beam].depth = (f32)(zscale * (*(u32*)(&(d->beams[beam].depth))));
		else
			ping->beams[beam].depth = (f32)(zscale * d->beams[beam].depth);
		ping->beams[beam].depth -= (f32)zoffset;
		ping->beams[beam].depth = -ping->beams[beam].depth;
			/* We need depths +ve for error modelling; this is corrected after
			 * the data is modelled.
			 */
		ping->beams[beam].flags = 0;	/* Initialise each round */
		ping->beams[beam].flags |= SOUNDING_PUTWINDOWSZ(d->beams[beam].detwin_len);
		if (EM_ISPHASEDET(d->beams[beam].quality))
			ping->beams[beam].flags |= SOUNDING_DETECT_MTHD_FLAG;
		else
			ping->beams[beam].flags &= ~SOUNDING_DETECT_MTHD_FLAG;
	}
	
	/* OK --- done with the data, release it! */
	simrad_cleanup(pkt);
	
	/* If the imagery and bathy ping numbers match, locate the imagery beams */
	if (ping->bathy_pingnum == ping->image_pingnum) {
		for (beam = 0; beam < ping->n_ibeams; ++beam) {
			/* First, find the beam that corresponds */
			found = False;
			for (sbeam = 0; sbeam < ping->n_beams && !found; ++sbeam) {
				if (ping->beams[sbeam].beam_number == ping->ibeams[beam].beam_number) {
					found = True;
					ping->ibeams[beam].east = ping->beams[sbeam].east;
					ping->ibeams[beam].north = ping->beams[sbeam].north;
				}
			}
			if (!found) {
				ping->ibeams[beam].east = 0.0;
				ping->ibeams[beam].north = 0.0;
			}
		}
	}
	return(FILE_OK);
}

/* Routine:	simrad_translate_attitude
 * Purpose:	Copy attitude from internal buffer into output structure
 * Inputs:	*data	Data from internal buffer store
 *			*orient	Output platform orientation with low-res attitude/position
 * Outputs:	Updates attitude information in *orient with high-res attitude
 * Comment:	This copies over the positioning information in the Platform
 *			structure from the low-res side, then replaces the attitude info
 *			from the buffered data (which is already stored internally as a
 *			Platform structure in translated format).
 */

FileError simrad_translate_attitude(void *data, Platform *orient)
{
	f64			lat, lon;
	Platform	*p = (Platform *)data;
	
	lat = orient->latitude; lon = orient->longitude;
	memcpy(orient, p, sizeof(Platform));
	orient->latitude = lat; orient->longitude = lon;
	return(FILE_OK);
}

/* Routine:	simrad_translate_imagery
 * Purpose:	Copy imagery data from raw buffer into output structure
 * Inputs:	dev		Device being used to generate the data
 *			proj	Projection parameters for current mapsheet
 *			*data	Pointer to low-level reader data structure
 *			*orient	Pointer to interpolated platform orientation space
 *			*ping	Pointer to current ping workspace
 * Outputs:	FILE_OK if the data is available, is configured, and is compatible
 *			with the current bathy ping stored in the Ping structure.  Otherwise
 *			a suitable error message.
 * Comment:	This breaks the information output of the imagery structure and into
 *			the Imagery structures.  We want to locate the beams in the imagery,
 *			but can't do this without the corresponding depth telegrams (since
 *			the imagery datagram doesn't have location).  Therefore, we need to
 *			have the depth datagram available before getting the imagery worked
 *			out.  Here, we keep the ping numbers up to date in the Ping structure
 *			and check that ping->bathy_pingnum is the same as the incoming
 *			ping number before locating beams.
 */

FileError simrad_translate_imagery(Device dev, Projection proj, void *data,
								   Platform *orient, Ping ping)
{
	simrad_t	*pkt = (simrad_t *)data;
	u32			beam, sbeam, samp;
	emseabed_t	*d;
	Bool		found;

	if (pkt->id != SIMRAD_IMAGERY) {
		error_msg(modname, "internal: translate_imagery with non-imagery"
				" packet.\n");
		simrad_cleanup(pkt);
		return(FILE_INVALID);
	}
	
	/* Check that there is enough space in the buffer for the beams */
	d = &(pkt->datagram.imagery);
	if (ping->buffer_space < d->validbeams) {
		error_msgv(modname, "translate: buffer space (%d) < valid beams"
			" (%d).\n", ping->buffer_space, d->validbeams);
		simrad_cleanup(pkt);
		return(FILE_INVALID);
	}
	ping->n_ibeams = d->validbeams;
	ping->image_pingnum = d->ping;

	/* Generate beam information */
	for (beam = 0; beam < d->validbeams; ++beam) {
		ping->ibeams[beam].beam_number = d->beamhdr[beam].beamindex;
		if (d->beamhdr[beam].sortdir == -1)
			ping->ibeams[beam].side = Beam_Port;
		else
			ping->ibeams[beam].side = Beam_Stbd;
		ping->ibeams[beam].n_samples = d->beamhdr[beam].numsamp;
		ping->ibeams[beam].central_samp = d->beamhdr[beam].detrange;
		
		/* Check if there is enough space in the buffer for the data (and
		 * reallocate if not).
		 */
		if (ping->ibeams[beam].n_buffer < d->beamhdr[beam].numsamp) {
			/* Attempt to reallocate buffer */
			s8*	tmp;
			u32	target;
			
			target = sizeof(s8)*d->beamhdr[beam].numsamp;
			if ((tmp = (s8*)realloc(ping->ibeams[beam].data, target)) == NULL) {
				error_msgv(modname, "failed allocating %d bytes for beam"
					" %d buffer.\n", target, beam);
				simrad_cleanup(pkt);
				return(FILE_NOMEM);
			}
			ping->ibeams[beam].data = tmp;
			ping->ibeams[beam].n_buffer = d->beamhdr[beam].numsamp;
		}
		for (samp = 0; samp < d->beamhdr[beam].numsamp; ++samp)
			ping->ibeams[beam].data[samp] = d->beamdata[beam][samp];
	}
	
	/* OK --- done with the data, release it! */
	simrad_cleanup(pkt);
	
	/* Locate beams if the bathy packet and imagery packet numbers match */
	if (ping->bathy_pingnum == ping->image_pingnum) {
		for (beam = 0; beam < ping->n_ibeams; ++beam) {
			/* First, find the beam that corresponds */
			found = False;
			for (sbeam = 0; sbeam < ping->n_beams && !found; ++sbeam) {
				if (ping->beams[sbeam].beam_number == ping->ibeams[beam].beam_number) {
					found = True;
					ping->ibeams[beam].east = ping->beams[sbeam].east;
					ping->ibeams[beam].north = ping->beams[sbeam].north;
				}
			}
			if (!found) {
				ping->ibeams[beam].east = 0.0;
				ping->ibeams[beam].north = 0.0;
			}
		}
	} else {
		for (beam = 0; beam < ping->n_ibeams; ++beam) {
			ping->ibeams[beam].east = 0.0;
			ping->ibeams[beam].north = 0.0;
		}
	}
	return(FILE_OK);
}

/* Routine:	read_simrad_execute_params
 * Purpose:	Execute parameters for this sub-sub-module
 * Inputs:	*list	List of ParList structures to work with
 * Outputs:	True if list was parsed correctly, otherwise False
 * Comment:	This looks for the number of resynch events to allow in the input
 *			stream, and a number of parameters to control the attitude
 *			decimation filter algorithm.
 */

typedef enum {
	RDS_UNKNOWN = 0,
	RDS_MAX_RESYNCH,
	RDS_ATT_OP_RATE,
	RDS_ATT_FAILURE_RATE,
	RDS_ATT_MIN_RATE,
	RDS_ATT_FIR_LEN,
	RDS_ATT_RATE_SLOP,
	RDS_SOG
} RdSmParamEnum;

Bool read_simrad_execute_params(ParList *list)
{
	ParTable tab[] = {
		{ "max_resynch",		RDS_MAX_RESYNCH		},
		{ "att_op_rate",		RDS_ATT_OP_RATE		},
		{ "att_failure_rate",	RDS_ATT_FAILURE_RATE},
		{ "att_min_rate",		RDS_ATT_MIN_RATE	},
		{ "att_fir_len",		RDS_ATT_FIR_LEN		},
		{ "att_rate_slop",		RDS_ATT_RATE_SLOP	},
		{ "default_sog",		RDS_SOG				},
		{ NULL,					RDS_UNKNOWN			}
	};
	ParList	*node, *match;
	u32		id;
	f64		dummy_float;
	u32		dummy_int;

	node = list;
	do {
		node = params_match(node, "sounding.raw.simrad", tab, &id, &match);
		switch (id) {
			case RDS_UNKNOWN:
				break;
			case RDS_MAX_RESYNCH:
				dummy_int = atoi(match->data);
				if (dummy_int < MIN_MAX_RESYNCH ||
												dummy_int > MAX_MAX_RESYNCH) {
					error_msgv(modname, "error: maximum number of resynchs"
						" must be in range [%d, %d] (not %d).\n",
						MIN_MAX_RESYNCH, MAX_MAX_RESYNCH, dummy_int);
					return(False);
				}
				default_maxresynch = dummy_int;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting maximum resynchs to %d (%s).\n",
	dummy_int, match->data);
#endif
				break;
			case RDS_ATT_OP_RATE:
				dummy_float = params_translate_rate(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" rate measurement.\n", match->data);
					return(False);
				}
				att_output_rate = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting attitude FIR output rate to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case RDS_ATT_FAILURE_RATE:
				dummy_float = params_translate_rate(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" rate measurement.\n", match->data);
					return(False);
				}
				att_failure_rate = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting attitude failure input rate to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case RDS_ATT_MIN_RATE:
				dummy_float = params_translate_rate(match->data);
				if (dummy_float == DBL_MAX || dummy_float < 0.0) {
					error_msgv(modname, "failed converting \"%s\" to a valid"
						" rate measurement.\n", match->data);
					return(False);
				}
				att_minimum_rate = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting attitude minimum input rate to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case RDS_ATT_FIR_LEN:
				dummy_int = atoi(match->data);
				if (dummy_int < MIN_FIR_LEN || dummy_int > MAX_FIR_LEN) {
					error_msgv(modname, "error: attitude decimation FIR filter"
						" length must be in range [%d, %d] (not %d).\n",
						MIN_FIR_LEN, MAX_FIR_LEN, dummy_int);
					return(False);
				}
				if ((dummy_int % 2) == 0) {
					error_msgv(modname, "error: attitude decimation FIR filter"
						" length must be an odd number (not %d).\n",
						dummy_int);
					return(False);
				}
				att_fir_length = dummy_int;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting attitude FIR filter length to %d (%s).\n",
	dummy_int, match->data);
#endif
				break;
			case RDS_ATT_RATE_SLOP:
				dummy_float = atof(match->data);
				if (dummy_float < 0.0 || dummy_float >= 1.0) {
					error_msgv(modname, "error: attitude decimation rate slop"
						" must be in range [0, 1) (not %f).\n", dummy_float);
					return(False);
				}
				att_rate_slop = (f32)dummy_float;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting attitude rate slop to %f (%s).\n",
	dummy_float, match->data);
#endif
				break;
			case RDS_SOG:
				dummy_float = atof(match->data);
				if (dummy_float <= 0.0 || dummy_float >= 15.4) {
					/* 15.4 m/s == 30 kts */
					error_msgv(modname, "error: ship's speed-over-ground must"
						" be in range (0.0, 15.4] m/s (not %.1f m/s).\n",
						dummy_float);
					return(False);
				}
				default_sog = (f32)dummy_float;
				match->used = True;
				break;
			default:
				error_msgv(modname, "error: unknown return from parameter"
					" matching module (%d).\n", id);
				return(False);
				break;
		}
	} while (node != NULL);
	
	/* Check that the attitude decimation filter rates make sense wrt each
	 * other: we need failure <= min <= output.
	 */
	if (att_failure_rate > att_minimum_rate ||
										att_minimum_rate > att_output_rate) {
		error_msgv(modname, "error: attitude decimation FIR rates are not"
			" consistent (need failure <= min <= output (have %.1f <= %.1f <="
			" %.1f Hz).\n", att_failure_rate, att_minimum_rate, att_output_rate);
		error_msg(modname, "warning: resetting attitude decimation FIR rates"
			" to hard defaults.\n");
		att_failure_rate = ATT_FAILURE_RATE;
		att_minimum_rate = ATT_MINIMUM_RATE;
		att_output_rate = ATT_OUTPUT_RATE;
	}
	return(True);
}
