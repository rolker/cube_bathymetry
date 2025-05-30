#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "lifecycle_msgs/msg/state.hpp"

#include "cube_bathymetry/error_model.h"
#include "geometry_msgs/msg/twist_with_covariance_stamped.hpp"
#include "marine_acoustic_msgs/msg/sonar_detections.hpp"
#include "sensor_msgs/msg/imu.hpp"
#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"

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
