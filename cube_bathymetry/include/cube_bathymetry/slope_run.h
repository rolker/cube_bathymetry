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

#ifndef CUBE_BATHYMETRY__SLOPE_RUN_H_
#define CUBE_BATHYMETRY__SLOPE_RUN_H_

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "cube_bathymetry/angle_basis.h"
#include "cube_bathymetry/derived_tiles.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/slope_incidence.h"
#include "marine_autonomy/gggs.h"

/// @file
/// @brief The slope run of `import_bag` (cube_bathymetry#173, decisions 12 and
///        13): what it asks of the command line, and the state it carries.
///
/// A slope run re-reads the bags exactly as an ordinary import does, stamps each
/// sounding's incidence from a finished depth store, estimates in memory with
/// the correction applied on incidence, and publishes ONLY backscatter tiles
/// into its own store, with a per-tile record (`derived_tiles.json`) of the
/// bags and depth tiles that went into each. It writes no depths: the depths
/// are the ordinary run's. The in-memory estimate exists only to attribute
/// beams to hypotheses, so the run stays fixed-level and is not compared with
/// the ordinary run's depth (the two can differ slightly where the ordinary run
/// warm-started from earlier tiles).

namespace cube
{

/// The command-line facts that decide whether a slope run is a valid request.
  struct SlopeRunRequest
  {
    BackscatterAngleBasis basis = BackscatterAngleBasis::Beam;
    std::string store_dir;  ///< `-o`
    std::string bs_store_dir;  ///< `--bs-store`
    std::string surface_dir;  ///< `--slope-surface`
    bool baseline_given = false;
    double baseline_deg = kDefaultSlopeBaselineDeg;
    bool depth_adaptive = false;
    int ping_limit = 0;  ///< `-l`
  };

/// Empty when the combination is valid, else the first problem, naming the flag.
/// On the beam basis the slope-only flags are refused; on the incidence basis
/// the required ones must be present and the forbidden ones absent.
  std::string slopeRunRequestProblem(const SlopeRunRequest & request);

/// Empty when the correction the run resolved can be applied on incidence:
/// none, or a table (whose basis the table check has already vetted). The
/// legacy single curve has no incidence form, and a curve delivered by
/// SonarInfo is a beam-angle curve, including under the default
/// `--backscatter-correction auto`.
/// @param kind the fingerprint kind of the resolved correction (`curve`, `table`,
///        or empty for none)
/// @param source where a curve came from (`sonar_info:<topic>` or a file)
  std::string slopeCorrectionProblem(const std::string & kind, const std::string & source);

/// The surface layers a slope run reads from @p surface_dir: `processed`, and
/// `draft` when that directory exists (the own-survey rule of uma#304). Never
/// `reference`, `chart` or `overviews/`. Empty when neither exists.
  std::vector < std::string > slopeSurfaceLayers(const std::string & surface_dir);

  struct SlopeRunConfig
  {
    std::string surface_dir;
    std::string bs_store_dir;
  /// Where the unique scratch directory is made; empty = the parent of the
  /// backscatter store.
    std::string scratch_root;
    double baseline_deg = kDefaultSlopeBaselineDeg;
  /// The store-level inputs of this run (basis, baseline, layers, correction,
  /// gain declaration, level).
    StoreInputs inputs;
  /// Every bag given to the run.
    std::vector < BagIdentity > bags;
  /// Resident-tile budget of the surface reader. Above BathyDem's default of 8:
  /// one lookup probes layers x levels tiles before its stencil.
    std::size_t surface_cache_tiles = 24;
  };

/// One slope run: the lock, the surface, the scratch stores, the contributor
/// bookkeeping and the publish.
  class SlopeRun
  {
public:
  /// Take the store lock, recover an interrupted publish, check the store (empty
  /// or already a slope-run store with the same inputs), open the surface and
  /// make the scratch directory. Null with @p error set when the run is
  /// refused; nothing has been written then but the lock file and, if one was
  /// found, the repair of an interrupted publish.
    static std::unique_ptr < SlopeRun > create(const SlopeRunConfig & config, std::string & error);

  /// Removes the scratch directory, on every exit path.
    ~SlopeRun();
    SlopeRun(const SlopeRun &) = delete;
    SlopeRun & operator = (const SlopeRun &) = delete;

    const std::string & scratchDir() const {return scratch_;}
    std::string scratchDepthDir() const;
    std::string scratchBsDir() const;
    std::string scratchSpillDir() const;
  /// What recovery of an interrupted publish did (empty when nothing was left).
    const std::string & recoveryText() const {return recovery_text_;}
  /// Paths of scratch directories of other runs beside this one, as a note.
    const std::string & leftoverText() const {return leftover_text_;}
    const IncidenceCounters & counters() const {return counters_;}

  /// Per ping, after the soundings are georeferenced and before the table
  /// accounting and `addBatch`: stamp incidence and attribute the ping's bag to
  /// every tile the batch can reach. False with @p error set when the run must
  /// stop (a corrupt surface tile, or a tile whose recorded contributors are not
  /// all among the bags given: the first-touch refusal, before anything is
  /// published).
    bool prepare(
      std::vector < GeoSounding > &soundings, std::size_t bag_index, const GeoMapSheet & sheet,
      std::string & error);

  /// Tiles the run has reached so far.
    std::size_t tilesReached() const {return reached_.size();}
  /// The bags (by index into @ref SlopeRunConfig::bags) attributed to @p tile.
    const std::set < std::size_t > * contributorsOf(const gggs::GridIndex & tile) const;

  /// After the accumulator has finalized into the scratch stores: publish the
  /// run into the destination. @p summary receives a plain-text account.
  /// Nothing is written to the destination when this is not called, so a run
  /// that failed, was refused, or exited 4 simply does not call it.
    bool publish(
      std::string & error, std::string & summary,
      const PublishFailpoint & failpoint = {});

private:
    explicit SlopeRun(const SlopeRunConfig & config);

    SlopeRunConfig config_;
    SlopeOptions options_;
    std::unique_ptr < StoreLock > lock_;
    std::optional < DerivedTiles > previous_;
    std::unique_ptr < marine_backscatter::seabed::SeabedHeightSampler > surface_;
    std::unique_ptr < SurfaceTileIndex > surface_index_;
    std::string scratch_;
    std::string recovery_text_;
    std::string leftover_text_;
    IncidenceCounters counters_;
    double max_influence_m_ = 0.0;
    double surface_pad_m_ = 0.0;
    std::map < gggs::GridIndex, std::set < std::size_t >> reached_;
  };

}  // namespace cube

#endif  // CUBE_BATHYMETRY__SLOPE_RUN_H_
