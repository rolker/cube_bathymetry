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

#ifndef CUBE_BATHYMETRY__DERIVED_TILES_H_
#define CUBE_BATHYMETRY__DERIVED_TILES_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

/// @file
/// @brief The record of what a slope-corrected backscatter store was derived
///        from, and the crash-safe way to publish into it (cube_bathymetry#173,
///        decision 13).
///
/// The slope-corrected layer is a DERIVED product: each of its tiles is
/// regenerated from the bags that touched it and from the finished depth tiles
/// its slopes were read from. `derived_tiles.json` at the root of such a store
/// records, per tile, which bags contributed and which depth tiles went in, and
/// per store the inputs that must be the same everywhere (angle basis, slope
/// settings, correction, gain declaration, level). A later tool can compare it
/// with the present depth store to list stale tiles; this file does not do that.
///
/// It is the staleness record of a slope-run store the way
/// `build_fingerprint.json` (ADR-0003) is of an ordinary one, and a slope-run
/// store never carries a build fingerprint.
///
/// ### The file
/// ```json
/// {
///   "format": "derived-tiles v1", "schema_version": 1,
///   "inputs": { "angle_basis": "incidence", "slope_baseline_deg": 5.0,
///               "slope_layers": "processed,draft", "correction": "none" or
///               "table:<sha256>", "gain_declaration_sha256": "", "gain_reference_state": "",
///               "level": 14 },
///   "tiles": { "14_9_8.tif": { "contributors": [{"name": ..., "metadata_sha256": ...}],
///                              "depth_tiles": [{"path": "processed/14_9_8.tif", "sha256": ...}],
///                              "stencil_margin_m": 12.5, "written": "...Z",
///                              "code_version": "..." } },
///   "last_publish": { "run": ..., "written": [...], "removed": [...], "registry": true }
/// }
/// ```
/// The loader is strict: an unknown version, a missing or unknown key, a wrong
/// type or a duplicate key is an error, never a default.
///
/// ### The publish
/// A slope run builds its tiles in a scratch store and publishes them into the
/// destination as one unit (see `publishDerived`). Its commit point is the
/// existence of `derived_tiles.json.pending`; an interrupted publish is rolled
/// forward (after the commit point) or cleaned up (before it) by
/// `recoverInterruptedPublish`, the first thing the next slope run does.

namespace cube
{

/// Algorithm version recorded with every tile. Bump it when the stamping or the
/// correction of a slope run changes in a way that makes old tiles different.
  constexpr const char * kDerivedTilesCodeVersion = "slope-run-1";

/// A bag as the manifest knows it: the normalised directory name and the
/// SHA-256 of its `metadata.yaml`. A copy of a bag on another disk is the same
/// bag; two different bags that share a name differ in the hash.
  struct BagIdentity
  {
    std::string name;
    std::string metadata_sha256;

    bool operator == (const BagIdentity & o) const
    {
      return name == o.name && metadata_sha256 == o.metadata_sha256;
    }
    bool operator < (const BagIdentity & o) const
    {
      return name != o.name ? name < o.name : metadata_sha256 < o.metadata_sha256;
    }
  };

/// The bag's directory name with trailing slashes stripped and no parents.
/// @throws std::runtime_error for an empty path or one with no name.
  std::string normalizedBagName(const std::string & path);

/// Identity of the bag directory @p path.
/// @throws std::runtime_error if @p path is not a directory holding a
///   readable `metadata.yaml` (a bag that cannot be identified cannot be
///   recorded as a contributor).
  BagIdentity bagIdentity(const std::string & path);

/// Empty when every bag has its own name; otherwise the reason. Two bags with
/// one name are refused whether or not they are the same bag: contributor
/// identity is the name, and a bag given twice would be double-counted.
  std::string duplicateBagProblem(const std::vector < BagIdentity > &bags);

  struct DepthTileRecord
  {
  /// Path relative to the surface store, e.g. `processed/14_9_8.tif`.
    std::string path;
    std::string sha256;
    bool operator == (const DepthTileRecord & o) const {return path == o.path && sha256 == o.sha256;
    }
  };

/// What is the same for every tile of a store.
  struct StoreInputs
  {
    std::string angle_basis = "incidence";
    double slope_baseline_deg = 0.0;
    std::string slope_layers;
  /// `none`, or `table:<sha256 of the table>`.
    std::string correction = "none";
  /// Empty without a gain declaration.
    std::string gain_declaration_sha256;
    std::string gain_reference_state;
    int level = 0;
  /// `--backscatter-mode-fallback` (identity or exclude), the ping floor
  /// (`--backscatter-mode-min-pings`), `--iho-order` and the capture spacing
  /// scale: each changes which beams reach a tile or what a tile holds.
    std::string mode_fallback = "identity";
    int64_t mode_min_pings = 0;
    std::string iho_order = "order1a";
    double capture_spacing_scale = 0.71;
  /// Identity of the prior the run was gated by (`--reference-store`): empty
  /// without one, else `sha256:<digest>` from @ref referenceStoreDigest, which is
  /// of the content of its reference and chart tiles, so it does not depend on
  /// where the store lives.
    std::string reference_store;

    bool operator == (const StoreInputs & o) const;
  /// Empty when @p o equals this; otherwise the first difference, in words.
    std::string difference(const StoreInputs & o) const;
  };

  struct TileRecord
  {
  /// Sorted, no duplicates.
    std::vector < BagIdentity > contributors;
  /// Sorted by path.
    std::vector < DepthTileRecord > depth_tiles;
    double stencil_margin_m = 0.0;
  /// Pings the run dropped for want of an earth transform (the ordinary
  /// run's tolerance), run-wide: how complete the contributors' beams were.
    uint64_t dropped_georef_pings = 0;
  /// UTC, `YYYY-MM-DDTHH:MM:SSZ`, from the clock.
    std::string written;
    std::string code_version;
  };

/// What the last publish did; the roll-forward after a crash reads it.
  struct PublishRecord
  {
    std::string run;
  /// Tile file names moved into place.
    std::vector < std::string > written;
  /// Tile file names deleted (touched by the run but left with no cells).
    std::vector < std::string > removed;
  /// Whether `registry.json` was installed.
    bool registry = false;
  };

  struct DerivedTiles
  {
    static constexpr const char * kFilename = "derived_tiles.json";
    static constexpr const char * kPendingFilename = "derived_tiles.json.pending";
    static constexpr const char * kLockFilename = ".slope-run.lock";
    static constexpr int kSchemaVersion = 1;

    StoreInputs inputs;
  /// Keyed by tile file name (`<level>_<row>_<col>.tif`).
    std::map < std::string, TileRecord > tiles;
    std::optional < PublishRecord > last_publish;

  /// Deterministic text (sorted keys, 2-space indent, trailing newline).
    std::string toJson() const;
  /// Strict parse. @throws std::runtime_error naming what is wrong.
    static DerivedTiles fromJson(const std::string & text);
  /// The record in @p store_dir, or nullopt when there is none.
  /// @throws std::runtime_error when the file exists and is malformed.
    static std::optional < DerivedTiles > load(const std::string & store_dir);
  /// Write @p store_dir/derived_tiles.json durably (temp, fsync, rename).
    void write(const std::string & store_dir) const;
  };

/// True when @p store_dir holds a slope-run record (committed or pending): an
/// ordinary run, batch_regen_bag and the live node refuse such a store.
  bool hasDerivedTilesRecord(const std::string & store_dir);

/// Empty when this run may proceed; otherwise why it cannot (cube_bathymetry#173
/// decision 13, the first-touch refusal). Every contributor recorded for the
/// tile must be among @p given, by name and metadata hash.
  std::string contributorProblem(
    const std::string & tile_name, const TileRecord & recorded,
    const std::vector < BagIdentity > &given);

/// Content digest of the prior store at @p dir for @ref StoreInputs::reference_store:
/// SHA-256 over the sorted (relative path, content SHA-256) of every tile file in
/// its `reference/` and `chart/` layers (the layers a run primes its blunder gate
/// from). Empty @p dir gives the empty string.
/// @throws std::runtime_error if a tile cannot be read.
  std::string referenceStoreDigest(const std::string & dir);

/// UTC now as `YYYY-MM-DDTHH:MM:SSZ`.
  std::string utcTimestamp();

/// Geographic extent of a GGGS tile, degrees.
  struct TileBounds
  {
    double south = 0.0;
    double north = 0.0;
    double west = 0.0;
    double east = 0.0;
  };

/// True for `<level>_<row>_<col>.tif`.
  bool isTileFileName(const std::string & name);
/// Extent of the tile named @p file_name, or nullopt when it is not a valid
/// GGGS tile name.
  std::optional < TileBounds > tileBoundsFromName(const std::string & file_name);

/// The depth tiles of a surface store that a tile's slopes can have been read
/// from. Scans the layer directories once; hashes a tile's content (streamed
/// SHA-256) the first time it is asked for, once per run.
  class SurfaceTileIndex
  {
public:
  /// @param layers layer directory names under @p surface_root (a missing one is
  ///        skipped; the scan finds the tiles of the ones present).
  /// @param eager hash every tile now, before anything is read from the
  ///        surface, so the recorded hash is of what the run read
  ///        (@ref verifyUnchanged then catches a change during the run).
  ///
  /// Known limit: the extent test does not wrap at the antimeridian, so a tile
  /// within the margin of +-180 deg longitude is not matched to the surface tiles
  /// on the other side.
    SurfaceTileIndex(
      const std::string & surface_root, const std::vector < std::string > &layers,
      bool eager = false);

  /// Re-hash every tile hashed so far; empty when all are unchanged, else a
  /// sentence naming the first that differs (or cannot be read).
    std::string verifyUnchanged();

  /// Every tile file, at any level, of the listed layers whose extent intersects
  /// @p tile grown by @p margin_m on every side, with its content hash; sorted
  /// by path. @throws std::runtime_error if a tile cannot be read.
    std::vector < DepthTileRecord > tilesNear(const TileBounds & tile, double margin_m);

    std::size_t tileCount() const {return entries_.size();}
    std::size_t hashedCount() const {return hashed_;}

private:
    struct Entry
    {
      std::string relative;
      std::string path;
      TileBounds bounds;
      std::string sha256;
      bool hashed = false;
    };
    std::vector < Entry > entries_;
    std::size_t hashed_ = 0;
  };

/// An exclusive advisory lock on `<store_dir>/.slope-run.lock`, held for the
/// life of the object. Two slope runs on one store would interleave between
/// the manifest check and the moves, so the second is refused at once.
  class StoreLock
  {
public:
  /// The lock, or null with @p error set (another run holds it, or the lock
  /// file cannot be created). Creates @p store_dir if needed.
    static std::unique_ptr < StoreLock > acquire(const std::string & store_dir,
      std::string & error);
    ~StoreLock();
    StoreLock(const StoreLock &) = delete;
    StoreLock & operator = (const StoreLock &) = delete;

private:
    explicit StoreLock(int fd)
    : fd_(fd) {
    }
    int fd_;
  };

/// What `recoverInterruptedPublish` found and did.
  struct RecoveryReport
  {
  /// A `.pending` manifest was found and its publish completed.
    bool rolled_forward = false;
    std::string rolled_run;
  /// Incoming directories removed because their publish never committed.
    std::vector < std::string > removed_incoming;
  /// Parallel to @ref removed_incoming: the directory held nothing, which is what a
  /// publish that had already completed leaves behind.
    std::vector < bool > incoming_was_empty;
    bool nothing() const {return !rolled_forward && removed_incoming.empty();}
    std::string text() const;
  };

/// Bring a store left by an interrupted publish back to a consistent state;
/// the first thing a slope run does once it holds the lock, before any bag is
/// read. A `.pending` manifest means the publish committed, so the remaining
/// moves are completed from it (no bags needed, whatever bags the caller has).
/// An incoming directory with no `.pending` manifest means it did not, so it is
/// deleted and the store is as it was.
/// @throws std::runtime_error when the pending publish cannot be completed
///   (a tile it names is in neither place, or the manifest is malformed).
  RecoveryReport recoverInterruptedPublish(const std::string & store_dir);

/// One publish: tiles to install, tiles to delete, the new manifest.
  struct PublishPlan
  {
    std::string store_dir;
  /// Unique per run; names the incoming directory.
    std::string run_id;
  /// Directory holding the scratch backscatter tiles (`<scratch>/<bs>/survey`).
    std::string scratch_survey_dir;
  /// Tile file names present in @ref scratch_survey_dir to install.
    std::vector < std::string > put_tiles;
  /// Tile file names to delete from the destination (reached, no cells).
    std::vector < std::string > delete_tiles;
  /// The scratch `registry.json` to install at the store root, or empty.
    std::string registry_source;
  /// The complete manifest after the publish; `last_publish` is filled in here.
    DerivedTiles manifest;
  };

/// Called at named points of `publishDerived`; throwing from it simulates a
/// crash there (the tests do). Points: `incoming-copied`, `pending-written`,
/// `tile-moved:<i>`, `registry-moved`, `tiles-deleted`, `manifest-moved`.
  using PublishFailpoint = std::function < void(const std::string & point) >;

/// Publish @p plan into its store (the caller holds the `StoreLock` and has run
/// `recoverInterruptedPublish`):
///   1. copy each tile (and the registry) to `survey/.incoming-<run>/`, fsync;
///   2. write the manifest as `derived_tiles.json.pending` (the commit point);
///   3. rename each incoming file over its final name, delete the tiles to
///      delete;
///   4. rename the pending manifest to `derived_tiles.json`, remove the incoming
///      directory.
/// @throws std::runtime_error on an I/O failure; the store is then in a state
///   `recoverInterruptedPublish` repairs.
  void publishDerived(const PublishPlan & plan, const PublishFailpoint & failpoint = {});

}  // namespace cube

#endif  // CUBE_BATHYMETRY__DERIVED_TILES_H_
