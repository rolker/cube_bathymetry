#include <ros/ros.h>

#include <sensor_msgs/PointCloud2.h>
#include <cube_bathymetry/map_sheet.h>
#include <tf2_ros/transform_listener.h>
#include <geometry_msgs/TransformStamped.h>
#include "tf2_ros/message_filter.h"
#include "message_filters/subscriber.h"
#include <sensor_msgs/point_cloud2_iterator.h>
#include <tf2_sensor_msgs/tf2_sensor_msgs.h>
#include <grid_map_ros/grid_map_ros.hpp>
#include <grid_map_msgs/GridMap.h>

std::shared_ptr<cube::MapSheet> map_sheet;
std::shared_ptr<tf2_ros::Buffer> tfBuffer;
std::string map_frame = "map";
ros::Time last_grid_publish_time;
ros::Publisher grid_publisher;

void publishGrid()
{
  grid_map::GridMap map;

  auto bounds = map_sheet->gridBounds();
  auto cell_counts = map_sheet->totalCellCounts();
  auto cell_sizes = map_sheet->cellSizes();

  auto width = bounds.maximum.x - bounds.minimum.x;
  auto height = bounds.maximum.y - bounds.minimum.y;

  map.setGeometry(grid_map::Length(width, height), cell_sizes.x);

  grid_map::Position center(bounds.minimum.x+width/2.0, bounds.minimum.y+height/2.0);

  map.setPosition(center);
  map.setFrameId(map_frame);

  auto epoch = std::chrono::time_point<std::chrono::steady_clock>{};
  map.setTimestamp(std::chrono::duration_cast<std::chrono::nanoseconds>((map_sheet->lastUpdateTime()-epoch)).count());

  map.add("elevation");
  map.add("uncertainty");

  for(auto grid: map_sheet->grids())
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
              map.at("elevation", index) = -depth_uncertainty.depth;
              map.at("uncertainty", index) = depth_uncertainty.uncertainty;
            }
        }

      }

  }
  grid_map_msgs::GridMap message;
  grid_map::GridMapRosConverter::toMessage(map, message);
  grid_publisher.publish(message);
}

void pingCallback(const sensor_msgs::PointCloud2::ConstPtr msg)
{
  ROS_INFO_STREAM(msg->header);
  try
  {
    auto transform = tfBuffer->lookupTransform(map_frame, msg->header.frame_id, msg->header.stamp, ros::Duration(1.0));
    sensor_msgs::PointCloud2 soundings_in_map_frame;
    tf2::doTransform(*msg, soundings_in_map_frame, transform);

    std::vector<cube::Sounding> soundings;

    sensor_msgs::PointCloud2ConstIterator<float> iter_original_z(*msg, "z");


    sensor_msgs::PointCloud2ConstIterator<float> iter_x(soundings_in_map_frame, "x");
    sensor_msgs::PointCloud2ConstIterator<float> iter_y(soundings_in_map_frame, "y");
    sensor_msgs::PointCloud2ConstIterator<float> iter_z(soundings_in_map_frame, "z");
    sensor_msgs::PointCloud2ConstIterator<float> iter_vertical_uncertainty(soundings_in_map_frame, "vertical_uncertainty");
    sensor_msgs::PointCloud2ConstIterator<float> iter_horizontal_uncertainty(soundings_in_map_frame, "horizontal_uncertainty");

    auto epoch = std::chrono::time_point<std::chrono::steady_clock>{};

    auto timestamp = epoch + std::chrono::seconds(msg->header.stamp.sec) + std::chrono::nanoseconds(msg->header.stamp.nsec);

    for (; (iter_x != iter_x.end()) &&
           (iter_y != iter_y.end()) &&
           (iter_z != iter_z.end()) &&
           (iter_vertical_uncertainty != iter_vertical_uncertainty.end()) &&
           (iter_horizontal_uncertainty != iter_horizontal_uncertainty.end());
           ++iter_x, ++iter_y, ++iter_z,
           ++iter_vertical_uncertainty, ++iter_horizontal_uncertainty)
          {
            cube::Sounding s;
            s.x = *iter_x;
            s.y = *iter_y;
            s.depth = -*iter_z;
            s.vertical_error =  *iter_vertical_uncertainty;
            s.horizontal_error = *iter_horizontal_uncertainty;
            soundings.push_back(s);
          }

          map_sheet->addSoundings(soundings, timestamp);
    
    if(last_grid_publish_time.isZero() || msg->header.stamp - last_grid_publish_time > ros::Duration(5.0))
    {
      publishGrid();
      last_grid_publish_time = msg->header.stamp;
    }


  }
  catch (const tf2::TransformException& e)
  {
    ROS_WARN_STREAM("tf2 exception: " << e.what());
  }


}

int main(int argc, char **argv)
{
  ros::init(argc, argv, "cube_bathymetry");

  ros::NodeHandle nh;

  map_frame = ros::NodeHandle("~").param("map_frame", map_frame);

  map_sheet = std::make_shared<cube::MapSheet>(cube::CellCounts(50), cube::CellSizes(5.0));

  tfBuffer = std::make_shared<tf2_ros::Buffer>();
  tf2_ros::TransformListener tfListener(*tfBuffer);

  grid_publisher = nh.advertise<grid_map_msgs::GridMap>("grid", 10);

  auto ping_sub = nh.subscribe("soundings", 10, &pingCallback);
  // message_filters::Subscriber<sensor_msgs::PointCloud2> ping_sub;
  // ping_sub.subscribe(nh, "soundings", 10);
  // tf2_ros::MessageFilter<sensor_msgs::PointCloud2> tf2_filter(ping_sub, *tfBuffer, map_frame, 10, 0);
  // tf2_filter.registerCallback(&pingCallback);

  ros::spin();

  return 0;
}
