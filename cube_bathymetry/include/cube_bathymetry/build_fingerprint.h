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

#ifndef CUBE_BATHYMETRY__BUILD_FINGERPRINT_H_
#define CUBE_BATHYMETRY__BUILD_FINGERPRINT_H_

#include <cstdint>
#include <optional>
#include <set>
#include <string>

#include "cube_bathymetry/gain_declaration.h"

/// @file
/// @brief The store's build fingerprint sidecar (ADR-0003), schema version 3 --
///        the **tiling** part (cube_bathymetry#143), the backscatter correction
///        (#172) and the gain-state declaration (#176).
///
/// ADR-0003 specifies `build_fingerprint.json` beside `registry.json` as the
/// record of what a store was built from, so an incremental regen can tell
/// whether previously-built tiles are stale. Schema version 1 carried a scalar
/// `cell_size_m`, which cannot describe a store whose tiles sit at several
/// levels; version 2 replaces it with a `tiling` object:
///
/// ```json
/// {
///   "schema_version": 3,
///   "tiling": {
///     "mode": "fixed" | "depth_adaptive",
///     "cell_size_m": 0.906,            // fixed mode only, else null
///     "iho_order": "order1a",
///     "policy": {
///       "capture_distance_scale": 0.05,
///       "capture_spacing_scale": 0.71,
///       "depth_adaptive_scale": 0.05,       // depth_adaptive only, else null
///       "coarsest_level": 8,                // "
///       "finest_level": 14,                 // "
///       "count_level": 14,                  // "
///       "min_obs_per_node": 5,              // "
///       "blunder_allowance": 0.2,           // "
///       "decision_depth_percentile": 0.02,  // "
///       "achieved_percentile": 0.95         // "
///     },
///     "levels_used": [8, 9, 10]
///   }
/// }
/// ```
///
/// `policy` and `iho_order` are written in **both** modes: the capture gate and
/// the IHO order's error model change fixed-level output too, so a fixed store
/// whose fingerprint omitted them could not be told stale when they change --
/// the job `cell_size_m` did in schema 1.
///
/// The depth-adaptive keys are **every** input that decides the level plan:
/// the ladder scale `--depth-adaptive-scale` and the two percentiles
/// (`--decision-depth-percentile`, `--achieved-percentile`) move tiles between
/// levels exactly as the level bounds do.
///
/// `backscatter_correction` (cube_bathymetry#172) records the correction the
/// backscatter layer was built with, beside `tiling`:
///
/// ```json
///   "backscatter_correction": {
///     "mode": "none" | "empirical" | "auto",
///     "kind": "none" | "curve" | "table" | "table-unresolved",
///     "source": "<file path or sonar_info topic>",   // informational
///     "sha256": "<hex of the file, or of the SonarInfo curve>" | null,
///     "fallback": "identity" | "exclude" | null,       // tables only
///     "min_pings": 0 | null                            // tables only
///   }
/// ```
///
/// `table-unresolved` is a table run in which not one ping resolved to a
/// table entry (wrong SonarInfo topic, no SonarInfo, no frequency): the
/// table was loaded and hashed, but the backscatter layer carries none of
/// its correction, so the kind differs from `table` and a staleness check
/// can never take such a store for a corrected one. import_bag exits 4 then.
///
/// `gain_declaration` (cube_bathymetry#176; schema 3) records the operator-written
/// gain-state declaration the backscatter intensities were shifted by before any
/// correction, beside `backscatter_correction`:
///
/// ```json
///   "gain_declaration": {
///     "reference_state": "<the declaration's reference-state label>",
///     "sha256": "<hex of the declaration's CANONICAL form>",
///     "source": "<file path>"                      // informational
///   }                                              // or null: no declaration
/// ```
///
/// The digest is of the canonical form (format version, reference state, each
/// segment's start, end and offset -- `GainDeclaration::canonicalForm`), not of
/// the file bytes, so editing a note or a comment does not stale a store.
/// `isStale` compares `reference_state` and `sha256` (not `source`), and
/// present against null is stale. A zero-segment declaration is present, so it
/// differs from no declaration: that store is in a named reference state.
/// Schema 3 is also why every store built before #176 reads stale: the gain
/// state it was built in is unknowable.
///
/// `isStale` compares mode, kind, sha256, fallback and min_pings (not the
/// source path: the same table under another name is the same correction).
/// A fingerprint without the key (written before #172) is stale against one
/// with it: what correction built it cannot be told.
///
/// **This is still a partial implementation of ADR-0003.** The other keys
/// (`tool_version`, the bag list and hashes, `reference_store`) and
/// `batch_regen --incremental`'s consumer are not implemented here; adding
/// them is additive within schema 3.

namespace cube
{

  struct BuildFingerprint
  {
    static constexpr int kSchemaVersion = 3;
    static constexpr const char * kFilename = "build_fingerprint.json";

    enum class Mode { Fixed, DepthAdaptive };

    struct Policy
    {
    /// The node capture gate's depth term (`Parameters::capture_distance_scale`).
    /// No CLI setter today -- it is a compile-time default -- so this key is
    /// inert until one exists; it is recorded so that the day it becomes
    /// settable, stores built before the change are correctly told stale.
      double capture_distance_scale = 0.05;
    /// The node capture gate's spacing term (`--capture-spacing-scale`).
      double capture_spacing_scale = 0.71;
    // Depth-adaptive only; ignored (written null) in fixed mode.
    /// The uma#369 ladder scale (`--depth-adaptive-scale`) -- NOT the capture
    /// gate's scale above, despite sharing a name in `LevelPlanPolicy`.
      double depth_adaptive_scale = 0.05;
      uint8_t coarsest_level = 8;
      uint8_t finest_level = 14;
      uint8_t count_level = 14;
      uint32_t min_obs_per_node = 5;
      double blunder_allowance = 0.2;
      double decision_depth_percentile = 0.02;
      double achieved_percentile = 0.95;
    };

  /// The backscatter correction a build applied (cube_bathymetry#172).
    struct BackscatterCorrection
    {
      std::string mode = "none";
    /// `none`, `curve`, `table`, or `table-unresolved` (above).
      std::string kind = "none";
      std::string source;
      std::string sha256;
      std::string fallback;
      uint64_t min_pings = 0;

    /// True for `table` and `table-unresolved`: the kinds that carry the
    /// table's fallback and ping floor.
      bool isTableKind() const {return kind == "table" || kind == "table-unresolved";}

    /// Equal when the correction is the same: mode, kind, hash, fallback,
    /// ping floor. `source` is informational.
      bool operator == (const BackscatterCorrection & other) const;
      bool operator != (const BackscatterCorrection & other) const {return !(*this == other);}
    };

  /// The gain-state declaration a build applied (cube_bathymetry#176).
    struct GainDeclarationRecord
    {
      std::string reference_state;
    /// Digest of the declaration's canonical form (`gainDeclarationSha256`).
      std::string sha256;
    /// Where it was read from; informational, never compared.
      std::string source;

    /// Equal when the correction is the same: reference state and digest.
      bool operator == (const GainDeclarationRecord & other) const;
      bool operator != (const GainDeclarationRecord & other) const {return !(*this == other);}
    };

    int schema_version = kSchemaVersion;
    Mode mode = Mode::Fixed;
  /// Fixed mode: the requested cell size the single sheet was built with.
    std::optional < double > cell_size_m;
  /// IHO order the error model ran at (`--iho-order`); both modes.
    std::string iho_order;
    Policy policy;
  /// Every GGGS level at which the store holds native tiles from this build.
    std::set < uint8_t > levels_used;
  /// The backscatter correction, when the writer recorded one (import_bag
  /// always does since #172); nullopt for a file written before.
    std::optional < BackscatterCorrection > backscatter_correction;
  /// The gain-state declaration the build applied; nullopt for a build with
  /// none (written as null) and for any file of an older schema.
    std::optional < GainDeclarationRecord > gain_declaration;

  /// @brief Whether a store built as @p previous must be fully regenerated
  ///        before tiles from a build with this fingerprint can join it: the
  ///        schema, mode, cell size, or any policy field differs.
  ///        `levels_used` is informational and never makes a store stale.
    bool isStale(const BuildFingerprint & previous) const;

  /// @brief Serialise to JSON text (indented; not canonical -- nothing hashes it).
    std::string toJson() const;

  /// @throws std::runtime_error on malformed JSON or a missing key; a schema
  ///         version other than 3 is returned as-is with `schema_version` set
  ///         so the caller can treat it as stale rather than unreadable.
    static BuildFingerprint fromJson(const std::string & json);

  /// @brief Read `<store_dir>/build_fingerprint.json`.
  /// @return nullopt when the file is absent.
  /// @throws std::runtime_error when present but unreadable or malformed.
    static std::optional < BuildFingerprint > read(const std::string & store_dir);

  /// @brief Atomically write `<store_dir>/build_fingerprint.json` (temp file,
  ///        fsync, rename, fsync of the directory), creating @p store_dir as
  ///        needed (ADR-0003).
  /// @throws std::runtime_error on any filesystem failure, the post-rename
  ///         directory fsync included -- a return means the fingerprint is on
  ///         disk under its final name, not merely renamed.
    void write(const std::string & store_dir) const;
  };

  const char * toString(BuildFingerprint::Mode mode);

/// @brief True when @p err is a filesystem saying it has no directory fsync at
///        all, rather than reporting a failure of one.
///
/// Some mounts (a number of network and FUSE filesystems) answer `fsync` on a
/// directory descriptor with `EINVAL`/`ENOTSUP`/`ENOSYS`. Treating that as a
/// write failure would make `BuildFingerprint::write` throw on every run on
/// such a store -- forcing a FULL regen forever (ADR-0003) -- while the rename
/// has in fact put a valid fingerprint in place. Durability is weaker there,
/// so `write` warns; a real failure (`EIO`, `ENOSPC`, `EBADF`, ...) still
/// throws.
  bool fsyncErrnoMeansUnsupported(int err) noexcept;

/// @brief SHA-256 of a file's bytes as lowercase hex (the fingerprint's
///        curve / table identity).
/// @throws std::runtime_error when the file cannot be read.
  std::string sha256File(const std::string & path);

/// @brief SHA-256 of @p text as lowercase hex.
  std::string sha256Text(const std::string & text);

/// @brief SHA-256 of a gain declaration's canonical form (cube_bathymetry#176):
///        the identity the fingerprint, the derive tool's table headers and
///        the import-time consistency check compare. See
///        `GainDeclaration::canonicalForm` for what is and is not in it.
  std::string gainDeclarationSha256(const GainDeclaration & declaration);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__BUILD_FINGERPRINT_H_
