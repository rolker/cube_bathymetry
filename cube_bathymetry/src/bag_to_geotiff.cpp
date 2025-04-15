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
#include <cube_bathymetry/geo_map_sheet.h>
#include <geometry_msgs/PointStamped.h>

#include "gdal_priv.h"

void usage()
{ 
  std::cout << "usage: bag_to_geotiff [options and input files]\n";
  std::cout << "  -m map: Map frame (depacrated)\n";
  std::cout << "  -n /fix: NavSatFix topic, optionally used to assess GPS uncertainty\n";
  std::cout << "  -o output.tiff: Output file name\n";
  std::cout << "  -t /soundings: Topic containing soundings as sensor_msgs/PointCloud2 messages\n";
  std::cout << "  -l 0: Number of pings to process before exiting (mainly for debugging)\n";
  exit(-1);
}

bool bag_filter(rosbag::ConnectionInfo const * info)
{
  if(info->datatype == "tf2_msgs/TFMessage")
    return true;
  if(info->datatype == "sensor_msgs/NavSatFix")
    return true;
  if(info->datatype == "sensor_msgs/PointCloud2")
    return true;
  return false;
}

int main(int argc, char *argv[])
{
  std::vector<std::string> arguments(argv+1,argv+argc);

  if (arguments.empty())
    usage();

  std::vector<std::string> bagfile_names;
  std::string bathymetry_topic = "/soundings";
  //std::string map_frame = "map";
  std::string output_filename;
  std::string nav_topic;
  double resolution = 1.0;

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
      //map_frame = *arg;
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
    else if (*arg == "-r")
    {
      arg++;
      resolution = std::stod(*arg);
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

  std::cout << "Bathymetry topic: " << bathymetry_topic << std::endl;

  rosbag::View view(true);

  // keep bags around since call to view takes a pointer to bags
  std::vector<std::shared_ptr<rosbag::Bag> > bags;
  for(auto bag: bagfile_names)
  {
    bags.push_back(std::make_shared<rosbag::Bag>(bag));
    view.addQuery(*bags.back(), &bag_filter);
  }

  std::cout << "calculating total time..." << std::endl;
  auto begin_time = view.getBeginTime();
  auto total_duration = view.getEndTime() - begin_time;
  std::cout << "total time: " << total_duration.toSec() << " seconds" << std::endl;

  tf2_ros::Buffer tfBuffer(total_duration);
  
  sensor_msgs::NavSatFix last_nav;

  cube::GeoMapSheet geo_map_sheet(resolution);
  std::cout << "requested resolution: " << resolution << " nominal used: "  << geo_map_sheet.nominalCellSizeMeters() << std::endl;

  std::list<std::pair<sensor_msgs::PointCloud2::ConstPtr, sensor_msgs::NavSatFix> > soundings_buffer;

  std::cout << "reading messages..." << std::endl;

  geometry_msgs::TransformStamped map_to_earth;

  int ping_count = 0;
  uint64_t msg_count = 0;
  auto start_time = std::chrono::system_clock::now();
  auto last_report_time = start_time;
  std::chrono::seconds	report_interval(1);

  for(const auto m: view)
  {
    bool check_buffer = false; // did we get a sounding or updated tf message?
    ++msg_count;

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
        try
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
          auto now = std::chrono::system_clock::now();
          if(now >= last_report_time + report_interval)
            if(!msg->transforms.empty())
            {
              double progress = (msg->transforms.front().header.stamp - begin_time).toSec()/total_duration.toSec();
              std::cout << "\r" << int(100*progress) << "%";
              std::cout << "\t" << (msg->transforms.front().header.stamp - begin_time).toSec() << " of " << total_duration.toSec() << " seconds, " << msg_count << " messages, " << ping_count << " pings            ";
              std::cout.flush();
              last_report_time = now;
            }
          check_buffer = true;
        }
        catch(const std::exception& e)
        {
          std::cerr << e.what() << '\n';
        }
      }
    }

    if (!nav_topic.empty() && m.getTopic() == nav_topic && m.getDataType() == "sensor_msgs/NavSatFix")
    {
      try
      {
        last_nav = *m.instantiate<sensor_msgs::NavSatFix>();
      }
      catch(const std::exception& e)
      {
        std::cerr << e.what() << '\n';
      }
    }

    if (m.getDataType() == "sensor_msgs/PointCloud2")
    {
      if(bathymetry_topic == "" || m.getTopic() == bathymetry_topic)
      {
        try
        {
          sensor_msgs::PointCloud2::ConstPtr msg = m.instantiate<sensor_msgs::PointCloud2>();
          soundings_buffer.push_back(std::make_pair(msg, last_nav));
          check_buffer = true;
        }
        catch(const std::exception& e)
        {
          std::cerr << e.what() << '\n';
        }
        
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
          auto transform = tfBuffer.lookupTransform("earth", msg->header.frame_id, msg->header.stamp);

          std::vector<cube::GeoSounding> soundings;
          sensor_msgs::PointCloud2ConstIterator<float> iter_x(*msg, "x");
          sensor_msgs::PointCloud2ConstIterator<float> iter_y(*msg, "y");
          sensor_msgs::PointCloud2ConstIterator<float> iter_z(*msg, "z");
          for (; (iter_x != iter_x.end()) && (iter_y != iter_y.end()) && (iter_z != iter_z.end()); ++iter_x, ++iter_y, ++iter_z)
          {
            geometry_msgs::PointStamped sounding_re_sensor;
            sounding_re_sensor.point.x = *iter_x;
            sounding_re_sensor.point.y = *iter_y;
            sounding_re_sensor.point.z = *iter_z;
            sounding_re_sensor.header = msg->header;

            geometry_msgs::PointStamped sounding_ecef;
            tf2::doTransform(sounding_re_sensor, sounding_ecef, transform);

            gz4d::GeoPointECEF ecef(sounding_ecef.point.x, sounding_ecef.point.y, sounding_ecef.point.z);
            gz4d::GeoPointLatLongDegrees ll(ecef);
            cube::GeoSounding s(ll);
            s.sounding.vertical_error = last_nav.position_covariance[8]*10.0;
            s.sounding.horizontal_error = std::max(last_nav.position_covariance[0], last_nav.position_covariance[4])*10.0;

            soundings.push_back(s);
          }
          geo_map_sheet.addSoundings(soundings);
          buffer_iterator = soundings_buffer.erase(buffer_iterator);
          ping_count++;
        }
        catch (const tf2::ExtrapolationException& e)
        {
          buffer_iterator++;
        }
        catch (const tf2::TransformException& e)
        {
          std::cerr << "Transform Exception: " << e.what() << std::endl;
          buffer_iterator = soundings_buffer.erase(buffer_iterator);
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

  auto bounds = geo_map_sheet.gridBounds();
  std::cout << "grid bounds: " << bounds << std::endl;

  auto rows =  bounds.cellRowCount();
  auto columns = bounds.cellColumnCount();
  std::cout << "Total cells: " << rows << " rows by " << columns << " columns" << std::endl;

  GDALAllRegister();
  auto driver = GetGDALDriverManager()->GetDriverByName("GTiff");

  char** options = nullptr;
  options = CSLSetNameValue(options, "COMPRESS", "LZW");
  auto dataset = driver->Create(output_filename.c_str(), columns, rows, 2, GDT_Float32, options);
  CSLDestroy(options);

  auto cellsize = geo_map_sheet.cellSizeDegrees();

  double geo_transform[6] = {bounds.minimum().westLongitude(), cellsize, 0, bounds.maximum().northLatitude(), 0, -cellsize};
  dataset->SetGeoTransform(geo_transform);

  OGRSpatialReference spatial_reference;
  spatial_reference.SetWellKnownGeogCS("WGS84");

  char * wkt = nullptr;
  spatial_reference.exportToWkt( &wkt);

  std::cout << wkt << std::endl;
  
  dataset->SetProjection(wkt);
  CPLFree(wkt);

  float nan = std::numeric_limits<float>::quiet_NaN();

  dataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, columns, rows, &nan, 1, 1, GDT_Float32, 0, 0);
  dataset->GetRasterBand(2)->RasterIO(GF_Write, 0, 0, columns, rows, &nan, 1, 1, GDT_Float32, 0, 0);

  auto grids = geo_map_sheet.grids();
  for(auto grid: grids)
  {
    auto row_offset = (grid->index().row() - bounds.minimum().row())*grid->index().cellRowCount();
    auto column_offset = (grid->index().column() - bounds.minimum().column())*grid->index().cellColumnCount();
    auto values = grid->values();
    // gdal organizes data with first row being top row
    auto gdal_y_index = rows - row_offset - grid->index().cellRowCount();
    for(int row = 0; row < grid->index().cellRowCount(); row++)
    {
      dataset->GetRasterBand(1)->RasterIO(GF_Write, column_offset, gdal_y_index+grid->index().cellRowCount()-1-row, grid->index().cellColumnCount(), 1, &(values[row*grid->index().cellColumnCount()].depth), grid->index().cellColumnCount(), 1, GDT_Float32, 2*sizeof(float), 0);
      dataset->GetRasterBand(2)->RasterIO(GF_Write, column_offset, gdal_y_index+grid->index().cellRowCount()-1-row, grid->index().cellColumnCount(), 1, &(values[row*grid->index().cellColumnCount()].uncertainty), grid->index().cellColumnCount(), 1, GDT_Float32, 2*sizeof(float), 0);
    }
  }

  GDALClose( (GDALDatasetH) dataset );

  std::cout << "done!" << std::endl;


  return 0;
}

