// Copyright 2025 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
// Hydrographic Center, University of New Hampshire
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.


#ifndef CUBE_BATHYMETRY__GRID_H_
#define CUBE_BATHYMETRY__GRID_H_

#include <cstdint>
#include <memory>
#include <vector>
#include "cube_bathymetry/node.h"
#include "cube_bathymetry/parameters.h"
#include "cube_bathymetry/sounding.h"
#include "cube_bathymetry/bounds.h"


namespace cube
{

  class Grid
  {
public:
    Grid(CellCounts counts, CellSizes sizes, MapPosition origin, const Parameters & parameters);


  /* Routine:	cube_grid_insert_depths
  * Purpose:	Add a sequence of depth estimates to the mapsheet
  * Inputs:	g		CubeGrid structure on which to work
  *			p		Cube algorithm parameters structure
  *			stream	SoundingStream which supplied the data
  *			*data	Pointer to the soundings which should be added
  *			nsnds	Number of soundings in this batch
  *			west	Easting of the most westerly node in the grid
  *			north	Northing of the most northerly node in the grid
  * Outputs:	True if data was added correctly, otherwise False
  *			*n_used set to number of points actually integrated into CubeGrid
  * Comment:	This code integrates the soundings presented into the current
  *			grid.  Note that in the newer version of the algorithm, soundings
  *			are queued in a median pre-filter before going to CUBE so there is
  *			a delay between presentation and readback effect.  Note that we
  *			assume at this point that the soundings presented have suitably
  *			defined error variances in depth and position associated with them,
  *			so that we don't have to try to re-generate them here.
  *				Remember that the grid doesn't have an absolute coordinate
  *			system, which is really a function of the higher order entity which
  *			would typically wrap the raw grid.  However, the soundings are
  *			linked to a particular projection system, and it is unreasonable
  *			for the code calling this to modify to local coordinates, then
  *			call here, then change everything back to global coordinates (since
  *			the caller of *that* routine is providing the soundings, and has a
  *			reasonable right to expect that they aren't going to be munged by
  *			subsidiary calls in the process of being integrated).  Hence, the
  *			caller needs to specify absolute bounds here, and we modify the
  *			node positions from the CubeGrid relative system accordingly.
  */
    bool insert(const MapSounding & sounding);
    bool insert(const std::vector < MapSounding > &soundings);

  /// @brief Seed the predicted depth at node (x, y), lazy-creating the Node.
  ///
  /// Planar peer of GeoGrid::setPredictedDepthAt: seeds the slope-correction /
  /// blunder-gate prior only. Per ADR-0008 the prior gates and corrects — it
  /// never seeds hypotheses, so an unsurveyed primed cell still reads NaN from
  /// values(). Out-of-range indices are ignored.
  /// @param x         Node column index from the origin corner.
  /// @param y         Node row index from the origin corner.
  /// @param depth     Predicted depth (negative-down), or NaN / INVALID_DATA
  ///                  per Node::setPredictedDepth conventions.
  /// @param variance  Variance of the predicted depth (meter^2); a real depth
  ///                  requires a finite positive variance (see Node).
    void setPredictedDepthAt(uint32_t x, uint32_t y, float depth, float variance);

  /// @brief Bilinear predicted-surface depth at map position (x, y), or the
  ///        no-correction sentinel INVALID_DATA when the stencil is unavailable.
  ///
  /// Port of the original cube_grid_interpolate (ADR-0008): nodes sit on the
  /// origin + index*sizes lattice, so the stencil is the floor lower-left node
  /// and its +1 neighbors. Returns INVALID_DATA when the stencil would leave
  /// the grid or any corner node is absent / has no prediction / is NaN — every
  /// sentinel path degrades to the uncorrected (offset 0) behaviour in
  /// Node::insert, never to a wrong correction.
    float interpolatePredictedDepth(double x, double y) const;

  // Returns the lower left grid position
    const MapPosition & origin() const;

    const CellCounts & cellCounts() const;
    const CellSizes & cellSizes() const;

    MapBounds bounds() const;

    std::vector < DepthAndUncertainty > values() const;

private:
    CellCounts counts_;
    CellSizes sizes_;

    MapPosition origin_;

    const Parameters & parameters_;

    std::vector < std::shared_ptr < Node >> nodes_;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__GRID_H_
