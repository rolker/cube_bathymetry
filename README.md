# CUBE Library Source Code

(forked from <https://bitbucket.org/ccomjhc/cube>)

This repository contains the reference version of the CUBE algorithm (Calder & Mayer, "Automatic processing of high-rate, high-density multibeam echosounder data", Geochem., Geophy., Geosys. 4(6), 2003, 10.1029/2002GC000486), supplied with an MIT license.

The code was developed mostly over the period 2000-2002, and first released under a commercial license in 2003.  Since then, of course, many things have changed.  You should not expect, therefore, that the code will compile cleanly on any particular system, or that it will compile completely (there may be libraries that need to be installed to support, and it is known that not all of those libraries are still available).  Development moved on to CHRT in approximately 2009, and this code was mostly abandoned.

# Important Parts of the Library

## CUBE

The main source code for CUBE is in the `libsrc/cube` directory.  The `cube_node.c` file provides the core algorithm for CUBE; the `cube_grid.c` file provides a regular grid of nodes at fixed resolution to represent a given area.  The `cube.c` file provides parameter handling.

## Mapsheets

The remainder of the code in the library is support code.  Probably most important is the mapsheet code (`libsrc/mapsheet`), which is the structuring concept that contains one or more layers of object data, including a collection of CUBE grids, over a given area.  You specify the geographic bounds for the mapsheet, and it works out how to tile smaller grids internally and map them into memory when required.  The code was built for very small memory computers (256MB was common at the time), and therefore the tiles can be small.  The code is designed to keep at least four in memory at a time so that a MBES swath that goes over the corner between the different tiles doesn't cause paging of tiles as you incorporate soundings across a given swath.

## Uncertainty Estimation

The library for CUBE assumes that the data has uncertainties associated.  When it was originally built, however, that was not common.  The code in `libsrc/errmod` therefore provides a mechanism for computing uncertainties using the Hare-Godin-Mayer model for MBES data, as specified in the original 1995 technical report.  Some bug-fixes have been applied, but the extra modifications done later, and subsequently published, are not included.  It uses a fairly basic model for the sounder measurement uncertainty, so its applicability for modern MBES with near-field focussing, and chirped sectors, is probably low.  A much better model would the IFREMER style quality factors.

## Source Soundings

Finally, the library reads soundings from a variety of sources, including an internal format that's intended to be as simple as possible so that it's fast.  Code in `libsrc/sounding` manages this.  The `sounding.c` code provides a high-level interface that then uses one of the format-specific drivers (e.g., `sounding_native.c` or `sounding_gsf.c`) to do the actual reading.  Some of these require additional libraries, which may not be present.

## Driver Utilities

User-level commandline utilities are provided in `utilities` that can be used to drive the library.  Originally, this was the only way to drive the CUBE library, with command scripts used to pull things together (see the `scripts` directory for some examples; these reflect NOAA practice circa 2002, and refer to a version of CARIS HIPS now long in the past --- your mileage will vary significantly on utility of these in the modern world).  Some of the utilities are likely still useful, however, particularly `estdepth.c` which shows a simple driver for reading data files and estimating a depth from them over a given area, and `assimilate.c`, a more sophisticated version of same.  Mapsheets can be constructed with `sdf2sht.c` from a description file (see `docs` directory for a description); once filled with data, `summarise.c` can be used to extract the estimates of depth and make them into a simple grid format (which is rarely used any more, but was common in the lab at the time); `sheetinfo.c` can provide information on the bounds, layers, etc. for a mapsheet (which is otherwise a quite opaque binary file).

# Likely Uses

The source code provided can be used for any purpose consistent with the license terms (see `LICENSE`).  However, the most likely use of the library is for spare parts; that is, the code is unlikely to compile cleanly without a lot of work and patching around obsolete libraries.  Therefore, the most likely value here is in the ideas inherent in the core CUBE algorithm, which would have to be reimplemented in user code to be useful.

That being said, any contributions of bug fixes or rebuilds of the library to build cleanly on modern systems would be seriously considered for merge to the library, consistent with the license.

# Support

Basically, there is none.  Development at CCOM/JHC has moved on (twice) since this code was developed, and it hasn't been maintained in over a decade.  Essentially (although this is meant in a kindly manner, honest!), you're on your own.  There are a set of development notes in `docs` that were built to help developers work out where things were, and how to use them in implementations in commercial code, which might be helpful.  A copy of the CUBE User Manual is also provided.