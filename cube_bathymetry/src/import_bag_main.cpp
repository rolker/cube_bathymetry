// Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
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

// import_bag: offline detections-bag -> CUBE GeoMapSheet -> bathymetry-store
// epoch importer (PR-B of unh_marine_autonomy#147, cube_bathymetry#57).
//
// Mirrors the bag_to_geotiff `-d` offline-projection chain (rosbag2
// SequentialReader -> tf2::BufferCore from /tf + /tf_static -> DetectionsProjector
// -> per-sounding lookupTransform("earth", frame_id, stamp) -> GeoSounding ->
// GeoMapSheet) but writes a marine_bathymetry_store epoch instead of a GeoTIFF.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cube_bathymetry/detections_projector.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/store_import.h"
#include "geometry_msgs/msg/point_stamped.hpp"
#include "marine_acoustic_msgs/msg/sonar_detections.hpp"
#include "marine_autonomy/gz4d_geo.h"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/epoch.hpp"
#include "marine_bathymetry_store/registry.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rosbag2_transport/reader_writer_factory.hpp"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "tf2_msgs/msg/tf_message.hpp"
#include "tf2_ros/buffer.h"

void usage()
{
  std::cout << "usage: import_bag [options] -o <store_dir> -e <epoch> "
    "-d <detections_topic> <bag> [<bag> ...]\n";
  std::cout << "  -o <store_dir>: Output bathymetry-store directory (created if "
    "needed)\n";
  std::cout << "  -e <epoch>: Epoch label (ISO-8601 acquisition date, e.g. "
    "2026-06-15)\n";
  std::cout << "  -d <detections_topic>: marine_acoustic_msgs/SonarDetections "
    "topic to replay through CUBE (required)\n";
  std::cout << "  -r <meters>: Grid resolution (nominal; snapped to GGGS). "
    "Default 1.0\n";
  std::cout << "  --iho-order <order>: CUBE IHO order (default order1a)\n";
  std::cout << "  -l <count>: Stop after this many pings (debugging)\n";
  std::cout << "  --source-id <id>: Registry source id recorded for every cell "
    "(default cube-replay)\n";
  std::cout << "  --platform / --sensor / --sensor-class / --campaign <str>: "
    "registry provenance fields\n";
  std::cout << "  Frame/range overrides for the offline projector (must match "
    "the bag's namespaced frames, or the grid comes out empty):\n";
  std::cout << "    --base-link-frame, --level-frame, --tide-frame\n";
  std::cout << "    --minimum-range, --maximum-range (meters)\n";
  exit(-1);
}

bool bag_filter(const std::string & type)
{
  return type == "tf2_msgs/msg/TFMessage" ||
         type == "marine_acoustic_msgs/msg/SonarDetections";
}

bool ends_with(const std::string & str, const std::string & suffix)
{
  if (str.length() >= suffix.length()) {
    return 0 == str.compare(str.length() - suffix.length(), suffix.length(), suffix);
  }
  return false;
}

/// Keep track of multiple bag files and retrieve messages in chronological order.
/// (Mirrors bag_to_geotiff's BagReaders.)
class BagReaders
{
public:
  /// A bag serialized message with the data type.
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
      for (auto & topic_info : reader.get_all_topics_and_types()) {
        if (topic_info.name == message->topic_name) {
          data_type = topic_info.type;
          break;
        }
      }
    }
  };

  explicit BagReaders(const std::vector<std::string> & bagfile_names)
  {
    for (const auto & bagfile_name : bagfile_names) {
      readers_[bagfile_name].open(bagfile_name);
    }
  }

  auto start_time()
  {
    auto start_time = readers_.begin()->second.reader->get_metadata().starting_time;
    for (auto & reader : readers_) {
      if (reader.second.reader->get_metadata().starting_time < start_time) {
        start_time = reader.second.reader->get_metadata().starting_time;
      }
    }
    return start_time;
  }

  auto end_time()
  {
    auto end_time = readers_.begin()->second.reader->get_metadata().starting_time +
      readers_.begin()->second.reader->get_metadata().duration;
    for (auto & reader : readers_) {
      if (reader.second.reader->get_metadata().starting_time +
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
    for (auto & reader : readers_) {
      if (reader.second.has_next()) {
        if (!has_next ||
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
      if (reader->has_next()) {
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
      if (reader->has_next()) {
        next_message = std::make_shared<Message>(reader->read_next(), *reader);
      } else {
        next_message.reset();
      }
      return return_value;
    }
  };

  std::map<std::string, Bag> readers_;
};


int main(int argc, char * argv[])
{
  std::vector<std::string> arguments(argv + 1, argv + argc);
  if (arguments.empty()) {
    usage();
  }

  std::vector<std::string> bagfile_names;
  std::string store_dir;
  std::string epoch_label;
  std::string detections_topic;  // required
  double resolution = 1.0;
  std::string iho_order = "order1a";
  int ping_count_limit = 0;

  // Registry provenance fields for the imported cells.
  marine_bathymetry_store::SourceRecord source_record;
  source_record.source_id = "cube-replay";

  // DetectionsProjector configuration for the offline path. Defaults match
  // detections_to_pointcloud's node defaults so a detections-only bag projects
  // the same way the live node would.
  cube::ProjectorParams projector_params;

  for (auto arg = arguments.begin(); arg != arguments.end(); arg++) {
    if (*arg == "-h") {
      usage();
    } else if (*arg == "-o") {
      arg++;
      store_dir = *arg;
    } else if (*arg == "-e") {
      arg++;
      epoch_label = *arg;
    } else if (*arg == "-d") {
      arg++;
      detections_topic = *arg;
    } else if (*arg == "-r") {
      arg++;
      resolution = std::stod(*arg);
    } else if (*arg == "--iho-order") {
      arg++;
      iho_order = *arg;
    } else if (*arg == "-l") {
      arg++;
      ping_count_limit = std::stoi(*arg);
    } else if (*arg == "--source-id") {
      arg++;
      source_record.source_id = *arg;
    } else if (*arg == "--platform") {
      arg++;
      source_record.platform = *arg;
    } else if (*arg == "--sensor") {
      arg++;
      source_record.sensor = *arg;
    } else if (*arg == "--sensor-class") {
      arg++;
      source_record.sensor_class = *arg;
    } else if (*arg == "--campaign") {
      arg++;
      source_record.campaign = *arg;
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
    } else {
      bagfile_names.push_back(*arg);
    }
  }

  if (store_dir.empty() || epoch_label.empty() || detections_topic.empty() ||
    bagfile_names.empty())
  {
    std::cerr << "error: -o <store_dir>, -e <epoch>, -d <detections_topic>, and "
      "at least one bag are all required\n";
    usage();
  }

  // Fail fast on a bad epoch label before doing any (potentially long) replay.
  try {
    marine_bathymetry_store::validateEpochLabel(epoch_label);
  } catch (const std::exception & e) {
    std::cerr << "error: invalid epoch label '" << epoch_label << "': " << e.what()
              << std::endl;
    return 1;
  }

  std::cout << "Detections topic: " << detections_topic
            << " (offline projection, vessel_speed = NaN)" << std::endl;
  std::cout << "Store dir: " << store_dir << "  Epoch: " << epoch_label << std::endl;

  cube::DetectionsProjector projector(projector_params);

  // Accumulated offline-projection diagnostics, surfaced at the end so a
  // misconfigured-frames or over-tight-range run is diagnosable rather than a
  // silently sparse/empty epoch (the failure mode #43 exists to kill).
  size_t proj_pings = 0;
  size_t proj_soundings = 0;
  size_t proj_filtered_range = 0;
  size_t proj_missing_attitude = 0;
  size_t proj_missing_heave = 0;

  BagReaders bag_readers(bagfile_names);

  std::cout << "calculating total time..." << std::endl;
  auto begin_time = bag_readers.start_time();
  auto end_time = bag_readers.end_time();
  auto total_duration = end_time - begin_time;

  auto start_time_t = std::chrono::system_clock::to_time_t(begin_time);
  std::cout << "start time: "
            << std::put_time(std::gmtime(&start_time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
  auto end_time_t = std::chrono::system_clock::to_time_t(end_time);
  std::cout << "end time: "
            << std::put_time(std::gmtime(&end_time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
  std::cout << "total time: "
            << std::chrono::duration_cast<std::chrono::seconds>(total_duration).count()
            << " seconds" << std::endl;

  // Single, deterministic per-cell timestamp: the bag's nominal start time in ns
  // since the Unix epoch. A single value per import keeps the epoch deterministic
  // (every replay of the same bag yields the same cell stamps).
  const int64_t cell_timestamp_ns =
    std::chrono::duration_cast<std::chrono::nanoseconds>(
    begin_time.time_since_epoch()).count();

  auto clock = std::make_shared<rclcpp::Clock>();
  tf2_ros::Buffer tfBuffer(clock, total_duration);

  cube::GeoMapSheet geo_map_sheet(resolution, iho_order);
  std::cout << "requested resolution: " << resolution << " nominal used: "
            << geo_map_sheet.nominalCellSizeMeters() << std::endl;

  std::cout << "reading messages..." << std::endl;

  int ping_count = 0;
  uint64_t msg_count = 0;
  auto last_report_time = std::chrono::system_clock::now();
  std::chrono::seconds report_interval(1);

  for (auto message = bag_readers.next(); message; message = bag_readers.next()) {
    if (!bag_filter(message->data_type)) {
      continue;
    }
    ++msg_count;

    if (message->data_type == "tf2_msgs/msg/TFMessage") {
      if (ends_with(message->message->topic_name, "/tf_static")) {
        rclcpp::SerializedMessage serialized_message(*message->message->serialized_data);
        tf2_msgs::msg::TFMessage tf_message;
        rclcpp::Serialization<tf2_msgs::msg::TFMessage>().deserialize_message(
          &serialized_message, &tf_message);
        for (const auto & t : tf_message.transforms) {
          tfBuffer.setTransform(t, "", true);
        }
      }
      if (ends_with(message->message->topic_name, "/tf")) {
        try {
          rclcpp::SerializedMessage serialized_message(*message->message->serialized_data);
          tf2_msgs::msg::TFMessage tf_message;
          rclcpp::Serialization<tf2_msgs::msg::TFMessage>().deserialize_message(
            &serialized_message, &tf_message);
          for (const auto & t : tf_message.transforms) {
            tfBuffer.setTransform(t, "", false);
          }
          auto now = std::chrono::system_clock::now();
          if (now >= last_report_time + report_interval && !tf_message.transforms.empty() &&
            total_duration.count() > 0)
          {
            double progress =
              (tf2_ros::fromMsg(tf_message.transforms.front().header.stamp) - begin_time)
              .count() / static_cast<double>(total_duration.count());
            std::cout << "\r" << static_cast<int>(100 * progress) << "%\t" << msg_count
                      << " messages, " << ping_count << " pings            ";
            std::cout.flush();
            last_report_time = now;
          }
        } catch (const std::exception & e) {
          std::cerr << e.what() << '\n';
        }
      }
    }

    if (message->data_type == "marine_acoustic_msgs/msg/SonarDetections" &&
      message->message->topic_name == detections_topic)
    {
      try {
        rclcpp::SerializedMessage serialized_message(*message->message->serialized_data);
        marine_acoustic_msgs::msg::SonarDetections detections;
        rclcpp::Serialization<marine_acoustic_msgs::msg::SonarDetections>()
        .deserialize_message(&serialized_message, &detections);

        auto projection = projector.project(detections, tfBuffer, std::nanf(""));
        ++proj_pings;
        proj_soundings += projection.soundings.size();
        proj_filtered_range += projection.diagnostics.filtered_range;
        proj_missing_attitude += projection.diagnostics.missing_attitude;
        proj_missing_heave += projection.diagnostics.missing_heave;

        // Georeference each sonar-frame sounding to lat/lon via the bag's TF
        // buffer at the ping stamp, exactly as bag_to_geotiff -d does.
        try {
          auto transform = tfBuffer.lookupTransform(
            "earth", detections.header.frame_id, detections.header.stamp);

          std::vector<cube::GeoSounding> soundings;
          soundings.reserve(projection.soundings.size());
          for (const auto & s : projection.soundings) {
            geometry_msgs::msg::PointStamped sounding_re_sensor;
            sounding_re_sensor.point.x = s.sonar_relative_position.x;
            sounding_re_sensor.point.y = s.sonar_relative_position.y;
            sounding_re_sensor.point.z = s.sonar_relative_position.z;
            sounding_re_sensor.header = detections.header;

            geometry_msgs::msg::PointStamped sounding_ecef;
            tf2::doTransform(sounding_re_sensor, sounding_ecef, transform);

            gz4d::GeoPointECEF ecef(
              sounding_ecef.point.x, sounding_ecef.point.y, sounding_ecef.point.z);
            gz4d::GeoPointLatLongDegrees ll(ecef);
            cube::GeoSounding gs(ll);
            gs.sounding.vertical_error = s.vertical_error;
            gs.sounding.horizontal_error = s.horizontal_error;
            soundings.push_back(gs);
          }
          geo_map_sheet.addSoundings(soundings);
          ping_count++;
        } catch (const tf2::TransformException & e) {
          // No earth transform at this stamp yet -- drop the ping (the bag may
          // start before the first map->earth fix); matches bag_to_geotiff.
          std::cerr << "Transform Exception: " << e.what() << std::endl;
        }
      } catch (const std::exception & e) {
        std::cerr << e.what() << '\n';
      }
    }

    if (ping_count_limit > 0 && ping_count >= ping_count_limit) {
      std::cout << "\nPing count limit of " << ping_count_limit << " reached" << std::endl;
      break;
    }
  }

  std::cout << "\ndone." << std::endl;
  std::cout << "Offline projection: " << proj_pings << " pings, " << proj_soundings
            << " soundings (" << proj_filtered_range << " range-filtered, "
            << proj_missing_attitude << " missing attitude, " << proj_missing_heave
            << " missing heave)" << std::endl;
  if (proj_pings > 0 && proj_soundings == 0) {
    std::cerr << "WARNING: projected 0 soundings from " << proj_pings
              << " pings -- check the --*-frame overrides match the bag's "
              << "namespaced frames (see README 'Configuring frames per platform')."
              << std::endl;
  }

  std::cout << "Building store epoch..." << std::endl;

  // The GeoMapSheet picks a GGGS level from the requested cell size; build the
  // store at the matching default level. importEpoch is multi-level, so the
  // GridIndex on each tile carries the authoritative level regardless.
  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(
    static_cast<float>(geo_map_sheet.nominalCellSizeMeters()));

  marine_bathymetry_store::SourceRegistry registry;
  const uint16_t source_index = registry.registerSource(source_record);

  auto tiles = cube::mapSheetToEpochTiles(geo_map_sheet, cell_timestamp_ns, source_index);
  std::cout << "Tiles with data: " << tiles.size() << std::endl;

  if (tiles.empty()) {
    std::cerr << "WARNING: no tiles had finite data -- nothing imported. Check "
      "the projector frame overrides and the detections topic." << std::endl;
  }

  // Draft layer + Replayed provenance: this is a full-bag CUBE replay, the
  // authoritative end-of-day compaction product (ADR-0002 A1.2).
  store.importEpoch(
    marine_bathymetry_store::SourceLayer::Draft, epoch_label, std::move(tiles),
    marine_bathymetry_store::Provenance::Replayed);

  std::size_t written = marine_bathymetry_store::save(store, store_dir, &registry);
  std::cout << "Saved " << written << " tiles to " << store_dir << " (epoch "
            << epoch_label << ")." << std::endl;

  std::cout << "done!" << std::endl;
  return 0;
}
