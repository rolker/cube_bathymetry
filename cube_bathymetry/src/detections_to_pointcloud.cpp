// Copyright 2025 Center for Coastal and Ocean Mapping and NOAA-UNH Joint Hydrographic Center, University of New Hampshire
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


#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "lifecycle_msgs/msg/state.hpp"

#include "cube_bathymetry/error_model.h"
#include "geometry_msgs/msg/twist_with_covariance_stamped.hpp"
#include "marine_acoustic_msgs/msg/sonar_detections.hpp"
#include "sensor_msgs/msg/imu.hpp"
#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "tf2/utils.hpp"

class DetectionsToPointCloud : public rclcpp_lifecycle::LifecycleNode
{
public:
  DetectionsToPointCloud()
  :rclcpp_lifecycle::LifecycleNode("detections_to_pointcloud")
  {

  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_configure(const rclcpp_lifecycle::State &state)
  {
    detections_subscriber_ = create_subscription<marine_acoustic_msgs::msg::SonarDetections>(
      "detections",
      rclcpp::SensorDataQoS(),
      std::bind(&DetectionsToPointCloud::detectionsCallback, this, std::placeholders::_1)
    );

    position_subscriber_ = create_subscription<sensor_msgs::msg::NavSatFix>(
      "position",
      rclcpp::SensorDataQoS(),
      std::bind(&DetectionsToPointCloud::positionCallback, this, std::placeholders::_1)
    );

    orientation_subscriber_ = create_subscription<sensor_msgs::msg::Imu>(
      "orientation",
      rclcpp::SensorDataQoS(),
      std::bind(&DetectionsToPointCloud::orientationCallback, this, std::placeholders::_1)
    );

    velocity_subscriber_ = create_subscription<geometry_msgs::msg::TwistWithCovarianceStamped>(
      "velocity",
      rclcpp::SensorDataQoS(),
      std::bind(&DetectionsToPointCloud::velocityCallback, this, std::placeholders::_1)
    );

    pointcloud_publisher_ = create_publisher<sensor_msgs::msg::PointCloud2>(
      "soundings",
      rclcpp::SensorDataQoS()
    );

    cube::Vessel vessel;
    cube::Device device;
    error_model_ = std::make_shared<cube::ErrorModel>(vessel, device);

    return rclcpp_lifecycle::LifecycleNode::on_configure(state);
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_activate(const rclcpp_lifecycle::State & state)
  {
    return LifecycleNode::on_activate(state);
  }

  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
  on_cleanup(const rclcpp_lifecycle::State &state)
  {
    return LifecycleNode::on_cleanup(state);
  }

private:
  void detectionsCallback(const marine_acoustic_msgs::msg::SonarDetections::UniquePtr& msg)
  {
    cube::Platform platform;
    platform.timestamp = rclcpp::Time(msg->header.stamp).seconds();
    if(position_)
    {
      platform.latitude = position_->latitude;
      platform.longitude = position_->longitude;
    }
    else
    {
      platform.latitude = std::nan("");
      platform.longitude = std::nan("");
    }
    if(orientation_)
    {
      double y,p,r;
      tf2::getEulerYPR(orientation_->orientation, y,p,r);
      platform.roll = r;
      platform.pitch = p;
      platform.heading = (M_PI/2.0)-y;
    }
    else
    {
      platform.roll = std::nan("");
      platform.pitch = std::nan("");
      platform.heading = std::nan("");
    }
    if(velocity_)
    {
      platform.vessel_speed = velocity_->twist.twist.linear.x;
    }
    else
    {
      platform.vessel_speed = std::nan("");
    }

    platform.mean_speed = msg->ping_info.sound_speed;
    platform.surf_sspeed = msg->ping_info.sound_speed;

    auto soundings =error_model_->compute(*msg, platform);

    sensor_msgs::msg::PointCloud2 pointcloud;
    pointcloud.header = msg->header;

    pointcloud.height = 1;
    pointcloud.width = soundings.size();
    pointcloud.point_step = 20; // 5 fields * 4 bytes each
    pointcloud.row_step = pointcloud.point_step * pointcloud.width;
    pointcloud.is_dense = true;
    pointcloud.is_bigendian = false;

    pointcloud.fields.resize(5);
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
    pointcloud.fields[3].name = "vertical_uncertainty";
    pointcloud.fields[3].offset = 12;
    pointcloud.fields[3].datatype = sensor_msgs::msg::PointField::FLOAT32;
    pointcloud.fields[3].count = 1;
    pointcloud.fields[4].name = "horizontal_uncertainty";
    pointcloud.fields[4].offset = 16;
    pointcloud.fields[4].datatype = sensor_msgs::msg::PointField::FLOAT32;
    pointcloud.fields[4].count = 1;

    pointcloud.data.resize(pointcloud.row_step * pointcloud.height);
    float* data_ptr = reinterpret_cast<float*>(pointcloud.data.data());

    for(const auto& sounding : soundings)
    {
      data_ptr[0] = sounding.sonar_relative_position.x;
      data_ptr[1] = sounding.sonar_relative_position.y;
      data_ptr[2] = sounding.sonar_relative_position.z;
      data_ptr[3] = sounding.vertical_error;
      data_ptr[4] = sounding.horizontal_error;
      data_ptr += 5; // Move to the next point
    }

    pointcloud_publisher_->publish(pointcloud);
  }

  void positionCallback(sensor_msgs::msg::NavSatFix::SharedPtr msg)
  {
    position_ = msg;
  }

  void orientationCallback(sensor_msgs::msg::Imu::SharedPtr msg)
  {
    orientation_ = msg;
  }

  void velocityCallback(geometry_msgs::msg::TwistWithCovarianceStamped::SharedPtr msg)
  {
    velocity_ = msg;
  }

  rclcpp::Subscription<marine_acoustic_msgs::msg::SonarDetections>::SharedPtr detections_subscriber_;
  rclcpp::Subscription<geometry_msgs::msg::TwistWithCovarianceStamped>::SharedPtr velocity_subscriber_;
  rclcpp::Subscription<sensor_msgs::msg::NavSatFix>::SharedPtr position_subscriber_;
  rclcpp::Subscription<sensor_msgs::msg::Imu>::SharedPtr orientation_subscriber_;

  rclcpp_lifecycle::LifecyclePublisher<sensor_msgs::msg::PointCloud2>::SharedPtr pointcloud_publisher_;

  std::shared_ptr<cube::ErrorModel> error_model_;
  sensor_msgs::msg::NavSatFix::SharedPtr position_;
  sensor_msgs::msg::Imu::SharedPtr orientation_;
  geometry_msgs::msg::TwistWithCovarianceStamped::SharedPtr velocity_;


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
