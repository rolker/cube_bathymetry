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
#include <set>
#include <string>
#include <vector>

#include "tf2_ros/transform_listener.h"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "lifecycle_msgs/msg/state.hpp"

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
#include "std_srvs/srv/trigger.hpp"

#include "marine_autonomy/gz4d_geo.h"
#include "cube_bathymetry/store_import.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/bathymetry_tile.hpp"
#include "marine_bathymetry_store/epoch.hpp"
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

    // Draft-tile persistence (#21). draft_dir empty (default) disables it; set
    // it per deployment to opt in. Tiles are written as marine_bathymetry_store
    // `draft/<epoch>/` GeoTIFFs so the costmap layer (#164) and sim live loop
    // (#77) read exactly what CUBE writes.
    draft_dir_ = declare_parameter("draft_dir", std::string(""));
    save_interval_s_ = declare_parameter("save_interval", 30.0);

    if (!draft_dir_.empty()) {
      // On startup, prime the fresh GeoMapSheet from the newest persisted draft
      // epoch so slope correction warm-starts on replayed areas.
      try {
        marine_bathymetry_store::BathymetryStore store =
          marine_bathymetry_store::BathymetryStore::fromCellSize(
          static_cast<float>(cell_size_));
        marine_bathymetry_store::load(store, draft_dir_);
        const auto & draft_epochs =
          store.epochs(marine_bathymetry_store::SourceLayer::Draft);
        if (!draft_epochs.empty()) {
          const auto & newest_epoch = draft_epochs.rbegin()->first;
          cube::loadEpochIntoSheet(
            store, marine_bathymetry_store::SourceLayer::Draft, newest_epoch,
            *geo_map_sheet_);
          RCLCPP_INFO(get_logger(),
            "Primed GeoMapSheet from draft epoch '%s' under %s",
            newest_epoch.c_str(), draft_dir_.c_str());
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
        "(saves run while ACTIVE; epoch = UTC date at each save)",
        draft_dir_.c_str(), save_interval_s_);
    }

    tf_buffer_ = std::make_unique<tf2_ros::Buffer>(get_clock());
    tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_, this);


    grid_publisher_ = create_publisher<grid_map_msgs::msg::GridMap>("grid", 10);

    ping_subscription_ = create_subscription<sensor_msgs::msg::PointCloud2>("soundings",
      rclcpp::SensorDataQoS(),
      std::bind(&CubeBathymetry::pingCallback, this, std::placeholders::_1));

    clear_grid_service_ = create_service<std_srvs::srv::Trigger>("clear_grid",
      std::bind(&CubeBathymetry::clearGridService, this,
        std::placeholders::_1, std::placeholders::_2));

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
  // Cached so clearGrid() can rebuild the sheet with the configured geometry.
  double cell_size_ = 1.0;
  int grid_cell_count_ = 25;
  rclcpp::Time last_grid_publish_time_;

  // Last good map<-earth transform, cached so a publish-time TF miss reuses the
  // last alignment rather than starving the collision-avoidance grid (#21).
  geometry_msgs::msg::TransformStamped last_publish_tf_;
  bool have_publish_tf_ = false;
  rclcpp_lifecycle::LifecyclePublisher<grid_map_msgs::msg::GridMap>::SharedPtr grid_publisher_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr ping_subscription_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr clear_grid_service_;

  // Draft-tile persistence (#21). draft_dir_ empty = disabled. The epoch label
  // (UTC date) is recomputed at each save in saveDirtyTiles() so a survey
  // crossing UTC midnight rolls into the next day's draft/<epoch>/ dir.
  std::string draft_dir_;
  double save_interval_s_ = 30.0;
  rclcpp::TimerBase::SharedPtr save_timer_;

  void publishGrid()
  {
    if(geo_map_sheet_->grids().empty()) {
      // No tiles populated yet (no soundings have been added). pingCallback
      // emits the actionable diagnostics for why soundings aren't arriving.
      return;
    }

    // Look up the map<-earth transform ONCE per publish; the projection helper
    // applies it as a single batched affine to every GGGS cell center (no
    // per-cell TF lookup). On a TF miss reuse the last good transform so a TF
    // outage degrades gracefully rather than starving the collision-avoidance
    // grid; skip publish only on the very first cycle, before any cache exists.
    geometry_msgs::msg::TransformStamped map_from_earth;
    if(lookupAtOrLatest(map_frame_, "earth", get_clock()->now(), map_from_earth)) {
      last_publish_tf_ = map_from_earth;
      have_publish_tf_ = true;
    } else {
      if(!have_publish_tf_) {
        RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
          "No transform " << map_frame_ << " <- earth at publish time and no "
          "cached transform yet; skipping this publish cycle");
        return;
      }
      map_from_earth = last_publish_tf_;
      RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
        "No transform " << map_frame_ << " <- earth at publish time; reusing the "
        "last good map<-earth transform (grid placement may be slightly stale)");
    }

    const Eigen::Isometry3d map_from_earth_eigen =
      tf2::transformToEigen(map_from_earth);

    grid_map::GridMap map = cube::geoMapSheetToGridMap(
      *geo_map_sheet_, map_frame_, cell_size_, map_from_earth_eigen);

    if(!map.exists("elevation")) {
      // No finite cells projected (e.g. every grid still queued in the
      // pre-filter). Nothing to publish this cycle.
      return;
    }

    auto epoch = std::chrono::time_point<std::chrono::steady_clock>{};
    map.setTimestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(
        (geo_map_sheet_->lastUpdateTime() - epoch)).count());

    size_t populated = 0;
    const grid_map::Matrix & elevation = map["elevation"];
    for(grid_map::GridMapIterator it(map); !it.isPastEnd(); ++it) {
      if(std::isfinite(elevation((*it)(0), (*it)(1)))) {
        ++populated;
      }
    }

    auto message = grid_map::GridMapRosConverter::toMessage(map);
    grid_publisher_->publish(*message);

    // Liveness heartbeat: confirms the grid is being emitted and how many cells
    // carry a depth estimate (a persistently-zero count means soundings arrive
    // but never resolve into the grid).
    RCLCPP_INFO_STREAM_THROTTLE(get_logger(), *get_clock(), 10000,
      "Published grid: " << populated << " populated cells over " <<
      geo_map_sheet_->grids().size() << " tiles");
  }

  // ISO-8601 UTC date (YYYY-MM-DD) -- the epoch label for this session's draft
  // tiles. Two sessions on the same UTC day share the epoch; the store's
  // LiveFused set() path accumulates correctly (newest value wins per cell).
  static std::string currentUtcDateString()
  {
    const std::time_t now = std::time(nullptr);
    std::tm tm_utc{};
    gmtime_r(&now, &tm_utc);
    char buf[16] = {0};
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_utc);
    return std::string(buf);
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
    // Recompute the UTC date STRING at each save so a survey crossing UTC
    // midnight writes into the correct day's draft/<epoch>/ dir (the label is
    // not pinned to on_configure time). Priming on load still reads the newest
    // persisted epoch, so a rollover during a session resumes seamlessly.
    const std::string epoch = currentUtcDateString();
    const std::string dir =
      draft_dir_ + "/" +
      marine_bathymetry_store::layerDirName(
      marine_bathymetry_store::SourceLayer::Draft) + "/" + epoch;

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
          // (same idiom as mapSheetToEpochTiles).
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

  // Replace the accumulated surface with a fresh, empty sheet of the same
  // configured geometry. Lets an operator reset the grid in place -- e.g. to
  // shed a surface that has grown too large for the telemetry downlink -- with
  // no process restart and no CONFIGURE/ACTIVATE cycle. Safe from a service
  // callback: main() runs a SingleThreadedExecutor, so this never overlaps
  // pingCallback's use of map_sheet_.
  void clearGrid()
  {
    // Flush any draft data accumulated since the last periodic save BEFORE
    // discarding the sheet, mirroring on_cleanup -- otherwise an operator reset
    // would silently drop unsaved-since-last-interval soundings. A no-op when
    // persistence is disabled or nothing is dirty.
    saveDirtyTiles();
    geo_map_sheet_ =
      std::make_shared<cube::GeoMapSheet>(static_cast<float>(cell_size_));
    // Force the next ping to republish immediately (a zero-nanosecond time is
    // the same first-publish trigger used at startup) so the cleared surface
    // propagates without waiting out the ~5 s publish interval.
    last_grid_publish_time_ = rclcpp::Time(0, 0, get_clock()->get_clock_type());
  }

  void clearGridService(
    const std::shared_ptr<std_srvs::srv::Trigger::Request>/*request*/,
    std::shared_ptr<std_srvs::srv::Trigger::Response> response)
  {
    clearGrid();
    response->success = true;
    response->message = "grid cleared";
    RCLCPP_INFO(get_logger(), "Grid cleared via clear_grid service");
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

    if(last_grid_publish_time_.nanoseconds() == 0 ||
      rclcpp::Time(msg->header.stamp) - last_grid_publish_time_ >
      rclcpp::Duration::from_seconds(5.0))
    {
      publishGrid();
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
