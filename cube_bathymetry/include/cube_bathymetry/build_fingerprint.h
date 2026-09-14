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

/// @file
/// @brief The store's build fingerprint sidecar (ADR-0003), schema version 2 --
///        the **tiling** part only (cube_bathymetry#143).
///
/// ADR-0003 specifies `build_fingerprint.json` beside `registry.json` as the
/// record of what a store was built from, so an incremental regen can tell
/// whether previously-built tiles are stale. Schema version 1 carried a scalar
/// `cell_size_m`, which cannot describe a store whose tiles sit at several
/// levels; version 2 replaces it with a `tiling` object:
///
/// ```json
/// {
///   "schema_version": 2,
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
/// **This is ADR-0003's first, partial implementation.** The other keys
/// (`tool_version`, the bag list and hashes, `reference_store`,
/// `backscatter_correction`) and `batch_regen --incremental`'s consumer are
/// not implemented here; a file written by this code carries only what is
/// listed above, and `isStale` compares only that. Adding the remaining keys
/// is additive within schema 2.

namespace cube
{

  struct BuildFingerprint
  {
    static constexpr int kSchemaVersion = 2;
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

    int schema_version = kSchemaVersion;
    Mode mode = Mode::Fixed;
  /// Fixed mode: the requested cell size the single sheet was built with.
    std::optional < double > cell_size_m;
  /// IHO order the error model ran at (`--iho-order`); both modes.
    std::string iho_order;
    Policy policy;
  /// Every GGGS level at which the store holds native tiles from this build.
    std::set < uint8_t > levels_used;

  /// @brief Whether a store built as @p previous must be fully regenerated
  ///        before tiles from a build with this fingerprint can join it: the
  ///        schema, mode, cell size, or any policy field differs.
  ///        `levels_used` is informational and never makes a store stale.
    bool isStale(const BuildFingerprint & previous) const;

  /// @brief Serialise to JSON text (indented; not canonical -- nothing hashes it).
    std::string toJson() const;

  /// @throws std::runtime_error on malformed JSON or a missing key; a schema
  ///         version other than 2 is returned as-is with `schema_version` set
  ///         so the caller can treat it as stale rather than unreadable.
    static BuildFingerprint fromJson(const std::string & json);

  /// @brief Read `<store_dir>/build_fingerprint.json`.
  /// @return nullopt when the file is absent.
  /// @throws std::runtime_error when present but unreadable or malformed.
    static std::optional < BuildFingerprint > read(const std::string & store_dir);

  /// @brief Atomically write `<store_dir>/build_fingerprint.json` (temp file,
  ///        flush, rename), creating @p store_dir as needed (ADR-0003).
  /// @throws std::runtime_error on any filesystem failure.
    void write(const std::string & store_dir) const;
  };

  const char * toString(BuildFingerprint::Mode mode);

}  // namespace cube

#endif  // CUBE_BATHYMETRY__BUILD_FINGERPRINT_H_
