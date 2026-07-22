// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
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


#ifndef CUBE_BATHYMETRY__BATCH_REGEN_H_
#define CUBE_BATHYMETRY__BATCH_REGEN_H_

#include <cstddef>
#include <fstream>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/store_import.h"
#include "marine_autonomy/gggs.h"
#include "marine_bathymetry_store/registry.hpp"
#include "marine_mbes_backscatter_store/registry.hpp"

namespace cube
{

/// @brief Exact, bounded-RAM store rebuild via scatter/gather (cube_bathymetry#96).
///
/// The authoritative off-boat rebuild path. Where @ref ImportAccumulator bounds RAM
/// by evicting cold tiles (which re-derives a revisited tile's depth UNCERTAINTY
/// from a single reseeded hypothesis — faithful depth, slightly drifted
/// uncertainty), batch-regen produces a **bit-exact** result: every tile is built
/// in a single pass over the COMPLETE set of soundings that touch it, so no tile is
/// ever evicted mid-disambiguation.
///
/// **Scatter (streaming):** @ref addBatch appends each batch to the per-tile binary
/// bucket files on disk — one bucket per GGGS tile in the batch's
/// influence-radius-expanded bounds (one-cell floor, #104), each receiving the
/// WHOLE batch, exactly the tiles + soundings
/// @ref GeoMapSheet::addSoundings feeds each grid. Writing the whole batch (not just
/// the soundings whose own centre is near a tile) preserves the far-radius
/// cross-sounding deposits addSoundings makes near a seam (out to
/// max_radius = CONF_99PC·√(horizontal_error)), so the rebuild stays bit-exact for
/// any TPU. Nothing is accumulated in RAM during the scatter; resident memory is
/// bounded by the open bucket-stream cache, not by surveyed area.
///
/// **Gather:** @ref finalize processes one bucket at a time. Each tile's soundings
/// are replayed, in global scatter order, into a FRESH @ref GeoMapSheet driven by a
/// single-tile @ref ImportAccumulator (unbounded, so it never evicts), which also
/// applies the two-rung seed precedence (survey warm-start / reference gate). Only
/// the target tile is written (@ref ImportAccumulator::persistResidentTile) — the
/// neighbour grids a seam sounding also created in the fresh sheet are each written
/// by their own bucket's gather. Because the tile's bucket holds exactly the
/// soundings a single unbounded pass would feed its grid, in the same order, the
/// output is bit-for-bit identical to that unbounded pass.
///
/// The scatter scratch directory is deleted by @ref finalize (and by the destructor
/// on an exception).
  class BatchRegen
  {
public:
  /// @brief Factory that builds a GeoMapSheet configured EXACTLY as the intended
  ///        single-pass run would (cell size, IHO order, backscatter correction).
  ///
  /// The gather builds one sheet per tile from this factory, and one long-lived
  /// index sheet for tile routing, so every sheet shares identical CUBE parameters
  /// — the precondition for bit-exact output.
    using SheetFactory = std::function < std::unique_ptr < GeoMapSheet > () >;

  /// @param factory Builds a fully-configured, empty GeoMapSheet (see @ref SheetFactory).
  /// @param config  Store paths + seed precedence. `max_resident_tiles` is ignored
  ///   (the gather is single-tile and never evicts); the rest (store_dir,
  ///   reference_store_dir, bs_store_dir, cell_size_m) drive persistence and seeding.
    BatchRegen(SheetFactory factory, ImportAccumulatorConfig config);

  /// @brief Delete the scratch scatter dir (RAII safety net for @ref finalize).
    ~BatchRegen();

    BatchRegen(const BatchRegen &) = delete;
    BatchRegen & operator = (const BatchRegen &) = delete;

  /// @brief Scatter a batch of soundings to their per-tile bucket files.
    void addBatch(const std::vector < GeoSounding > &soundings);

  /// @brief Gather every tile bucket, build + write each tile once, write the
  ///        store-level metadata, and delete the scratch scatter dir.
    void finalize(
      const marine_bathymetry_store::StoreMetadata * bathy_metadata = nullptr,
      const marine_mbes_backscatter_store::StoreMetadata * bs_metadata = nullptr);

  /// @brief Cumulative bathy tile writes (valid after @ref finalize).
    std::size_t bathyTilesPersisted() const {return bathy_persisted_;}
  /// @brief Cumulative backscatter tile writes (valid after @ref finalize).
    std::size_t backscatterTilesPersisted() const {return bs_persisted_;}
  /// @brief Number of distinct tiles scattered to (bucket count).
    std::size_t tileCount() const {return tiles_.size();}
  /// @brief The scratch scatter directory (empty until the first scatter). Exposed
  ///        for tests that assert it is cleaned up after @ref finalize.
    const std::string & scratchDir() const {return scratch_dir_;}

private:
  /// Return an append-open stream for @p index's bucket, lazily creating the
  /// scratch dir and truncating the file the first time this tile is seen; keeps a
  /// bounded LRU of open streams so the scatter never exhausts file descriptors.
    std::ofstream & bucketStream(const gggs::GridIndex & index);
  /// Flush every open bucket stream and throw if any flush failed (disk-full / I/O
  /// error), so a truncated bucket is a hard error before the gather reads it rather
  /// than a silently-wrong tile. Called by @ref finalize before @ref closeAllStreams.
    void flushOpenStreams();
    void closeAllStreams();
    void cleanupScratch();
    std::string bucketPath(const gggs::GridIndex & index) const;

    SheetFactory factory_;
    ImportAccumulatorConfig cfg_;
  /// Tile routing only (no accumulation).
    std::unique_ptr < GeoMapSheet > index_sheet_;

  /// Lazily created on first scatter; "" = none.
    std::string scratch_dir_;
  /// Every tile ever scattered to (its bucket file exists).
    std::set < gggs::GridIndex > tiles_;

  // Bounded LRU of open append streams (front = most recently used).
    std::list < gggs::GridIndex > lru_;
    std::map < gggs::GridIndex, std::pair < std::ofstream,
    std::list < gggs::GridIndex > ::iterator >>
    open_streams_;

    std::size_t bathy_persisted_ = 0;
    std::size_t bs_persisted_ = 0;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__BATCH_REGEN_H_
