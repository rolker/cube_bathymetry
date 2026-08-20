// Copyright 2025 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
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


#include <sys/stat.h>

#include <chrono>
#include <cmath>
#include <ctime>
#include <deque>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "tf2_ros/transform_listener.h"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "lifecycle_msgs/msg/state.hpp"
#include "rcl_interfaces/msg/parameter_descriptor.hpp"

#include "sensor_msgs/msg/point_cloud2.hpp"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/grid_projection.h"
#include "geometry_msgs/msg/point_stamped.hpp"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2_ros/message_filter.h"
#include "message_filters/subscriber.h"
#include "sensor_msgs/point_cloud2_iterator.hpp"
#include "tf2_sensor_msgs/tf2_sensor_msgs.hpp"
#include "tf2_eigen/tf2_eigen.hpp"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "grid_map_ros/grid_map_ros.hpp"
#include "grid_map_msgs/msg/grid_map.hpp"

#include "marine_autonomy/gz4d_geo.h"
#include "cube_bathymetry/angular_response_curve.h"
#include "cube_bathymetry/sonar_info_curve.h"
#include "marine_interfaces/msg/sonar_info.hpp"
#include "cube_bathymetry/store_import.h"
#include "cube_bathymetry/quantize_tile.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "marine_tiled_raster_store/tile_catalog.hpp"
#include "marine_interfaces/msg/sonar_visualization_tile.hpp"
#include "marine_interfaces/msg/tile_catalog.hpp"
#include "marine_interfaces/msg/tile_request.hpp"


class CubeBathymetry : public rclcpp_lifecycle::LifecycleNode
{
public:
  CubeBathymetry()
  :rclcpp_lifecycle::LifecycleNode("cube_bathymetry")
  {
  }


  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_configure(const rclcpp_lifecycle::State & state)
  {
    map_frame_ = this->declare_parameter("map_frame", "map");

    declare_parameter("cell_size", 1.0);
    cell_size_ = get_parameter("cell_size").as_double();

    // grid_cell_count governed the Cartesian MapSheet's per-grid extent. The
    // node now accumulates into a GeoMapSheet whose GGGS grids are a fixed
    // 960x960 cells, so this parameter is ignored post-migration (#21). Kept
    // declared (with a deprecation WARN) so existing launch files that set it
    // still configure cleanly.
    declare_parameter("grid_cell_count", 25);
    grid_cell_count_ = get_parameter("grid_cell_count").as_int();
    if (grid_cell_count_ != 25) {
      RCLCPP_WARN(get_logger(),
        "grid_cell_count=%d is ignored: GeoMapSheet GGGS grids are fixed at "
        "960x960 cells (#21). Remove it from the launch config.", grid_cell_count_);
    }

    geo_map_sheet_ =
      std::make_shared<cube::GeoMapSheet>(static_cast<float>(cell_size_));
    // Fresh sheet on (re)configure: drop any evicted-tile markers from a prior
    // configure cycle so a stale index can't trigger a spurious reload (#70 r2).
    evicted_indices_.clear();
    // Same defense-in-depth for the disk-serve queue (#106 review r1):
    // deactivate/cleanup already clear it on the normal path, but a stale
    // queued index from a prior configure cycle must never survive into a
    // fresh sheet's serving.
    disk_serve_queue_.clear();
    disk_serve_queued_.clear();
    // Reset the tile-version registry too (#78): a fresh sheet must not advertise
    // phantom tiles from a prior configure cycle in the catalog (ADR-0008 D4).
    catalog_builder_ = marine_tiled_raster_store::TileCatalogBuilder{};

    // Backscatter angular-response correction (cube_bathymetry#81, #102).
    // Default "auto": a curve arriving via SonarInfo (or an explicit file)
    // enables the correction; without one it is the old identity behavior.
    // "none" hard-disables (SonarInfo curves ignored); "empirical" is the
    // explicit request, warning when no curve materializes. An explicit
    // backscatter_curve_file always wins over SonarInfo (the reprocessing
    // override). The setter must run AFTER the sheet is constructed (grids
    // hold a const ref to the sheet's Parameters).
    const std::string bs_correction_str =
      declare_parameter("backscatter_angle_correction", std::string("auto"));
    const std::string bs_curve_file =
      declare_parameter("backscatter_curve_file", std::string(""));
    bs_mode_ = cube::BackscatterAngleCorrection::None;
    if (!cube::parseBackscatterAngleCorrection(bs_correction_str, bs_mode_)) {
      RCLCPP_WARN(get_logger(),
        "backscatter_angle_correction='%s' is not 'none', 'empirical' or "
        "'auto'; defaulting to none (no correction).",
        bs_correction_str.c_str());
      bs_mode_ = cube::BackscatterAngleCorrection::None;
    }
    const auto bs_mode = bs_mode_;
    bs_explicit_file_ = !bs_curve_file.empty();
    bs_curve_applied_ = false;
    cube::AngularResponseCurve bs_curve;
    if (bs_mode != cube::BackscatterAngleCorrection::None &&
      !bs_curve_file.empty())
    {
      bs_curve = cube::loadAngularResponseCurveWithHeader(bs_curve_file);
      bs_curve_applied_ = !bs_curve.points.empty();
      if (!bs_curve_applied_) {
        // Loud in EVERY mode (not just empirical): an explicit file also
        // suppresses the SonarInfo fallback (it stays the operator's chosen
        // source), so a failed load must never vanish silently (#102 r1).
        RCLCPP_WARN(get_logger(),
          "backscatter_curve_file='%s' yielded an EMPTY curve (missing/"
          "unparseable) -- no correction from it, and published SonarInfo "
          "curves stay IGNORED because an explicit file was configured. "
          "Fix or clear the parameter.", bs_curve_file.c_str());
      }
    }
    if (bs_mode == cube::BackscatterAngleCorrection::Empirical && bs_curve.points.empty()) {
      // Loud, not silent: the operator asked for the correction but no curve was
      // loaded (empty/missing/unparseable file), so it degrades to a no-op.
      RCLCPP_WARN(get_logger(),
        "backscatter_angle_correction=empirical but no curve was loaded from "
        "backscatter_curve_file='%s' -- the correction is ENABLED but a NO-OP "
        "(intensity emitted uncorrected). Provide a valid curve CSV.",
        bs_curve_file.c_str());
    } else if (!bs_curve.points.empty()) {
      if (bs_curve.tl_removed) {
        // tier-2 (cube#87): TL-removed residual; estimator also removes
        // 40*log10(R) + 2*alpha*R per beam (R from the sensor-frame point norm).
        RCLCPP_INFO(get_logger(),
          "Backscatter angular-response correction: empirical, %zu-point curve "
          "from %s [tier-2: TL-removed, alpha=%g dB/m]", bs_curve.points.size(),
          bs_curve_file.c_str(),
          static_cast<double>(bs_curve.absorption_db_per_m));
      } else {
        RCLCPP_INFO(get_logger(),
          "Backscatter angular-response correction: empirical, %zu-point curve "
          "from %s", bs_curve.points.size(), bs_curve_file.c_str());
      }
    }
    bs_applied_curve_ = bs_curve;  // kept for mid-run change comparison (#102)
    geo_map_sheet_->setBackscatterCorrection(
      bs_mode, std::move(bs_curve.points),
      bs_curve.tl_removed, bs_curve.absorption_db_per_m);

    // Long-duration bounding (#70, ADR-0001). Declared BEFORE the draft prime so
    // the prime can be trimmed to the same budget -- otherwise loadIntoSheet
    // (whole store) re-creates the unbounded-RAM condition #70 exists to prevent.
    //  - max_resident_tiles: RAM budget; cold tiles beyond it are persisted then
    //    evicted (lossless). Eviction requires draft persistence -- without a
    //    draft_dir the node leaves data resident and warns (it never drops data).
    //  - ca_window_radius_m: half-extent of the vessel-centered window published
    //    on `grid` (the bounded collision-avoidance view that replaces the old
    //    whole-survey publish).
    //  - base_link_frame: vessel frame used to center the CA window.
    max_resident_tiles_ =
      static_cast<std::size_t>(declare_parameter("max_resident_tiles", 64));
    ca_window_radius_m_ = declare_parameter("ca_window_radius_m", 200.0);
    base_link_frame_ = declare_parameter("base_link_frame", std::string("base_link"));

    // Coherence WARN (ADR-0001): the resident budget must cover the CA window's
    // tile span, else a window tile could be evicted and render as a NaN/lethal
    // hole inside the avoidance window. gridsInCaWindow() selects tiles within
    // R + one tile span (the boundary-straddle margin), so the budget must cover
    // that same R + span extent -- not just R.
    const double tile_span_m =
      geo_map_sheet_->nominalCellSizeMeters() * gggs::GridIndex::cellRowCount();
    if (tile_span_m > 0.0) {
      const double per_axis = std::ceil(
        (2.0 * (ca_window_radius_m_ + tile_span_m)) / tile_span_m) + 1.0;
      const std::size_t window_tiles =
        static_cast<std::size_t>(per_axis * per_axis);
      if (max_resident_tiles_ < window_tiles) {
        RCLCPP_WARN(get_logger(),
          "max_resident_tiles=%zu is smaller than the ~%zu tiles spanning the "
          "%.0fm CA window (tile span ~%.0fm); window tiles may be evicted and "
          "show as lethal holes. Raise max_resident_tiles or shrink "
          "ca_window_radius_m.", max_resident_tiles_, window_tiles,
          ca_window_radius_m_, tile_span_m);
      }
    }

    // Prior-store predicted-surface prime (#91 -- the live half of the
    // chart-gate gap; the offline half is seedNewTile's Chart rung, #119).
    // Seeds CUBE's predicted surface from the prior store's Chart + Reference
    // layers (Chart first, Reference overwrites -- store priority order,
    // exact-survey-level tiles only) so the blunder gate rejects false-deep
    // detections live. Predicted-only (seed_settled=false): the prior gates
    // but never fills -- no survey or co-estimated-backscatter contamination
    // (cube#89; gates-not-fills reaffirmed in ADR-0008).
    //
    // Side-effect (#59): a primed predicted surface also activates LIVE
    // slope-corrected depth via the touchdown interpolation in
    // Grid/GeoGrid::insert -- this parameter is what turns that on afloat.
    //
    // Runs BEFORE the draft warm-start below so an already-surveyed cell ends
    // with its finer draft-derived predicted depth (the draft prime overwrites).
    //
    // Prior-primed tiles are clean and dataless, so this trim (and the draft
    // prime's trim below) may evict them; a revisit re-primes the gate from
    // prior_store_dir in reloadEvictedTile (#118), so eviction costs one
    // windowed disk read on return, never the gate.
    //
    // A lifecycle node WARNS and continues ungated on a missing/bad/empty
    // prior -- a misconfigured prior must never take down live perception.
    prior_store_dir_ = declare_parameter("prior_store_dir", std::string(""));
    if (!prior_store_dir_.empty()) {
      try {
        marine_bathymetry_store::BathymetryStore prior =
          marine_bathymetry_store::BathymetryStore::fromCellSize(
          static_cast<float>(cell_size_));
        marine_bathymetry_store::load(prior, prior_store_dir_);
        const cube::PriorLayerPrimeResult primed =
          cube::primeFromPriorLayers(prior, *geo_map_sheet_);
        if (primed.total() == 0) {
          RCLCPP_WARN(get_logger(),
            "prior_store_dir='%s' primed no tiles (%zu level-mismatched "
            "skipped); predicted surface NOT seeded -- blunder gate and live "
            "slope correction INACTIVE. Check the path and that the store has "
            "reference/ or chart/ tiles at the survey level.",
            prior_store_dir_.c_str(), primed.level_mismatched);
        } else {
          RCLCPP_INFO(get_logger(),
            "Primed predicted surface from prior store '%s': %zu Reference + "
            "%zu Chart tile(s), %zu level-mismatched skipped. Blunder gate "
            "active (#91); live slope correction active (#59).",
            prior_store_dir_.c_str(), primed.reference_tiles,
            primed.chart_tiles, primed.level_mismatched);
          // Bound the prime to the resident budget (#70 pattern, same as the
          // draft prime below). Evicted prior-primed tiles get their gate
          // re-primed on revisit (reloadEvictedTile, #118); bounded RAM wins
          // on a long survey. As with the draft prime, the WHOLE store is
          // loaded before this trim, so a very large prior spikes RAM
          // transiently at configure -- point prior_store_dir at a
          // region-scoped store (a windowed prime needs a startup position
          // not available at on_configure; same known limitation).
          trimResidentToBudget();
        }
      } catch (const std::exception & e) {
        RCLCPP_WARN(get_logger(),
          "Could not load prior store from '%s': %s (continuing ungated).",
          prior_store_dir_.c_str(), e.what());
      }
    }

    // Draft-tile persistence (#21). draft_dir empty (default) disables it; set
    // it per deployment to opt in. Tiles are written as marine_bathymetry_store
    // `draft/` GeoTIFFs (single fused grid, no per-day epochs,
    // unh_marine_autonomy#221) so the costmap layer (#164) and sim live loop
    // (#77) read exactly what CUBE writes.
    draft_dir_ = declare_parameter("draft_dir", std::string(""));

    // save_interval is a disk-I/O-frequency / crash-durability knob -- it does
    // NOT affect the depth estimate. saveDirtyTiles() flushes the CUBE median
    // pre-filter via GeoGrid::values() (queueFlush per node), but publishGrid()
    // calls the same values() flush and is throttled to ~5s (see pingCallback),
    // so for any save_interval >= the 5s publish cadence the estimate is already
    // governed by publish, not by saving. 30s = ~6x the publish cadence, so each
    // save coalesces several publish cycles. The tradeoff is purely: a crash
    // loses <= one interval of un-persisted draft tiles (recoverable offline
    // from the raw bag -- the draft store is a live convenience, the
    // authoritative path is offline processing), against per-save disk I/O
    // (~775KB x dirty-tile count) competing with continuous bag recording.
    // See #74 for the full rationale.
    rcl_interfaces::msg::ParameterDescriptor save_interval_desc;
    save_interval_desc.description =
      "Draft-tile persistence cadence in seconds (only while ACTIVE and "
      "draft_dir is set). Disk-I/O-frequency / crash-durability knob; does NOT "
      "affect the depth estimate (the ~5s publish path already flushes the CUBE "
      "median pre-filter). A crash loses <= one interval of un-persisted draft "
      "tiles, which are reconstructable offline from the raw bag. Default 30s "
      "(~6x the publish cadence) balances restart-loss against disk contention "
      "with bag recording. See #74.";
    save_interval_s_ =
      declare_parameter("save_interval", 30.0, save_interval_desc);

    if (!draft_dir_.empty()) {
      // On startup, prime the fresh GeoMapSheet from the persisted draft grid so
      // slope correction warm-starts on previously-surveyed areas and the settled
      // depths round-trip through values() (lossless reload, ADR-0001).
      try {
        marine_bathymetry_store::BathymetryStore store =
          marine_bathymetry_store::BathymetryStore::fromCellSize(
          static_cast<float>(cell_size_));
        marine_bathymetry_store::load(store, draft_dir_);
        // The live node WRITES the `draft` layer (ADR-0010 D8), but it PRIMES
        // from Processed union Draft. On a boat's first boot after co-land, load()
        // auto-migrates a legacy `survey/` dir to `processed/` (ADR-0010 D8), so
        // priming from draft alone would warm-start from an empty layer and lose
        // both the GeoMapSheet seed and the disk-serve catalog.
        //
        // Processed must WIN per cell where the two layers overlap (it is the
        // authoritative surface; the store's own query walks `Processed > Draft`).
        // Seeding order alone does NOT deliver that: two settled 1-sample
        // hypotheses tie-break in CUBE's chooseHypothesis to the FIRST-seeded, so a
        // Draft-then-Processed prime would report Draft on overlap. Instead seed
        // Processed FULLY first, then layer Draft with a per-cell filter that SKIPS
        // any cell Processed already covers (loadDraftSkippingProcessed) -- the
        // overlapped cell then carries only the Processed hypothesis, deterministic
        // regardless of the tie-break.
        const auto & processed_tiles =
          store.tiles(marine_bathymetry_store::SourceLayer::Processed);
        const auto & draft_tiles =
          store.tiles(marine_bathymetry_store::SourceLayer::Draft);
        if (!processed_tiles.empty() || !draft_tiles.empty()) {
          cube::loadIntoSheet(
            store, marine_bathymetry_store::SourceLayer::Processed, *geo_map_sheet_);
          cube::loadDraftSkippingProcessed(store, *geo_map_sheet_);
          RCLCPP_INFO(get_logger(),
            "Primed GeoMapSheet from %zu processed + %zu draft tiles under %s",
            processed_tiles.size(), draft_tiles.size(), draft_dir_.c_str());
          // Seed the tile-version registry from ALL persisted tiles BEFORE the
          // trim (#106): the catalog advertises the full on-disk store, so
          // tiles evicted by the trim below stay advertised and disk-servable.
          // (Seeding from grids() AFTER the trim -- the previous order -- lost
          // the just-evicted tiles from the catalog, and the consumer's
          // prune-on-absence deleted valid coverage, ADR-0008 D4.)
          //
          // Seed from BOTH layers (each under its own dir for the mtime) so
          // migrated `processed/` coverage is advertised too; catalog_builder_ is
          // newest-wins per index, so a grid present in both takes the fresher
          // file mtime.
          //
          // Version = the tile FILE's mtime, not now() (#106 review r1):
          // stamping the prime time made every reboot advertise the whole
          // store as fresh, so a warm consumer re-pulled everything over the
          // link. mtime is stable across restarts and bumps only when a tile
          // is re-saved (genuinely newer data); tiles carry no per-cell time
          // raster since uma#248, so the file stamp is the durable version
          // source. A stat failure falls back to now() -- worst case is one
          // redundant refresh of that tile, never loss.
          const std::int64_t fallback_version = now().nanoseconds();
          const auto seed_catalog =
            [&](marine_bathymetry_store::SourceLayer layer, const auto & tiles) {
              const std::string layer_dir = draft_dir_ + "/" +
                marine_bathymetry_store::layerDirName(layer);
              for (const auto & [tile_index, tile] : tiles) {
                std::int64_t version = fallback_version;
                struct stat st;
                const std::string tile_path = layer_dir + "/" +
                  marine_bathymetry_store::tileFilename(tile_index);
                if (::stat(tile_path.c_str(), &st) == 0) {
                  version = static_cast<std::int64_t>(st.st_mtim.tv_sec) *
                    1000000000LL + st.st_mtim.tv_nsec;
                }
                catalog_builder_.update(tile_index, version);
              }
            };
          seed_catalog(marine_bathymetry_store::SourceLayer::Draft, draft_tiles);
          seed_catalog(
            marine_bathymetry_store::SourceLayer::Processed, processed_tiles);
          // Bound the prime to the resident budget (must-fix): the fused prime
          // loads the WHOLE store, so without this a restart mid-long-survey
          // re-creates the unbounded RAM #70 prevents. Primed tiles are clean and
          // already on disk, so dropping the cold ones is lossless; they reload on
          // revisit. (The transient peak before the trim is now the TWO-layer union:
          // `store` holds BOTH the Processed AND the Draft tiles fully resident, and
          // the sheet holds the fused Processed-over-Draft surface on top of them --
          // higher than a single-layer prime. A windowed prime would bound it but
          // needs a startup position not available at on_configure; tracked as a
          // follow-up.)
          trimResidentToBudget();
        }
      } catch (const std::exception & e) {
        // A missing/empty store dir is normal on a first run; a genuine load
        // error must not block configure -- log and continue with an empty sheet.
        RCLCPP_WARN(get_logger(),
          "Could not load existing draft tiles from %s: %s (starting empty)",
          draft_dir_.c_str(), e.what());
      }

      // The periodic save timer is created on_activate and cancelled
      // on_deactivate (below) so draft tiles are written only while the node is
      // ACTIVE (surveying) -- a deactivated node must not keep touching disk.
      // The on-load prime above stays in on_configure.
      RCLCPP_INFO(get_logger(),
        "Draft-tile persistence enabled: dir=%s interval=%.1fs "
        "(saves run while ACTIVE; single fused draft grid)",
        draft_dir_.c_str(), save_interval_s_);
    }

    tf_buffer_ = std::make_unique<tf2_ros::Buffer>(get_clock());
    tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_, this);

    grid_publisher_ = create_publisher<grid_map_msgs::msg::GridMap>("grid", 10);

    // Incremental per-tile coverage stream (#70): one GridMap per changed tile
    // per publish cycle, BEST_EFFORT (high-rate, loss-tolerant -- the durable
    // record is the draft store). Interface contract for the boat->CAMP live
    // coverage view (unh_marine_autonomy#86/#250); no in-tree consumer yet.
    tiles_publisher_ = create_publisher<grid_map_msgs::msg::GridMap>(
      "~/tiles", rclcpp::QoS(10).best_effort());

    // Quantized display-tile transport (#78, ADR-0008): the boat->operator/CAMP
    // live coverage view. `~/coverage_tiles` is the live PUSH of changed tiles
    // (best-effort, loss-tolerant -- durable record is the draft store). The
    // periodic `~/coverage_catalog` (transient_local for late joiners) is the
    // COMPLETE snapshot that drives anti-entropy reconciliation, and
    // `~/coverage_requests` lets a consumer ask for tiles it is missing/stale on.
    catalog_interval_s_ = declare_parameter("catalog_interval", 5.0);
    // Disk-serve drain queue (#106): rate-limited from-disk catch-up of
    // evicted tiles requested by a consumer. interval x tiles_per_tick is the
    // catch-up throughput knob an operator tunes against the link budget
    // (the operator bridge rate-limits coverage_tiles to ~2/s); deliberately
    // a separate timer from catalog_interval so a catalog retune cannot
    // silently change catch-up rate.
    rcl_interfaces::msg::ParameterDescriptor per_tick_desc;
    per_tick_desc.description =
      "Evicted tiles served from the draft store per drain tick "
      "(disk_serve_interval). tiles_per_tick / interval = catch-up rate; "
      "default 4 per 0.5s. Tune against the link budget.";
    // Validate before the int -> size_t casts and the timer creation (PR
    // review): a negative count would underflow to a huge size_t (defeating
    // the pacing / unbounding the queue), and a <= 0 interval would make the
    // wall timer fire continuously and starve the executor. A bad value falls
    // back to the default with a WARN -- field configs change under pressure.
    const std::int64_t per_tick_raw =
      declare_parameter("disk_serve_tiles_per_tick", 4, per_tick_desc);
    if (per_tick_raw < 1) {
      RCLCPP_WARN_STREAM(get_logger(),
        "disk_serve_tiles_per_tick=" << per_tick_raw <<
          " is not positive; using default 4");
    }
    disk_serve_tiles_per_tick_ =
      per_tick_raw < 1 ? 4u : static_cast<std::size_t>(per_tick_raw);
    rcl_interfaces::msg::ParameterDescriptor depth_desc;
    depth_desc.description =
      "Max queued disk-serve entries; excess requests are dropped (the "
      "consumer re-requests via the next catalog). Bounds boat-side memory "
      "against a buggy or hostile consumer.";
    const std::int64_t depth_raw =
      declare_parameter("disk_serve_queue_max_depth", 64, depth_desc);
    if (depth_raw < 1) {
      RCLCPP_WARN_STREAM(get_logger(),
        "disk_serve_queue_max_depth=" << depth_raw <<
          " is not positive; using default 64");
    }
    disk_serve_queue_max_depth_ =
      depth_raw < 1 ? 64u : static_cast<std::size_t>(depth_raw);
    const double interval_raw = declare_parameter("disk_serve_interval", 0.5);
    if (!(interval_raw > 0.0) || !std::isfinite(interval_raw)) {
      RCLCPP_WARN(get_logger(),
        "disk_serve_interval=%g is not a positive finite duration; "
        "using default 0.5s", interval_raw);
      disk_serve_interval_s_ = 0.5;
    } else {
      disk_serve_interval_s_ = interval_raw;
    }
    sonar_tile_publisher_ =
      create_publisher<marine_interfaces::msg::SonarVisualizationTile>(
      "~/coverage_tiles", rclcpp::QoS(10).best_effort());
    tile_catalog_publisher_ = create_publisher<marine_interfaces::msg::TileCatalog>(
      "~/coverage_catalog", rclcpp::QoS(1).transient_local().reliable());
    tile_request_subscription_ =
      create_subscription<marine_interfaces::msg::TileRequest>(
      "~/coverage_requests", rclcpp::QoS(10).reliable(),
      std::bind(&CubeBathymetry::tileRequestCallback, this, std::placeholders::_1));

    ping_subscription_ = create_subscription<sensor_msgs::msg::PointCloud2>("soundings",
      rclcpp::SensorDataQoS(),
      std::bind(&CubeBathymetry::pingCallback, this, std::placeholders::_1));

    // SonarInfo curve delivery (#102): the driver declares the sensor's
    // angular-response calibration on a latched sibling topic (relative --
    // resolves beside the sonar chain in a shared namespace; on platforms
    // with no SonarInfo producer the subscription simply never fires).
    // Matched transient_local so the latched message arrives on configure.
    // Skipped entirely when the mode is none (hard off).
    // Reset unconditionally so a reconfigure to mode 'none' cannot leave a
    // prior cycle's live subscription attached (#102 r1). NOTE: rosbag2
    // replays topics as VOLATILE by default, which is durability-incompatible
    // with this transient_local request -- a bag played into the live node
    // delivers no sonar_info; reprocess bags with import_bag (whose pre-pass
    // reads the topic directly) or use a playback QoS override.
    const std::string sonar_info_topic =
      declare_parameter("sonar_info_topic", std::string("sonar_info"));
    sonar_info_subscription_.reset();
    if (bs_mode != cube::BackscatterAngleCorrection::None) {
      sonar_info_subscription_ =
        create_subscription<marine_interfaces::msg::SonarInfo>(
        sonar_info_topic, rclcpp::QoS(1).reliable().transient_local(),
        std::bind(&CubeBathymetry::sonarInfoCallback, this,
        std::placeholders::_1));
    } else {
      RCLCPP_INFO(get_logger(),
        "backscatter_angle_correction=none: any published SonarInfo "
        "angular-response curve will be ignored.");
    }

    return rclcpp_lifecycle::LifecycleNode::on_configure(state);
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_activate(const rclcpp_lifecycle::State & state)
  {
    // Start periodic draft-tile saves only while ACTIVE (surveying). A
    // deactivated node accumulates no new soundings (pingCallback gates on the
    // ACTIVE state) and must not keep writing to disk. The on-load prime stays
    // in on_configure; the on_cleanup final flush stays a final flush.
    if (!draft_dir_.empty() && !save_timer_) {
      save_timer_ = create_wall_timer(
        std::chrono::duration<double>(save_interval_s_),
        std::bind(&CubeBathymetry::saveDirtyTiles, this));
      RCLCPP_INFO(get_logger(),
        "Draft-tile save timer started (interval=%.1fs)", save_interval_s_);
    }
    // Periodic complete catalog for anti-entropy reconciliation (#78/#230).
    if (!catalog_timer_) {
      catalog_timer_ = create_wall_timer(
        std::chrono::duration<double>(catalog_interval_s_),
        std::bind(&CubeBathymetry::publishCatalog, this));
    }
    // Disk-serve drain (#106): trickle from-disk catch-up of requested
    // evicted tiles. Only meaningful with a draft store to serve from.
    if (!draft_dir_.empty() && !disk_serve_timer_) {
      disk_serve_timer_ = create_wall_timer(
        std::chrono::duration<double>(disk_serve_interval_s_),
        std::bind(&CubeBathymetry::drainDiskServeQueue, this));
    }
    return LifecycleNode::on_activate(state);
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_deactivate(const rclcpp_lifecycle::State & state)
  {
    // Stop disk writes while deactivated. Flush first so accumulation since the
    // last periodic save is not stranded across the deactivate.
    saveDirtyTiles();
    if (save_timer_) {
      save_timer_->cancel();
      save_timer_.reset();
    }
    if (catalog_timer_) {
      catalog_timer_->cancel();
      catalog_timer_.reset();
    }
    if (disk_serve_timer_) {
      disk_serve_timer_->cancel();
      disk_serve_timer_.reset();
    }
    // Pending catch-up work is dropped with the timer: requests are only
    // accepted while ACTIVE, and a consumer re-requests via the catalog
    // after the next activate (#106).
    disk_serve_queue_.clear();
    disk_serve_queued_.clear();
    return LifecycleNode::on_deactivate(state);
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_cleanup(const rclcpp_lifecycle::State & state)
  {
    // Final flush so the last accumulation since the previous periodic save is
    // not lost on shutdown. The save timer is normally cancelled on_deactivate,
    // but guard here too in case cleanup is reached by another path.
    saveDirtyTiles();
    if (save_timer_) {
      save_timer_->cancel();
      save_timer_.reset();
    }
    if (catalog_timer_) {
      catalog_timer_->cancel();
      catalog_timer_.reset();
    }
    if (disk_serve_timer_) {
      disk_serve_timer_->cancel();
      disk_serve_timer_.reset();
    }
    disk_serve_queue_.clear();
    disk_serve_queued_.clear();
    // Symmetric teardown for the one conditionally-created subscription
    // (#102): its callback has no lifecycle-state gate, so it must not
    // outlive the configured state and touch the stale sheet.
    sonar_info_subscription_.reset();
    return LifecycleNode::on_cleanup(state);
  }

private:
  std::shared_ptr<cube::GeoMapSheet> geo_map_sheet_;

  std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;

  std::string map_frame_ = "map";
  // Cached so the startup prime and revisit-reload can rebuild a GeoMapSheet /
  // scratch store with the configured geometry.
  double cell_size_ = 1.0;
  int grid_cell_count_ = 25;
  rclcpp::Time last_grid_publish_time_;

  // Last good map<-earth transform, cached so a publish-time TF miss reuses the
  // last alignment rather than starving the collision-avoidance grid (#21).
  geometry_msgs::msg::TransformStamped last_publish_tf_;
  bool have_publish_tf_ = false;
  rclcpp_lifecycle::LifecyclePublisher<grid_map_msgs::msg::GridMap>::SharedPtr grid_publisher_;
  // Incremental per-tile coverage stream (~/tiles, #70).
  rclcpp_lifecycle::LifecyclePublisher<grid_map_msgs::msg::GridMap>::SharedPtr tiles_publisher_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr ping_subscription_;

  // SonarInfo curve delivery state (#102). All touched only from the node's
  // single-threaded executor (configure + subscription callbacks).
  rclcpp::Subscription<marine_interfaces::msg::SonarInfo>::SharedPtr
    sonar_info_subscription_;
  cube::BackscatterAngleCorrection bs_mode_ =
    cube::BackscatterAngleCorrection::None;
  bool bs_explicit_file_ = false;   // explicit file wins over SonarInfo
  bool bs_curve_applied_ = false;   // latch-first: one curve per run
  cube::AngularResponseCurve bs_applied_curve_;

  // Latch-first curve adoption (#102). The latched (transient_local)
  // producer normally delivers before activation, so the whole grid is
  // corrected consistently; a LATE producer means early beams fold in
  // uncorrected before the curve applies -- acceptable under auto's
  // "no curve = identity" contract, and the adoption INFO log timestamps
  // the transition for post-survey scrutiny.
  void sonarInfoCallback(const marine_interfaces::msg::SonarInfo & info)
  {
    cube::AngularResponseCurve curve;
    std::string reject_reason;
    if (!cube::curveFromSonarInfo(info, curve, reject_reason)) {
      // Empty-curve messages are normal (a producer with no calibration);
      // only malformed/unusable non-empty curves deserve a warning.
      if (!info.angular_response_angle_deg.empty() ||
        !info.angular_response_db_rel_nadir.empty())
      {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 60000,
          "Ignoring SonarInfo angular-response curve: %s",
          reject_reason.c_str());
      }
      return;
    }
    if (bs_explicit_file_) {
      RCLCPP_INFO_ONCE(get_logger(),
        "SonarInfo carries an angular-response curve, but the explicit "
        "backscatter_curve_file takes precedence (reprocessing override); "
        "ignoring the published curve.");
      return;
    }
    if (bs_curve_applied_) {
      // Latch-first: heartbeats re-publish the identical curve (fine,
      // silent); a genuinely different curve mid-run is ignored because the
      // record-time Welford correction must not mix curves within one grid.
      const bool same = curve.points == bs_applied_curve_.points &&
        curve.tl_removed == bs_applied_curve_.tl_removed &&
        curve.absorption_db_per_m == bs_applied_curve_.absorption_db_per_m;
      if (!same) {
        RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 60000,
          "SonarInfo published a DIFFERENT angular-response curve mid-run; "
          "keeping the first curve (one correction per grid). Restart or "
          "reconfigure the node to adopt the new curve.");
      }
      return;
    }
    if (curve.tl_removed) {
      RCLCPP_INFO(get_logger(),
        "Backscatter angular-response correction: %zu-point curve from "
        "SonarInfo (%s) [tier-2: TL-removed, alpha=%g dB/m]",
        curve.points.size(), info.sonar_model.c_str(),
        static_cast<double>(curve.absorption_db_per_m));
    } else {
      RCLCPP_INFO(get_logger(),
        "Backscatter angular-response correction: %zu-point curve from "
        "SonarInfo (%s)", curve.points.size(), info.sonar_model.c_str());
    }
    bs_curve_applied_ = true;
    bs_applied_curve_ = curve;
    geo_map_sheet_->setBackscatterCorrection(
      bs_mode_, std::move(curve.points),
      curve.tl_removed, curve.absorption_db_per_m);
  }

  // Quantized display-tile transport (#78, ADR-0008).
  rclcpp_lifecycle::LifecyclePublisher<marine_interfaces::msg::SonarVisualizationTile>::SharedPtr
    sonar_tile_publisher_;
  rclcpp_lifecycle::LifecyclePublisher<marine_interfaces::msg::TileCatalog>::SharedPtr
    tile_catalog_publisher_;
  rclcpp::Subscription<marine_interfaces::msg::TileRequest>::SharedPtr
    tile_request_subscription_;
  rclcpp::TimerBase::SharedPtr catalog_timer_;
  double catalog_interval_s_ = 5.0;
  // Source-side tile->version registry (#230/#106) backing the periodic
  // complete catalog; seeded from the whole draft store at startup, bumped
  // whenever a tile is pushed. Evicted tiles stay registered (they remain
  // servable from disk), so the catalog reflects the store, not RAM.
  marine_tiled_raster_store::TileCatalogBuilder catalog_builder_;

  // Disk-serve drain queue (#106): evicted tiles a consumer requested, served
  // from the draft store at a bounded rate (see drainDiskServeQueue). The set
  // is the queue's dedup companion -- always mutate the two together.
  std::deque<gggs::GridIndex> disk_serve_queue_;
  std::set<gggs::GridIndex> disk_serve_queued_;
  rclcpp::TimerBase::SharedPtr disk_serve_timer_;
  std::size_t disk_serve_tiles_per_tick_ = 4;
  std::size_t disk_serve_queue_max_depth_ = 64;
  double disk_serve_interval_s_ = 0.5;

  // Long-duration bounding parameters (#70, ADR-0001).
  std::size_t max_resident_tiles_ = 64;
  double ca_window_radius_m_ = 200.0;
  std::string base_link_frame_ = "base_link";

  // Last known vessel position, cached so a brief vessel-TF gap keeps the CA
  // window bounded (reuse the last fix) instead of falling back to the full set.
  double last_vessel_lat_ = 0.0;
  double last_vessel_lon_ = 0.0;
  bool have_vessel_pos_ = false;

  // Tiles persisted and evicted from RAM this session, pending lossless reload on
  // revisit. GridIndex is tiny (~level+row+col) so this set is negligible next to
  // the 920k-cell grids it lets us drop; it is the revisit-detection signal.
  std::set<gggs::GridIndex> evicted_indices_;

  // Draft-tile persistence (#21). draft_dir_ empty = disabled. Tiles are written
  // to a single fused `draft/` grid (no per-day epochs, unh_marine_autonomy#221);
  // newest value wins per cell across saves and sessions.
  std::string draft_dir_;
  // Prior-store directory for the predicted-surface prime (#91); empty = no
  // prior, node runs ungated (pre-#91 behaviour).
  std::string prior_store_dir_;
  double save_interval_s_ = 30.0;
  rclcpp::TimerBase::SharedPtr save_timer_;

  // Degrees->radians (avoid relying on M_PI being defined).
  static constexpr double kDegToRad = 0.017453292519943295;

  // Look up the map<-earth transform ONCE per publish and return it as a batched
  // affine. On a TF miss reuse the last good transform so a TF outage degrades
  // gracefully; fail only on the very first cycle, before any cache exists.
  bool currentMapFromEarth(Eigen::Isometry3d & out)
  {
    geometry_msgs::msg::TransformStamped map_from_earth;
    if(lookupAtOrLatest(map_frame_, "earth", get_clock()->now(), map_from_earth)) {
      last_publish_tf_ = map_from_earth;
      have_publish_tf_ = true;
    } else {
      if(!have_publish_tf_) {
        RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
          "No transform " << map_frame_ << " <- earth at publish time and no "
          "cached transform yet; skipping this publish cycle");
        return false;
      }
      map_from_earth = last_publish_tf_;
      RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
        "No transform " << map_frame_ << " <- earth at publish time; reusing the "
        "last good map<-earth transform (grid placement may be slightly stale)");
    }
    out = tf2::transformToEigen(map_from_earth);
    return true;
  }

  // Stamp the grid with the sheet's last-update time (steady-clock ns), matching
  // the pre-#70 publish.
  void stampGrid(grid_map::GridMap & map)
  {
    auto epoch = std::chrono::time_point<std::chrono::steady_clock>{};
    map.setTimestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(
        (geo_map_sheet_->lastUpdateTime() - epoch)).count());
  }

  // Bounded publish (#70, ADR-0001): a vessel-centered collision-avoidance window
  // on `grid` PLUS the incremental per-tile coverage stream on `~/tiles`.
  // Replaces the monolithic whole-survey publishGrid(). Eviction is NOT done here
  // -- it runs separately in pingCallback so a publish-time TF miss (which
  // early-returns below) can never stall RAM bounding while pings keep ingesting.
  void publishBounded()
  {
    if(geo_map_sheet_->grids().empty()) {
      // No tiles populated yet. pingCallback emits the actionable diagnostics.
      return;
    }
    Eigen::Isometry3d map_from_earth;
    if(!currentMapFromEarth(map_from_earth)) {
      return;
    }
    publishCaGrid(map_from_earth);
    publishDirtyTiles(map_from_earth);
  }

  // Collision-avoidance grid: project only the resident tiles within
  // ca_window_radius_m of the vessel onto the existing `grid` topic (bounded
  // extent, unchanged contract). Unknown cells in the window are NaN (lethal
  // under the costmap's unsurveyed_is_lethal). On a vessel-TF miss, reuse the last
  // known vessel position so the window stays bounded through brief TF gaps; only
  // before any fix has ever been seen do we fall back to the full resident set
  // (itself bounded by eviction) so the CA grid stays alive rather than starving.
  void publishCaGrid(const Eigen::Isometry3d & map_from_earth)
  {
    std::vector<std::shared_ptr<const cube::GeoGrid>> window;
    double lat = 0.0;
    double lon = 0.0;
    if(lookupVesselLatLon(lat, lon)) {
      last_vessel_lat_ = lat;
      last_vessel_lon_ = lon;
      have_vessel_pos_ = true;
      window = gridsInCaWindow(lat, lon);
    } else if(have_vessel_pos_) {
      RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
        "No vessel transform earth <- " << base_link_frame_ << "; reusing the last "
        "known vessel position to keep the CA window bounded");
      window = gridsInCaWindow(last_vessel_lat_, last_vessel_lon_);
    } else {
      RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
        "No vessel transform earth <- " << base_link_frame_ << " yet; publishing "
        "the full resident set as the CA grid (bounded by eviction)");
      for (const auto & g : geo_map_sheet_->grids()) {
        window.push_back(g);
      }
    }
    if(window.empty()) {
      return;
    }
    grid_map::GridMap map = cube::geoGridsToGridMap(
      window, map_frame_, cell_size_, map_from_earth);
    if(!map.exists("elevation")) {
      // No finite cells projected this cycle. Nothing to publish.
      return;
    }
    stampGrid(map);
    auto message = grid_map::GridMapRosConverter::toMessage(map);
    grid_publisher_->publish(*message);
    RCLCPP_INFO_STREAM_THROTTLE(get_logger(), *get_clock(), 10000,
      "Published CA grid: " << window.size() << " of " <<
      geo_map_sheet_->grids().size() << " resident tiles in the " <<
      ca_window_radius_m_ << "m window");
  }

  // Incremental coverage: emit each changed tile as its own GridMap on ~/tiles,
  // then clear the publish-dirty set. Bounds per-message size to one tile and
  // per-cycle cost to the tiles that actually changed.
  void publishDirtyTiles(const Eigen::Isometry3d & map_from_earth)
  {
    // One publish time stamps every tile this cycle: it is both the tile's
    // version (newest-wins, ADR-0008 D3) and its catalog version, so the two
    // always agree.
    const rclcpp::Time pub_time = now();
    const builtin_interfaces::msg::Time stamp = pub_time;
    const std::int64_t version = pub_time.nanoseconds();

    const std::set<gggs::GridIndex> dirty = geo_map_sheet_->publishDirtyGrids();
    for (const auto & index : dirty) {
      auto grid = geo_map_sheet_->gridAt(index);
      if(!grid) {
        continue;
      }
      std::vector<std::shared_ptr<const cube::GeoGrid>> one{grid};
      grid_map::GridMap map = cube::geoGridsToGridMap(
        one, map_frame_, cell_size_, map_from_earth);
      if(!map.exists("elevation")) {
        continue;
      }
      stampGrid(map);
      auto message = grid_map::GridMapRosConverter::toMessage(map);
      tiles_publisher_->publish(*message);

      // Quantized display tile (#78): push the changed tile and register its
      // version in the catalog. quantizeTile returns nullopt for an all-empty
      // tile, which the GridMap path already skipped above.
      if (auto vt = cube::quantizeTile(*grid, stamp)) {
        sonar_tile_publisher_->publish(*vt);
        catalog_builder_.update(index, version);
      }
    }
    geo_map_sheet_->clearPublishDirtyGrids();
  }

  // Publish the COMPLETE tile catalog (anti-entropy basis, #78/#230): the
  // consumer converges its cache to exactly this set. generation_time stamps
  // "now" so the consumer's prune gate is well-defined.
  void publishCatalog()
  {
    const rclcpp::Time gen = now();
    marine_interfaces::msg::TileCatalog msg;
    msg.header.stamp = gen;
    msg.header.frame_id = "gggs";

    // The catalog is the COMPLETE set of SERVABLE tiles: everything in the
    // version registry -- resident grids AND tiles evicted to the draft store
    // (#106; evicted tiles are served from disk via the drain queue). Building
    // from the registry rather than grids() keeps evicted coverage advertised,
    // so the consumer's prune-on-absence (ADR-0008 D4) converges to the full
    // store instead of eroding to the boat's resident window. The registry is
    // seeded from the whole store at startup and bumped on every push, so a
    // tile absent from it has no servable data yet.
    const marine_tiled_raster_store::TileCatalog catalog =
      catalog_builder_.buildCatalog(gen.nanoseconds());
    for (const auto & e : catalog.entries) {
      marine_interfaces::msg::TileCatalogEntry entry;
      entry.index.level = e.index.level();
      entry.index.row = e.index.row();
      entry.index.col = e.index.column();
      entry.version.sec = static_cast<std::int32_t>(e.version / 1000000000LL);
      entry.version.nanosec = static_cast<std::uint32_t>(e.version % 1000000000LL);
      msg.entries.push_back(entry);
    }
    tile_catalog_publisher_->publish(msg);
  }

  // Serve a consumer's TileRequest (#78/#230/#106). Resident tiles are served
  // immediately in full (3-band, via quantizeTile). A requested tile that has
  // been evicted to the draft store is QUEUED for the rate-limited disk-serve
  // drain (drainDiskServeQueue) rather than silently dropped, so a cold or
  // stale consumer can catch up on the whole store, not just the resident
  // window (ADR-0008 D4 completeness; #104 field symptom).
  void tileRequestCallback(const marine_interfaces::msg::TileRequest::SharedPtr msg)
  {
    // Serving publishes on the lifecycle tile publisher, which only emits while
    // ACTIVE; a request that arrives configured-but-inactive would otherwise
    // log-spam an inactive-publisher warning per tile.
    if (get_current_state().id() !=
      lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE)
    {
      return;
    }
    // The wire TileIndex cannot construct a gggs::GridIndex directly (the
    // (level,row,col) ctor is private), so requests are matched field-wise
    // against the indices the node already holds: resident grids for the
    // immediate serve, evicted_indices_ for the disk-serve queue. Both sets
    // are boat-local and small (resident <= max_resident_tiles; evicted =
    // this session's overflow), so the linear scans are cheap.
    const auto matches = [](const gggs::GridIndex & idx,
      const marine_interfaces::msg::TileIndex & ti) {
        return idx.level() == ti.level && idx.row() == ti.row &&
               idx.column() == ti.col;
      };
    const builtin_interfaces::msg::Time stamp = now();
    std::size_t served = 0;
    std::size_t queued = 0;
    std::size_t overflow = 0;
    for (const auto & ti : msg->tiles) {
      bool handled = false;
      for (const auto & grid : geo_map_sheet_->grids()) {
        if (grid && matches(grid->index(), ti)) {
          if (auto vt = cube::quantizeTile(*grid, stamp)) {
            sonar_tile_publisher_->publish(*vt);
            ++served;
          }
          handled = true;
          break;
        }
      }
      if (handled) {
        continue;
      }
      // Not resident: queue evicted tiles for the disk-serve drain, bounded
      // and dedup'd. An index that is neither resident nor evicted has no
      // servable data (never surveyed this store) and is skipped.
      for (const auto & index : evicted_indices_) {
        if (!matches(index, ti)) {
          continue;
        }
        if (!disk_serve_queued_.count(index)) {
          if (disk_serve_queue_.size() >= disk_serve_queue_max_depth_) {
            ++overflow;
          } else {
            disk_serve_queue_.push_back(index);
            disk_serve_queued_.insert(index);
            ++queued;
          }
        }
        break;
      }
    }
    if (overflow > 0) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 30000,
        "TileRequest: disk-serve queue full (%zu); dropped %zu request(s) -- "
        "the consumer re-requests via the next catalog. Raise "
        "disk_serve_queue_max_depth or drain rate if this persists.",
        disk_serve_queue_max_depth_, overflow);
    }
    if (served + queued < msg->tiles.size()) {
      RCLCPP_DEBUG_THROTTLE(get_logger(), *get_clock(), 5000,
        "TileRequest: served %zu, queued %zu of %zu (the rest have no "
        "servable data or overflowed the disk-serve queue)",
        served, queued, msg->tiles.size());
    }
  }

  // Drain the disk-serve queue (#106): serve up to disk_serve_tiles_per_tick_
  // queued evicted tiles per tick from the draft store, pacing a cold
  // consumer's catch-up onto spare link bandwidth. READ-ONLY by design: each
  // tile is loaded into a SCRATCH store/sheet, quantized, published, and
  // discarded -- deliberately NOT inserted into geo_map_sheet_. Do not merge
  // this with reloadEvictedTile (the revisit path): reloading into the live
  // sheet here would let a bulk catch-up churn the LRU and evict the tiles
  // the survey is actively updating.
  void drainDiskServeQueue()
  {
    if (disk_serve_queue_.empty() || draft_dir_.empty()) {
      return;
    }
    // The per-tick budget counts ATTEMPTS, not published tiles (#106 review
    // r1): pacing on publishes would let a run of empty/absent entries turn
    // one tick into up to queue-depth loadWindow reads, defeating the I/O
    // pacing the budget exists for.
    const builtin_interfaces::msg::Time stamp = now();
    std::size_t attempts = 0;
    while (attempts < disk_serve_tiles_per_tick_ && !disk_serve_queue_.empty()) {
      ++attempts;
      const gggs::GridIndex index = disk_serve_queue_.front();
      disk_serve_queue_.pop_front();
      disk_serve_queued_.erase(index);

      // Became resident again since it was queued (revisit reload): serve the
      // live grid, which is at least as fresh as the on-disk copy.
      if (auto grid = geo_map_sheet_->gridAt(index)) {
        if (auto vt = cube::quantizeTile(*grid, stamp)) {
          sonar_tile_publisher_->publish(*vt);
        }
        continue;
      }
      try {
        marine_bathymetry_store::BathymetryStore scratch =
          marine_bathymetry_store::BathymetryStore::fromCellSize(
          static_cast<float>(cell_size_));
        marine_bathymetry_store::loadWindow(
          scratch, draft_dir_, index.southWestPosition(),
          index.northEastPosition(), nullptr);
        // Best source across Processed union Draft (ADR-0010 D8): serve the fused
        // tile. Processed must win per overlapping cell (the store's query walks
        // `Processed > Draft`), and seeding order alone cannot guarantee that -- two
        // settled 1-sample hypotheses tie-break to the first-seeded in CUBE's
        // chooseHypothesis. Seed Processed FULLY first, then layer Draft with a
        // per-cell filter (primeFromTileSkippingMask with the Processed tile as
        // mask) that skips any cell Processed already covers, so the overlapped cell
        // keeps only the Processed hypothesis.
        const auto & processed =
          scratch.tiles(marine_bathymetry_store::SourceLayer::Processed);
        const auto & draft =
          scratch.tiles(marine_bathymetry_store::SourceLayer::Draft);
        const auto d_it = draft.find(index);
        const auto p_it = processed.find(index);
        if (d_it == draft.end() && p_it == processed.end()) {
          continue;  // absent on disk after all; nothing to serve
        }
        cube::GeoMapSheet scratch_sheet(static_cast<float>(cell_size_));
        if (p_it != processed.end()) {
          cube::primeFromTile(p_it->second, scratch_sheet);
        }
        if (d_it != draft.end()) {
          cube::primeFromTileSkippingMask(
            d_it->second,
            p_it != processed.end() ? &p_it->second : nullptr, scratch_sheet);
        }
        if (auto grid = scratch_sheet.gridAt(index)) {
          if (auto vt = cube::quantizeTile(*grid, stamp)) {
            sonar_tile_publisher_->publish(*vt);
          }
        }
        // scratch store/sheet destroyed here; geo_map_sheet_ untouched.
      } catch (const std::exception & e) {
        RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
          "Disk-serve: could not load requested evicted tile: " << e.what() <<
          " (skipping; the consumer re-requests via the next catalog)");
      }
    }
  }

  // LRU eviction (#70, ADR-0001): bound resident RAM losslessly. Persist all
  // pending tiles first so every resident tile is on disk, then drop the coldest
  // beyond budget (reloadable on revisit). Without a draft_dir there is nowhere
  // to persist, so do NOT evict -- leave the data resident and warn (dropping it
  // would be data loss).
  void evictColdTiles()
  {
    if(geo_map_sheet_->residentTileCount() <= max_resident_tiles_) {
      return;
    }
    if(draft_dir_.empty()) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 30000,
        "Resident tiles (%zu) exceed max_resident_tiles (%zu) but draft_dir is "
        "unset: not evicting (eviction needs persistence to stay lossless). Set "
        "draft_dir to bound RAM.",
        geo_map_sheet_->residentTileCount(), max_resident_tiles_);
      return;
    }
    // Flush every dirty tile so all resident tiles are durably on disk; only then
    // is dropping a cold tile lossless (it can be reloaded on revisit).
    saveDirtyTiles();
    const std::size_t before = geo_map_sheet_->residentTileCount();
    trimResidentToBudget();
    const std::size_t after = geo_map_sheet_->residentTileCount();
    if (after < before) {
      RCLCPP_INFO_STREAM_THROTTLE(get_logger(), *get_clock(), 30000,
        "Evicted " << (before - after) << " cold tile(s) to disk; " << after <<
        " resident (budget " << max_resident_tiles_ << ")");
    }
    // If a save failed, the still-dirty cold tiles are NOT dropped (see
    // trimResidentToBudget) -- RAM stays transiently over budget rather than
    // losing unsaved data. Surface that so a persistent disk failure is visible.
    if (after > max_resident_tiles_) {
      RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 30000,
        "Could not evict to budget this cycle: %zu resident > %zu budget. Cold "
        "tiles with unsaved data are kept to avoid loss; check draft_dir writes.",
        after, max_resident_tiles_);
    }
  }

  // Drop clean, already-persisted cold tiles down to the resident budget,
  // recording them as evicted (a revisit reloads from disk). A cold tile that is
  // STILL in the dirty set (its save did not succeed) is kept resident -- dropping
  // it would lose unsaved soundings, breaking the lossless guarantee (must-fix).
  // Used after the startup prime (every primed tile is clean/on-disk) and by
  // evictColdTiles after its flush.
  void trimResidentToBudget()
  {
    const std::set<gggs::GridIndex> still_dirty = geo_map_sheet_->dirtyGrids();
    for (const auto & index : geo_map_sheet_->coldTiles(max_resident_tiles_)) {
      if (still_dirty.count(index)) {
        continue;  // unsaved -- never drop (would lose data); retry next cycle
      }
      geo_map_sheet_->dropTile(index);
      evicted_indices_.insert(index);
    }
  }

  // Vessel lat/lon from earth <- base_link (translation = the ECEF position of
  // the base_link origin).
  bool lookupVesselLatLon(double & lat, double & lon)
  {
    geometry_msgs::msg::TransformStamped t;
    if(!lookupAtOrLatest("earth", base_link_frame_, get_clock()->now(), t)) {
      return false;
    }
    gz4d::GeoPointECEF ecef(
      t.transform.translation.x, t.transform.translation.y,
      t.transform.translation.z);
    gz4d::GeoPointLatLongDegrees ll(ecef);
    lat = ll.latitude();
    lon = ll.longitude();
    return true;
  }

  // Resident tiles whose center is within ca_window_radius_m (plus one tile of
  // margin so a tile straddling the boundary is included) of (lat, lon). An
  // equirectangular metric is ample for a few-hundred-metre window.
  std::vector<std::shared_ptr<const cube::GeoGrid>> gridsInCaWindow(
    double lat, double lon)
  {
    std::vector<std::shared_ptr<const cube::GeoGrid>> ret;
    const double tile_span_m =
      geo_map_sheet_->nominalCellSizeMeters() * gggs::GridIndex::cellRowCount();
    const double max_dist = ca_window_radius_m_ + tile_span_m;
    const double m_per_deg = 111320.0;
    const double cos_lat = std::cos(lat * kDegToRad);
    for (const auto & g : geo_map_sheet_->grids()) {
      if(!g) {
        continue;
      }
      const gggs::GridIndex & idx = g->index();
      const double center_lat = 0.5 * (idx.southLatitude() + idx.northLatitude());
      const double center_lon = 0.5 * (idx.westLongitude() + idx.eastLongitude());
      const double dy = (center_lat - lat) * m_per_deg;
      const double dx = (center_lon - lon) * m_per_deg * cos_lat;
      if(std::sqrt(dx * dx + dy * dy) <= max_dist) {
        ret.push_back(g);
      }
    }
    return ret;
  }

  // Lossless revisit reload (#70, ADR-0001): window-load just this previously-
  // evicted tile into a scratch store and reseed its settled cells so
  // accumulation continues from the saved state and the next save is complete.
  //
  // Returns true when the tile is fully restored and the on-disk state is
  // consistent with what a save will write -- the tile was found and reseeded (or
  // genuinely absent, nothing to preserve) AND the prior gate re-prime succeeded.
  // Returns false on a load error -- a survey read error (the file may exist but be
  // transiently unreadable) OR a prior READ error (#118): the caller must then keep
  // the tile evicted and NOT let the partial re-created grid be saved over the
  // intact on-disk surface (review #70 round 2), so a later revisit retries.
  bool reloadEvictedTile(const gggs::GridIndex & index)
  {
    // Re-prime the prior FIRST (#118): a tile whose predicted surface came only
    // from the prior prime returns from eviction with the blunder gate (and #59
    // slope correction) silently OFF -- the survey restore below rebuilds only
    // measured cells. Prior before the survey restore so the finer
    // survey-derived predicted depth overwrites where survey data exists (the
    // same order as on_configure). Runs even without draft persistence:
    // prior-primed tiles are clean and evictable regardless of draft_dir.
    //
    // Single-tile exact-level primes, NOT primeFromPriorLayers over the loaded
    // window -- the window also holds edge-adjacent neighbor tiles, and priming
    // them would lazy-create nodes that inflate the resident count against the
    // very budget doing the evicting. Chart first, Reference overwrites -- the
    // same layered, exact-level-only semantics as the on_configure prime (no
    // cross-level fallback in the live node). On a prior READ error the tile is
    // kept evicted (prior_ok=false, returned below) so a later revisit retries the
    // re-prime instead of silently running ungated for the rest of the session
    // (#118): the read is transient, and a permanently-off blunder gate is exactly
    // the failure this issue closes.
    bool prior_ok = true;
    if (!prior_store_dir_.empty()) {
      try {
        marine_bathymetry_store::BathymetryStore prior_scratch =
          marine_bathymetry_store::BathymetryStore::fromCellSize(
          static_cast<float>(cell_size_));
        const auto psw = index.southWestPosition();
        const auto pne = index.northEastPosition();
        marine_bathymetry_store::loadWindow(
          prior_scratch, prior_store_dir_, psw, pne, nullptr);
        bool primed = false;
        const auto & chart_tiles =
          prior_scratch.tiles(marine_bathymetry_store::SourceLayer::Chart);
        auto chart_it = chart_tiles.find(index);
        if (chart_it != chart_tiles.end()) {
          cube::primeFromTile(chart_it->second, *geo_map_sheet_,
            /*seed_settled=*/false);
          primed = true;
        }
        const auto & ref_tiles =
          prior_scratch.tiles(marine_bathymetry_store::SourceLayer::Reference);
        auto ref_it = ref_tiles.find(index);
        if (ref_it != ref_tiles.end()) {
          cube::primeFromTile(ref_it->second, *geo_map_sheet_,
            /*seed_settled=*/false);
          primed = true;
        }
        if (primed) {
          // Observability (#118): gate re-activation must be visible; throttled
          // so a burst of revisits does not spam the log.
          RCLCPP_INFO_STREAM_THROTTLE(get_logger(), *get_clock(), 30000,
            "Re-primed prior gate (blunder + slope) on revisited tile " << index);
        }
      } catch (const std::exception & e) {
        prior_ok = false;
        // Observability (#118): a failed gate re-activation is operator-actionable,
        // so name the tile and throttle much tighter than the success INFO (30 s)
        // -- distinct failing tiles are far less likely to be collapsed into one
        // suppressed line, and the message states the (now real) retry contract.
        RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 1000,
          "Could not re-prime prior blunder gate on revisited tile " << index <<
          ": " << e.what() << " (kept evicted and retried on its next revisit; "
          "this batch's soundings on it are dropped to keep the gate correct)");
      }
    }
    if(draft_dir_.empty()) {
      // Prior-only tile (no survey to restore): its consistency IS the prior
      // re-prime, so a failed prior read must keep it evicted for retry (#118).
      return prior_ok;
    }
    try {
      marine_bathymetry_store::BathymetryStore scratch =
        marine_bathymetry_store::BathymetryStore::fromCellSize(
        static_cast<float>(cell_size_));
      const auto sw = index.southWestPosition();
      const auto ne = index.northEastPosition();
      marine_bathymetry_store::loadWindow(scratch, draft_dir_, sw, ne, nullptr);
      // Best source across Processed union Draft (ADR-0010 D8): restore the fused
      // cells. Processed must win per overlapping cell (the store's query walks
      // `Processed > Draft`), and seeding order alone cannot guarantee that -- two
      // settled 1-sample hypotheses tie-break to the first-seeded in CUBE's
      // chooseHypothesis. Seed Processed FULLY first, then layer Draft with a
      // per-cell filter (primeFromTileSkippingMask with the Processed tile as mask)
      // that skips any cell Processed already covers, so the overlapped cell keeps
      // only the Processed hypothesis.
      const auto & processed =
        scratch.tiles(marine_bathymetry_store::SourceLayer::Processed);
      const auto & draft =
        scratch.tiles(marine_bathymetry_store::SourceLayer::Draft);
      const auto p_it = processed.find(index);
      if (p_it != processed.end()) {
        cube::primeFromTile(p_it->second, *geo_map_sheet_);
      }
      if (const auto it = draft.find(index); it != draft.end()) {
        cube::primeFromTileSkippingMask(
          it->second,
          p_it != processed.end() ? &p_it->second : nullptr, *geo_map_sheet_);
      }
      // Stored cells restored, but if the prior read failed keep the tile evicted
      // so the prior gate is retried on the next revisit (#118) -- the caller drops
      // the tile; the intact on-disk surface is untouched.
      return prior_ok;
    } catch (const std::exception & e) {
      RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
        "Could not reload evicted tile on revisit: " << e.what() <<
        " (dropping the partial re-created tile to protect the on-disk surface; "
        "will retry on the next revisit)");
      return false;
    }
  }

  // Persist every grid touched since the last save as a marine_bathymetry_store
  // draft tile (via tile_io::saveTile -- a direct, flush/close-checked write, not
  // crash-atomic), then clear the dirty set. A no-op when persistence is disabled or
  // nothing changed.
  //
  // NOTE: geoGridToTile() calls GeoGrid::values(), which flushes the median
  // pre-filter -- the same flush the end-of-session export does, now happening
  // every save_interval. The queue refills from subsequent pings; the published
  // grid already triggers the same flush on every publish, so this adds no new
  // behavior beyond the cadence. (Pinned by the periodic-save test.)
  void saveDirtyTiles()
  {
    if (draft_dir_.empty() || !geo_map_sheet_) {
      return;
    }
    const std::set<gggs::GridIndex> dirty = geo_map_sheet_->dirtyGrids();
    if (dirty.empty()) {
      return;
    }

    // Single fused draft grid (unh_marine_autonomy#221): tiles go directly under
    // <draft_dir>/draft/ with no per-day epoch segment. The live node writes the
    // `draft` layer (ADR-0010 D8): a bounded/approximate boat-side product that an
    // off-boat batch-regen later supersedes as the authoritative `processed` layer
    // (Processed > Draft, and each processed write clears the overlapped draft
    // cells). Newest value wins per cell, so successive saves and sessions
    // accumulate into one grid.
    const std::string dir =
      draft_dir_ + "/" +
      marine_bathymetry_store::layerDirName(
      marine_bathymetry_store::SourceLayer::Draft);

    std::size_t written = 0;
    try {
      std::filesystem::create_directories(dir);
      for (const auto & index : dirty) {
        auto grid_ptr = geo_map_sheet_->gridAt(index);
        if (!grid_ptr) {
          continue;
        }
        // BathyCell is 2-band since uma#248 (no per-cell timestamp/source_index);
        // coarse provenance is store-level StoreMetadata (not wired in the live
        // node yet, #21 scope).
        marine_bathymetry_store::BathymetryTile tile =
          cube::geoGridToTile(*grid_ptr);
        if (!tile.dirty()) {
          // No finite cells (all queued/NaN) -- nothing to write. dirty() is the
          // value-raster flag geoGridToTile sets iff it wrote a finite cell
          // (same idiom as mapSheetToTiles).
          continue;
        }
        const std::string path =
          dir + "/" + marine_bathymetry_store::tileFilename(index);
        marine_bathymetry_store::saveTile(tile, path);
        ++written;
      }
    } catch (const std::exception & e) {
      // A write failure must not crash the node -- log and keep the dirty set so
      // the next save retries (do NOT clear on failure).
      RCLCPP_ERROR_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
        "Failed to save draft tiles to " << dir << ": " << e.what() <<
        " (will retry next interval)");
      return;
    }

    geo_map_sheet_->clearDirtyGrids();
    RCLCPP_INFO_STREAM_THROTTLE(get_logger(), *get_clock(), 30000,
      "Saved " << written << " draft tile(s) to " << dir);
  }

  // Look up target<-source at the exact stamp; on extrapolation (the requested
  // time is outside the buffered window) fall back to the latest available
  // transform. The map<-sensor chain includes the position-driven `map`
  // transform, which on some platforms updates slower (~1 Hz) than the sonar
  // ping rate (#36), so an exact-stamp lookup routinely extrapolates. Platform
  // pose varies slowly relative to a ping interval, so the latest transform is
  // an acceptable placement -- far better than dropping the ping and silently
  // emptying the grid. Mirrors detections_to_pointcloud (PR #33).
  bool lookupAtOrLatest(
    const std::string & target, const std::string & source,
    const rclcpp::Time & stamp, geometry_msgs::msg::TransformStamped & out)
  {
    try {
      out = tf_buffer_->lookupTransform(target, source, stamp);
      return true;
    } catch (const tf2::ExtrapolationException &) {
      try {
        out = tf_buffer_->lookupTransform(target, source, tf2::TimePointZero);
        return true;
      } catch (const tf2::TransformException &) {
        return false;
      }
    } catch (const tf2::TransformException &) {
      return false;
    }
  }

  void pingCallback(const sensor_msgs::msg::PointCloud2::UniquePtr & msg)
  {
    if (get_current_state().id() != lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE) {
      return;
    }

    // Migrated to geographic accumulation (#21): look up the EARTH-frame
    // transform (instead of map_frame_) and convert each sounding to lat/lon,
    // mirroring bag_to_geotiff.cpp:553-559. The published grid_map stays in
    // map_frame_ -- publishGrid() projects the GeoMapSheet back at publish time.
    geometry_msgs::msg::TransformStamped transform;
    if(!lookupAtOrLatest("earth", msg->header.frame_id,
        rclcpp::Time(msg->header.stamp), transform))
    {
      RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
        "No transform earth <- " << msg->header.frame_id <<
        " (at ping time or latest); dropping ping -- grid will not update");
      return;
    }

    // PointCloud2 point count is width * height (height > 1 for organized clouds).
    const size_t point_count = static_cast<size_t>(msg->width) * msg->height;
    std::vector<cube::GeoSounding> soundings;
    soundings.reserve(point_count);

    try {
      sensor_msgs::PointCloud2ConstIterator<float> iter_x(*msg, "x");
      sensor_msgs::PointCloud2ConstIterator<float> iter_y(*msg, "y");
      sensor_msgs::PointCloud2ConstIterator<float> iter_z(*msg, "z");
      sensor_msgs::PointCloud2ConstIterator<float> iter_vertical_uncertainty(*msg,
        "vertical_uncertainty");
      sensor_msgs::PointCloud2ConstIterator<float> iter_horizontal_uncertainty(
        *msg, "horizontal_uncertainty");

      // Per-beam backscatter inputs (ADR-0007 D3), both OPTIONAL: intensity has
      // long been emitted by detections_to_pointcloud, beam_angle is newer, so an
      // older recorded /soundings bag may carry neither/either. Absent or NaN is
      // fine -- the estimator's recordBeam() excludes NaN-intensity beams -- so we
      // tolerate a missing field rather than dropping the ping. Optional iterators
      // are advanced inside the loop (before any `continue`) to stay aligned.
      const auto has_field = [&msg](const char * name) {
          for (const auto & f : msg->fields) {
            if (f.name == name) {return true;}
          }
          return false;
        };
      std::optional<sensor_msgs::PointCloud2ConstIterator<float>> iter_intensity;
      std::optional<sensor_msgs::PointCloud2ConstIterator<float>> iter_beam_angle;
      if (has_field("intensity")) {iter_intensity.emplace(*msg, "intensity");}
      if (has_field("beam_angle")) {iter_beam_angle.emplace(*msg, "beam_angle");}

      size_t dropped = 0;
      for (; (iter_x != iter_x.end()) &&
        (iter_y != iter_y.end()) &&
        (iter_z != iter_z.end()) &&
        (iter_vertical_uncertainty != iter_vertical_uncertainty.end()) &&
        (iter_horizontal_uncertainty != iter_horizontal_uncertainty.end());
        ++iter_x, ++iter_y, ++iter_z,
        ++iter_vertical_uncertainty, ++iter_horizontal_uncertainty)
      {
        const float x = *iter_x, y = *iter_y, z = *iter_z;
        const float vu = *iter_vertical_uncertainty, hu = *iter_horizontal_uncertainty;
        const float intensity = iter_intensity ? **iter_intensity : std::nanf("");
        const float beam_angle = iter_beam_angle ? **iter_beam_angle : std::nanf("");
        if (iter_intensity) {++*iter_intensity;}
        if (iter_beam_angle) {++*iter_beam_angle;}

        // Drop soundings the CUBE estimator can't use. A non-finite position or
        // uncertainty -- or a non-positive vertical / negative horizontal
        // uncertainty (sqrt of which is NaN) -- propagates a NaN variance into
        // the estimator: NaN depth estimate -> the whole cell drops out of the
        // published grid. Skipping here keeps the dropped-count diagnostic
        // honest and matches Grid::insert's guard. (Upstream cause is usually
        // missing attitude/odom TF -- e.g. the simulator before its frame
        // params were set -- which makes detections_to_pointcloud emit NaN.)
        if(!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
          !std::isfinite(vu) || !std::isfinite(hu) || vu <= 0.0f || hu < 0.0f)
        {
          ++dropped;
          continue;
        }

        // Sensor-frame point -> earth (ECEF) -> lat/lon, the same chain
        // bag_to_geotiff / import_bag use for native-GGGS accumulation.
        geometry_msgs::msg::PointStamped point_re_sensor;
        point_re_sensor.point.x = x;
        point_re_sensor.point.y = y;
        point_re_sensor.point.z = z;
        point_re_sensor.header = msg->header;

        geometry_msgs::msg::PointStamped point_ecef;
        tf2::doTransform(point_re_sensor, point_ecef, transform);

        gz4d::GeoPointECEF ecef(
          point_ecef.point.x, point_ecef.point.y, point_ecef.point.z);
        gz4d::GeoPointLatLongDegrees ll(ecef);
        cube::GeoSounding s(ll);
        s.sounding.vertical_error = vu;
        s.sounding.horizontal_error = hu;
        s.sounding.intensity = intensity;    // per-beam backscatter (may be NaN)
        s.sounding.beam_angle = beam_angle;  // incidence rel. nadir (may be NaN)
        // Per-beam slant range for the tier-2 TL correction (cube#87). The
        // /soundings cloud carries the touchdown in the SENSOR frame, so its norm
        // equals the slant range R = twtt*c/2 EXACTLY when the transmit tilt is
        // zero -- which holds for the M3 (flat downward array, tx_angle == 0, the
        // only tier-2-calibrated sonar today). For a tilted-transmit sonar the
        // norm is R*sqrt(1 + sin^2(tx)*sin^2(rx)), a small bounded overestimate;
        // the offline import_bag path uses twtt*c/2 directly, so live/offline
        // match for the M3. Revisit (carry twtt) if a tilted-tx sonar is added.
        s.sounding.slant_range = std::sqrt(x * x + y * y + z * z);
        soundings.push_back(s);
      }

      if(dropped > 0) {
        RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
          dropped << " of " << point_count << " soundings dropped (non-finite "
          "or non-positive position/uncertainty) -- check attitude/odom TF "
          "feeding " << msg->header.frame_id);
      }
    } catch (const std::exception & e) {
      // Missing field in the cloud, etc. -- don't let it kill the callback.
      RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
        "Could not read soundings cloud: " << e.what());
      return;
    }

    auto epoch = std::chrono::time_point<std::chrono::steady_clock>{};
    auto timestamp = epoch + std::chrono::seconds(msg->header.stamp.sec) +
      std::chrono::nanoseconds(msg->header.stamp.nanosec);

    // Lossless revisit reload, BEFORE the add (#118, #70, ADR-0001). Reload any
    // evicted tile this batch is about to touch FIRST -- before addSoundings -- so
    // the batch's beams accrete onto the reloaded/re-primed hypotheses AND the
    // blunder gate (+ #59 slope correction) is already active for the batch's own
    // soundings. Node::insert accepts anything while a cell's predicted_depth_ is
    // NaN, so a false-deep blunder in the FIRST revisit batch of an evicted
    // prior-only tile would otherwise be settled ungated before the reload
    // re-primes it. The offline importer already reloads-before-add
    // (store_import.cpp addBatch); this mirrors it.
    //
    // Key off gridIndicesForSoundings -- the SAME influence-radius-expanded window
    // addSoundings will touch (one-cell floor, #104; spills into a near-seam
    // neighbour tile) -- NOT the sounding centres, so an evicted neighbour reached
    // only by spillover is still reloaded, not clobbered (review #70 round 2). It
    // is the post-add dirty set computed one step early.
    //
    // On a reload error, record the tile and -- AFTER the add -- DROP its partial
    // re-created grid (the on-disk surface is the real data and must not be
    // clobbered) and keep the evicted marker so the next revisit retries; the few
    // new soundings for that tile this cycle are discarded (they re-survey cheaply;
    // disk/gate integrity wins).
    // Also run when only a prior store is configured (#118): without draft
    // persistence a prior-primed (clean) tile can still be evicted, and its
    // revisit must re-prime the gate even though there is no survey to restore.
    std::vector<gggs::GridIndex> reload_failed;
    if((!draft_dir_.empty() || !prior_store_dir_.empty()) &&
      !evicted_indices_.empty())
    {
      for (const auto & idx : geo_map_sheet_->gridIndicesForSoundings(soundings)) {
        if(evicted_indices_.count(idx)) {
          if(reloadEvictedTile(idx)) {
            evicted_indices_.erase(idx);
          } else {
            reload_failed.push_back(idx);
          }
        }
      }
    }

    geo_map_sheet_->addSoundings(soundings, timestamp);

    for (const auto & idx : reload_failed) {
      geo_map_sheet_->dropTile(idx);  // protect the intact on-disk surface
    }

    if(last_grid_publish_time_.nanoseconds() == 0 ||
      rclcpp::Time(msg->header.stamp) - last_grid_publish_time_ >
      rclcpp::Duration::from_seconds(5.0))
    {
      publishBounded();
      // Evict OUTSIDE publishBounded so RAM bounding runs even when a publish-time
      // TF miss makes publishBounded early-return (#70 review): eviction depends
      // only on the draft store, not on any transform.
      evictColdTiles();
      last_grid_publish_time_ = msg->header.stamp;
    }
  }
};

int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);
  auto cube = std::make_shared<CubeBathymetry>();

  rclcpp::executors::SingleThreadedExecutor exe;
  exe.add_node(cube->get_node_base_interface());
  exe.spin();


  rclcpp::shutdown();
  return 0;
}
