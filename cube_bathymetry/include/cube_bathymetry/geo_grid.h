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


#ifndef CUBE_BATHYMETRY__GEO_GRID_H_
#define CUBE_BATHYMETRY__GEO_GRID_H_

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include "cube_bathymetry/node.h"
#include "cube_bathymetry/parameters.h"
#include "cube_bathymetry/geo_sounding.h"
#include "marine_autonomy/gggs.h"

namespace cube
{

/// @brief Inclusive bounding box of cells within ONE tile, in that tile's
///        (row, column) coordinates. Empty until something expands it.
///
/// Accumulated by @ref GeoGrid::insert over the cells a sounding batch actually
/// wrote, and consumed by the incremental display-tile publish so it can emit
/// only the changed sub-window (`SonarVisualizationTile`'s
/// `window_col`/`window_row`/`window_width`/`window_height`, ADR-0001 § 4
/// sub-window addendum).
///
/// A bounding BOX rather than a per-cell mask is deliberate: it is four uint16
/// per tile (against a 960x960 bitset), @ref expand is O(1) on the
/// profiler-hot insert path (cube#63/#107), and the wire contract carries a
/// rectangular window anyway -- a mask could not be transmitted without
/// changing the message. The cost is over-coverage: a diagonal or two-lobed
/// change set sends unchanged cells inside its hull, degrading to the full
/// tile in the worst case (which is exactly today's behaviour, never worse).
  struct CellBox
  {
  /// Inclusive bounds. The empty box is encoded as min > max, so a
  /// default-constructed CellBox is empty and @ref expand needs no branch.
    uint16_t min_row = std::numeric_limits < uint16_t > ::max();
    uint16_t min_col = std::numeric_limits < uint16_t > ::max();
    uint16_t max_row = 0;
    uint16_t max_col = 0;

    bool empty() const noexcept
    {
      return min_row > max_row || min_col > max_col;
    }

    void expand(uint16_t row, uint16_t column) noexcept
    {
      min_row = std::min(min_row, row);
      max_row = std::max(max_row, row);
      min_col = std::min(min_col, column);
      max_col = std::max(max_col, column);
    }

    void clear() noexcept
    {
      *this = CellBox();
    }

  /// Number of cell rows covered (0 when empty).
    uint16_t rows() const noexcept
    {
      return empty() ? 0 : static_cast < uint16_t > (max_row - min_row + 1);
    }

  /// Number of cell columns covered (0 when empty).
    uint16_t columns() const noexcept
    {
      return empty() ? 0 : static_cast < uint16_t > (max_col - min_col + 1);
    }

  /// @brief The box covering every cell of a GGGS tile (the full-tile window).
    static CellBox wholeTile() noexcept
    {
      CellBox box;
      box.min_row = 0;
      box.min_col = 0;
      box.max_row = static_cast < uint16_t > (gggs::GridIndex::cellRowCount() - 1);
      box.max_col = static_cast < uint16_t > (gggs::GridIndex::cellColumnCount() - 1);
      return box;
    }
  };

  class GeoGrid
  {
public:
    GeoGrid(gggs::GridIndex index, const Parameters & parameters);


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
    bool insert(const GeoSounding & sounding);
    bool insert(const std::vector < GeoSounding > &soundings);

  /// @brief Seed the predicted depth at @p cell, lazy-creating the Node if absent.
  ///
  /// Warm-starts CUBE's slope-correction prior from a persisted draft tile on the
  /// on-startup load path (issue #21): the running session continues accumulating
  /// CUBE hypotheses from scratch on top of this predicted surface. It does NOT
  /// restore hypothesis/queue state -- full Node deserialization is out of scope.
  /// @param cell      Cell within this grid; must belong to `index()`.
  /// @param depth     Predicted depth (negative-down), or NaN / INVALID_DATA per
  ///                  Node::setPredictedDepth conventions.
  /// @param variance  Variance of the predicted depth (meter^2); a real depth
  ///                  requires a finite positive variance (see Node).
    void setPredictedDepthAt(const gggs::CellIndex & cell, float depth, float variance);

  /// @brief Reseed a previously-settled depth/uncertainty at @p cell as a CUBE
  ///        hypothesis (lossless reload; lazy-creates the Node).
  ///
  /// Unlike @ref setPredictedDepthAt (slope prior only), this restores the cell's
  /// best estimate so @ref values() re-emits it on the next save and subsequent
  /// soundings refine it as a Bayesian prior (ADR-0001). Used by the tile-eviction
  /// revisit-reload and the startup prime. Does NOT mark the grid dirty -- it
  /// reproduces already-persisted data; the next *survey* ping marks it dirty.
  /// @param cell         Cell within this grid; must belong to `index()`.
  /// @param depth        Stored best-estimate depth (negative-down, finite).
  /// @param uncertainty  Stored 1.96-sigma confidence interval (m).
    void setSettledDepthAt(const gggs::CellIndex & cell, float depth, float uncertainty);

  /// @brief Predicted-surface depth at @p cell, or `INVALID_DATA` if no node
  ///        (or no prediction) exists there.
  ///
  /// Read accessor for the warm-start prime path and its tests; mirrors
  /// `Node::predictedDepth()`. The running best-estimate of the seabed is
  /// extracted separately via @ref values().
    float predictedDepthAt(const gggs::CellIndex & cell) const;

  /// @brief Bilinear predicted-surface depth at the touchdown (latitude,
  ///        longitude), or the no-correction sentinel INVALID_DATA when the
  ///        stencil is unavailable.
  ///
  /// Port of the original cube_grid_interpolate for the GGGS tile (ADR-0008):
  /// nodes sit on the cells' SW-corner (row, column) lattice — the same
  /// position insert() distance-gates against — so the stencil is the floor
  /// lower-left node and its +1 neighbors, with bilinear weights in (row,
  /// column) space (the equirectangular within-cell approximation insert()
  /// already accepts). Returns INVALID_DATA when the stencil crosses the tile
  /// edge (per-tile scope, like the original's per-tile interpolation) or any
  /// corner node is absent / has no prediction / is NaN — every sentinel path
  /// degrades to the uncorrected (offset 0) behaviour in Node::insert.
    float interpolatePredictedDepth(double latitude, double longitude) const;

  /// Returns the lower left grid position
    const gggs::GridIndex & index() const;

    std::vector < DepthAndUncertainty > values() const;

  /// @brief Per-node enriched records (depth + co-estimated backscatter) for
  ///        every cell, in `CellAreaIterator` order over `index()`.
  ///
  /// Parallel to @ref values() but emits @ref NodeRecord (adds the co-estimated
  /// `intensity`/`intensity_var`, ADR-0007 D5) so both the offline import (#80,
  /// backscatter store layer) and the live display-tile producer (#78,
  /// `SonarVisualizationTile`) surface backscatter from the same pass. Absent
  /// cells push a default `NodeRecord` (NaN intensity) — the same positional NaN
  /// sentinel @ref values() uses — so the result is index-aligned with a second
  /// `CellAreaIterator` over `index()`. Like @ref values() it flushes the median
  /// pre-filter (`queueFlush`); calling it after @ref values() on the same grid
  /// is a harmless no-op flush. The surfaced intensity is UNCORRECTED (the
  /// beam-angle/GeoCoder correction is deferred to cube#81).
    std::vector < NodeRecord > nodeRecords() const;

  /// @brief Corrected-intensity Welford of every cell's winning hypothesis, keyed
  ///        by `gggs::CellIndex` (cube_bathymetry#92/#93 lossless eviction spill).
  ///
  /// Iterates the SPARSE node map (not all 960x960 cells), flushes each node's
  /// median pre-filter (`queueFlush`, like @ref nodeRecords) and emits the
  /// @ref Node::chosenIntensityWelford for cells with intensity-bearing beams
  /// (n > 0). The result is what must be persisted to a scratch spill so a tile
  /// evicted from RAM can be reloaded with its backscatter accumulator intact
  /// (the bathy tile stores only the depth summary; the intensity Welford lives
  /// only here). Three numbers per cell -- O(1), the cube#93 memory fix.
    std::map < gggs::CellIndex, IntensityWelford > nodeIntensityWelford() const;

  /// @brief Restore a corrected-intensity Welford onto @p cell's winning
  ///        hypothesis (cube_bathymetry#92/#93 lossless eviction reload).
  ///
  /// Forwards to @ref Node::setSettledIntensityWelford on the node at @p cell. A
  /// no-op if no node exists there (the cell was not reseeded by the depth
  /// reload). Must run AFTER @ref setSettledDepthAt (which lazy-creates the node)
  /// and BEFORE the revisit's soundings are added, so those beams then continue
  /// the Welford on the same reloaded hypothesis (bit-identical to never-evicting).
    void setSettledIntensityWelfordAt(
      const gggs::CellIndex & cell, const IntensityWelford & intensity);

  /// @brief Bounding box of the cells @ref insert has written since the last
  ///        @ref clearPublishDirtyCells(); empty when nothing has changed.
  ///
  /// The cell-resolution companion to `GeoMapSheet::publishDirtyGrids()`: that
  /// set says WHICH tiles changed, this box says WHERE inside one of them, so
  /// the incremental publish can send a sub-window instead of 3.7 MB of whole
  /// tile (ADR-0001 § 4 sub-window addendum). Only the *publish* stream reads
  /// it -- the save path writes whole tiles and needs no bounds.
  ///
  /// Tracked HERE and not in GeoMapSheet because only @ref insert knows which
  /// cells its per-sounding influence-radius gate actually accepted; the sheet
  /// sees a single bool per grid, and recomputing a box from the sounding
  /// bounds would duplicate the radius math the two are meant to share (#104).
  ///
  /// The seed/reload accessors (@ref setPredictedDepthAt,
  /// @ref setSettledDepthAt, @ref setSettledIntensityWelfordAt) deliberately do
  /// NOT expand it, matching their existing no-dirty-mark contract: they
  /// reproduce already-persisted data the consumer either holds or will heal
  /// via the catalog/TileRequest path.
    const CellBox & publishDirtyCells() const {return publish_dirty_cells_;}

  /// @brief Reset the publish-dirty cell box (called after a publish).
  ///
  /// `GeoMapSheet::clearPublishDirtyGrids()` calls this for every grid in the
  /// publish-dirty set, so the set and the boxes always clear together.
    void clearPublishDirtyCells() {publish_dirty_cells_.clear();}

private:
  /// Pack a cell's (row, column) into a single uint32 hash-map key. Every node
  /// in this grid shares `index_` as its `gggs::CellIndex::grid()`, and row/
  /// column are `uint16_t`, so `(row << 16) | column` is a lossless,
  /// collision-free key within one GeoGrid (cube#107). This replaces
  /// `std::map<gggs::CellIndex>` keyed on the fat `gggs::operator<` -- which
  /// re-ran `valid()` -> `GridIndex::valid()` -> `columnCount()`/
  /// `latitudeScaleFactor()` ~20x per red-black descent and dominated
  /// `GeoGrid::insert` (8/8 profiler samples). The reverse mapping is
  /// `gggs::CellIndex(index_, key >> 16, key & 0xFFFF)`.
    static uint32_t nodeKey(const gggs::CellIndex & cell) noexcept
    {
      return (static_cast < uint32_t > (cell.row()) << 16) | cell.column();
    }

    gggs::GridIndex index_;

    const Parameters & parameters_;

  // std::vector<std::shared_ptr<Node> > nodes_;
    std::unordered_map < uint32_t, std::shared_ptr < Node >> nodes_;

  /// Cells written since the last clearPublishDirtyCells(). See
  /// @ref publishDirtyCells.
    CellBox publish_dirty_cells_;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__GEO_GRID_H_
