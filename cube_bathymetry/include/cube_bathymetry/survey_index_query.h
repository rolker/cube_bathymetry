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

#ifndef CUBE_BATHYMETRY__SURVEY_INDEX_QUERY_H_
#define CUBE_BATHYMETRY__SURVEY_INDEX_QUERY_H_

#include <sqlite3.h>

#include <string>
#include <vector>

#include "marine_autonomy/gggs.h"
#include "marine_survey_index/query.hpp"

namespace cube
{

/// @brief One dirty store-level (L10) tile plus the survey-index passes that
///        contribute to it (cube_bathymetry#111, ADR-0002).
  struct DirtyTile
  {
  /// The store-level (L10) tile that must be rebuilt.
    gggs::GridIndex tile;

  /// Every pass (across all bags in the index, old and new) that touches the
  /// footprint rolled into @ref tile — the contributing-bag set + per-bag pass
  /// intervals a tile-scoped rebuild would replay. May be empty for a tile that
  /// entered the dirty set purely through the conservative margin (no indexed
  /// pass overlaps it — a harmless false positive per ADR-0002).
    std::vector < marine_survey_index::PassRow > passes;
  };

/// @brief Compute the store-level (L10) tiles that must be rebuilt after the
///        given new bags were added to the survey index (ADR-0002).
///
/// Pure query — reads the index, builds nothing. The algorithm (ADR-0002):
///   1. Read the index-level (L14) footprint tiles the new bags touched.
///   2. Expand that footprint by one index-level tile in each direction (a
///      conservative margin that covers the per-sounding influence radius the
///      index does not record; see ADR-0002).
///   3. Roll the expanded footprint up to @p store_level via iterated
///      `gggs::parent()` (L14 → L10 is four applications) and deduplicate — this
///      is the dirty set.
///   4. Attach, to each dirty tile, the passes (all bags) overlapping its FULL
///      index-level extent via `marine_survey_index::queryPasses` — re-enumerated
///      from each dirty tile's bounds, not just the new-bag footprint, so an
///      old-bag pass in a sub-tile outside the footprint is not omitted (required
///      for a tile-scoped rebuild to replay every contributing pass).
///
/// The result is a provably conservative superset of the tiles a full regen
/// would rebuild for the new soundings: a missed tile would be a correctness
/// failure, an extra tile only costs a bit-identical rebuild.
///
/// @param db            An open survey-index handle (see
///   `marine_survey_index::openIndexDb`). Not owned; the caller closes it.
/// @param new_bag_paths The newly-added bag paths, exactly as stored in the
///   index `bags.path` column. An empty list yields no dirty tiles. A bag not
///   present in the index contributes no footprint (the caller falls back to
///   full regen when the index cannot answer).
/// @param store_level   The store/output GGGS level (L10 for the bathy store).
///   Precondition: the index footprint level is at least this level (finer or
///   equal); L14 ≥ L10 holds for the bathy store.
/// @param sensor_filter Passed to the footprint and contributing-pass queries.
///   Empty (default) = all sensors. Use the bathy sensor to scope the dirty set
///   to the surface being rebuilt.
/// @return The dirty tiles, ordered by `gggs::GridIndex`, each with its
///   contributing passes ordered by bag path then start time.
/// @throws std::invalid_argument if a footprint (or a dirty tile's extent)
///   spans more than 180° of longitude — an antimeridian-crossing box, which
///   `marine_survey_index::tilesForBoundingBox` refuses rather than enumerate
///   the long way around. Survey areas in this workspace never cross it; a
///   caller (e.g. the CLI dry-run) should catch this and fall back to full
///   regen, and PR2's rebuild path must replicate that catch.
  std::vector < DirtyTile > dirtyL10Tiles(
  sqlite3 * db,
  const std::vector < std::string > &new_bag_paths,
  const gggs::Level & store_level,
  const std::string & sensor_filter = "");

}  // namespace cube

#endif  // CUBE_BATHYMETRY__SURVEY_INDEX_QUERY_H_
