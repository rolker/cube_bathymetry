/*
 * $Id: stdtypes.h 2 2003-02-03 20:18:41Z brc $
 * $Log$
 * Revision 1.1  2003/02/03 20:18:44  brc
 * Initial revision
 *
 * Revision 1.2.4.1  2003/01/28 14:29:12  dneville
 * Latest updates from Brian C.
 *
 * Revision 1.2.2.1  2002/07/14 02:19:11  brc
 * This is first check-in of the Win32 branch of the library, so that
 * we have a version in the repository.  The code appears to work
 * on the Win32 platform (tested extensively with almost a billion data
 * points during the GoM2002 cruise on the R/V Moana Wave,
 * including multiple devices operating simultaneously), but as yet,
 * a Unix compile of the branch has not been done.  Once this is
 * checked, a merge of this branch to HEAD will be done and will
 * result in a suitably combined code-base.
 *
 * Revision 1.2  2000/10/27 20:53:31  roland
 * libccom has now been cplusplusized!
 *
 * Revision 1.1.1.1  2000/08/10 15:53:25  brc
 * A collection of `object oriented' (or at least well encapsulated) libraries
 * that deal with a number of tasks associated with and required for processing
 * bathymetric and associated sonar imagery.
 *
 *
 * File:	stdtypes.h
 * Purpose:	Standard type definitions.
 * Date:	04 July 2000
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

#ifndef __STDTYPES_H__
#define __STDTYPES_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned char	u8, *u8p, byte;
typedef signed char		s8, *s8p;
typedef unsigned short	u16, *u16p;
typedef signed short	s16, *s16p;
typedef unsigned int	u32, *u32p;
typedef signed int		s32, *s32p;
typedef float			f32, *f32p;
typedef double			f64, *f64p;

#ifndef False

typedef enum {
	False = 0,
	True
} Bool;

#ifndef M_PI
#define M_PI 3.141592654
#endif
#ifndef M_PI_2
#define M_PI_2 1.570796327
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif
