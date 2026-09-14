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

#include "cube_bathymetry/count_grid.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

#include "marine_tiled_raster_store/tile_io.hpp"

namespace cube
{

namespace
{
constexpr const char * kLevelFile = "level.txt";
constexpr const char * kSpreadFile = "spread_terms.txt";

std::string tileName(const gggs::GridIndex & grid)
{
  return std::to_string(grid.level()) + "_" + std::to_string(grid.row()) + "_" +
         std::to_string(grid.column());
}

double centreLatitude(const gggs::GridIndex & grid)
{
  return 0.5 * (grid.southLatitude() + grid.northLatitude());
}

double centreLongitude(const gggs::GridIndex & grid)
{
  return 0.5 * (grid.westLongitude() + grid.eastLongitude());
}
}  // namespace

gggs::GridIndex CountGrid::neighbourGrid(const gggs::GridIndex & grid, int d_row, int d_column)
{
  if (!grid.valid()) {
    return gggs::GridIndex();
  }
  // GridIndex has no public row/column constructor: resolve the neighbour by
  // looking up the position one grid span away. Latitude is clamped by
  // gridIndex at the poles and longitude wraps; a request past the world's
  // edge therefore resolves to the grid itself (the caller treats that as
  // "no neighbour").
  const double lat = centreLatitude(grid) + d_row * grid.latitudinalSpan();
  const double lon = centreLongitude(grid) + d_column * grid.longitudinalSpan();
  if (lat < -90.0 || lat > 90.0) {
    return gggs::GridIndex();
  }
  return gggs::Level(grid.level()).gridIndex(lat, lon);
}

CountGrid::CountGrid(uint8_t level)
: level_(level)
{
}

void CountGrid::checkCell(const gggs::CellIndex & cell) const
{
  if (!cell.valid()) {
    throw std::invalid_argument("CountGrid: invalid cell");
  }
  if (cell.level() != level_.level()) {
    throw std::invalid_argument(
            "CountGrid: cell is at level " + std::to_string(cell.level()) +
            ", grid is at level " + std::to_string(level_.level()));
  }
}

void CountGrid::add(double latitude, double longitude, double spread_term_m)
{
  add(level_.cellIndex(gggs::geoPoint(latitude, longitude)), spread_term_m);
}

void CountGrid::add(const gggs::CellIndex & cell, double spread_term_m)
{
  checkCell(cell);
  auto it = tiles_.find(cell.grid());
  if (it == tiles_.end()) {
    it = tiles_.emplace(cell.grid(), Tile(cell.grid(), 1, Count{0})).first;
  }
  const Count current = it->second.get(cell.row(), cell.column(), 0);
  if (current < std::numeric_limits<Count>::max()) {
    it->second.set(cell.row(), cell.column(), 0, static_cast<Count>(current + 1));
  }
  ++total_;
  if (std::isfinite(spread_term_m) && spread_term_m > 0.0) {
    double & recorded = max_spread_term_[cell.grid()];
    recorded = std::max(recorded, spread_term_m);
  }
  invalidateSat(cell.grid());
  invalidateHistograms(cell.grid());
}

CountGrid::Count CountGrid::countAt(const gggs::CellIndex & cell) const
{
  checkCell(cell);
  const Tile * tile = tileAt(cell.grid());
  return tile ? tile->get(cell.row(), cell.column(), 0) : Count{0};
}

double CountGrid::maxSpreadTerm(const gggs::GridIndex & tile) const
{
  auto it = max_spread_term_.find(tile);
  return it == max_spread_term_.end() ? 0.0 : it->second;
}

const CountGrid::Tile * CountGrid::tileAt(const gggs::GridIndex & grid) const
{
  auto it = tiles_.find(grid);
  return it == tiles_.end() ? nullptr : &it->second;
}

void CountGrid::invalidateSat(const gggs::GridIndex & grid) const
{
  for (auto it = sat_cache_.begin(); it != sat_cache_.end(); ++it) {
    if (it->first == grid) {
      sat_cache_.erase(it);
      return;
    }
  }
}

void CountGrid::invalidateHistograms(const gggs::GridIndex & grid) const
{
  // This tile and its eight neighbours: their boxes read this tile's counts.
  for (auto it = histogram_cache_.begin(); it != histogram_cache_.end(); ) {
    const gggs::GridIndex & g = it->first.first;
    const int dr = static_cast<int>(g.row()) - static_cast<int>(grid.row());
    const int dc = static_cast<int>(g.column()) - static_cast<int>(grid.column());
    if (g.level() == grid.level() && dr >= -1 && dr <= 1 && dc >= -1 && dc <= 1) {
      it = histogram_cache_.erase(it);
    } else {
      ++it;
    }
  }
}

// Inclusive 2-D prefix sums with a zero border: sat[(r+1)*(E+1) + (c+1)] is the
// sum of counts over rows [0, r] x cols [0, c]. Saturating at UINT32_MAX (a
// tile can hold up to 960^2 * 65535 in theory; the queries only ever compare
// against a small n_req, so saturation is harmless).
const CountGrid::Sat & CountGrid::satFor(const gggs::GridIndex & grid) const
{
  for (auto it = sat_cache_.begin(); it != sat_cache_.end(); ++it) {
    if (it->first == grid) {
      sat_cache_.splice(sat_cache_.begin(), sat_cache_, it);
      return sat_cache_.front().second;
    }
  }
  constexpr std::size_t stride = static_cast<std::size_t>(kEdge) + 1;
  Sat sat(stride * stride, 0u);
  const Tile * tile = tileAt(grid);
  if (tile) {
    const auto & band = tile->band(0);
    for (std::size_t r = 0; r < kEdge; ++r) {
      uint64_t row_sum = 0;
      for (std::size_t c = 0; c < kEdge; ++c) {
        row_sum += band[r * kEdge + c];
        const uint64_t above = sat[r * stride + (c + 1)];
        sat[(r + 1) * stride + (c + 1)] = static_cast<uint32_t>(
          std::min<uint64_t>(above + row_sum, std::numeric_limits<uint32_t>::max()));
      }
    }
  }
  sat_cache_.emplace_front(grid, std::move(sat));
  while (sat_cache_.size() > kSatCacheSize) {
    sat_cache_.pop_back();
  }
  return sat_cache_.front().second;
}

// Sum over the inclusive cell rectangle [r0, r1] x [c0, c1] of one tile; the
// caller has already clipped the rectangle to [0, kEdge).
uint32_t CountGrid::satSum(const Sat & sat, int r0, int c0, int r1, int c1)
{
  constexpr int stride = static_cast<int>(kEdge) + 1;
  const uint64_t a = sat[(r1 + 1) * stride + (c1 + 1)];
  const uint64_t b = sat[r0 * stride + (c1 + 1)];
  const uint64_t c = sat[(r1 + 1) * stride + c0];
  const uint64_t d = sat[r0 * stride + c0];
  return static_cast<uint32_t>(a - b - c + d);
}

uint64_t CountGrid::countInBox(const gggs::CellIndex & cell, uint32_t lambda) const
{
  checkCell(cell);
  if (lambda > kMaxLambda) {
    throw std::invalid_argument("CountGrid::countInBox: lambda exceeds kMaxLambda");
  }
  const int edge = static_cast<int>(kEdge);
  const int half = static_cast<int>(lambda);
  // Box in cell coordinates relative to the centre tile's origin.
  const int box_r0 = static_cast<int>(cell.row()) - half;
  const int box_r1 = static_cast<int>(cell.row()) + half;
  const int box_c0 = static_cast<int>(cell.column()) - half;
  const int box_c1 = static_cast<int>(cell.column()) + half;

  uint64_t sum = 0;
  const gggs::GridIndex & centre = cell.grid();
  for (int dr = -1; dr <= 1; ++dr) {
    for (int dc = -1; dc <= 1; ++dc) {
      // Neighbour tile by grid row/column arithmetic. GGGS rows grow northward
      // and cell rows grow from the south edge, so a grid at row+1 holds cells
      // [edge, 2*edge) in the centre tile's frame. (Column scaling changes only
      // across the polar latitude bands, outside any survey area this serves.)
      const gggs::GridIndex neighbour = neighbourGrid(centre, dr, dc);
      if (!neighbour.valid() || !tileAt(neighbour)) {
        continue;
      }
      if ((dr != 0 || dc != 0) && neighbour == centre) {
        continue;  // clamped at the world edge: no such neighbour
      }
      // Intersect the box with this tile's cell range [dr*edge, (dr+1)*edge).
      const int r0 = std::max(box_r0, dr * edge) - dr * edge;
      const int r1 = std::min(box_r1, (dr + 1) * edge - 1) - dr * edge;
      const int c0 = std::max(box_c0, dc * edge) - dc * edge;
      const int c1 = std::min(box_c1, (dc + 1) * edge - 1) - dc * edge;
      if (r0 > r1 || c0 > c1) {
        continue;
      }
      sum += satSum(satFor(neighbour), r0, c0, r1, c1);
    }
  }
  return sum;
}

CountGrid::LevelOfAggregation CountGrid::levelOfAggregation(
  const gggs::CellIndex & cell, uint64_t n_req) const
{
  if (n_req == 0) {
    throw std::invalid_argument("CountGrid::levelOfAggregation: n_req must be > 0");
  }
  LevelOfAggregation result;
  if (countInBox(cell, kMaxLambda) < n_req) {
    result.lambda = kMaxLambda;
    result.saturated = true;
    return result;
  }
  // countInBox is monotone non-decreasing in lambda: find the smallest lambda
  // whose box reaches n_req.
  uint32_t lo = 0, hi = kMaxLambda;
  while (lo < hi) {
    const uint32_t mid = lo + (hi - lo) / 2;
    if (countInBox(cell, mid) >= n_req) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  result.lambda = lo;
  return result;
}

double CountGrid::achievedSpacing(const LevelOfAggregation & loa) const noexcept
{
  if (loa.saturated) {
    return std::numeric_limits<double>::infinity();
  }
  return (2.0 * loa.lambda + 1.0) * level_.cellSize();
}

bool CountGrid::tileInside(const gggs::GridIndex & tile, const gggs::GridIndex & coarse)
{
  if (!tile.valid() || !coarse.valid() || tile.level() < coarse.level()) {
    return false;
  }
  if (tile.level() == coarse.level()) {
    return tile == coarse;
  }
  const double lat = 0.5 * (tile.southLatitude() + tile.northLatitude());
  const double lon = 0.5 * (tile.westLongitude() + tile.eastLongitude());
  return gggs::Level(coarse.level()).gridIndex(lat, lon) == coarse;
}

std::optional<double> CountGrid::achievedSpacingPercentile(
  const gggs::GridIndex & coarse, double p, uint64_t n_req) const
{
  if (!coarse.valid()) {
    throw std::invalid_argument("CountGrid::achievedSpacingPercentile: invalid grid");
  }
  if (coarse.level() > level_.level()) {
    throw std::invalid_argument(
            "CountGrid::achievedSpacingPercentile: grid is finer than the count level");
  }
  if (!(p >= 0.0 && p <= 1.0)) {
    throw std::invalid_argument("CountGrid::achievedSpacingPercentile: p must be in [0, 1]");
  }
  if (n_req == 0) {
    throw std::invalid_argument("CountGrid::achievedSpacingPercentile: n_req must be > 0");
  }

  std::vector<double> spacings;
  for (const auto & [grid, tile] : tiles_) {
    if (!tileInside(grid, coarse)) {
      continue;
    }
    const auto & band = tile.band(0);
    for (uint16_t r = 0; r < kEdge; ++r) {
      for (uint16_t c = 0; c < kEdge; ++c) {
        if (band[static_cast<std::size_t>(r) * kEdge + c] == 0) {
          continue;
        }
        spacings.push_back(achievedSpacing(levelOfAggregation(gggs::CellIndex(grid, r, c), n_req)));
      }
    }
  }
  if (spacings.empty()) {
    return std::nullopt;
  }
  // Nearest-rank percentile: the smallest value at or above which p of the
  // votes lie. +infinity (saturated cells) sorts last, so a high percentile
  // landing on one reads as "coarsest".
  std::sort(spacings.begin(), spacings.end());
  const std::size_t rank = static_cast<std::size_t>(std::ceil(p * spacings.size()));
  const std::size_t index = rank == 0 ? 0 : std::min(rank - 1, spacings.size() - 1);
  return spacings[index];
}

const CountGrid::LevelHistogram & CountGrid::achievedLevelHistogram(
  const gggs::GridIndex & tile, uint64_t n_req) const
{
  if (n_req == 0) {
    throw std::invalid_argument("CountGrid::achievedLevelHistogram: n_req must be > 0");
  }
  const Tile * t = tileAt(tile);
  if (!t) {
    throw std::invalid_argument("CountGrid::achievedLevelHistogram: tile is absent");
  }
  const auto key = std::make_pair(tile, n_req);
  auto cached = histogram_cache_.find(key);
  if (cached != histogram_cache_.end()) {
    return cached->second;
  }
  LevelHistogram histogram{};
  const auto & band = t->band(0);
  for (uint16_t r = 0; r < kEdge; ++r) {
    for (uint16_t c = 0; c < kEdge; ++c) {
      if (band[static_cast<std::size_t>(r) * kEdge + c] == 0) {
        continue;
      }
      const auto loa = levelOfAggregation(gggs::CellIndex(tile, r, c), n_req);
      if (loa.saturated) {
        ++histogram[kSaturatedBin];
        continue;
      }
      const double spacing = achievedSpacing(loa);
      // Finest level whose cell is no finer than the spacing (the coarser
      // neighbour of fromCellSize's at-or-finer answer), over the full range.
      const gggs::Level at_or_finer = gggs::Level::fromCellSize(static_cast<float>(spacing));
      int level = at_or_finer.level();
      if (at_or_finer.cellSize() < spacing * (1.0 - 1e-9) && level > 0) {
        level -= 1;
      }
      ++histogram[static_cast<std::size_t>(level)];
    }
  }
  return histogram_cache_.emplace(key, histogram).first->second;
}

std::optional<CountGrid::AchievedLevel> CountGrid::achievedLevelPercentile(
  const gggs::GridIndex & coarse, double p, uint64_t n_req) const
{
  if (!coarse.valid()) {
    throw std::invalid_argument("CountGrid::achievedLevelPercentile: invalid grid");
  }
  if (coarse.level() > level_.level()) {
    throw std::invalid_argument(
            "CountGrid::achievedLevelPercentile: grid is finer than the count level");
  }
  if (!(p >= 0.0 && p <= 1.0)) {
    throw std::invalid_argument("CountGrid::achievedLevelPercentile: p must be in [0, 1]");
  }
  LevelHistogram total{};
  uint64_t votes = 0;
  for (const auto & [grid, tile] : tiles_) {
    if (!tileInside(grid, coarse)) {
      continue;
    }
    const auto & h = achievedLevelHistogram(grid, n_req);
    for (std::size_t i = 0; i < kLevelBins; ++i) {
      total[i] += h[i];
      votes += h[i];
    }
  }
  if (votes == 0) {
    return std::nullopt;
  }
  // Nearest-rank over spacing ascending == level descending: walk from the
  // finest level toward the coarsest, then the saturated bin.
  const uint64_t rank = std::max<uint64_t>(1, static_cast<uint64_t>(std::ceil(p * votes)));
  uint64_t seen = 0;
  for (int level = kSaturatedBin - 1; level >= 0; --level) {
    seen += total[static_cast<std::size_t>(level)];
    if (seen >= rank) {
      return AchievedLevel{static_cast<uint8_t>(level), false};
    }
  }
  return AchievedLevel{0, true};
}

void CountGrid::merge(const CountGrid & other)
{
  if (other.level_.level() != level_.level()) {
    throw std::invalid_argument("CountGrid::merge: level mismatch");
  }
  for (const auto & [grid, tile] : other.tiles_) {
    auto it = tiles_.find(grid);
    if (it == tiles_.end()) {
      it = tiles_.emplace(grid, Tile(grid, 1, Count{0})).first;
    }
    auto & mine = it->second.band(0);
    const auto & theirs = tile.band(0);
    for (std::size_t i = 0; i < mine.size(); ++i) {
      const uint32_t sum = static_cast<uint32_t>(mine[i]) + theirs[i];
      mine[i] = static_cast<Count>(std::min<uint32_t>(sum, std::numeric_limits<Count>::max()));
    }
    it->second.markDirty();
    invalidateSat(grid);
    invalidateHistograms(grid);
  }
  for (const auto & [grid, term] : other.max_spread_term_) {
    double & mine = max_spread_term_[grid];
    mine = std::max(mine, term);
  }
  total_ += other.total_;
}

std::size_t CountGrid::saveTo(const std::string & dir) const
{
  namespace fs = std::filesystem;
  fs::create_directories(dir);
  {
    std::ofstream level_file(fs::path(dir) / kLevelFile);
    if (!level_file) {
      throw std::runtime_error("CountGrid::saveTo: cannot write " + dir + "/" + kLevelFile);
    }
    level_file << static_cast<int>(level_.level()) << "\n";
  }
  {
    std::ofstream spread_file(fs::path(dir) / kSpreadFile);
    if (!spread_file) {
      throw std::runtime_error("CountGrid::saveTo: cannot write " + dir + "/" + kSpreadFile);
    }
    spread_file.precision(17);
    for (const auto & [grid, term] : max_spread_term_) {
      spread_file << centreLatitude(grid) << " " << centreLongitude(grid) << " " << term << "\n";
    }
  }
  std::size_t written = 0;
  for (const auto & [grid, tile] : tiles_) {
    const auto path = fs::path(dir) / (tileName(grid) + ".tif");
    marine_tiled_raster_store::saveTile<Count>(tile, path.string(), {std::nullopt});
    ++written;
  }
  return written;
}

uint8_t CountGrid::levelOf(const std::string & dir)
{
  std::ifstream level_file(std::filesystem::path(dir) / kLevelFile);
  int level = -1;
  if (!(level_file >> level) || level < 0 || level >= static_cast<int>(gggs::levels.size())) {
    throw std::runtime_error("CountGrid::levelOf: no valid " + std::string(kLevelFile) + " in " +
        dir);
  }
  return static_cast<uint8_t>(level);
}

std::size_t CountGrid::mergeFrom(const std::string & dir)
{
  const uint8_t level = levelOf(dir);
  if (level != level_.level()) {
    throw std::invalid_argument(
            "CountGrid::mergeFrom: " + dir + " holds level " + std::to_string(level) +
            " counts, this grid is level " + std::to_string(level_.level()));
  }
  CountGrid loaded(level);
  std::map<gggs::GridIndex, Tile> tiles;
  const std::size_t read = marine_tiled_raster_store::loadTiles<Count>(tiles, dir, level_, 1);
  for (auto & [grid, tile] : tiles) {
    const auto & band = tile.band(0);
    for (const Count c : band) {
      loaded.total_ += c;
    }
    loaded.tiles_.emplace(grid, std::move(tile));
  }
  std::ifstream spread_file(std::filesystem::path(dir) / kSpreadFile);
  std::string line;
  while (std::getline(spread_file, line)) {
    std::istringstream in(line);
    double lat = 0.0, lon = 0.0, term = 0.0;
    if (!(in >> lat >> lon >> term)) {
      continue;
    }
    loaded.max_spread_term_[level_.gridIndex(lat, lon)] = term;
  }
  merge(loaded);
  return read;
}

}  // namespace cube
