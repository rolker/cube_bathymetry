#include "rclcpp/rclcpp.hpp"
#include "rclcpp_lifecycle/lifecycle_node.hpp"
#include "lifecycle_msgs/msg/state.hpp"

#include "sensor_msgs/msg/point_cloud2.hpp"
#include "cube_bathymetry/map_sheet.h"
#include <tf2_ros/transform_listener.h>
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
  on_configure(const rclcpp_lifecycle::State &state)
  {
    map_frame_ = this->declare_parameter("map_frame", "map");

    declare_parameter("cell_size", 1.0);
    double cell_size = get_parameter("cell_size").as_double();

    map_sheet_ = std::make_shared<cube::MapSheet>(cube::CellCounts(5), cube::CellSizes(cell_size));

    tf_buffer_ = std::make_unique<tf2_ros::Buffer>(get_clock());
    tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_, this);


    grid_publisher_ = create_publisher<grid_map_msgs::msg::GridMap>("grid", 10);

    ping_subscription_ = create_subscription<sensor_msgs::msg::PointCloud2>("soundings", 10, std::bind(&CubeBathymetry::pingCallback, this, std::placeholders::_1));

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
    if(isnan(bounds.maximum.x))
      return;
    auto cell_counts = map_sheet_->totalCellCounts();
    auto cell_sizes = map_sheet_->cellSizes();

    auto width = bounds.maximum.x - bounds.minimum.x;
    auto height = bounds.maximum.y - bounds.minimum.y;

    map.setGeometry(grid_map::Length(width, height), cell_sizes.x);

    grid_map::Position center(bounds.minimum.x+width/2.0, bounds.minimum.y+height/2.0);

    map.setPosition(center);
    map.setFrameId(map_frame_);

    auto epoch = std::chrono::time_point<std::chrono::steady_clock>{};
    map.setTimestamp(std::chrono::duration_cast<std::chrono::nanoseconds>((map_sheet_->lastUpdateTime()-epoch)).count());

    map.add("elevation");
    map.add("uncertainty");

    for(auto grid: map_sheet_->grids())
    {
      auto origin = grid->origin();
      auto counts = grid->cellCounts();
      auto sizes = grid->cellSizes();
      auto values = grid->values();

      for(int j = 0; j < counts.y; j++)
        for(int i = 0; i < counts.x; i++)
        {
          auto grid_index = j*counts.x + i;
          auto depth_uncertainty = values[grid_index];
          if(!std::isnan(depth_uncertainty.depth))
          {
            grid_map::Position p(origin.x + i*sizes.x, origin.y + j*sizes.y);
            grid_map::Index index;
              if(map.getIndex(p,index))
              {
                map.at("elevation", index) = depth_uncertainty.depth;
                map.at("uncertainty", index) = depth_uncertainty.uncertainty;
              }
          }
        }
    }
    auto message = grid_map::GridMapRosConverter::toMessage(map);
    grid_publisher_->publish(*message);
  }

  void pingCallback(const sensor_msgs::msg::PointCloud2::UniquePtr& msg)
  {
    if (get_current_state().id() != lifecycle_msgs::msg::State::PRIMARY_STATE_ACTIVE)
      return;

    try
    {
      auto transform = tf_buffer_->lookupTransform(map_frame_, msg->header.frame_id, msg->header.stamp, tf2::durationFromSec(2.0));
      sensor_msgs::msg::PointCloud2 soundings_in_map_frame;
      tf2::doTransform(*msg, soundings_in_map_frame, transform);

      std::vector<cube::MapSounding> soundings;

      sensor_msgs::PointCloud2ConstIterator<float> iter_original_z(*msg, "z");


      sensor_msgs::PointCloud2ConstIterator<float> iter_x(soundings_in_map_frame, "x");
      sensor_msgs::PointCloud2ConstIterator<float> iter_y(soundings_in_map_frame, "y");
      sensor_msgs::PointCloud2ConstIterator<float> iter_z(soundings_in_map_frame, "z");
      sensor_msgs::PointCloud2ConstIterator<float> iter_vertical_uncertainty(soundings_in_map_frame, "vertical_uncertainty");
      sensor_msgs::PointCloud2ConstIterator<float> iter_horizontal_uncertainty(soundings_in_map_frame, "horizontal_uncertainty");

      auto epoch = std::chrono::time_point<std::chrono::steady_clock>{};

      auto timestamp = epoch + std::chrono::seconds(msg->header.stamp.sec) + std::chrono::nanoseconds(msg->header.stamp.nanosec);

      for (; (iter_x != iter_x.end()) &&
            (iter_y != iter_y.end()) &&
            (iter_z != iter_z.end()) &&
            (iter_vertical_uncertainty != iter_vertical_uncertainty.end()) &&
            (iter_horizontal_uncertainty != iter_horizontal_uncertainty.end());
            ++iter_x, ++iter_y, ++iter_z,
            ++iter_vertical_uncertainty, ++iter_horizontal_uncertainty)
            {
              cube::MapSounding s(*iter_x, *iter_y, *iter_z);
              s.sounding.vertical_error =  *iter_vertical_uncertainty;
              s.sounding.horizontal_error = *iter_horizontal_uncertainty;
              soundings.push_back(s);
            }

            map_sheet_->addSoundings(soundings, timestamp);

      if(last_grid_publish_time_.nanoseconds() == 0 || rclcpp::Time(msg->header.stamp) - last_grid_publish_time_ > rclcpp::Duration::from_seconds(5.0))
      {
        publishGrid();
        last_grid_publish_time_ = msg->header.stamp;
      }


    }
    catch (const tf2::TransformException& e)
    {
      RCLCPP_WARN_STREAM(get_logger(), "tf2 exception: " << e.what());
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
