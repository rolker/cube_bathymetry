// Copyright 2026 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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

#include "cube_bathymetry/slope_run.h"

#include <stdlib.h>

#include <algorithm>
#include <iostream>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>

#include "marine_backscatter/bathy_dem.hpp"
#include "marine_mbes_backscatter_store/tile_io.hpp"

namespace cube
{

namespace fs = std::filesystem;
namespace mbs = marine_mbes_backscatter_store;

std::string slopeRunRequestProblem(const SlopeRunRequest & r)
{
  if (r.basis == BackscatterAngleBasis::Beam) {
    if (!r.surface_dir.empty()) {
      return "--slope-surface needs --backscatter-angle-basis incidence (the slope run)";
    }
    if (r.baseline_given) {
      return "--slope-baseline-deg needs --backscatter-angle-basis incidence (the slope run)";
    }
    if (!r.beam_ledger_out.empty()) {
      return "--beam-ledger-out needs --backscatter-angle-basis incidence (the slope run)";
    }
    return "";
  }
  if (r.surface_dir.empty()) {
    return "--backscatter-angle-basis incidence needs --slope-surface <bathy store>: the finished "
           "depth store the slopes are read from";
  }
  if (r.bs_store_dir.empty()) {
    return "--backscatter-angle-basis incidence needs --bs-store <dir>: the slope run publishes "
           "only a backscatter store";
  }
  if (!r.store_dir.empty()) {
    return "-o is refused with --backscatter-angle-basis incidence: a slope run writes no depths "
           "(the depths are the ordinary import's)";
  }
  if (r.depth_adaptive) {
    return "--depth-adaptive is refused with --backscatter-angle-basis incidence: the slope run "
           "is fixed-level, like the ordinary import it follows";
  }
  if (r.ping_limit != 0) {
    return "-l is refused with --backscatter-angle-basis incidence: a ping limit would publish "
           "tiles built from incomplete contributors";
  }
  if (!(r.baseline_deg > 0.0 && r.baseline_deg <= kMaxSlopeBaselineDeg)) {
    return "--slope-baseline-deg must be in (0, " + std::to_string(
      static_cast<int>(kMaxSlopeBaselineDeg)) + "] degrees";
  }
  return "";
}

std::string slopeCorrectionProblem(const std::string & kind, const std::string & source)
{
  if (kind != "curve") {
    return "";
  }
  if (source.rfind("sonar_info:", 0) == 0) {
    return "the angular-response curve comes from the bag's SonarInfo (" + source + "), which is "
           "a beam-angle curve; an incidence run applies a per-pulse-mode incidence table or "
           "nothing. Pass --backscatter-correction none (or an incidence table with "
           "--backscatter-curve)";
  }
  return "--backscatter-curve '" + source + "' is a single curve; a single curve has no "
         "incidence form. An incidence run applies a per-pulse-mode incidence table or nothing";
}

std::vector<std::string> slopeSurfaceLayers(const std::string & surface_dir)
{
  std::vector<std::string> layers;
  std::error_code ec;
  for (const char * layer : {"processed", "draft"}) {
    const fs::path dir = fs::path(surface_dir) / layer;
    bool has_tile = false;
    if (fs::is_directory(dir, ec)) {
      for (const auto & entry : fs::directory_iterator(dir, ec)) {
        if (isTileFileName(entry.path().filename().string())) {
          has_tile = true;
          break;
        }
      }
    }
    if (has_tile) {
      layers.push_back(layer);
    }
  }
  return layers;
}

namespace
{

std::vector<std::string> splitLayers(const std::string & csv)
{
  std::vector<std::string> out;
  std::stringstream ss(csv);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty()) {
      out.push_back(item);
    }
  }
  return out;
}

/// True when the directory holds any regular file other than the lock.
bool holdsFiles(const std::string & dir)
{
  std::error_code ec;
  if (!fs::is_directory(dir, ec)) {
    return false;
  }
  for (const auto & entry : fs::recursive_directory_iterator(dir, ec)) {
    if (entry.is_regular_file(ec) && entry.path().filename() != DerivedTiles::kLockFilename) {
      return true;
    }
  }
  return false;
}

}  // namespace

SlopeRun::SlopeRun(const SlopeRunConfig & config)
: config_(config)
{
  options_.baseline_deg = config.baseline_deg;
}

std::unique_ptr<SlopeRun> SlopeRun::create(const SlopeRunConfig & config, std::string & error)
{
  error.clear();
  std::unique_ptr<SlopeRun> run(new SlopeRun(config));
  const std::string & bs = config.bs_store_dir;

  // 1. The lock, before anything is read or repaired.
  run->lock_ = StoreLock::acquire(bs, error);
  if (!run->lock_) {
    return nullptr;
  }
  // 2. An interrupted publish is completed or cleaned up first, whatever bags
  //    this run was given.
  try {
    run->recovery_text_ = recoverInterruptedPublish(bs).text();
  } catch (const std::exception & e) {
    error = std::string("--bs-store ") + bs + ": " + e.what();
    return nullptr;
  }
  // 3. The store: empty, or already a slope-run store with the same inputs.
  try {
    run->previous_ = DerivedTiles::load(bs);
  } catch (const std::exception & e) {
    error = std::string("--bs-store ") + bs + ": " + e.what();
    return nullptr;
  }
  if (!run->previous_) {
    if (holdsFiles(bs)) {
      error = "--bs-store " + bs + " holds backscatter tiles but no derived_tiles.json: it is a "
        "beam-angle backscatter store, and a slope run's tiles are a different product. Use a new "
        "directory for the slope-corrected layer";
      return nullptr;
    }
  } else {
    const std::string difference = run->previous_->inputs.difference(config.inputs);
    if (!difference.empty()) {
      error = "--bs-store " + bs + " is a slope-run store and " + difference + ". One store holds "
        "one correction: use a new directory, or delete this one";
      return nullptr;
    }
  }
  if (!config.beam_ledger_path.empty()) {
    run->ledger_ = BeamLedgerWriter::open(
      config.beam_ledger_path, config.inputs, config.bags, error);
    if (!run->ledger_) {
      return nullptr;
    }
  }
  // 4. The surface.
  const std::vector<std::string> layers = splitLayers(config.inputs.slope_layers);
  if (layers.empty()) {
    error = "--slope-surface " + config.surface_dir +
      " has neither a processed nor a draft layer with tiles";
    return nullptr;
  }
  try {
    auto dem = std::make_unique<marine_backscatter::BathyDem>(
      config.surface_dir, layers, config.surface_cache_tiles);
    for (const std::string & warning : dem->warnings()) {
      std::cerr << "warning: --slope-surface " << config.surface_dir << ": " << warning << "\n";
    }
    std::cout << "Slope surface: " << dem->describe() << std::endl;
    if (!dem->levels().empty()) {
      // The depth tiles a slope was read from can lie this far beyond a probe:
      // the bilinear stencil of the coarsest level present, twice.
      run->surface_pad_m_ = 2.0 * gggs::levels[static_cast<std::size_t>(dem->levels().back())]
        .nominal_cell_size;
    }
    run->surface_ = std::move(dem);
  } catch (const std::exception & e) {
    error = "--slope-surface " + config.surface_dir + ": " + e.what();
    return nullptr;
  }
  try {
    run->surface_index_ = std::make_unique<SurfaceTileIndex>(config.surface_dir, layers,
                                                                                         /*eager=*/
        true);
  } catch (const std::exception & e) {
    error = "--slope-surface " + config.surface_dir + ": " + e.what();
    return nullptr;
  }
  // 5. A scratch directory unique to this run.
  // A trailing slash on --bs-store would make parent_path() the store itself.
  fs::path store_path = fs::path(bs).lexically_normal();
  if (store_path.filename().empty()) {
    store_path = store_path.parent_path();
  }
  fs::path root = config.scratch_root.empty() ?
    store_path.parent_path() : fs::path(config.scratch_root);
  if (root.empty()) {
    root = ".";
  }
  std::error_code ec;
  fs::create_directories(root, ec);
  if (ec) {
    error = "cannot create the scratch root " + root.string() + ": " + ec.message();
    return nullptr;
  }
  std::string tmpl = (root / "slope-run-XXXXXX").string();
  std::vector<char> buffer(tmpl.begin(), tmpl.end());
  buffer.push_back('\0');
  if (::mkdtemp(buffer.data()) == nullptr) {
    error = "cannot create a scratch directory under " + root.string() + ": " +
      std::strerror(errno);
    return nullptr;
  }
  run->scratch_ = buffer.data();
  fs::create_directories(run->scratchSpillDir(), ec);
  // Leftovers of runs that were killed before they could remove their scratch.
  std::ostringstream leftovers;
  for (const auto & entry : fs::directory_iterator(root, ec)) {
    const std::string name = entry.path().filename().string();
    if (name.rfind("slope-run-", 0) == 0 && entry.path() != fs::path(run->scratch_)) {
      leftovers << "  " << entry.path().string() << "\n";
    }
  }
  if (!leftovers.str().empty()) {
    run->leftover_text_ = "Found scratch directories of other slope runs beside this one:\n" +
      leftovers.str() + "If no other slope run is in progress they are leftovers of a killed "
      "run and can be deleted; they do not affect this run.\n";
  }
  return run;
}

SlopeRun::~SlopeRun()
{
  if (!scratch_.empty()) {
    std::error_code ec;
    fs::remove_all(scratch_, ec);  // best effort; never throws from a destructor
  }
}

std::string SlopeRun::scratchDepthDir() const {return (fs::path(scratch_) / "depth").string();}
std::string SlopeRun::scratchBsDir() const {return (fs::path(scratch_) / "bs").string();}
std::string SlopeRun::scratchSpillDir() const {return (fs::path(scratch_) / "spill").string();}

const std::set<std::size_t> * SlopeRun::contributorsOf(const gggs::GridIndex & tile) const
{
  const auto it = reached_.find(tile);
  return it == reached_.end() ? nullptr : &it->second;
}

bool SlopeRun::prepare(
  std::vector<GeoSounding> & soundings, std::size_t bag_index, int64_t ping_ns,
  const GeoMapSheet & sheet, std::string & error)
{
  try {
    stampIncidence(soundings, *surface_, options_, counters_);
  } catch (const std::exception & e) {
    error = std::string("the slope surface could not be read: ") + e.what();
    return false;
  }
  if (ledger_) {
    std::vector<LedgerBeam> beams;
    beams.reserve(soundings.size());
    for (const GeoSounding & gs : soundings) {
      const Sounding & snd = gs.sounding;
      if (snd.beam_index != Sounding::kNoBeamIndex) {
        beams.push_back({snd.beam_index, ledgerIncidence(snd.incidence_angle),
            snd.incidence_source});
      }
    }
    ledger_->addPing(ping_ns, static_cast<uint16_t>(bag_index), beams);
  }
  for (const GeoSounding & s : soundings) {
    const double radius = sheet.parameters().influenceRadius(s.sounding);
    if (std::isfinite(radius)) {
      max_influence_m_ = std::max(max_influence_m_, radius);
    }
  }
  // Every tile this batch can reach gets this ping's bag. This is the bounding
  // box of the batch grown by each sounding's influence radius, so it
  // over-attributes at edges: at worst an extra refusal later, never a missed
  // contributor. (GeoMapSheet::dirtyGrids is not used: eviction erases from it.)
  for (const gggs::GridIndex & index : sheet.gridIndicesForSoundings(soundings)) {
    const auto [it, first_touch] = reached_.try_emplace(index);
    it->second.insert(bag_index);
    if (first_touch && previous_) {
      const std::string name = mbs::tileFilename(index);
      const auto recorded = previous_->tiles.find(name);
      if (recorded != previous_->tiles.end()) {
        const std::string problem = contributorProblem(name, recorded->second, config_.bags);
        if (!problem.empty()) {
          error = problem + ". Nothing has been published.";
          return false;
        }
      }
    }
  }
  return true;
}

bool SlopeRun::publish(
  std::string & error, std::string & summary,
  const PublishFailpoint & failpoint)
{
  error.clear();
  const fs::path scratch_survey = fs::path(scratchBsDir()) /
    mbs::layerDirName(mbs::SourceLayer::Survey);
  std::set<std::string> reached_names;
  std::map<std::string, gggs::GridIndex> index_of;
  for (const auto & [index, bags] : reached_) {
    const std::string name = mbs::tileFilename(index);
    reached_names.insert(name);
    index_of.emplace(name, index);
  }
  // Every tile the scratch store holds must be one the bookkeeping reached;
  // otherwise a contributor went unrecorded and the publish is unsafe.
  std::vector<std::string> put;
  std::error_code ec;
  if (fs::is_directory(scratch_survey, ec)) {
    for (const auto & entry : fs::directory_iterator(scratch_survey, ec)) {
      const std::string name = entry.path().filename().string();
      if (!isTileFileName(name)) {
        continue;
      }
      if (!reached_names.count(name)) {
        error = "internal error: the scratch store holds tile " + name + ", which no batch was "
          "attributed to; nothing is published";
        return false;
      }
      put.push_back(name);
    }
  }
  std::sort(put.begin(), put.end());
  std::vector<std::string> remove;
  for (const std::string & name : reached_names) {
    if (std::binary_search(put.begin(), put.end(), name)) {
      continue;
    }
    // Reached but left with no cells: any tile already published for it is stale.
    const bool recorded = previous_ && previous_->tiles.count(name);
    // Only a tile the manifest owns is deleted; an unrecorded file is not ours.
    if (recorded) {
      remove.push_back(name);
    }
  }

  DerivedTiles manifest;
  manifest.inputs = config_.inputs;
  if (previous_) {
    for (const auto & [name, record] : previous_->tiles) {
      if (!reached_names.count(name)) {
        manifest.tiles[name] = record;  // untouched by this run: carried over
      }
    }
  }
  // The surface must be what it was when the run began reading it (hashed
  // eagerly at create): a tile rewritten meanwhile fails the run here, before
  // the commit point.
  const std::string changed = surface_index_->verifyUnchanged();
  if (!changed.empty()) {
    error = changed + ". Nothing is published; re-run once the depth store is stable";
    return false;
  }
  const double margin_m = counters_.max_half_baseline_m + max_influence_m_ + surface_pad_m_;
  const std::string written = utcTimestamp();
  try {
    for (const std::string & name : put) {
      TileRecord record;
      std::set<BagIdentity> bags;
      for (const std::size_t b : reached_.at(index_of.at(name))) {
        bags.insert(config_.bags.at(b));
      }
      record.contributors.assign(bags.begin(), bags.end());
      const auto bounds = tileBoundsFromName(name);
      if (!bounds) {
        error = "internal error: " + name + " is not a tile name";
        return false;
      }
      record.depth_tiles = surface_index_->tilesNear(*bounds, margin_m);
      record.stencil_margin_m = margin_m;
      record.dropped_georef_pings = dropped_georef_;
      record.written = written;
      record.code_version = kDerivedTilesCodeVersion;
      manifest.tiles[name] = record;
    }
  } catch (const std::exception & e) {
    error = std::string("could not record the depth tiles: ") + e.what();
    return false;
  }

  std::ostringstream os;
  os << "Slope run: " << reached_.size() << " tile(s) reached, " << put.size()
     << " with backscatter to publish, " << remove.size() << " reached with no cells (their "
    "published tiles are removed), " << (manifest.tiles.size() - put.size())
     << " earlier tile(s) untouched. Depth-tile margin " << margin_m << " m ("
     << surface_index_->hashedCount() << " surface tile(s) hashed of "
     << surface_index_->tileCount() << ").\n";
  if (put.empty()) {
    error = "no cell had finite backscatter, so there is no product to publish. Check that the "
      "detections carry intensities, that the correction does not exclude every beam, and "
      "that the soundings fall on the slope surface. Nothing was published";
    return false;
  }
  if (dropped_georef_ > 0) {
    os << "NOTE: " << dropped_georef_ << " ping(s) were dropped for want of an earth "
      "transform (as an ordinary import tolerates); recorded in every tile entry as "
      "dropped_georef_pings.\n";
  }
  PublishPlan plan;
  plan.store_dir = config_.bs_store_dir;
  plan.run_id = fs::path(scratch_).filename().string();
  plan.scratch_survey_dir = scratch_survey.string();
  plan.put_tiles = put;
  plan.delete_tiles = remove;
  const fs::path registry = fs::path(scratchBsDir()) / "registry.json";
  // registry.json (platform, sensor, survey) describes the store, not this run's
  // subset of tiles: the first publish installs it and later ones leave it be.
  if (fs::exists(registry, ec) && !fs::exists(fs::path(config_.bs_store_dir) / "registry.json",
      ec))
  {
    plan.registry_source = registry.string();
  }
  plan.manifest = manifest;
  try {
    if (ledger_) {
      ledger_->finish();
    }
  } catch (const std::exception & e) {
    error = std::string("the beam ledger could not be written: ") + e.what() +
      ". Nothing was published";
    return false;
  }
  try {
    publishDerived(plan, failpoint);
  } catch (const std::exception & e) {
    if (ledger_) {
      ledger_->discard();  // a ledger is only left by a run that completed
    }
    error = std::string("the publish failed: ") + e.what() + ". The next slope run on " +
      config_.bs_store_dir + " completes it (if it had committed) or cleans it up.";
    return false;
  }
  os << "Published " << put.size() << " backscatter tile(s) to " << config_.bs_store_dir
     << " (derived_tiles.json updated).\n";
  if (ledger_) {
    os << "Wrote the beam ledger " << config_.beam_ledger_path << " (" << ledger_->pings()
       << " ping(s)).\n";
  }
  summary = os.str();
  return true;
}

}  // namespace cube
