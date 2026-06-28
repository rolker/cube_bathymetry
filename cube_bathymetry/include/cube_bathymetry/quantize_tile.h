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

#ifndef CUBE_BATHYMETRY__QUANTIZE_TILE_H_
#define CUBE_BATHYMETRY__QUANTIZE_TILE_H_

#include <optional>

#include "builtin_interfaces/msg/time.hpp"
#include "marine_interfaces/msg/sonar_visualization_tile.hpp"

#include "cube_bathymetry/geo_grid.h"

namespace cube
{

/// @brief Quantize one GGGS `GeoGrid` into a display-grade `SonarVisualizationTile`
///        (ADR-0008): the boat-side producer for the live operator/CAMP view.
///
/// Bands (named, self-describing per ADR-0008 D1; consumer dequantizes generically
/// `value = raw*scale + offset`):
/// - `depth` — int16 cm, **fixed** scale 0.01 m, nodata −32768.
/// - `uncertainty` — uint8, **fixed** scale 0.05 m, nodata 255.
/// - `backscatter` — uint8, **per-tile auto-range** (scale/offset from this tile's
///   finite intensities; #54 co-estimate, currently uncorrected pending cube#81),
///   nodata 255.
///
/// The tile is GGGS-cell-aligned (no map-frame projection): band data is row-major
/// in GGGS cell order, matching `GeoGrid::nodeRecords()` (`CellAreaIterator` order).
/// v1 emits the **full tile window** (the per-tile dirty-set publish already bounds
/// what is sent; a finer dirty sub-window is a later refinement). `stamp` is the
/// tile version time (newest-wins, D3).
///
/// @return nullopt when the tile has no finite-depth cell (nothing to display).
  std::optional < marine_interfaces::msg::SonarVisualizationTile >
  quantizeTile(const GeoGrid & grid, const builtin_interfaces::msg::Time & stamp);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__QUANTIZE_TILE_H_
