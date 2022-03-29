/*
 * $Id: sounding.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:45  brc
 * Initial revision
 *
 * Revision 1.1.4.1  2003/01/28 14:30:14  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.13.2.2  2002/12/15 01:45:50  brc
 * Added prototype for the dual-head vessel construction call.
 *
 * Revision 1.13.2.1  2002/07/14 02:20:37  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.13  2002/06/15 03:28:55  brc
 * Added more defines for the flags word --- allowed up to eight bits of flags
 * above the window size to allow for vendor flags to be passed through into
 * the sounding stream.  Currently, the implementation of pass-through is
 * restricted to the HIPS/IO library, since it is the only one to reliably
 * report variable numbers of flags.  Extension to the OMG/HDCS formats could
 * be made too.
 *
 * Revision 1.12  2002/03/14 04:16:02  brc
 * Added data type for CARIS/HIPS(HDCS) module and prototype for
 * sounding_get_projection().
 *
 * Revision 1.11  2001/12/07 21:18:12  brc
 * Added enum for the various types of sounding files known, and a prototype
 * for sounding_new_from().  This allows the user to request a file be opened,
 * knowing it to be a particular type, without having to use the type-specific
 * open functions.
 *
 * Revision 1.10  2001/09/23 19:08:14  brc
 * Added code to re-incorporte the SOUNDING_NATIVE method as a useful entity!
 * This now does a basic dump of all of the components that are in memory for
 * a single ping, which allows the code to dump a summary of everything required
 * for integrating data in one file (including error assignments).  This was
 * intended to support large-area survey where the same data file may have to
 * be offered to multiple mapsheets, and it would be inefficient to read and
 * translate a raw datagram stream each time (it might even be impossible).
 *
 * Revision 1.9  2001/06/03 01:10:07  brc
 * Added interface to allow external callers to get at the point location
 * code (geodetic arc construction).
 *
 * Revision 1.8  2001/05/14 04:23:28  brc
 * Updated to make modules 'params'-aware.
 *
 * Revision 1.7  2001/04/10 23:26:23  brc
 * Added prototypes for GSF file reading and depth/angle gate set.
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
 * Revision 1.5  2001/02/10 19:03:42  brc
 * Added types and prototypes to support the full error modeling code (dr, dz
 * in sounding structure, comments and modified prototypes for startup code).
 *
 * Revision 1.4  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
 *
 * Revision 1.3  2000/09/25 20:19:05  brc
 * Major modification to raw file reading structure, and corresponding changes
 * to the remainder of the library to deal with the changes that this caused
 * at the sounding stream level.
 *
 * Revision 1.2  2000/09/07 21:13:43  brc
 * Removed .install*: mistake in original import.
 *
 * Revision 1.1.1.1  2000/08/10 15:53:26  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	sounding.h
 * Purpose:	Type to hold a sounding, and routines to generate them
 * Date:	8 July 2000
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

#ifndef __SOUNDING_H__
#define __SOUNDING_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "stdtypes.h"
#include "projection.h"
#include "error.h"
#include "nav.h"

/* In theory, this should be opaque, and have the user call access routines
 * to access the contents.  However, since access of this structure will be
 * done *very* frequently, this would probably cause a significant slow-down.
 * Hence, the interface is exposed, but should be considered read-only.
 */
typedef struct {
	u16	file_id;
	u8	beam_number;
	u8	backscatter;
	f64	east,
		north;
	f32	depth, range;	/* Depth in the two standard forms */
	f32	dz,				/* Base vertical error at nominal boresight */
		dr;				/* Base horizontal error at nominal boresight */
	u32	flags;			/* Phase/Amplitude detection, quality, etc. */
} Sounding;	

#define SOUNDING_DETECT_MTHD_FLAG 0x00000001 /* not set => amplitude */
#define SOUNDING_DETECT_MTHD_SHFT 0			 /* Shift RIGHT to LSB */
#define SOUNDING_ISAMPDET(x) ((Bool)(((x)&SOUNDING_DETECT_MTHD_FLAG)==0))

#define SOUNDING_WINDOW_SZ   0x000001fe	/* 8-bit count #samples in beam */
#define SOUNDING_WINDOW_SHFT 1			/* Shift RIGHT to LSB */
#define SOUNDING_GETWINDOWSZ(x) ((u8)(((x)&SOUNDING_WINDOW_SZ)>>SOUNDING_WINDOW_SHFT))
#define SOUNDING_PUTWINDOWSZ(x) ((((u32)(x))<<SOUNDING_WINDOW_SHFT)&SOUNDING_WINDOW_SZ)

#define SOUNDING_VFLAGS_SZ 		0x0001fe00	/* 8-bits of vendor flags */
#define SOUNDING_VFLAGS_SHIFT	9
#define SOUNDING_GET_VFLAGS(x)	((u8)(((x)&SOUNDING_VFLAGS_SZ)>>SOUNDING_VFLAGS_SHIFT))
#define SOUNDING_PUT_VFLAGS(x)	((((u32)(x))<<SOUNDING_VFLAGS_SHIFT)&SOUNDING_VFLAGS_SZ)

/* Same argument as the Sounding structure --- too slow to go through an
 * interface call sequence.  This encapsulates information on the orientation
 * of the survey platform at the instant that the ping was received (or as
 * close as possible due to asynchronicity of the information streams).
 */

typedef struct {
	f64	timestamp;		/* seconds (with ms accuracy) since 00:00 01/01/1970 */
	f64	latitude;		/* Latitude in degrees */
	f64	longitude;		/* Longitude in degrees */
	f32	roll;			/* Roll in degrees, +ve is port side up */
	f32	pitch;			/* Pitch in degrees, +ve is bow up */
	f32	heading;		/* Heading in degrees, +ve CW from N */
	f32	heave;			/* Heave in meters, +ve down */
	f32	surf_sspeed;	/* Surface sound speed, m/s */
	f32	mean_speed;		/* Geometric mean equivalent sound speed, m/s */
	f32	vessel_speed;	/* Vessel's speed-over-ground, m/s */
} Platform;

/* Same argument as the Sounding and Platform structures --- too slow to go
 * through an interface call sequence.  This encapsulates information about the
 * backscatter imagery from the survey device.
 */

typedef enum {
	Beam_Port,
	Beam_Stbd
} Side;

typedef struct {
	u8		beam_number;	/* Beam ID number (0..N-1) */
	f64		east, north;	/* Projected beam location (or (0,0) if not avail.) */
	Side	side;			/* Sorting direction: Port or Stbd. */
	u16		n_samples;		/* Number of samples in data series */
	u16		central_samp;	/* Sample picked as center of beam (MB only) */
	u16		n_buffer;		/* Samples available in buffer data */
	s8		*data;			/* Buffer space for dB encoded data */
} Imagery;

#include "device.h"
#include "vessel.h"
#include "errmod.h"

typedef enum {
	SOUNDING_UNKNOWN = 0,	/* Unknown or unset */
	SOUNDING_BATHY = 1,		/* Provide bathymetry information */
	SOUNDING_ATTITUDE = 2,	/* Provide attitude information */
	SOUNDING_IMAGERY = 4	/* Provide imagery information */
} SStreamTypes;

typedef struct _sounding_hdr	*SoundingStream;

/*
 * Enum indicating the type of input file being read.
 */
typedef enum {
	SND_IP_UNKNOWN = 0,		/* Input stream type unknown */
	SND_IP_MERGED,			/* Input stream is OMG1 .merged file */
	SND_IP_GSF,				/* Input stream is GSF V1.10 file */
	SND_IP_HIPS,			/* Input stream is HIPS/IO HDCS */
	SND_IP_RAW,				/* Input stream is a manufacturer's raw format */
	SND_IP_NATIVE			/* Input stream is LIBCCOM's pre-digested format */
} SndIpType;

/* Construction and destruction methods */

extern SoundingStream sounding_new(Projection proj);

/* Routine:	sounding_new_from_plain
 * Purpose:	Initialise a stream for a JBOS file.
 * Inputs:	*name		Name to read from
 *			maxbeams	Number of beams to read at a time
 *			proj		Projection to use to convert to mapsheet space
 *			device		Generator used in constructing the data
 *			vessel		Vessel configuration for data platform
 *			mthd		Error attribution method to use
 * Outputs:	Pointer to stream, or NULL on failure
 * Comment:	This is about as basic as it gets --- a stream with no header,
 *			containing data about which we know nothing.  This should *only*
 *			be used as a last resort!  You need to specify the device that was
 *			used, and the vessel configuration ... and if you get it wrong,
 *			expect all sorts of bad things to happen (including, but not
 *			limited to core dumps as the beam_numbers in the file don't match
 *			what the device_*() code expects, if they even exist).  You would
 *			be well advised to use ERRMTHD_IHO for this sort of data, unless
 *			you know that the data has valid beam numbers, etc.
 */

extern SoundingStream sounding_new_from_plain(const char *name, u32 maxbeams,
									   		  Projection proj, Device device,
									   		  Vessel vessel, ErrMthd mthd);
/* Routine:	sounding_new_from_native
 * Purpose:	Open a stream in native format
 * Inputs:	*name	Name of the file to open
 *			proj	Projection to convert into mapsheet space
 *			mthd	Method to use in generating error attributes
 * Outputs:	Pointer to SoundingStream, or NULL on failure
 * Comment:	This routine assumes that the Vessel configuration was saved when
 *			the file was constructed, and hence that there is no need to set
 *			a new profile.  The error model is constructed based on the device
 *			and vessel configuration status stored in the file.
 */

extern SoundingStream sounding_new_from_native(const char *name,
											   Projection proj, ErrMthd mthd);
/* Routine:	sounding_new_from_omg
 * Purpose:	Opens a .merged OMG/HDCS file and initialises stream parameters
 * Inputs:	*name	Name of file to open
 *			proj	Projection parameters to use for conversion to mapsheet
 *			vessel	Vessel configuration for the platform in use
 *			mthd	Error modeling method to use in attributing accuracies
 * Outputs:	Returns pointer to SoundingStream opaque type, or NULL on failure
 * Comment:	This opens whatever .merged file the libunb.a code understands,
 *			and parses out the device that generated the data, etc. to
 *			initialise the internal stream.  An error model is constructed
 *			locally using the device from the OMG/HDCS file, the _vessel_
 *			and _mthd_.  Of course, the code has no way of checking that the
 *			_vessel_ is correct ...
 */

extern SoundingStream sounding_new_from_omg(const char *name, Projection proj,
									 		Vessel vessel, ErrMthd mthd);

/* Routine:	sounding_new_from_gsf
 * Purpose:	Opens a GSF file, checks header and initialises streams
 * Inputs:	*name	Name of the file to work from
 *			proj	Projection parameters to use for conversion to mapsheet
 *			vessel	Vessel description structure for the survey platform
 *			mthd	Error modeling method to use in attributing accuracies
 * Outputs:	Returns pointer to SoundingStream opaque type, or NULL on failure
 * Comment: This uses the supplied GSF library (at least V1.10) to open files,
 *			and understands most of what it does (the limitation is to known
 *			and supported multibeam systems).  The code here has no way to
 *			check, a priori, that the device provided actually matches that
 *			which was used to generate the data, but it assumes that it is the
 *			case initially and gripes massively if this later turns out not to
 *			be the case ... you have been warned.  There is also no checking of
 *			the vessel description; caveat emptor.
 */

extern SoundingStream sounding_new_from_gsf(char *name, Projection proj,
											Device device, Vessel vessel,
											ErrMthd mthd);

/* Routine:	sounding_new_from_raw
 * Purpose:	Generate a sounding stream from a manufacturer's raw datastream
 * Inputs:	*name		Name of the file to read
 *			proj		Projection for the mapsheet in use
 *			dev			Device associated with the file
 *			vessel		Vessel configuration structure for data platform
 *			mthd		Error attribution method to use
 *			data_reqd	Logical OR of the SStreamTypes enum elements
 * Outputs:	Pointer to opaque type for sounding stream, or NULL
 * Comment:	Note that the code has no way to check that the stream is in fact
 *			the same as the device passed here ... caveat emptor.  Invalid
 *			elements in data_reqd are not checked.
 */

extern SoundingStream sounding_new_from_raw(const char *name, Projection proj,
									 Device dev, Vessel vessel, ErrMthd mthd,
									 u32 data_reqd);

/* Routine:	sounding_new_from
 * Purpose:	Construct a sounding stream given parameters and type of file
 * Inputs:	type	Type of file to expect on input stream
 *			name	Filename to use for input file
 *			proj	Projection method information
 *			dev		Device generating the data (see comment)
 *			vessel	Vessel description for the survey platform (see comment)
 *			mthd	Error computation method (see comment)
 *			param	Generic u32 parameter for underlying code
 * Outputs:	Returns a pointer to the opened, initialised SoundingStream or NULL
 *			on failure to initialise
 * Comment:	This call acts as a marshalling point for the various file opening
 *			calls so that the caller only has to set the _type_ variable, and
 *			not worry about switch(){} ing for type in user code.
 *				Note that not all of the underlying file types require all of
 *			the parameters passed.  For example, native streams don't need
 *			either a Device or Vessel description since these are constructed
 *			when the files are generated and stored internally.  If you are
 *			*certain* that these are not required, you can of course pass NULL
 *			instead of something valid.  However, if it all goes horribly pear-
 *			shaped, don't say you weren't warned ...
 */

extern SoundingStream sounding_new_from(SndIpType type, char *name,
								 Projection proj, Device dev, Vessel vessel,
								 ErrMthd mthd, u32 param);

/* Routine:	sounding_new_from_dual
 * Purpose:	Construct a sounding stream given parameters and type of file
 * Inputs:	type	Type of file to expect on input stream
 *			name	Filename to use for input file
 *			proj	Projection method information
 *			dev		Device generating the data (see comment)
 *			v_port	Vessel description for port head (see comment)
 *			v_stbd	Vessel description for starboard head (or NULL)
 *			mthd	Error computation method (see comment)
 *			param	Generic u32 parameter for underlying code
 * Outputs:	Returns a pointer to the opened, initialised SoundingStream or NULL
 *			on failure to initialise
 * Comment:	This call acts as a marshalling point for the various file opening
 *			calls so that the caller only has to set the _type_ variable, and
 *			not worry about switch(){} ing for type in user code.
 *				Note that not all of the underlying file types require all of
 *			the parameters passed.  For example, native streams don't need
 *			either a Device or Vessel description since these are constructed
 *			when the files are generated and stored internally.  If you are
 *			*certain* that these are not required, you can of course pass NULL
 *			instead of something valid.  However, if it all goes horribly pear-
 *			shaped, don't say you weren't warned ...
 *				The only difference between this any sounding_new_from() is that
 *			this routine can have a dual head specification, which means that you
 *			can use it to create multiple SoundingStreams simultaneously or
 *			consecutively with different devices and vessels.  Note that it is an
 *			undocumented feature of the current implementation that the Vessel
 *			passed to sounding_new_from() is basically ignored in favour of the
 *			default vessel that happens to be implemented when the SoundingStream
 *			is created, mainly due to the fact that the Vessel structure doesn't
 *			have any facility to record the various offsets required for dual-head
 *			systems.  This routine allows the specification to be made exactly.
 */

extern SoundingStream sounding_new_from_dual(SndIpType type, char *name,
								 Projection proj, Device dev,
								 Vessel v_port, Vessel v_stbd,
								 ErrMthd mthd, u32 param);

/* Routine:	sounding_new_to_file
 * Purpose:	Generate an output stream for writing in native format
 * Inputs:	*name		Name of the file to open for output
 *			generator	Device being used to generate the data
 *			vessel		Vessel configuration for data platform
 *			proj		Projection used for mapsheet conversion
 * Outputs:	Pointer to output stream, or NULL on failure
 * Comment:	This opens a basic output stream which just encapsulates the data
 *			being passed, including the platform data available at the time.
 */


extern SoundingStream sounding_new_to_file(const char *name, Device generator,
										   Vessel vessel, Projection proj);

extern void sounding_release(SoundingStream stream);

/* Read/Write methods */
extern FileError sounding_get_next(SoundingStream stream, Platform *plat,
								   Sounding **data, u32 *n_read);
extern FileError sounding_put_next(SoundingStream stream, Platform *plat,
								   Sounding *data, u32 n_snd);
	/* i.e., to convert to native file format from any understood */

/* Routine:	sounding_add_depthgate
 * Purpose:	Add a static depth gate to the SoundingStream algorithm
 * Inputs:	stream	SoundingStream to work from
 *			min		Minimum depth to accept (see comment)
 *			max		Maximum depth to accept (see comment)
 * Outputs:	True if depth gate was added, otherwise False
 * Comment:	Attempts to set the depth gate for accepting soundings.  Recall that
 *			depths are always negative internally, but we specify the depths
 *			here positive --- that is, min < max is required
 */

extern Bool sounding_add_depthgate(SoundingStream stream, f32 min, f32 max);

/* Routine:	sounding_add_anglegate
 * Purpose:	Sets a gate on angles acceptable in multibeam data
 * Inputs:	stream	SoundingStream to work from
 *			min		Minimum angle to accept (degrees)
 *			max		Maximum angle to accept (degrees)
 * Outputs:	True if angle gate was set, otherwise False
 * Comment:	This sets up a fixed angular range (note: not beam number range)
 *			which is acceptable for data.  Angles are specified wrt nadir
 *			(i.e., straight down), with port side angles negative.  We restrict
 *			angles -90 <= min < max <= 90.
 */

extern Bool sounding_add_anglegate(SoundingStream stream, f32 min, f32 max);

/* Routine:	sounding_get_next_attitude
 * Purpose:	Extract high resolution attitude information from input stream
 * Inputs:	stream	SoundingStream to operate on
 *			plat	Platform structure to read attitude into
 * Outputs:	*plat is set up.  Returns FILE_OK for success, otherwise an error
 *			message and a suitable FileError return code.
 * Comment:	This routine only operates if the stream is of type
 *			SOUNDING_FILE_RAW, and only then if the stream actually has
 *			high resolution attitude data *and* the user requested that this
 *			be buffered when the sounding stream was configured.  Note that
 *			the return codes and error messages will come from different
 *			modules depending on what the actual error is.
 */

extern FileError sounding_get_next_attitude(SoundingStream stream,
											Platform *plat);

/* Routine:	sounding_get_next_imagery
 * Purpose:	Extract full time-series imagery from input stream
 * Inputs:	stream	Stream to operate on
 *			plat	Platform structure to interpolate attitude into
 * Outputs:	image	Imagery pointer to set to internal store
 *			n_read	Set to number of beams available in internal store
 * Comment:	This routine only operates if the stream is of type
 *			SOUNDING_FILE_RAW, and then only if the stream actually has some
 *			time-series backscatter, *and* the user requested that this be
 *			buffered in the first place.  Note that the return codes and error
 *			messages will come from different modules, depending on what the
 *			error actually is.
 */

extern FileError sounding_get_next_imagery(SoundingStream stream,
										   Platform *plat, Imagery **imagery,
										   u32 *n_read);

/* Routine:	sounding_get_time
 * Purpose:	Return timestamp on a buffer
 * Inputs:	stream	SoundingStream to operate on
 *			buf		Buffer to access
 * Outputs:	*stamp	Timestamp on buffer
 *			Returns FILE_INVALID if the stream is not a Raw type, or
 *			FILE_NOTINIT if the stream doesn't have the data you request; may
 *			return other FileErrors from the underlying buffer stream (e.g.,
 *			FILE_TRYAGAIN if there is no data in the buffer).
 * Comment: Timestamps are held internally at UTC seconds since 00:00:00 on
 *			01/01/1970, and are at the maximal accuracy of the underlying
 *			file stream (typically milliseconds).
 */

extern FileError sounding_get_time(SoundingStream stream, SStreamTypes buf,
								   f64p stamp);

/* Routine:	sounding_get_nav
 * Purpose:	Return currently available navigation information
 * Inputs:	stream	SoundingStream to read from
 * Outputs:	*nav	Pointer to Nav structures available currently, or NULL if
 *					no nav data is currently available.
 * Comment:	This reads back all of the currently available navigation data that
 *			the underlying stream can provide.  Note that the extent of this
 *			navigation data depends on the type of stream --- HIPS I/O data has
 *			all of the navigation for the line available on-line; RAW data may
 *			have all data read from the file so far; most other systems can't
 *			provide accumulated data, just reading it as they go along.  Note
 *			that a return of NULL doesn't mean that no data will ever be available,
 *			just that there is none currently.
 */

extern Nav sounding_get_nav(SoundingStream stream);

/* Stream manipulation methods */

/* Routine:	sounding_attach_file
 * Purpose:	Attach a new file stream to an extant SoundingStream structure
 * Inputs:	stream	Stream to reuse
 *			*name	Name of new file to attach
 *			reqd	Data types required from the file (see comment)
 * Outputs:	True if stream was sucessfully attached, otherwise False
 * Comment:	This detaches the current file from the stream, and attempts to
 *			open and initialise a new one (e.g., after a line break).  The
 *			_reqd_ element is a logical OR of elements from the SStreamTypes
 *			enum, but is ignored unless the stream is of type SOUNDING_FILE_RAW
 *			which is the only one that currently provides more than one data
 *			type.
 */

extern Bool sounding_attach_file(SoundingStream stream, const char *name,
								 u32 reqd);
extern void sounding_detach_file(SoundingStream stream);

/* Routine:	sounding_flush
 * Purpose:	Flush any buffered data associated with the stream
 * Inputs:	stream	Stream to operate on
 * Outputs:	-
 * Comment:	At present, this only has any effect on raw sounding streams, where
 *			it releases all of the buffered data currently being held.
 */

extern void sounding_flush(SoundingStream stream);

/* Routine:	sounding_locate_pt
 * Purpose:	Determine the location of a point given center position and
 *			offset relative to heading
 * Inputs:	x, y			Offset relative to center (m)
 *			heading			Heading CW from N, dec. deg.
 *			t_lon, t_lat	Center longitude & lattitude
 * Outputs:	*p_lon, *b_lat	Point geographic location
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

extern void sounding_locate_pt(f32 x, f32 y, f32 heading, f64 t_lon, f64 t_lat,
						 	   f64p p_lon, f64p p_lat);

/* Access methods */
extern Device sounding_get_device(SoundingStream stream);
extern Projection sounding_get_projection(SoundingStream stream);
extern s32 sounding_get_pingnum(SoundingStream stream);

/* Routine:	sounding_execute_params
 * Purpose:	Execute parameter list for the sounding module
 * Inputs:	*list	ParList to walk down and parse
 * Outputs:	True if parameter reading works OK, otherwise False
 * Comment:	This looks for a default surface sound speed, and then passes the
 *			list on to the sub-modules for further parsing.
 */

#include "params.h"

extern Bool sounding_execute_params(ParList *list);

#ifdef __cplusplus
}
#endif

#endif
