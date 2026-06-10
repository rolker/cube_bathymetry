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


#include <cmath>
#include <string>
#include <vector>

#include "tf2_ros/transform_listener.h"
#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "lifecycle_msgs/msg/state.hpp"

#include "sensor_msgs/msg/point_cloud2.hpp"
#include "cube_bathymetry/map_sheet.h"
#include "geometry_msgs/msg/transform_stamped.hpp"
#include "tf2_ros/message_filter.h"
#include "message_filters/subscriber.h"
#include "sensor_msgs/point_cloud2_iterator.hpp"
#include "tf2_sensor_msgs/tf2_sensor_msgs.hpp"
#include "grid_map_ros/grid_map_ros.hpp"
#include "grid_map_msgs/msg/grid_map.hpp"


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
    double cell_size = get_parameter("cell_size").as_double();

    declare_parameter("grid_cell_count", 25);
    int grid_cell_count = get_parameter("grid_cell_count").as_int();

    map_sheet_ = std::make_shared<cube::MapSheet>(cube::CellCounts(grid_cell_count),
      cube::CellSizes(cell_size));

    tf_buffer_ = std::make_unique<tf2_ros::Buffer>(get_clock());
    tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_, this);


    grid_publisher_ = create_publisher<grid_map_msgs::msg::GridMap>("grid", 10);

    ping_subscription_ = create_subscription<sensor_msgs::msg::PointCloud2>("soundings",
      rclcpp::SensorDataQoS(),
      std::bind(&CubeBathymetry::pingCallback, this, std::placeholders::_1));

    return rclcpp_lifecycle::LifecycleNode::on_configure(state);
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_activate(const rclcpp_lifecycle::State & state)
  {
    return LifecycleNode::on_activate(state);
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_cleanup(const rclcpp_lifecycle::State & state)
  {
    return LifecycleNode::on_cleanup(state);
  }

private:
  std::shared_ptr<cube::MapSheet> map_sheet_;

  std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;

  std::string map_frame_ = "map";
  rclcpp::Time last_grid_publish_time_;
  rclcpp_lifecycle::LifecyclePublisher<grid_map_msgs::msg::GridMap>::SharedPtr grid_publisher_;
  rclcpp::Subscription<sensor_msgs::msg::PointCloud2>::SharedPtr ping_subscription_;

  void publishGrid()
  {
    grid_map::GridMap map;

    auto bounds = map_sheet_->gridBounds();
    if(isnan(bounds.maximum.x)) {
      // No tiles populated yet (no soundings have been added). pingCallback
      // emits the actionable diagnostics for why soundings aren't arriving.
      return;
    }
    auto cell_counts = map_sheet_->totalCellCounts();
    auto cell_sizes = map_sheet_->cellSizes();

    auto width = bounds.maximum.x - bounds.minimum.x;
    auto height = bounds.maximum.y - bounds.minimum.y;

    map.setGeometry(grid_map::Length(width, height), cell_sizes.x);

    grid_map::Position center(bounds.minimum.x + width / 2.0, bounds.minimum.y + height / 2.0);

    map.setPosition(center);
    map.setFrameId(map_frame_);

    auto epoch = std::chrono::time_point<std::chrono::steady_clock>{};
    map.setTimestamp(std::chrono::duration_cast<std::chrono::nanoseconds>(
        (map_sheet_->lastUpdateTime() - epoch)).count());

    map.add("elevation");
    map.add("uncertainty");

    size_t populated = 0;
    for (auto grid  :  map_sheet_->grids()) {
      auto origin = grid->origin();
      auto counts = grid->cellCounts();
      auto sizes = grid->cellSizes();
      auto values = grid->values();

      for(int j = 0; j < counts.y; j++) {
        for(int i = 0; i < counts.x; i++) {
          auto grid_index = j * counts.x + i;
          auto depth_uncertainty = values[grid_index];
          if(!std::isnan(depth_uncertainty.depth)) {
            grid_map::Position p(origin.x + i * sizes.x, origin.y + j * sizes.y);
            grid_map::Index index;
            if(map.getIndex(p, index)) {
              map.at("elevation", index) = depth_uncertainty.depth;
              map.at("uncertainty", index) = depth_uncertainty.uncertainty;
              ++populated;
            }
          }
        }
      }
    }
    auto message = grid_map::GridMapRosConverter::toMessage(map);
    grid_publisher_->publish(*message);

    // Liveness heartbeat: confirms the grid is being emitted and how many cells
    // carry a depth estimate (a persistently-zero count means soundings arrive
    // but never resolve into the grid).
    RCLCPP_INFO_STREAM_THROTTLE(get_logger(), *get_clock(), 10000,
      "Published grid: " << populated << " populated cells over " <<
      map_sheet_->grids().size() << " tiles");
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

    geometry_msgs::msg::TransformStamped transform;
    if(!lookupAtOrLatest(map_frame_, msg->header.frame_id,
        rclcpp::Time(msg->header.stamp), transform))
    {
      RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 5000,
        "No transform " << map_frame_ << " <- " << msg->header.frame_id <<
        " (at ping time or latest); dropping ping -- grid will not update");
      return;
    }

    sensor_msgs::msg::PointCloud2 soundings_in_map_frame;
    tf2::doTransform(*msg, soundings_in_map_frame, transform);

    // PointCloud2 point count is width * height (height > 1 for organized clouds).
    const size_t point_count = static_cast<size_t>(msg->width) * msg->height;
    std::vector<cube::MapSounding> soundings;
    soundings.reserve(point_count);

    try {
      sensor_msgs::PointCloud2ConstIterator<float> iter_x(soundings_in_map_frame, "x");
      sensor_msgs::PointCloud2ConstIterator<float> iter_y(soundings_in_map_frame, "y");
      sensor_msgs::PointCloud2ConstIterator<float> iter_z(soundings_in_map_frame, "z");
      sensor_msgs::PointCloud2ConstIterator<float> iter_vertical_uncertainty(soundings_in_map_frame,
        "vertical_uncertainty");
      sensor_msgs::PointCloud2ConstIterator<float> iter_horizontal_uncertainty(
        soundings_in_map_frame, "horizontal_uncertainty");

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

        cube::MapSounding s(x, y, z);
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

    map_sheet_->addSoundings(soundings, timestamp);

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
