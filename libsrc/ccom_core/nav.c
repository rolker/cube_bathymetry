/*
 * $Id: nav.c 24 2006-01-20 16:23:59Z brc $
 * $Log$
 * Revision 1.2  2006/01/20 16:23:59  brc
 * Fixed bug in nav_parse_gga() that cause the E/W indicator in GGA strings
 * not be correctly parsed.
 *
 * Revision 1.1.1.1  2003/02/03 20:18:44  brc
 * This is the re-organized distribution of libccom (a.k.a. CUBE),
 * which has a more realistic structure for future development.  The
 * code re-organization and build system was contributed by IVS
 * (www.ivs.unb.ca).
 *
 * Revision 1.2.4.1  2003/01/28 14:29:27  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.11.2.1  2002/07/14 02:20:47  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.11  2002/03/14 04:07:16  brc
 * Added code to compute approximate speed-over-ground using navigation
 * positions projected into a suitable ground coordinate space.  Of course,
 * this is likely to be rather noisy ...  Speeds are reported in m/s.
 *
 * Revision 1.10  2001/12/20 00:41:35  roland
 * Made a few fixes to make it compile under Linux
 *
 * Revision 1.9  2001/09/23 17:56:15  brc
 * Added facility to specify latency in navigation data as a parameter to the
 * nav module.  This allows the user to correct for nav offsets as the nav data
 * is loaded or inserted.  Default latency 0.0s.
 *
 * Revision 1.8  2001/05/14 04:22:22  brc
 * Updated to make module 'params'-aware.
 *
 * Revision 1.7  2001/02/26 03:18:27  brc
 * Fixed bug which included strings.h (which works for some reason!) but not
 * string.h, needed for GGA parser.
 *
 * Revision 1.6  2001/02/10 18:15:35  brc
 * Added entries in interpolation structure for the speed-over-ground data from
 * the GPS strings (or otherwise), which is required for full MBES error
 * modeling code.  Modified call interface to allow for this data to be requested
 * if the calling code requires it, but also put in wrappers so that legacy code
 * gets just the positioning information as before.
 *
 * Revision 1.5  2000/12/07 22:29:42  brc
 * Fixed problems with being a little too zealous with u8p's in GGA parsing
 * (compiler complains about not being able to map functionally identical
 * pointers!)
 *
 * Revision 1.4  2000/11/14 22:45:54  brc
 * Added facility to parse NEMA GGA strings for timestamp and position, and a
 * wrapper around this and nav_insert() to allow GGA strings to be inserted
 * directly.
 *
 * Revision 1.3  2000/09/24 01:24:45  brc
 * Fixed initialisation bug (last_soln was not initialised properly).
 *
 * Revision 1.2  2000/09/07 21:06:13  brc
 * Modified nav.{c,h} to allow the navigation data to be read back by the
 * user.  This is required if we need to convert the nav back to ASCII form.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:25  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	nav.c
 * Purpose:	Load and interpolate navigation information
 * Date:	13 July 2000
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
#include <limits.h>
#include <float.h>
#include "stdtypes.h"
#include "nav.h"
#include "error.h"
#include "ccom_general.h"
#include "params.h"
#include "projection.h"

static char *modname = "nav";
static char *modrev = "$Revision: 24 $";

#undef __DEBUG__

#define DEFAULT_BUFFER_QUANTUM 1024
#define DEFAULT_NAV_LATENCY		0.0

static u32 buffer_quantum = DEFAULT_BUFFER_QUANTUM;
static f64 nav_latency = DEFAULT_NAV_LATENCY;

#ifdef sgi
#	define FSEEK fseek64
#	define FTELL ftell64
#else
#	define FSEEK fseek
#	define FTELL ftell
#endif

#ifdef sgi
#	undef SWAP_BYTES
#else
#	define SWAP_BYTES
#endif

typedef struct _navpt {
	f64	epoch;
	f64	lon, lat;
	f64	sog;		/* Speed over ground, m/s */
} NavPt;

typedef struct _nav {
	u32		n_points;	/* Number of points used in buffer */
	u32		n_buffer;	/* Number of buffer spaces available */
	NavPt	*lut;
	u32		last_soln;	/* Solution point for last run of interpolation */
} sNav /* , *Nav */;

typedef struct {
	f64	lat, lon;
	u32	time;
	u16	hsecs;
	f32	azi;
	f32	velocity;
	u16	depth;
	u16	flag[4];
} jcu_nav;

void nav_release(Nav nav)
{
	free(nav->lut);
	free(nav);
}

static Nav nav_new_n(u32 n_points)
{
	Nav	rtn;
	
	if ((rtn = (Nav)calloc(1, sizeof(sNav))) == NULL)
		return(NULL);
	if ((rtn->lut = (NavPt*)malloc(sizeof(NavPt)*n_points)) == NULL) {
		free(rtn);
		return(NULL);
	}
	rtn->n_points = 0;
	rtn->n_buffer = n_points;
	rtn->last_soln = 0;
	return(rtn);
}

Nav nav_new(void)
{
	Nav		rtn;
	
	if ((rtn = nav_new_n(buffer_quantum)) == NULL) {
		error_msg(modname, "failed allocating default quantum buffer.\n");
		return(NULL);
	}
	return(rtn);
}
	
Nav nav_new_from_omg(const char *name)
{
	FILE	*f;
	Nav		rtn;
	jcu_nav	buffer;
	u32		n_points, point;
#ifdef sgi
	long long	position = 0LL;
#else
	long	position= 0L;
#endif
	
	if ((f = fopen(name, "rb")) == NULL) {
		error_msgv(modname, "failed to open \"%s\" for reading.\n",
			name);
		return(NULL);
	}
	/* Compute number of points in the file */
	FSEEK(f, position, SEEK_END);	/* EOF */
	position = FTELL(f);
	rewind(f);
	n_points = (u32)(position/sizeof(jcu_nav));
	if ((rtn = nav_new_n(n_points)) == NULL) {
		error_msg(modname, "failed getting memory.\n");
		fclose(f);
		return(NULL);
	}
	for (point = 0; point < n_points; ++point) {
		if (fread(&buffer, sizeof(jcu_nav), 1, f) != 1) {
			error_msgv(modname, "failed reading nav point %d.\n",
				point+1);
			fclose(f);
			nav_release(rtn);
			return(NULL);
		}
#ifdef SWAP_BYTES
		ccom_swap_4(&buffer.time);
		ccom_swap_4(&buffer.velocity);
		ccom_swap_2(&buffer.hsecs);
		ccom_swap_8(&buffer.lon);
		ccom_swap_8(&buffer.lat);
#endif
		rtn->lut[point].epoch = buffer.time + (double)buffer.hsecs/100.0
								- nav_latency;
		rtn->lut[point].lon = buffer.lon;
		rtn->lut[point].lat = buffer.lat;
		rtn->lut[point].sog = buffer.velocity;
	}
	rtn->n_points = n_points;
	error_msgv(modname, "epoch start = %lf (l,l) = (%lf, %lf) end = %lf"
		" (lon,lat) = (%lf, %lf)\n",
		rtn->lut[0].epoch, rtn->lut[0].lon, rtn->lut[0].lat,
		rtn->lut[n_points-1].epoch,
		rtn->lut[n_points-1].lon, rtn->lut[n_points-1].lat);
	fclose(f);
	return(rtn);
}

Bool nav_insert_v(Nav nav, f64 epoch, f64 lon, f64 lat, f64 sog)
{
	u32		target;
	NavPt	*tmp;
	
	if (nav->n_points >= nav->n_buffer) {
		/* Need to re-allocate buffer for more space */
		target = (nav->n_buffer + buffer_quantum)*sizeof(NavPt);
		if ((tmp = (NavPt*)realloc(nav->lut, target)) == NULL) {
			error_msgv(modname, "failed to increase nav buffer by %d points "
				"from %d.\n", buffer_quantum, nav->n_points);
			return(False);
		}
		nav->lut = tmp;
		nav->n_buffer += buffer_quantum;
	}
	nav->lut[nav->n_points].epoch = epoch - nav_latency;
	nav->lut[nav->n_points].lon = lon;
	nav->lut[nav->n_points].lat = lat;
	nav->lut[nav->n_points].sog = sog;
	++nav->n_points;
	return(True);
}

Bool nav_insert(Nav nav, f64 epoch, f64 lon, f64 lat)
{
	return(nav_insert_v(nav, epoch, lon, lat, 0.0));
}

Bool nav_interp_v(Nav nav, f64 epoch, f64p lon, f64p lat, f64p sog)
{
	u32	left, right, mid;
	f64	scale_left, scale_right;

	if (nav->n_points < 2) {
		error_msgv(modname, "can't interpolate with %d points.\n", nav->n_points);
		return(False);
	}
	if (epoch < nav->lut[0].epoch || epoch > nav->lut[nav->n_points-1].epoch) {
		error_msgv(modname, "time %lf is outside nav range %lf--%lf.\n",
			epoch, nav->lut[0].epoch, nav->lut[nav->n_points-1].epoch);
		return(False);
	}
	/* Check whether the time is in the same bracket as the previous solution */
	if (nav->lut[nav->last_soln].epoch < epoch &&
		epoch < nav->lut[nav->last_soln+1].epoch) {
		left = nav->last_soln;
	} else if (nav->last_soln < nav->n_points-2 &&
			   nav->lut[nav->last_soln+1].epoch < epoch &&
			   epoch < nav->lut[nav->last_soln+2].epoch) {
		/* Normal linear sequence -- we've move forward one bracket */
		left = nav->last_soln+1;
	} else {
		/* OK: two test cases for usual call sequence (i.e., monotonic
		 * increase in time) didn't work, so now we need to find the elements
		 * which bracket the time from scratch.  To make things a little more
		 * efficient, we use a bracketed bisection (almost a binary search -
		 * except here the key isn't actually in the table, and we look for the
		 * entry with the closest smaller ping epoch --- i.e., we look for
		 * arg max { X : x_i \sup E, x_i < e, 0 <= i < N_i }, where e is the
		 * search epoch, and E = {e_i, 0 <= i < N_i} are the nav epochs).
		 */
		left = 0; right = nav->n_points-2;
		while (right-left > 1) {
			mid = (left + right)/2;
			if (nav->lut[mid].epoch <= epoch) {
				/* Keep the right hand half */
				left = mid;
			} else {
				/* Keep the left hand half */
				right = mid;
			}
		}
	}

	/* Now we need do the linear interpolation */
	right = left + 1; /* NB: we are guaranteed that we will not exceed the
				 * array bounds by initial limits check */
	scale_left = (nav->lut[right].epoch - epoch)/
				(nav->lut[right].epoch - nav->lut[left].epoch);
	scale_right = (epoch - nav->lut[left].epoch)/
				(nav->lut[right].epoch - nav->lut[left].epoch);
	*lon = scale_left*nav->lut[left].lon +
		 	scale_right*nav->lut[right].lon;
	*lat = scale_left*nav->lut[left].lat +
		 	scale_right*nav->lut[right].lat;
	*sog = scale_left*nav->lut[left].sog +
			scale_right*nav->lut[right].sog;

	/* Finally, update solution point in hope of linear shift */
	nav->last_soln = left;

	return(True);
}

Bool nav_interp(Nav nav, f64 epoch, f64p lon, f64p lat)
{
	f64	sog;
	
	return(nav_interp_v(nav, epoch, lon, lat, &sog));
}

void nav_get_bound_times(Nav nav, f64p start, f64p end)
{
	if (nav->n_points == 0) {
		*start = 0.0;
		*end = 0.0;
	} else {
		*start = nav->lut[0].epoch;
		*end = nav->lut[nav->n_points-1].epoch;
	}
}

Bool nav_get_point_n_v(Nav nav, u32 n, f64p epoch, f64p lon, f64p lat, f64p sog)
{
	if (n >= nav->n_points) {
		error_msgv(modname, "point %d requested; only %d available.\n",
					n, nav->n_points);
		return(False);
	}
	*lat = nav->lut[n].lat;
	*lon = nav->lut[n].lon;
	*epoch = nav->lut[n].epoch;
	*sog = nav->lut[n].sog;
	return(True);
}

Bool nav_get_point_n(Nav nav, u32 n, f64p epoch, f64p lon, f64p lat)
{
	f64	sog;
	
	return(nav_get_point_n_v(nav, n, epoch, lon, lat, &sog));
}

u32 nav_get_npoints(Nav nav)
{
	return(nav->n_points);
}

/* Routine:	nav_parse_gga
 * Purpose:	Parse a NEMA 2.0 GGA string into Latitude, Longitude & Time
 * Inputs:	*gga	GGA string (see comment)
 * Outputs:	epoch	Timestamp from GGA string (UTC time)
 *			lat		Latitude string from GGA as decimal degrees
 *			lon		Longitude string from GGA as decimal degrees
 * Comment:	This expects the GGA string to be of the following format:
 *				$??GGA,T[T][.T[T]],D[D]MM[.M[M]],(N|S),[D]DMM[.M[M]],(E|W),*
 *			For example:
 *				$INGGA,203521.7816,4304.51332,N,07042.57241,W,2,09,1.1,1.4, ...
 *			of which just the time, latitude and longitude are retained.  As
 *			usual, northern and eastern hemispheres are taken positive.
 */

Bool nav_parse_gga(u8 *gga, f64p epoch, f64p lon, f64p lat)
{
	char	*epoch_p, *lon_p, *ew_p, *lat_p, *ns_p;
	f64	raw_epoch, raw_lon, raw_lat;
	u32	lon_deg, lat_deg;
	u32	hr, min;
	
	/* Check that we have got a valid GGA string */
	if (gga[0] != '$' || strncmp((char *)gga+3, "GGA", 3) != 0) {
		error_msgv(modname, "string \"%s\" is not valid GGA format.\n",
			gga);
		return(False);
	}
	epoch_p = strchr((char *)gga, ',');
	lat_p = strchr(++epoch_p, ',');
	ns_p = strchr(++lat_p, ',');
	lon_p = strchr(++ns_p, ',');
	ew_p = strchr(++lon_p, ',');
	++ew_p;
	
	sscanf(epoch_p, "%lf,", &raw_epoch);
	sscanf(lat_p, "%lf,", &raw_lat);
	sscanf(lon_p, "%lf,", &raw_lon);
	
	/* Epoch is given as HHMMSS.SSS rather than as seconds, so we need to
	 * covert to seconds past midnight.
	 */
	hr = (u32)floor(raw_epoch/10000.0);
	raw_epoch -= hr*10000.0;
	min = (u32)floor(raw_epoch/100.0);
	raw_epoch -= min*100.0;
	*epoch = (hr*60.0 + min)*60.0 + raw_epoch;
	
	/* Lon/Lat have degrees*100 + decimal minutes, so we need to do some
	 * reformating to get to decimal degrees ...
	 */
	lon_deg = (u32)(raw_lon/100.0);
	*lon = lon_deg + (raw_lon - (lon_deg*100))/60.0;
	if (*ew_p == 'W') *lon = -*lon;
	
	lat_deg = (u32)(raw_lat/100.0);
	*lat = lat_deg + (raw_lat - (lat_deg*100))/60.0;
	if (*ns_p == 'S') *lat = -*lat;

	return(True);
}

/* Routine:	nav_insert_gga
 * Purpose:	Insert navigation information direct from a GPS GGA string
 * Inputs:	nav		Nav structure to append information to
 *			*gga	String to parse data from
 * Outputs:	True if string was parsed and inserted; False otherwise
 * Comment:	This uses nav_parse_gga() to parse the string, and nav_insert() to
 *			add it to the navigational buffer.  Return code is as returned by
 *			these two calls, or True if both return True
 */

Bool nav_insert_gga(Nav nav, u8p gga)
{
	f64	epoch, lon, lat;
	
	if (!nav_parse_gga(gga, &epoch, &lon, &lat)) {
		error_msg(modname, "rejecting data point (GGA not converted).\n");
		return(False);
	}
	if (!nav_insert(nav, epoch, lon, lat)) {
		error_msg(modname, "rejecting data point (nav not inserted).\n");
		return(False);
	}
	return(True);
}

/* Routine:	nav_compute_sog
 * Purpose:	Compute SoG from Nav points
 * Inputs:	nav		Nav structure to work from
 *			proj	Projection structure to use for computations
 * Outputs:	-
 * Comment:	This computes an approximation to Speed-Over-Ground from the
 *			navigation information in the interpolation buffer.  This is done
 *			by forward differencing on projected coordinates, with the end-point
 *			set equal to the penultimate point.  Speed-Over-Ground is computed
 *			in m/s.
 */

void nav_compute_sog(Nav nav, Projection proj)
{
	f64		e1, n1, e2, n2, dist, t1, t;
	u32		n;
	
	projection_ll_to_en_deg(proj, nav->lut[0].lon, nav->lut[0].lat, &e1, &n1);
	t1 = nav->lut[0].epoch;
	
	for (n = 0; n < nav->n_points-1; ++n) {
		projection_ll_to_en_deg(proj,
								nav->lut[n+1].lon, nav->lut[n+1].lat,
								&e2, &n2);
		dist = sqrt((e2-e1)*(e2-e1)+(n2-n1)*(n2-n1));
		t = nav->lut[n+1].epoch - t1;
		nav->lut[n].sog = dist / t;
		e1 = e2; n1 = n2; t1 = nav->lut[n+1].epoch;
	}
	
	nav->lut[nav->n_points-1].sog = nav->lut[nav->n_points-2].sog;
}

/* Routine:	nav_execute_params
 * Purpose:	Execute parameters list in this module
 * Inputs:	*list	List to parameters to work from
 * Outputs:	True if parameters used were OK, otherwise False
 * Comment:	Note that not finding a parameter is not a major problem; the only
 *			difficulty is if one is found, and it's wrong!
 */

typedef enum {
	NAV_UNKNOWN = 0,
	NAV_BUFFER_QUANT,
	NAV_LATENCY
} NavParamEnum;

Bool nav_execute_params(ParList *list)
{
	ParTable	tab[] = {
		{ "buffer_quantum",	NAV_BUFFER_QUANT },
		{ "latency",		NAV_LATENCY },
		{ NULL,				NAV_UNKNOWN }
	};
	ParList	*node, *match;
	u32		id;
	s32		quantum;
	f64		latency;
	
	node = list;
	do {
		node = params_match(node, "nav", tab, &id, &match);
		switch (id) {
			case NAV_UNKNOWN:
				/* Matched nothing ... but that may just mean that there's
				 * nothing there for this module.
				 */
				break;
			case NAV_BUFFER_QUANT:
				quantum = atoi(match->data);
				if (quantum <= 0 || quantum > 10*DEFAULT_BUFFER_QUANTUM) {
					error_msgv(modname, "buffer quantum (%d) out of range."
						" Switching back to default.\n", quantum);
					return(False);
				}
				buffer_quantum = quantum;
				match->used = True;

#ifdef __DEBUG__
error_msgv(modname, "debug: setting buffer quantum to %d.\n", buffer_quantum);
#endif

				break;
			case NAV_LATENCY:
				latency = params_translate_duration(match->data);
				if (latency == DBL_MAX) {
					error_msgv(modname, "error: could not convert \"%s\" as "
						"a time duration.\n", match->data);
					return(False);
				}
				nav_latency = latency;
				match->used = True;
#ifdef __DEBUG__
error_msgv(modname, "debug: setting navigation system latency to %lf s.\n",
	nav_latency);
#endif
				break;
			default:
				error_msgv(modname, "error: unknown return from parameter"
					" matching module (%d).\n", id);
				return(False);
				break;
		}
	} while (node != NULL);
	return(True);
}
