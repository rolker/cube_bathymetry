#include <rosbag/bag.h>
#include <rosbag/view.h>
#include <tf2_ros/buffer.h>
#include <tf2_msgs/TFMessage.h>
#include <sensor_msgs/PointCloud2.h>
#include <sensor_msgs/point_cloud2_iterator.h>
#include <tf2_sensor_msgs/tf2_sensor_msgs.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <sensor_msgs/NavSatFix.h>
#include <cube_bathymetry/map_sheet.h>
#include <geometry_msgs/PointStamped.h>

#include "gdal_priv.h"
//#include <errno.h>

void usage()
{ 
  std::cout << "usage: bag_to_geotiff [options and input files]\n";
  std::cout << "  -m map: Map frame\n";
  std::cout << "  -n /fix: NavSatFix topic, optionally used to assess GPS uncertainty\n";
  std::cout << "  -o output.tiff: Output file name\n";
  std::cout << "  -t /soundings: Topic containing soundings as sensor_msgs/PointCloud2 messages\n";
  exit(-1);
}

int main(int argc, char *argv[])
{
  std::vector<std::string> arguments(argv+1,argv+argc);

  if (arguments.empty())
    usage();

  std::vector<std::string> bagfile_names;
  std::string bathymetry_topic = "/soundings";
  std::string map_frame = "map";
  std::string output_filename;
  std::string nav_topic;

  int ping_count_limit = 0;

  for (auto arg = arguments.begin(); arg != arguments.end();arg++) 
  {
    if (*arg == "-h")
    {
      usage();
    }
    else if (*arg == "-m")
    {
      arg++;
      map_frame = *arg;
    }
    else if (*arg == "-n")
    {
      arg++;
      nav_topic = *arg;
    }
    else if (*arg == "-o")
    {
      arg++;
      output_filename = *arg;
    }
    else if (*arg == "-t")
    {
      arg++;
      bathymetry_topic = *arg;
    }
    else if (*arg == "-l")
    {
      arg++;
      ping_count_limit = std::stoi(*arg);
    }
    else
    {
      bagfile_names.push_back(*arg);
    }
  }

  rosbag::View view(true);

  // keep bags around since call to view takes a pointer to bags
  std::vector<std::shared_ptr<rosbag::Bag> > bags;
  for(auto bag: bagfile_names)
  {
    bags.push_back(std::make_shared<rosbag::Bag>(bag));
    view.addQuery(*bags.back());
  }

  std::cout << "calculating total time..." << std::endl;
  auto begin_time = view.getBeginTime();
  auto total_duration = view.getEndTime() - begin_time;
  std::cout << "total time: " << total_duration.toSec() << " seconds" << std::endl;

  tf2_ros::Buffer tfBuffer(total_duration);
  
  sensor_msgs::NavSatFix last_nav;

  cube::MapSheet map_sheet(cube::CellCounts(100), cube::CellSizes(0.1));

  std::list<std::pair<sensor_msgs::PointCloud2::ConstPtr, sensor_msgs::NavSatFix> > soundings_buffer;

  std::cout << "reading messages..." << std::endl;

  geometry_msgs::TransformStamped map_to_earth;

  int ping_count = 0;

  for(const auto m: view)
  {
    bool check_buffer = false; // did we get a sounding or updated tf message?

    if (m.getDataType() == "tf2_msgs/TFMessage")
    {
      if(m.getTopic() == "/tf_static")
      {
        tf2_msgs::TFMessage::ConstPtr msg = m.instantiate<tf2_msgs::TFMessage>();
        for(const auto &t :msg->transforms)
        {
          tfBuffer.setTransform(t, m.getCallerId(), true);
        }
      }
      if(m.getTopic() == "/tf")
      {
        tf2_msgs::TFMessage::ConstPtr msg = m.instantiate<tf2_msgs::TFMessage>();
        for(const auto &t :msg->transforms)
        {
          tfBuffer.setTransform(t, m.getCallerId(), false);
          if(t.header.frame_id == "earth")
          {
            if(t.transform.translation.x != map_to_earth.transform.translation.x || t.transform.translation.y != map_to_earth.transform.translation.y || t.transform.translation.z != map_to_earth.transform.translation.z)
            {
              std::cout << "\nnew map to earth transform:\n" << t << std::endl;
              map_to_earth = t;
            }
          }
        }
        if(!msg->transforms.empty())
        {
          double progress = (msg->transforms.front().header.stamp - begin_time).toSec()/total_duration.toSec();
          std::cout << "\r" << int(100*progress) << "%";
          std::cout.flush();
        }
        check_buffer = true;
      }
    }

    if (!nav_topic.empty() && m.getTopic() == nav_topic && m.getDataType() == "sensor_msgs/NavSatFix")
    {
      last_nav = *m.instantiate<sensor_msgs::NavSatFix>();
    }

    if (m.getDataType() == "sensor_msgs/PointCloud2")
    {
      if(bathymetry_topic == "" || m.getTopic() == bathymetry_topic)
      {
        sensor_msgs::PointCloud2::ConstPtr msg = m.instantiate<sensor_msgs::PointCloud2>();
        soundings_buffer.push_back(std::make_pair(msg, last_nav));

        double progress = (msg->header.stamp - begin_time).toSec()/total_duration.toSec();
        std::cout << "\r" << int(100*progress) << "%\t" << soundings_buffer.size() << " soundings in buffer   ";
        std::cout.flush();
        check_buffer = true;
      }
    }

    if(check_buffer)
    {
      auto buffer_iterator = soundings_buffer.begin();
      while(buffer_iterator != soundings_buffer.end())
      {
        auto msg = buffer_iterator->first;
        auto last_nav = buffer_iterator->second;
        try
        {
          auto transform = tfBuffer.lookupTransform(map_frame, msg->header.frame_id, msg->header.stamp);
          sensor_msgs::PointCloud2 soundings_in_map_frame;
          tf2::doTransform(*msg, soundings_in_map_frame, transform);

          std::vector<cube::Sounding> soundings;

          sensor_msgs::PointCloud2ConstIterator<float> iter_x_sensor(*msg, "x");
          sensor_msgs::PointCloud2ConstIterator<float> iter_y_sensor(*msg, "y");
          sensor_msgs::PointCloud2ConstIterator<float> iter_z_sensor(*msg, "z");

          sensor_msgs::PointCloud2ConstIterator<float> iter_x(soundings_in_map_frame, "x");
          sensor_msgs::PointCloud2ConstIterator<float> iter_y(soundings_in_map_frame, "y");
          sensor_msgs::PointCloud2ConstIterator<float> iter_z(soundings_in_map_frame, "z");
          for (; (iter_x != iter_x.end()) && (iter_y != iter_y.end()) && (iter_z != iter_z.end()) && (iter_x_sensor != iter_x_sensor.end()) && (iter_y_sensor != iter_y_sensor.end()) && (iter_z_sensor != iter_z_sensor.end()); ++iter_x, ++iter_y, ++iter_z, ++iter_x_sensor, ++iter_y_sensor, ++iter_z_sensor)
          {
            cube::Sounding s;
            s.x = *iter_x;
            s.y = *iter_y;
            s.depth = *iter_z;
            s.vertical_error = last_nav.position_covariance[8]*10.0;
            s.horizontal_error = std::max(last_nav.position_covariance[0], last_nav.position_covariance[4])*10.0;

            soundings.push_back(s);
          }

          map_sheet.addSoundings(soundings);
          buffer_iterator = soundings_buffer.erase(buffer_iterator);
          ping_count++;
        }
        catch (const tf2::ExtrapolationException& e)
        {
          buffer_iterator++;
        }

      }
    }
    if(ping_count_limit > 0 && ping_count >= ping_count_limit)
    {
      std::cout << "\nPing count limit of " << ping_count_limit << " reached" << std::endl;
      break;
    }
  }

  std::cout << "\ndone." << std::endl;

  std::cout << "Generating output..." << std::endl;

  auto total_cell_counts = map_sheet.totalCellCounts();

  std::cout << "Total cells: " << total_cell_counts << std::endl;

  GDALAllRegister();

  auto driver = GetGDALDriverManager()->GetDriverByName("GTiff");
  auto dataset = driver->Create(output_filename.c_str(), total_cell_counts.x, total_cell_counts.y, 2, GDT_Float32, nullptr);

  auto bounds = map_sheet.gridBounds();
  std::cout << "grid bounds: " << bounds << std::endl;

  auto cellsizes = map_sheet.cellSizes();

  double geo_transform[6] = {bounds.minimum.x, cellsizes.x, 0, bounds.maximum.y, 0, -cellsizes.y};
  dataset->SetGeoTransform(geo_transform);

  geometry_msgs::PointStamped p;
  p.header.frame_id = map_frame;
  p.header.stamp = view.getEndTime();

  auto earth_transform = tfBuffer.lookupTransform("earth", map_frame, ros::Time());
  geometry_msgs::PointStamped origin;
  tf2::doTransform(p, origin, earth_transform);

  std::cout << "origin: " << origin << std::endl;
  
  // Example proj string: +proj=topocentric +X_0=3771793.97 +Y_0=140253.34 +Z_0=5124304.35

  std::stringstream projection;
  projection << "+proj=topocentric +X_0=" << origin.point.x << " +Y_0=" << origin.point.y << " +Z_0=" << origin.point.z;

  OGRSpatialReference spatial_reference;
  spatial_reference.importFromProj4(projection.str().c_str());
  spatial_reference.SetWellKnownGeogCS("WGS84");

  char * wkt = nullptr;
  spatial_reference.exportToWkt( &wkt);

  std::cout << wkt;
  
  dataset->SetProjection(wkt);
  CPLFree(wkt);

  float nan = std::numeric_limits<float>::quiet_NaN();

  dataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, total_cell_counts.x, total_cell_counts.y, &nan, 1, 1, GDT_Float32, 0, 0);
  dataset->GetRasterBand(2)->RasterIO(GF_Write, 0, 0, total_cell_counts.x, total_cell_counts.y, &nan, 1, 1, GDT_Float32, 0, 0);

  auto grids = map_sheet.grids();
  auto cell_counts = map_sheet.cellCountsPerGrid();
  for(auto grid: grids)
  {
    auto origin_index = cube::MapOffset(bounds.minimum, grid->origin())/grid->cellSizes();
    auto values = grid->values();
    // gdal organizes data with first row being top row
    auto gdal_y_index = total_cell_counts.y - origin_index.y - cell_counts.y;
    for(int row = 0; row < cell_counts.y; row++)
    {
      dataset->GetRasterBand(1)->RasterIO(GF_Write, origin_index.x, gdal_y_index+cell_counts.y-1-row, cell_counts.x, 1, &(values[row*cell_counts.x].depth), cell_counts.x, 1, GDT_Float32, 2*sizeof(float), 0);
      dataset->GetRasterBand(2)->RasterIO(GF_Write, origin_index.x, gdal_y_index+cell_counts.y-1-row, cell_counts.x, 1, &(values[row*cell_counts.x].uncertainty), cell_counts.x, 1, GDT_Float32, 2*sizeof(float), 0);
    }
  }

  GDALClose( (GDALDatasetH) dataset );

// example reprojection command
// gdalwarp -t_srs EPSG:4326 -ct "+proj=pipeline +step +inv +proj=topocentric +X_0=1274750 +Y_0=-4812100 +Z_0=3974030 +step +inv +proj=cart +ellps=WGS84 +step +proj=unitconvert +xy_in=rad +xy_out=deg +step +proj=axisswap +order=2,1"  2024-08-08_map.tiff 2024-08-08_map_reprojected.tiff

  std::cout << "\ngdalwarp -t_srs EPSG:4326 -ct \"+proj=pipeline +step +inv +proj=topocentric +X_0=" << origin.point.x << " +Y_0=" << origin.point.y << " +Z_0=" << origin.point.z << " +step +inv +proj=cart +ellps=WGS84 +step +proj=unitconvert +xy_in=rad +xy_out=deg +step +proj=axisswap +order=2,1\" " << output_filename << " " << output_filename << ".geographic.tiff" << std::endl;

  return 0;
}

