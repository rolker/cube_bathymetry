/*
 * $Id: sounding_private.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.7  2002/03/14 04:15:12  brc
 * Added stream types for CARIS/HIPS(HDCS) data and modifications to allow dual-
 * head error models and vessel descriptions to be held as part of the stream
 * descriptor.
 *
 * Revision 1.6  2001/04/10 23:29:33  brc
 * Added internal type for GSF raw files, parameters for the depth/angle gates
 * and a prototype for the beam solution code in sounding.c
 *
 * Revision 1.5  2001/02/11 17:56:14  brc
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
 * Revision 1.4  2001/02/10 19:12:20  brc
 * Added includes for the vessel and error model descriptions, and a slot for
 * the error model in the private parameters structure for the stream.
 *
 * Revision 1.3  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
 *
 * Revision 1.2  2000/09/25 20:19:05  brc
 * Major modification to raw file reading structure, and corresponding changes
 * to the remainder of the library to deal with the changes that this caused
 * at the sounding stream level.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	sounding_private.h
 * Purpose:	Private types and prototypes for soundings code
 * Date:	14 July 2000
 *
 * (c) Center for Coastal and Ocean Mapping, University of New Hampshire, 2000.
 *     This file is unpublished source code of the University of New Hampshire,
 *     and may only be disposed of under the terms of the software license
 *     supplied with the source-code distribution.
 */

#ifndef __SOUNDING_PRIVATE_H__
#define __SOUNDING_PRIVATE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "stdtypes.h"
#include "sounding.h"
#include "projection.h"
#include "device.h"
#include "vessel.h"
#include "errmod.h"

typedef struct _ping {
	u32			n_beams;		/* Number of beams in the sounding buffer */
	u32			n_ibeams;		/* Number of beams in the imagery buffer */
	u32			bathy_pingnum;	/* ID sequence number for the bathy info */
	u32			image_pingnum;	/* ID sequence number for the imagery info */
	u16			file_id;		/* File ID to allow trace-back */
	u32			buffer_space;	/* Space in the following buffers */
	Sounding	*beams;			/* Data associated with this ping */
	Imagery		*ibeams;		/* Imagery associated with this ping */
} sPing, *Ping;

typedef enum {
	SOUNDING_FILE_UNKNOWN = 0,
	SOUNDING_FILE_OMG,		/* An OMG HDCS variant file */
	SOUNDING_FILE_PLAIN,	/* A plain file of RawSounding structures */
	SOUNDING_FILE_NATIVE,	/* A file with ping information and Soundings */
	SOUNDING_FILE_RAW,		/* A file with Raw datagrams */
	SOUNDING_FILE_GSF,		/* A file in GSF V1.10 format */
	SOUNDING_FILE_HIPS		/* A directory structure in HIPS format */
} SndFileType;

typedef void (*SRelFunc)(void *);

typedef struct _sounding_hdr {
	FILE		*fd;			/* File descriptor for open file */
	SndFileType	type;			/* File type being read/written */
	s32			ping_num;		/* Space to remember which ping was last read */
	
	/* Buffer space for the current ping */
	Ping		current;
	
	/* Filter Parameters for Simple Depth and Angle Gates */
	f32			min_depth, max_depth;
	f32			min_angle, max_angle;
	
	/* Protected variables required during the reading process */
	u32			data_avail;		/* Data available from the stream */
	void		*private;		/* Private storage for reader module */
	SRelFunc	release_func;	/* Private release function (or NULL) */
	Projection	proj;			/* Projection for map-sheet */
	Device		device;			/* Data generator associated with stream */
	ErrMod		errormodel;		/* Parameters for error attribution code */
	ErrMod		port_errmod;	/* Parameters for port head in dual-head mode */
	ErrMod		stbd_errmod;	/* Parameters for stbd head in dual-head mode */
	Vessel		port_vsl;		/* Port head/primary head vessel definition */
	Vessel		stbd_vsl;		/* Starboard Head description if required */
} Hdr, *Header /*, *SoundingStream */;

/* Routine:	sounding_construct_arc
 * Purpose:	Solves the direct problem in geodesy (locates points relative
 *			to each other)
 * Inputs:	*lon	Longitude of the initial point (see comment)
 *			*lat	Lattitude of the initial point
 *			length	Length of the vector to the next point
 *			azimuth	Azimuth of the next point from the initial point
 * Outputs:	*lon, *lat are updated with the location of the next point
 * Comment:	This code is based on Semme's CGeoReference class, transformed here
 *			into C and encapsulated.  Note that this routine is in the global
 *			namespace, but is only prototyped in sounding_private.h so that
 *			it is only `known' internal to the sounding_*.c routines.
 *			Original comment is:
 *
 *	CGeoReference::ConstructArc solves the direct problem in geodesy, that 
 *	is: given the coordinates of point p1, plus the azimuth and distance 
 *	to a second point -- compute the coordinates of the second point
 *	the solution is iterated untill the change in sigma is less than DEL
 *
 *	Note the ugly coding and ungainly variable names -- due to direct 
 *	translation from Fortran -- You should've seen the original!!
 *
 *	REFERENCE  T. VINCENTY - SURVEY REVIEW, APRIL 1975 
 *	Original Fortran source GLOB by Inge Nesbo, Jan 1976.
 *
 *			Note that the input lattitude, longitude and azimuth must be in
 *			radians, rather than degrees, and that this system only works for
 *			WGS84 due to the embedded constants for mean radius of the earth
 *			eccentricity, etc.
 */

extern void sounding_construct_arc(f64p lon, f64p lat, f64 length, f64 azimuth);

/* Routine:	sounding_locate_beam
 * Purpose:	Determine the location of a beam given transducer position and
 *			offset relative to transducer heading
 * Inputs:	x, y			Offset relative to transducer
 *			heading			Heading CW from N, dec. deg.
 *			t_lon, t_lat	Transducer longitude & lattitude
 *			proj			Projection in use on mapsheet
 * Outputs:	*b_east, *b_north	Beam projected location
 * Comment:	This uses code from Luciano to fix up aximuth from heading and
 *			offset, and code from Semme to compute the position of the beam
 *			using a standard solution to `the direct problem in geodesy' (it
 *			says here).  Consequence: don't have to project beams to compute
 *			the locate of the end (it's all done in geographic coordinates).
 *				This routine is in the global namespace, but is only prototyped
 *			in sounding_private.h, so that (like sounding_construct_arc()) it
 *			is only available to the sounding_*.c files.  Typically, code would
 *			use this as a wrapper around sounding_construct_arc().
 */

extern void sounding_locate_beam(f32 x, f32 y, f32 heading, f64 t_lon,
								 f64 t_lat, Projection proj,
								 f64p b_east, f64p b_north);

#ifdef __cplusplus
}
#endif

#endif
