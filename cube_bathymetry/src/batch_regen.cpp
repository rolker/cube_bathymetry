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


#include "cube_bathymetry/batch_regen.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <vector>

#include "marine_autonomy/gz4d_geo.h"
#include "marine_bathymetry_store/tile_io.hpp"

namespace cube
{

namespace
{
// Bound on simultaneously-open bucket streams so a wide survey never exhausts file
// descriptors. Scatter is spatially local (consecutive pings touch adjacent tiles),
// so a small LRU keeps nearly every write on an already-open stream.
constexpr std::size_t kMaxOpenStreams = 128;

// One scattered sounding: the fields the CUBE insert path reads (geo_grid.cpp /
// node.cpp), so a reconstructed GeoSounding drives an identical estimate. Native
// layout — scratch-only, single-machine. `depth` is the position altitude (the
// GeoSounding ctor sets sounding.depth from point[2]).
#pragma pack(push, 1)
struct ScatterRecord
{
  double latitude;
  double longitude;
  double depth;
  float vertical_error;
  float horizontal_error;
  float intensity;
  float beam_angle;
  float slant_range;
  float predicted_depth_at_touchdown;
};
#pragma pack(pop)

ScatterRecord toRecord(const GeoSounding & s)
{
  ScatterRecord r;
  r.latitude = s.latitude;
  r.longitude = s.longitude;
  r.depth = static_cast<double>(s.sounding.depth);
  r.vertical_error = s.sounding.vertical_error;
  r.horizontal_error = s.sounding.horizontal_error;
  r.intensity = s.sounding.intensity;
  r.beam_angle = s.sounding.beam_angle;
  r.slant_range = s.sounding.slant_range;
  r.predicted_depth_at_touchdown = s.sounding.predicted_depth_at_touchdown;
  return r;
}

GeoSounding fromRecord(const ScatterRecord & r)
{
  GeoSounding s(gz4d::GeoPointLatLongDegrees(r.latitude, r.longitude, r.depth));
  s.sounding.vertical_error = r.vertical_error;
  s.sounding.horizontal_error = r.horizontal_error;
  s.sounding.intensity = r.intensity;
  s.sounding.beam_angle = r.beam_angle;
  s.sounding.slant_range = r.slant_range;
  s.sounding.predicted_depth_at_touchdown = r.predicted_depth_at_touchdown;
  return s;
}
}  // namespace

BatchRegen::BatchRegen(SheetFactory factory, ImportAccumulatorConfig config)
: factory_(std::move(factory)), cfg_(std::move(config)), index_sheet_(factory_())
{
}

BatchRegen::~BatchRegen()
{
  // RAII safety net: finalize() normally cleans up, but an exception mid-scatter
  // must not leak the scratch buckets.
  closeAllStreams();
  cleanupScratch();
}

std::string BatchRegen::bucketPath(const gggs::GridIndex & index) const
{
  // Reuse the bathy tile naming for a unique per-tile stem.
  return scratch_dir_ + "/" + marine_bathymetry_store::tileFilename(index) + ".bin";
}

std::ofstream & BatchRegen::bucketStream(const gggs::GridIndex & index)
{
  auto oit = open_streams_.find(index);
  if (oit != open_streams_.end()) {
    // Already open: move to the front of the LRU and reuse.
    lru_.splice(lru_.begin(), lru_, oit->second.second);
    return oit->second.first;
  }

  // Lazily create the scratch dir on first scatter. mkdtemp atomically creates a
  // uniquely-named directory (retries internally on collision), so two concurrent
  // batch_regen processes can never share a scatter dir and cross-corrupt buckets.
  if (scratch_dir_.empty()) {
    std::string tmpl =
      (std::filesystem::temp_directory_path() / "cube_regen_scatter_XXXXXX").string();
    std::vector<char> buf(tmpl.begin(), tmpl.end());
    buf.push_back('\0');
    if (::mkdtemp(buf.data()) == nullptr) {
      throw std::runtime_error(
        std::string("batch_regen: cannot create scatter scratch dir from template ") +
        tmpl + ": " + std::strerror(errno));
    }
    scratch_dir_ = std::string(buf.data());
  }

  // First time we see this tile: truncate (fresh bucket). A later reopen after LRU
  // eviction appends, so the bucket accumulates the full sounding sequence in order.
  const bool first_time = tiles_.insert(index).second;
  const std::ios::openmode mode = std::ios::binary |
    (first_time ? std::ios::trunc : std::ios::app);

  // Evict the least-recently-used open stream(s) to stay under the FD budget.
  while (open_streams_.size() >= kMaxOpenStreams && !lru_.empty()) {
    const gggs::GridIndex victim = lru_.back();
    lru_.pop_back();
    open_streams_.erase(victim);  // closes the stream
  }

  const std::string path = bucketPath(index);
  std::ofstream out(path, mode);
  if (!out) {
    throw std::runtime_error("batch_regen: cannot open scatter bucket " + path);
  }
  lru_.push_front(index);
  auto res = open_streams_.emplace(
    index, std::make_pair(std::move(out), lru_.begin()));
  return res.first->second.first;
}

void BatchRegen::addBatch(const std::vector<GeoSounding> & soundings)
{
  // Scatter each sounding into EVERY tile it would touch — its one-cell-expanded
  // window (gridIndicesForSoundings on the singleton), the same set addSoundings
  // spreads it into. So a tile's bucket ends up holding exactly the soundings a
  // single unbounded pass would feed that tile's grid, in global scatter order.
  for (const auto & s : soundings) {
    const std::vector<GeoSounding> one{s};
    for (const auto & idx : index_sheet_->gridIndicesForSoundings(one)) {
      std::ofstream & out = bucketStream(idx);
      const ScatterRecord rec = toRecord(s);
      out.write(reinterpret_cast<const char *>(&rec), sizeof(rec));
      if (!out) {
        throw std::runtime_error("batch_regen: failed writing scatter bucket");
      }
    }
  }
}

void BatchRegen::closeAllStreams()
{
  open_streams_.clear();  // destructor flushes + closes each ofstream
  lru_.clear();
}

void BatchRegen::finalize(
  const marine_bathymetry_store::StoreMetadata * bathy_metadata,
  const marine_mbes_backscatter_store::StoreMetadata * bs_metadata)
{
  // Flush and close every bucket before reading them back.
  closeAllStreams();

  // No eviction in the gather (each bucket is a single tile). Copy the config with
  // the budget forced unbounded so the gather accumulator never drops a grid.
  ImportAccumulatorConfig gather_cfg = cfg_;
  gather_cfg.max_resident_tiles = 0;

  // Gather one tile at a time, in deterministic (sorted GridIndex) order.
  for (const auto & idx : tiles_) {
    const std::string path = bucketPath(idx);
    std::vector<GeoSounding> bucket;
    {
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        continue;  // best-effort: a missing bucket just yields no cells for this tile
      }
      ScatterRecord rec;
      while (in.read(reinterpret_cast<char *>(&rec), sizeof(rec))) {
        bucket.push_back(fromRecord(rec));
      }
    }
    if (bucket.empty()) {
      continue;
    }
    // Fresh sheet + single-tile accumulator: replays the bucket in one pass (never
    // evicts), applies the survey/reference seed precedence via addBatch, then
    // writes ONLY the target tile (not the neighbour grids a seam sounding created).
    std::unique_ptr<GeoMapSheet> sheet = factory_();
    ImportAccumulator acc(*sheet, gather_cfg);
    acc.addBatch(bucket);
    acc.persistResidentTile(idx);
    bathy_persisted_ += acc.bathyTilesPersisted();
    bs_persisted_ += acc.backscatterTilesPersisted();
  }

  // Store-level provenance sidecars, written once (uma#248 StoreMetadata).
  if (!cfg_.store_dir.empty() && bathy_metadata != nullptr &&
    !bathy_metadata->empty())
  {
    std::filesystem::create_directories(cfg_.store_dir);
    bathy_metadata->save(cfg_.store_dir);
  }
  if (!cfg_.bs_store_dir.empty() && bs_metadata != nullptr &&
    !bs_metadata->empty())
  {
    std::filesystem::create_directories(cfg_.bs_store_dir);
    bs_metadata->save(cfg_.bs_store_dir);
  }

  // The scatter buckets were only needed to bucket-by-tile; the rebuild is done.
  cleanupScratch();
}

void BatchRegen::cleanupScratch()
{
  if (!scratch_dir_.empty()) {
    std::error_code ec;
    std::filesystem::remove_all(scratch_dir_, ec);  // best-effort; never throws here
    scratch_dir_.clear();
  }
}

}  // namespace cube
