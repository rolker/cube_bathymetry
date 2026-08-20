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
#include <iostream>
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
// layout — scratch-only, single-machine; buckets never outlive one run, so the
// layout can change freely. `depth` is the position altitude (the GeoSounding
// ctor sets sounding.depth from point[2]). Sounding fields derived at insert
// time are deliberately NOT serialized: predicted_depth_at_touchdown is
// recomputed by GeoGrid::insert from the primed predicted surface on replay
// (#123), and sonar_relative_position is not read by the insert path.
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
    auto vit = open_streams_.find(victim);
    if (vit != open_streams_.end()) {
      // Flush before the stream is closed+destroyed so a disk-full / I/O failure on
      // the buffered data surfaces HERE. Letting the ofstream destructor swallow a
      // close-time flush failure would truncate this bucket and yield a
      // silently-wrong tile when the gather reads it back.
      vit->second.first.flush();
      if (!vit->second.first) {
        throw std::runtime_error(
          "batch_regen: failed flushing scatter bucket " + bucketPath(victim) +
          " on eviction (disk full?) -- aborting to avoid a silently-truncated tile");
      }
      open_streams_.erase(vit);  // closes the stream
    }
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
  if (soundings.empty()) {
    return;
  }
  // Route the WHOLE batch into every tile addSoundings would create for it, so the
  // scatter mirrors a single unbounded pass EXACTLY. GeoMapSheet::addSoundings takes
  // gridIndicesForSoundings(batch) (the batch's influence-radius-expanded bounds,
  // one-cell floor -- #104) and calls
  // grid->insert(batch) on every grid in that set -- i.e. each such grid sees the
  // ENTIRE batch, not just the soundings whose own centre is near it. A sounding
  // near a tile seam deposits into a neighbour tile's cells out to
  // max_radius = CONF_99PC*sqrt(horizontal_error), MULTIPLE cells for realistic TPU.
  // Scattering each sounding only to its own one-cell window (the earlier approach)
  // dropped those far-radius cross-sounding deposits, so the gather was not bit-exact
  // near seams -- masked only because the tests use a sub-cell horizontal_error.
  // Writing the whole batch to every grid in its expanded bounds reproduces
  // addSoundings' touch set exactly; the gather's GeoGrid::insert re-applies the true
  // per-cell radius test, so a bucketed sounding that does not actually reach the tile
  // is harmlessly filtered (no false deposit) -- the bucket is a superset the radius
  // test trims back to the exact single-pass deposit set.
  for (const auto & idx : index_sheet_->gridIndicesForSoundings(soundings)) {
    std::ofstream & out = bucketStream(idx);
    for (const auto & s : soundings) {
      const ScatterRecord rec = toRecord(s);
      out.write(reinterpret_cast<const char *>(&rec), sizeof(rec));
      if (!out) {
        throw std::runtime_error("batch_regen: failed writing scatter bucket");
      }
    }
  }
}

void BatchRegen::flushOpenStreams()
{
  // Force every still-open bucket's buffered data out and CHECK the result, so a
  // disk-full / I/O error surfaces before the gather reads the buckets back rather
  // than being swallowed by ofstream's destructor in closeAllStreams().
  for (auto & entry : open_streams_) {
    std::ofstream & out = entry.second.first;
    out.flush();
    if (!out) {
      throw std::runtime_error(
        "batch_regen: failed flushing scatter bucket " + bucketPath(entry.first) +
        " (disk full?) -- aborting to avoid a silently-truncated tile");
    }
  }
}

void BatchRegen::closeAllStreams()
{
  // Deliberately swallows close-time flush errors (the ofstream destructor cannot
  // report them). That is only safe because of a strict ordering invariant: on any
  // SUCCESS path a checked flushOpenStreams() MUST run first (finalize() does), so every
  // buffer is already empty and surfaced any disk-full/I/O fault. The only caller that
  // reaches here with unflushed buffers is ~BatchRegen (an exception mid-scatter), where
  // throwing would terminate during stack unwinding -- there, dropping the scratch data
  // we were going to delete anyway is correct. Do NOT call this as the success-path close
  // without a preceding flushOpenStreams().
  open_streams_.clear();  // destructor flushes + closes each ofstream
  lru_.clear();
}

void BatchRegen::finalize(
  const marine_bathymetry_store::StoreMetadata * bathy_metadata,
  const marine_mbes_backscatter_store::StoreMetadata * bs_metadata)
{
  // Flush every bucket and CHECK the result before reading them back, so a disk-full
  // truncation is a hard error here rather than a silently-wrong tile at gather; then
  // close them all.
  flushOpenStreams();
  closeAllStreams();

  // A non-empty output processed layer means batch-regen is rebuilding over a
  // populated store. The gather forces from-scratch (skip_survey_seed below) so it
  // never blends onto the tiles it rebuilds, but tiles NOT touched by this run stay
  // behind as stale processed data mixed with the fresh rebuild -- warn so the
  // operator can point -o at an empty directory for a clean exact rebuild.
  if (!cfg_.store_dir.empty()) {
    const std::string processed_dir = cfg_.store_dir + "/" +
      marine_bathymetry_store::layerDirName(
      marine_bathymetry_store::SourceLayer::Processed);
    std::error_code ec;
    if (std::filesystem::is_directory(processed_dir, ec) &&
      !std::filesystem::is_empty(processed_dir, ec))
    {
      std::cerr << "batch_regen: WARNING output processed layer '" << processed_dir
                << "' is not empty; batch-regen rebuilds each touched tile from "
        "scratch, but any pre-existing tile this run does NOT touch is left in "
        "place (stale data mixed with the rebuild). Point -o at an empty directory "
        "for a clean exact rebuild." << std::endl;
    }
  }

  // No eviction in the gather (each bucket is a single tile). Copy the config with
  // the budget forced unbounded so the gather accumulator never drops a grid, and
  // skip the rung-1 survey warm-start so the gather never double-counts a tile it is
  // rebuilding from its complete bucket (exact rebuild, not a blend).
  ImportAccumulatorConfig gather_cfg = cfg_;
  gather_cfg.max_resident_tiles = 0;
  gather_cfg.skip_survey_seed = true;

  // Gather one tile at a time, in deterministic (sorted GridIndex) order.
  for (const auto & idx : tiles_) {
    const std::string path = bucketPath(idx);
    std::vector<GeoSounding> bucket;
    {
      // Every idx in tiles_ had its bucket created (and written) by bucketStream, so a
      // reopen failure here is a real I/O fault, NOT a benign "missing bucket" -- treat
      // it symmetrically with the hard-throwing scatter/flush path so it cannot silently
      // drop a tile from the "bit-exact" rebuild.
      std::ifstream in(path, std::ios::binary);
      if (!in) {
        throw std::runtime_error(
          "batch_regen: cannot reopen scatter bucket " + path +
          " for gather -- a bucket this run created is unreadable; aborting to avoid a "
          "silently-missing tile");
      }
      ScatterRecord rec;
      while (in.read(reinterpret_cast<char *>(&rec), sizeof(rec))) {
        bucket.push_back(fromRecord(rec));
      }
      // read() sets eofbit+failbit at a clean end-of-file, but badbit on an actual I/O
      // error -- distinguish them so a hardware read fault is not mistaken for EOF. A
      // partial trailing record (gcount != 0 after the failed read) means the bucket was
      // truncated to a non-record boundary; both are silently-wrong tiles, so throw.
      if (in.bad()) {
        throw std::runtime_error(
          "batch_regen: I/O error reading scatter bucket " + path +
          " during gather -- aborting to avoid a silently-truncated tile");
      }
      if (in.gcount() != 0) {
        throw std::runtime_error(
          "batch_regen: scatter bucket " + path +
          " ends with a partial record (truncated) -- aborting to avoid a "
          "silently-wrong tile");
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
