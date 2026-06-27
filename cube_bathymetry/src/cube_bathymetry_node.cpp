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


#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
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
#include "cube_bathymetry/store_import.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/tile_io.hpp"


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
        const auto & draft_tiles =
          store.tiles(marine_bathymetry_store::SourceLayer::Draft);
        if (!draft_tiles.empty()) {
          cube::loadIntoSheet(
            store, marine_bathymetry_store::SourceLayer::Draft, *geo_map_sheet_);
          RCLCPP_INFO(get_logger(),
            "Primed GeoMapSheet from %zu draft tiles under %s",
            draft_tiles.size(), draft_dir_.c_str());
          // Bound the prime to the resident budget (must-fix): loadIntoSheet loads
          // the WHOLE store, so without this a restart mid-long-survey re-creates
          // the unbounded RAM #70 prevents. Primed tiles are clean and already on
          // disk, so dropping the cold ones is lossless; they reload on revisit.
          // (The transient peak during the whole-store load before the trim is a
          // known limitation -- a windowed prime needs a startup position that is
          // not available at on_configure; tracked as a follow-up.)
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

    ping_subscription_ = create_subscription<sensor_msgs::msg::PointCloud2>("soundings",
      rclcpp::SensorDataQoS(),
      std::bind(&CubeBathymetry::pingCallback, this, std::placeholders::_1));

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
    }
    geo_map_sheet_->clearPublishDirtyGrids();
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
  // Returns true when the on-disk state is now consistent with what a save will
  // write -- either the tile was found and reseeded, or it is genuinely absent on
  // disk (nothing to preserve). Returns false ONLY on a load error (the file may
  // exist but be transiently unreadable): the caller must then NOT let the partial
  // re-created grid be saved over the intact on-disk surface (review #70 round 2).
  bool reloadEvictedTile(const gggs::GridIndex & index)
  {
    if(draft_dir_.empty()) {
      return true;
    }
    try {
      marine_bathymetry_store::BathymetryStore scratch =
        marine_bathymetry_store::BathymetryStore::fromCellSize(
        static_cast<float>(cell_size_));
      const auto sw = index.southWestPosition();
      const auto ne = index.northEastPosition();
      marine_bathymetry_store::loadWindow(scratch, draft_dir_, sw, ne, nullptr);
      const auto & tiles =
        scratch.tiles(marine_bathymetry_store::SourceLayer::Draft);
      auto it = tiles.find(index);
      if(it != tiles.end()) {
        cube::primeFromTile(it->second, *geo_map_sheet_);
      }
      return true;
    } catch (const std::exception & e) {
      RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
        "Could not reload evicted tile on revisit: " << e.what() <<
        " (dropping the partial re-created tile to protect the on-disk surface; "
        "will retry on the next revisit)");
      return false;
    }
  }

  // Persist every grid touched since the last save as a marine_bathymetry_store
  // draft tile (atomic temp-then-rename via tile_io::saveTile), then clear the
  // dirty set. A no-op when persistence is disabled or nothing changed.
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

    const int64_t ts_ns = get_clock()->now().nanoseconds();
    // Single fused draft grid (unh_marine_autonomy#221): tiles go directly under
    // <draft_dir>/draft/ with no per-day epoch segment. Newest value wins per
    // cell, so successive saves and sessions accumulate into one grid.
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
        // source_index 0 = no registry wired yet (#21 scope); follow-on once
        // marine_control device-control lands.
        marine_bathymetry_store::BathymetryTile tile =
          cube::geoGridToTile(*grid_ptr, ts_ns, /*source_index=*/0);
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

    geo_map_sheet_->addSoundings(soundings, timestamp);

    // Lossless revisit reload (#70, ADR-0001). Any tile this batch just
    // re-created/dirtied that was evicted earlier must be reseeded from disk
    // before the next save, or that save overwrites the tile's full on-disk
    // surface with only the freshly-accumulated cells.
    //
    // Key off the DIRTY set -- the grids insert() actually touched -- NOT the
    // sounding centres: addSoundings expands the bounds by a cell and spills into
    // neighbour tiles near a GGGS seam, so a centre-only check would miss an
    // evicted neighbour and clobber it (review #70 round 2). Reseeding does not
    // mark dirty, so the grid stays dirty for the save (reloaded settled cells +
    // new cells); a resurveyed cell keeps the new value, others the reloaded one.
    //
    // On a reload error, DROP the partial re-created grid (the on-disk surface is
    // the real data and must not be clobbered) and keep the evicted marker so the
    // next revisit retries -- the few new soundings for that tile this cycle are
    // discarded (they re-survey cheaply; disk integrity wins).
    if(!draft_dir_.empty() && !evicted_indices_.empty()) {
      std::vector<gggs::GridIndex> revisited;
      for (const auto & idx : geo_map_sheet_->dirtyGrids()) {
        if(evicted_indices_.count(idx)) {
          revisited.push_back(idx);
        }
      }
      for (const auto & idx : revisited) {
        if(reloadEvictedTile(idx)) {
          evicted_indices_.erase(idx);
        } else {
          geo_map_sheet_->dropTile(idx);  // protect the intact on-disk surface
        }
      }
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
