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


#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <optional>
#include <vector>

#include "tf2_ros/buffer.h"
#include "tf2_msgs/msg/tf_message.hpp"
#include "marine_acoustic_msgs/msg/sonar_detections.hpp"
#include "std_msgs/msg/header.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "sensor_msgs/msg/point_field.hpp"
#include "sensor_msgs/point_cloud2_iterator.hpp"
#include "tf2_sensor_msgs/tf2_sensor_msgs/tf2_sensor_msgs.hpp"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "sensor_msgs/msg/nav_sat_fix.hpp"
#include "cube_bathymetry/detections_projector.h"
#include "cube_bathymetry/projection_summary.h"
#include "cube_bathymetry/map_sheet.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "geometry_msgs/msg/point_stamped.hpp"
#include "rosbag2_transport/reader_writer_factory.hpp"
#include "gdal_priv.h"

void usage()
{
  std::cout << "usage: bag_to_geotiff [options and input files]\n";
  std::cout << "  -n /fix: NavSatFix topic, optionally used to assess GPS uncertainty\n";
  std::cout << "  -o output.tiff: Output file name\n";
  std::cout << "  -t /soundings: Topic containing pre-projected soundings as "
    "sensor_msgs/PointCloud2 messages\n";
  std::cout << "  -d /detections: Topic containing marine_acoustic_msgs/SonarDetections; "
    "projected to soundings offline via DetectionsProjector (vessel_speed = NaN). "
    "Without an explicit -t, the default /soundings topic is not also gridded.\n";
  std::cout << "  -r 1.0: Output resolution in meters (nominal; snapped to the grid)\n";
  std::cout << "  -l 0: Number of pings to process before exiting (mainly for debugging)\n";
  std::cout << "  Frame/range overrides for the -d offline projector (must match the "
    "bag's namespaced frames, or the grid comes out empty):\n";
  std::cout << "    --base-link-frame, --level-frame, --tide-frame\n";
  std::cout << "    --minimum-range, --maximum-range (meters)\n";
  exit(-1);
}

bool bag_filter(const std::string & type)
{
  if(type == "tf2_msgs/msg/TFMessage") {
    return true;
  }
  if(type == "sensor_msgs/msg/NavSatFix") {
    return true;
  }
  if(type == "sensor_msgs/msg/PointCloud2") {
    return true;
  }
  if(type == "marine_acoustic_msgs/msg/SonarDetections") {
    return true;
  }
  return false;
}

// Pack projected soundings into a 6-field (x,y,z,intensity,
// vertical_uncertainty,horizontal_uncertainty) PointCloud2, so the detections
// offline path feeds the identical downstream soundings_buffer/grid machinery
// as a pre-projected bag.
//
// This is the first SIX of the seven fields detections_to_pointcloud
// publishes: the seventh, `beam_angle`, is not emitted here. Consumers read
// PointCloud2 fields by name and cube_bathymetry_node treats `beam_angle` as
// optional (NaN-filling when it is absent), so the omission is safe -- but it
// does mean the offline `-d` path carries no beam angle, and anything that
// comes to depend on one has to add it here too.
sensor_msgs::msg::PointCloud2::SharedPtr soundingsToPointCloud2(
  const std::vector<cube::Sounding> & soundings,
  const std_msgs::msg::Header & header)
{
  auto pointcloud = std::make_shared<sensor_msgs::msg::PointCloud2>();
  pointcloud->header = header;
  pointcloud->height = 1;
  pointcloud->width = soundings.size();
  pointcloud->point_step = 24;  // 6 fields * 4 bytes each
  pointcloud->row_step = pointcloud->point_step * pointcloud->width;
  pointcloud->is_dense = true;
  pointcloud->is_bigendian = false;

  const char * names[6] = {"x", "y", "z", "intensity",
    "vertical_uncertainty", "horizontal_uncertainty"};
  pointcloud->fields.resize(6);
  for (size_t f = 0; f < 6; ++f) {
    pointcloud->fields[f].name = names[f];
    pointcloud->fields[f].offset = static_cast<uint32_t>(f * 4);
    pointcloud->fields[f].datatype = sensor_msgs::msg::PointField::FLOAT32;
    pointcloud->fields[f].count = 1;
  }

  pointcloud->data.resize(pointcloud->row_step * pointcloud->height);
  float * data_ptr = reinterpret_cast<float *>(pointcloud->data.data());
  for (const auto & sounding : soundings) {
    data_ptr[0] = sounding.sonar_relative_position.x;
    data_ptr[1] = sounding.sonar_relative_position.y;
    data_ptr[2] = sounding.sonar_relative_position.z;
    data_ptr[3] = sounding.intensity;
    data_ptr[4] = sounding.vertical_error;
    data_ptr[5] = sounding.horizontal_error;
    data_ptr += 6;
  }
  return pointcloud;
}


bool ends_with(const std::string & str, const std::string & suffix)
{
  if (str.length() >= suffix.length()) {
    return  0 == str.compare(str.length() - suffix.length(), suffix.length(), suffix);
  } else {
    return false;
  }
}

/// Keep track of multiple bag files and retieve messages in chronological order
class BagReaders
{
public:
  /// A bag serialized message with the data type
  struct Message
  {
    std::string data_type;
    rosbag2_storage::SerializedBagMessageSharedPtr message;

    using ConstPtr = std::shared_ptr<const Message>;

    Message(
      rosbag2_storage::SerializedBagMessageSharedPtr message,
      const rosbag2_cpp::Reader & reader)
    : message(message)
    {
      for (auto & topic_info  :  reader.get_all_topics_and_types()) {
        if(topic_info.name == message->topic_name) {
          data_type = topic_info.type;
          break;
        }
      }
    }
  };


  explicit BagReaders(const std::vector<std::string> & bagfile_names)
  {
    for (const auto & bagfile_name  :  bagfile_names) {
      readers_[bagfile_name].open(bagfile_name);
    }
  }

  auto start_time()
  {
    auto start_time = readers_.begin()->second.reader->get_metadata().starting_time;
    for (auto & reader  :  readers_) {
      if(reader.second.reader->get_metadata().starting_time < start_time) {
        start_time = reader.second.reader->get_metadata().starting_time;
      }
    }
    return start_time;
  }

  auto end_time()
  {
    auto end_time = readers_.begin()->second.reader->get_metadata().starting_time +
      readers_.begin()->second.reader->get_metadata().duration;
    for (auto & reader  :  readers_) {
      if(reader.second.reader->get_metadata().starting_time +
        reader.second.reader->get_metadata().duration > end_time)
      {
        end_time = reader.second.reader->get_metadata().starting_time +
          reader.second.reader->get_metadata().duration;
      }
    }
    return end_time;
  }

  Message::ConstPtr next()
  {
    Bag * has_next = nullptr;

    for (auto & reader  :  readers_) {
      if(reader.second.has_next()) {
        if(!has_next ||
          reader.second.peek_next()->message->send_timestamp <
          has_next->peek_next()->message->send_timestamp)
        {
          has_next = &reader.second;
        }
      }
    }

    if (has_next) {
      return has_next->pop_next();
    }
    return {};
  }

private:
  struct Bag
  {
    std::unique_ptr<rosbag2_cpp::Reader> reader;
    Message::ConstPtr next_message;

    void open(const std::string & file_name)
    {
      rosbag2_storage::StorageOptions storage_options;
      storage_options.uri = file_name;
      reader = rosbag2_transport::ReaderWriterFactory::make_reader(storage_options);
      reader->open(storage_options);
      if(reader->has_next()) {
        next_message = std::make_shared<Message>(reader->read_next(), *reader);
      }
    }

    bool has_next()
    {
      return static_cast<bool>(next_message);
    }

    Message::ConstPtr peek_next()
    {
      return next_message;
    }

    Message::ConstPtr pop_next()
    {
      auto return_value = next_message;
      if(reader->has_next()) {
        next_message = std::make_shared<Message>(reader->read_next(), *reader);
      } else {
        next_message.reset();
      }
      return return_value;
    }
  };

  std::map<std::string, Bag> readers_;
};


int main(int argc, char *argv[])
{
  std::vector<std::string> arguments(argv + 1, argv + argc);

  if (arguments.empty()) {
    usage();
  }

  std::vector<std::string> bagfile_names;
  std::string bathymetry_topic = "/soundings";
  bool bathymetry_topic_explicit = false;  // true once -t is given
  std::string detections_topic;  // empty = detections-offline path disabled
  std::string output_filename;
  std::string nav_topic;
  double resolution = 1.0;

  // DetectionsProjector configuration for the offline (-d) path. Defaults match
  // detections_to_pointcloud's node defaults so a detections-only bag projects
  // the same way the live node would.
  cube::ProjectorParams projector_params;

  int ping_count_limit = 0;

  for (auto arg = arguments.begin(); arg != arguments.end(); arg++) {
    if (*arg == "-h") {
      usage();
    } else if (*arg == "-n") {
      arg++;
      nav_topic = *arg;
    } else if (*arg == "-o") {
      arg++;
      output_filename = *arg;
    } else if (*arg == "-r") {
      arg++;
      resolution = std::stod(*arg);
    } else if (*arg == "-t") {
      arg++;
      bathymetry_topic = *arg;
      bathymetry_topic_explicit = true;
    } else if (*arg == "-d") {
      arg++;
      detections_topic = *arg;
    } else if (*arg == "--base-link-frame") {
      arg++;
      projector_params.base_link_frame = *arg;
    } else if (*arg == "--level-frame") {
      arg++;
      projector_params.level_frame = *arg;
    } else if (*arg == "--tide-frame") {
      arg++;
      projector_params.tide_frame = *arg;
    } else if (*arg == "--minimum-range") {
      arg++;
      projector_params.minimum_range = std::stod(*arg);
    } else if (*arg == "--maximum-range") {
      arg++;
      projector_params.maximum_range = std::stod(*arg);
    } else if (*arg == "-l") {
      arg++;
      ping_count_limit = std::stoi(*arg);
    } else {
      bagfile_names.push_back(*arg);
    }
  }

  // When projecting detections offline (-d) without an explicit -t, disable the
  // default /soundings admission. Otherwise a bag carrying BOTH a pre-projected
  // /soundings topic and the detections topic would grid every sounding twice.
  const bool process_soundings_topic = detections_topic.empty() || bathymetry_topic_explicit;
  if (!detections_topic.empty()) {
    std::cout << "Detections topic: " << detections_topic <<
      " (offline projection, vessel_speed = NaN)" << std::endl;
  }
  if (process_soundings_topic) {
    std::cout << "Bathymetry topic: " << bathymetry_topic << std::endl;
  } else {
    std::cout << "Pre-projected soundings topic disabled (-d given without -t)" << std::endl;
  }

  cube::DetectionsProjector projector(projector_params);

  // Accumulated offline-projection diagnostics, surfaced at the end so a
  // misconfigured-frames or over-tight-range run is diagnosable rather than a
  // silently sparse/empty GeoTIFF (the failure mode #43 exists to kill).
  // Whole-run projection totals; cube::report_projection_summary prints them.
  // This tool does not georeference in the projection pass, so its summary
  // line omits the georeferenced/dropped clause.
  cube::ProjectionRunTotals proj_totals;
  proj_totals.reports_georeferencing = false;

  BagReaders bag_readers(bagfile_names);


  std::cout << "calculating total time..." << std::endl;

  auto begin_time = bag_readers.start_time();
  auto end_time = bag_readers.end_time();

  auto total_duration = end_time - begin_time;

  auto start_time_t = std::chrono::system_clock::to_time_t(begin_time);
  std::cout << "start time: " << std::put_time(std::gmtime(&start_time_t),
    "%Y-%m-%d %H:%M:%S") << std::endl;

  auto end_time_t = std::chrono::system_clock::to_time_t(end_time);
  std::cout << "end time: " << std::put_time(std::gmtime(&end_time_t),
    "%Y-%m-%d %H:%M:%S") << std::endl;


  std::cout << "total time: " <<
    std::chrono::duration_cast<std::chrono::seconds>(total_duration).count() << " seconds" <<
    std::endl;

  auto clock = std::make_shared<rclcpp::Clock>();

  tf2_ros::Buffer tfBuffer(clock, total_duration);

  sensor_msgs::msg::NavSatFix last_nav;

  cube::GeoMapSheet geo_map_sheet(resolution);
  std::cout << "requested resolution: " << resolution << " nominal used: " <<
    geo_map_sheet.nominalCellSizeMeters() << std::endl;

  std::list<std::pair<sensor_msgs::msg::PointCloud2::SharedPtr,
    sensor_msgs::msg::NavSatFix>> soundings_buffer;

  std::cout << "reading messages..." << std::endl;

  geometry_msgs::msg::TransformStamped map_to_earth;

  int ping_count = 0;
  uint64_t msg_count = 0;
  auto last_report_time = std::chrono::system_clock::now();
  std::chrono::seconds  report_interval(1);

  for(auto message = bag_readers.next(); message; message = bag_readers.next()) {
    if(!bag_filter(message->data_type)) {
      continue;
    }


    bool check_buffer = false;  // did we get a sounding or updated tf message?
    ++msg_count;

    if (message->data_type == "tf2_msgs/msg/TFMessage") {
      if(ends_with(message->message->topic_name, "/tf_static")) {
        rclcpp::SerializedMessage serialized_message(*message->message->serialized_data);
        tf2_msgs::msg::TFMessage tf_message;
        rclcpp::Serialization<tf2_msgs::msg::TFMessage>().deserialize_message(&serialized_message,
          &tf_message);
        for (const auto & t   : tf_message.transforms) {
          tfBuffer.setTransform(t, "", true);
        }
      }
      if(ends_with(message->message->topic_name, "/tf")) {
        try {
          rclcpp::SerializedMessage serialized_message(*message->message->serialized_data);
          tf2_msgs::msg::TFMessage tf_message;
          rclcpp::Serialization<tf2_msgs::msg::TFMessage>().deserialize_message(&serialized_message,
            &tf_message);
          for (const auto & t   : tf_message.transforms) {
            tfBuffer.setTransform(t, "", false);
            if(t.header.frame_id == "earth") {
              if(t.transform.translation.x != map_to_earth.transform.translation.x ||
                t.transform.translation.y != map_to_earth.transform.translation.y ||
                t.transform.translation.z != map_to_earth.transform.translation.z)
              {
                std::cout << "\nnew map to earth transform:\n" << t.transform.translation.x <<
                  ", " << t.transform.translation.y << ", " << t.transform.translation.z <<
                  std::endl;
                map_to_earth = t;
              }
            }
          }
          auto now = std::chrono::system_clock::now();
          if(now >= last_report_time + report_interval) {
            if(!tf_message.transforms.empty()) {
              double progress = ( tf2_ros::fromMsg(tf_message.transforms.front().header.stamp) -
                begin_time).count() / static_cast<double>(total_duration.count());
              std::cout << "\r" << int(100 * progress) << "%";
              std::cout << "\t" <<
                (tf2_ros::fromMsg(tf_message.transforms.front().header.stamp) -
              begin_time).count() / 1000000000.0 << " of " <<
                total_duration.count() / 1000000000.0 << " seconds, " << msg_count <<
                " messages, " << ping_count << " pings            ";
              std::cout.flush();
              last_report_time = now;
            }
          }
          check_buffer = true;
        } catch(const std::exception & e) {
          std::cerr << e.what() << '\n';
        }
      }
    }

    if (!nav_topic.empty() && message->message->topic_name == nav_topic &&
      message->data_type == "sensor_msgs/msg/NavSatFix")
    {
      try {
        rclcpp::SerializedMessage serialized_message(*message->message->serialized_data);
        sensor_msgs::msg::NavSatFix nsf_message;
        rclcpp::Serialization<sensor_msgs::msg::NavSatFix>().deserialize_message(
          &serialized_message, &nsf_message);
        last_nav = nsf_message;
      } catch(const std::exception & e) {
        std::cerr << e.what() << '\n';
      }
    }

    if (process_soundings_topic && message->data_type == "sensor_msgs/msg/PointCloud2") {
      if(bathymetry_topic == "" || message->message->topic_name == bathymetry_topic) {
        try {
          rclcpp::SerializedMessage serialized_message(*message->message->serialized_data);
          auto pc_message = std::make_shared<sensor_msgs::msg::PointCloud2>();
          rclcpp::Serialization<sensor_msgs::msg::PointCloud2>().deserialize_message(
            &serialized_message, &(*pc_message));
          soundings_buffer.push_back(std::make_pair(pc_message, last_nav));
          check_buffer = true;
        } catch(const std::exception & e) {
          std::cerr << e.what() << '\n';
        }
      }
    }

    // Detections-only-bag path: project SonarDetections to soundings offline
    // using the bag's TF buffer (passed as const tf2::BufferCore&) and NaN
    // vessel_speed (no odom in a detections-only bag). The resulting soundings
    // are packed into the same PointCloud2 layout as the live /soundings topic
    // and fed through the identical buffer/grid machinery.
    if (!detections_topic.empty() &&
      message->data_type == "marine_acoustic_msgs/msg/SonarDetections" &&
      message->message->topic_name == detections_topic)
    {
      try {
        rclcpp::SerializedMessage serialized_message(*message->message->serialized_data);
        marine_acoustic_msgs::msg::SonarDetections detections;
        rclcpp::Serialization<marine_acoustic_msgs::msg::SonarDetections>().deserialize_message(
          &serialized_message, &detections);
        auto projection = projector.project(detections, tfBuffer, std::nanf(""));
        ++proj_totals.pings;
        proj_totals.soundings += projection.soundings.size();
        proj_totals.filtered_range += projection.diagnostics.filtered_range;
        proj_totals.missing_attitude += projection.diagnostics.missing_attitude;
        proj_totals.missing_heave += projection.diagnostics.missing_heave;
        proj_totals.beams += projection.diagnostics.total;
        proj_totals.default_beamwidth_beams += projection.diagnostics.default_beamwidth_beams;
        proj_totals.missing_rx_angle_beams += projection.diagnostics.missing_rx_angle_beams;
        auto pc_message = soundingsToPointCloud2(projection.soundings, detections.header);
        soundings_buffer.push_back(std::make_pair(pc_message, last_nav));
        check_buffer = true;
      } catch(const std::exception & e) {
        std::cerr << e.what() << '\n';
      }
    }

    if(check_buffer) {
      auto buffer_iterator = soundings_buffer.begin();
      while(buffer_iterator != soundings_buffer.end()) {
        auto msg = buffer_iterator->first;
        auto last_nav = buffer_iterator->second;
        try {
          auto transform = tfBuffer.lookupTransform("earth", msg->header.frame_id,
            msg->header.stamp);

          // Use the cloud's per-sounding TPU (from cube::ErrorModel via
          // detections_to_pointcloud) when present -- the same data the live
          // cube_bathymetry_node grid uses. Fall back to the nav-covariance
          // estimate only for older bags that lack the fields.
          bool has_vu = false;
          bool has_hu = false;
          for (const auto & f  :  msg->fields) {
            if (f.name == "vertical_uncertainty") {has_vu = true;}
            if (f.name == "horizontal_uncertainty") {has_hu = true;}
          }
          const bool has_tpu = has_vu && has_hu;  // need both, else fall back
          const double fallback_vert = last_nav.position_covariance[8] * 10.0;
          const double fallback_horiz = std::max(last_nav.position_covariance[0],
            last_nav.position_covariance[4]) * 10.0;

          std::vector<cube::GeoSounding> soundings;
          sensor_msgs::PointCloud2ConstIterator<float> iter_x(*msg, "x");
          sensor_msgs::PointCloud2ConstIterator<float> iter_y(*msg, "y");
          sensor_msgs::PointCloud2ConstIterator<float> iter_z(*msg, "z");
          std::optional<sensor_msgs::PointCloud2ConstIterator<float>> iter_vu, iter_hu;
          if (has_tpu) {
            iter_vu.emplace(*msg, "vertical_uncertainty");
            iter_hu.emplace(*msg, "horizontal_uncertainty");
          }
          for (; (iter_x != iter_x.end()) && (iter_y != iter_y.end()) && (iter_z != iter_z.end());
            ++iter_x, ++iter_y, ++iter_z)
          {
            geometry_msgs::msg::PointStamped sounding_re_sensor;
            sounding_re_sensor.point.x = *iter_x;
            sounding_re_sensor.point.y = *iter_y;
            sounding_re_sensor.point.z = *iter_z;
            sounding_re_sensor.header = msg->header;

            geometry_msgs::msg::PointStamped sounding_ecef;
            tf2::doTransform(sounding_re_sensor, sounding_ecef, transform);

            gz4d::GeoPointECEF ecef(sounding_ecef.point.x, sounding_ecef.point.y,
              sounding_ecef.point.z);
            gz4d::GeoPointLatLongDegrees ll(ecef);
            cube::GeoSounding s(ll);
            if (has_tpu) {
              s.sounding.vertical_error = **iter_vu;
              s.sounding.horizontal_error = **iter_hu;
              ++(*iter_vu);
              ++(*iter_hu);
            } else {
              s.sounding.vertical_error = fallback_vert;
              s.sounding.horizontal_error = fallback_horiz;
            }

            soundings.push_back(s);
          }
          geo_map_sheet.addSoundings(soundings);
          buffer_iterator = soundings_buffer.erase(buffer_iterator);
          ping_count++;
        } catch (const tf2::ExtrapolationException & e) {
          buffer_iterator++;
        } catch (const tf2::TransformException & e) {
          std::cerr << "Transform Exception: " << e.what() << std::endl;
          buffer_iterator = soundings_buffer.erase(buffer_iterator);
        }
      }
    }
    if(ping_count_limit > 0 && ping_count >= ping_count_limit) {
      std::cout << "\nPing count limit of " << ping_count_limit << " reached" << std::endl;
      break;
    }
  }

  std::cout << "\ndone." << std::endl;

  if (!detections_topic.empty()) {
    cube::report_projection_summary(proj_totals);
  }

  std::cout << "Generating output..." << std::endl;

  auto bounds = geo_map_sheet.gridBounds();
  std::cout << "grid bounds: " << bounds << std::endl;

  auto rows = bounds.cellRowCount();

  // Compute max column count across all grid rows to determine raster width.
  // Column count varies by latitude due to GGGS polar scaling (1x/3x/9x).
  uint64_t columns = 0;
  for(uint32_t r = bounds.minimum().row(); r <= bounds.maximum().row(); r++) {
    columns = std::max(columns, bounds.cellColumnCount(r));
  }
  std::cout << "Total cells: " << rows << " rows by " << columns << " columns" << std::endl;

  GDALAllRegister();
  auto driver = GetGDALDriverManager()->GetDriverByName("GTiff");

  char ** options = nullptr;
  options = CSLSetNameValue(options, "COMPRESS", "LZW");
  auto dataset = driver->Create(output_filename.c_str(), columns, rows, 2, GDT_Float32, options);
  CSLDestroy(options);

  auto cellsize = geo_map_sheet.cellSizeDegrees();

  double raster_west = bounds.minimum().westLongitude();
  double geo_transform[6] = {raster_west, cellsize, 0,
    bounds.maximum().northLatitude(), 0, -cellsize};
  dataset->SetGeoTransform(geo_transform);

  OGRSpatialReference spatial_reference;
  spatial_reference.SetWellKnownGeogCS("WGS84");

  char * wkt = nullptr;
  spatial_reference.exportToWkt(&wkt);

  std::cout << wkt << std::endl;

  dataset->SetProjection(wkt);
  CPLFree(wkt);

  float nan = std::numeric_limits<float>::quiet_NaN();

  dataset->GetRasterBand(1)->RasterIO(GF_Write, 0, 0, columns, rows, &nan, 1, 1, GDT_Float32, 0, 0);
  dataset->GetRasterBand(2)->RasterIO(GF_Write, 0, 0, columns, rows, &nan, 1, 1, GDT_Float32, 0, 0);

  auto grids = geo_map_sheet.grids();
  for (auto grid  :  grids) {
    auto grid_row = grid->index().row();
    auto row_offset = (grid_row - bounds.minimum().row()) * grid->index().cellRowCount();

    // Compute output column offset from longitude difference
    auto column_offset = static_cast<int64_t>(
      std::round((grid->index().westLongitude() - raster_west) / cellsize));

    // Stretch factor: ratio of max columns to this row's columns (1, 3, or 9)
    auto row_columns = bounds.cellColumnCount(grid_row);
    uint32_t stretch_factor = (row_columns > 0) ? columns / row_columns : 1;

    auto values = grid->values();
    auto src_cols = grid->index().cellColumnCount();  // always 960
    auto out_cols = static_cast<uint32_t>(src_cols) * stretch_factor;

    // gdal organizes data with first row being top row
    auto gdal_y_index = rows - row_offset - grid->index().cellRowCount();

    // Precompute interpolation lookup table and buffers once per grid
    struct InterpEntry
    {
      int left;
      int right;
      float t;
    };
    std::vector<InterpEntry> interp_table;
    std::vector<float> depth_buf;
    std::vector<float> uncert_buf;
    if(stretch_factor != 1) {
      depth_buf.resize(out_cols);
      uncert_buf.resize(out_cols);
      interp_table.resize(out_cols);
      for(uint32_t p = 0; p < out_cols; p++) {
        double src_pos = (p + 0.5) / stretch_factor - 0.5;
        int left = static_cast<int>(std::floor(src_pos));
        int right = left + 1;
        interp_table[p].t = static_cast<float>(src_pos - left);
        interp_table[p].left = std::clamp(left, 0, static_cast<int>(src_cols) - 1);
        interp_table[p].right = std::clamp(right, 0, static_cast<int>(src_cols) - 1);
      }
    }

    for(int row = 0; row < grid->index().cellRowCount(); row++) {
      auto gdal_row = gdal_y_index + grid->index().cellRowCount() - 1 - row;
      auto src_row_offset = row * src_cols;

      if(stretch_factor == 1) {
        // No stretching needed — write source data directly
        // Pixel-space byte stride = the size of one DepthAndUncertainty so the
        // read walks consecutive elements' .depth/.uncertainty. This MUST track
        // the struct size: #54 grew it (added intensity + beam_angle) from 8 to
        // 16 bytes, so a hardcoded 2*sizeof(float) would now stride into the
        // wrong field. Use sizeof(DepthAndUncertainty).
        dataset->GetRasterBand(1)->RasterIO(GF_Write, column_offset,
          gdal_row, src_cols, 1,
          &(values[src_row_offset].depth), src_cols, 1,
          GDT_Float32, sizeof(cube::DepthAndUncertainty), 0);
        dataset->GetRasterBand(2)->RasterIO(GF_Write, column_offset,
          gdal_row, src_cols, 1,
          &(values[src_row_offset].uncertainty), src_cols, 1,
          GDT_Float32, sizeof(cube::DepthAndUncertainty), 0);
      } else {
        // Polar-scaled row: interpolate to fill the wider raster

        for(uint32_t p = 0; p < out_cols; p++) {
          const auto & ie = interp_table[p];

          float d_left = values[src_row_offset + ie.left].depth;
          float d_right = values[src_row_offset + ie.right].depth;
          float u_left = values[src_row_offset + ie.left].uncertainty;
          float u_right = values[src_row_offset + ie.right].uncertainty;

          // NaN-aware linear interpolation
          bool d_left_nan = std::isnan(d_left);
          bool d_right_nan = std::isnan(d_right);
          if(d_left_nan && d_right_nan) {
            depth_buf[p] = nan;
          } else if(d_left_nan) {
            depth_buf[p] = d_right;
          } else if(d_right_nan) {
            depth_buf[p] = d_left;
          } else {
            depth_buf[p] = (1.0f - ie.t) * d_left + ie.t * d_right;
          }

          bool u_left_nan = std::isnan(u_left);
          bool u_right_nan = std::isnan(u_right);
          if(u_left_nan && u_right_nan) {
            uncert_buf[p] = nan;
          } else if(u_left_nan) {
            uncert_buf[p] = u_right;
          } else if(u_right_nan) {
            uncert_buf[p] = u_left;
          } else {
            uncert_buf[p] = (1.0f - ie.t) * u_left + ie.t * u_right;
          }
        }

        dataset->GetRasterBand(1)->RasterIO(GF_Write, column_offset,
          gdal_row, out_cols, 1,
          depth_buf.data(), out_cols, 1,
          GDT_Float32, 0, 0);
        dataset->GetRasterBand(2)->RasterIO(GF_Write, column_offset,
          gdal_row, out_cols, 1,
          uncert_buf.data(), out_cols, 1,
          GDT_Float32, 0, 0);
      }
    }
  }

  GDALClose(static_cast<GDALDatasetH>(dataset));

  std::cout << "done!" << std::endl;


  return 0;
}
