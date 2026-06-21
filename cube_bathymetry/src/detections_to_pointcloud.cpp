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
#include <mutex>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "lifecycle_msgs/msg/state.hpp"

#include "cube_bathymetry/detections_projector.h"
#include "marine_acoustic_msgs/msg/sonar_detections.hpp"
#include "nav_msgs/msg/odometry.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "tf2_ros/buffer.h"
#include "tf2_ros/transform_listener.h"

class DetectionsToPointCloud : public rclcpp_lifecycle::LifecycleNode
{
public:
  DetectionsToPointCloud()
  :rclcpp_lifecycle::LifecycleNode("detections_to_pointcloud")
  {
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_configure(const rclcpp_lifecycle::State & state)
  {
    if(!has_parameter("minimum_range")) {
      declare_parameter("minimum_range", minimum_range_);
    }
    minimum_range_ = get_parameter("minimum_range").as_double();

    if(!has_parameter("maximum_range")) {
      declare_parameter("maximum_range", maximum_range_);
    }
    maximum_range_ = get_parameter("maximum_range").as_double();

    // TF frame names (platform-specific; set via params/yaml). Defaults are the
    // unprefixed mru_transform names; e.g. on BizzyBoat set to bizzy/base_link etc.
    if(!has_parameter("base_link_frame")) {
      declare_parameter("base_link_frame", base_link_frame_);
    }
    base_link_frame_ = get_parameter("base_link_frame").as_string();
    if(!has_parameter("level_frame")) {
      declare_parameter("level_frame", level_frame_);
    }
    level_frame_ = get_parameter("level_frame").as_string();
    if(!has_parameter("tide_frame")) {
      declare_parameter("tide_frame", tide_frame_);
    }
    tide_frame_ = get_parameter("tide_frame").as_string();

    // Error-model tuning (platform-specific; set via params/yaml).
    // ellipsoidal_referenced=true (default) keeps the grid ellipsoid-referenced
    // and omits tide terms; set false for tidal-datum mode. range_error_percent
    // / range_error_floor_m parameterize the sonar's range measurement error.
    if(!has_parameter("ellipsoidal_referenced")) {
      declare_parameter("ellipsoidal_referenced", ellipsoidal_referenced_);
    }
    ellipsoidal_referenced_ = get_parameter("ellipsoidal_referenced").as_bool();
    if(!has_parameter("range_error_percent")) {
      declare_parameter("range_error_percent", range_error_percent_);
    }
    range_error_percent_ = get_parameter("range_error_percent").as_double();
    if(!has_parameter("range_error_floor_m")) {
      declare_parameter("range_error_floor_m", range_error_floor_m_);
    }
    range_error_floor_m_ = get_parameter("range_error_floor_m").as_double();

    // Guard against typo'd configs: a negative percent or floor would feed a
    // bogus (still-positive, since squared) range variance silently. Clamp to
    // non-negative and warn rather than propagate a nonsense uncertainty.
    if(range_error_percent_ < 0.0) {
      RCLCPP_WARN(
        get_logger(),
        "range_error_percent (%g) is negative; clamping to 0.0",
        range_error_percent_);
      range_error_percent_ = 0.0;
    }
    if(range_error_floor_m_ < 0.0) {
      RCLCPP_WARN(
        get_logger(),
        "range_error_floor_m (%g) is negative; clamping to 0.0",
        range_error_floor_m_);
      range_error_floor_m_ = 0.0;
    }

    detections_subscriber_ = create_subscription<marine_acoustic_msgs::msg::SonarDetections>(
      "detections",
      rclcpp::SensorDataQoS(),
      std::bind(&DetectionsToPointCloud::detectionsCallback, this, std::placeholders::_1)
    );

    tf_buffer_ = std::make_shared<tf2_ros::Buffer>(get_clock());
    tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

    odom_subscriber_ = create_subscription<nav_msgs::msg::Odometry>(
      "odom",
      rclcpp::SensorDataQoS(),
      std::bind(&DetectionsToPointCloud::odomCallback, this, std::placeholders::_1)
    );

    pointcloud_publisher_ = create_publisher<sensor_msgs::msg::PointCloud2>(
      "soundings",
      rclcpp::SensorDataQoS()
    );

    cube::ProjectorParams params;
    params.base_link_frame = base_link_frame_;
    params.level_frame = level_frame_;
    params.tide_frame = tide_frame_;
    params.minimum_range = minimum_range_;
    params.maximum_range = maximum_range_;
    params.vessel.ellipsoidal_referenced = ellipsoidal_referenced_;
    params.device.range_error_percent = range_error_percent_;
    params.device.range_error_floor_m = range_error_floor_m_;
    projector_ = std::make_shared<cube::DetectionsProjector>(params);

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
  bool notTooOld(const rclcpp::Time & msg_time, const rclcpp::Time & current_time)
  {
    if(msg_time.nanoseconds() == 0) {
      return false;
    }
    return (current_time - msg_time).seconds() < 1.0;
  }

  void odomCallback(const nav_msgs::msg::Odometry::SharedPtr msg)
  {
    const auto & v = msg->twist.twist.linear;  // body-frame velocity
    std::lock_guard<std::mutex> lock(odom_mutex_);
    last_vessel_speed_ = static_cast<float>(std::hypot(v.x, v.y));  // SOG
    last_odom_stamp_ = msg->header.stamp;
  }

  void detectionsCallback(const marine_acoustic_msgs::msg::SonarDetections::UniquePtr & msg)
  {
    const rclcpp::Time stamp(msg->header.stamp);

    // Speed over ground from odometry (latest cached value). The staleness gate
    // stays in the node: when odom is stale/absent, pass NaN so the error model
    // produces NaN uncertainty exactly as before.
    float vessel_speed;
    {
      std::lock_guard<std::mutex> lock(odom_mutex_);
      if(notTooOld(last_odom_stamp_, stamp)) {
        vessel_speed = last_vessel_speed_;
      } else {
        RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 10000,
          "No recent odometry; vessel_speed = NaN");
        vessel_speed = std::nan("");
      }
    }

    // All projection math (beam geometry, attitude/heave TF reads, TPU, range
    // gate) lives in the node-free DetectionsProjector. The node only supplies
    // SOG, packs the cloud, and logs from the returned diagnostics.
    auto projection = projector_->project(*msg, *tf_buffer_, vessel_speed);
    const auto & soundings = projection.soundings;

    if(projection.diagnostics.missing_attitude > 0) {
      RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 10000,
        "No attitude TF (" << level_frame_ << " <- " << base_link_frame_ <<
        "); roll/pitch = NaN");
    }
    if(projection.diagnostics.filtered_range > 0) {
      RCLCPP_DEBUG_STREAM_THROTTLE(get_logger(), *get_clock(), 10000,
        projection.diagnostics.filtered_range << " of " <<
        projection.diagnostics.total << " soundings filtered by range");
    }

    sensor_msgs::msg::PointCloud2 pointcloud;
    pointcloud.header = msg->header;

    pointcloud.height = 1;
    pointcloud.width = soundings.size();
    pointcloud.point_step = 24;  // 6 fields * 4 bytes each
    pointcloud.row_step = pointcloud.point_step * pointcloud.width;
    pointcloud.is_dense = true;
    pointcloud.is_bigendian = false;

    // Field order: x, y, z, intensity, vertical_uncertainty, horizontal_uncertainty.
    // intensity (per-beam backscatter) sits with the geometry, ahead of the
    // uncertainty fields. Consumers read by field name, so the order is safe.
    pointcloud.fields.resize(6);
    pointcloud.fields[0].name = "x";
    pointcloud.fields[0].offset = 0;
    pointcloud.fields[0].datatype = sensor_msgs::msg::PointField::FLOAT32;
    pointcloud.fields[0].count = 1;
    pointcloud.fields[1].name = "y";
    pointcloud.fields[1].offset = 4;
    pointcloud.fields[1].datatype = sensor_msgs::msg::PointField::FLOAT32;
    pointcloud.fields[1].count = 1;
    pointcloud.fields[2].name = "z";
    pointcloud.fields[2].offset = 8;
    pointcloud.fields[2].datatype = sensor_msgs::msg::PointField::FLOAT32;
    pointcloud.fields[2].count = 1;
    pointcloud.fields[3].name = "intensity";
    pointcloud.fields[3].offset = 12;
    pointcloud.fields[3].datatype = sensor_msgs::msg::PointField::FLOAT32;
    pointcloud.fields[3].count = 1;
    pointcloud.fields[4].name = "vertical_uncertainty";
    pointcloud.fields[4].offset = 16;
    pointcloud.fields[4].datatype = sensor_msgs::msg::PointField::FLOAT32;
    pointcloud.fields[4].count = 1;
    pointcloud.fields[5].name = "horizontal_uncertainty";
    pointcloud.fields[5].offset = 20;
    pointcloud.fields[5].datatype = sensor_msgs::msg::PointField::FLOAT32;
    pointcloud.fields[5].count = 1;

    pointcloud.data.resize(pointcloud.row_step * pointcloud.height);
    float * data_ptr = reinterpret_cast<float *>(pointcloud.data.data());

    for (const auto & sounding   :  soundings) {
      data_ptr[0] = sounding.sonar_relative_position.x;
      data_ptr[1] = sounding.sonar_relative_position.y;
      data_ptr[2] = sounding.sonar_relative_position.z;
      data_ptr[3] = sounding.intensity;
      data_ptr[4] = sounding.vertical_error;
      data_ptr[5] = sounding.horizontal_error;
      data_ptr += 6;  // Move to the next point
    }

    pointcloud_publisher_->publish(pointcloud);
  }


  rclcpp::Subscription<marine_acoustic_msgs::msg::SonarDetections>::SharedPtr
    detections_subscriber_;

  std::shared_ptr<tf2_ros::Buffer> tf_buffer_;
  std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
  rclcpp::Subscription<nav_msgs::msg::Odometry>::SharedPtr odom_subscriber_;

  rclcpp_lifecycle::LifecyclePublisher<sensor_msgs::msg::PointCloud2>::SharedPtr
    pointcloud_publisher_;

  std::shared_ptr<cube::DetectionsProjector> projector_;

  std::string base_link_frame_ = "base_link";
  std::string level_frame_ = "base_link_north_up";  // level, north-aligned
  std::string tide_frame_ = "map_tide";

  bool ellipsoidal_referenced_ = true;
  double range_error_percent_ = 0.005;  // fraction of depth (0.5%)
  double range_error_floor_m_ = 0.05;   // absolute floor, m

  std::mutex odom_mutex_;
  float last_vessel_speed_ = std::nanf("");
  rclcpp::Time last_odom_stamp_{0, 0, RCL_ROS_TIME};

  double minimum_range_ = 0.0;  // meters
  double maximum_range_ = 12000.0;  // meters
};

int main(int argc, char **argv)
{
  rclcpp::init(argc, argv);
  auto detections_to_pointcloud = std::make_shared<DetectionsToPointCloud>();

  rclcpp::executors::SingleThreadedExecutor exe;
  exe.add_node(detections_to_pointcloud->get_node_base_interface());
  exe.spin();


  rclcpp::shutdown();
  return 0;
}
