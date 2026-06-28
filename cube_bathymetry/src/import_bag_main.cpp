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
// importer (PR-B of unh_marine_autonomy#147, cube_bathymetry#57; adapted to the
// single fused draft grid in unh_marine_autonomy#221).
//
// Mirrors the bag_to_geotiff `-d` offline-projection chain (rosbag2
// SequentialReader -> tf2::BufferCore from /tf + /tf_static -> DetectionsProjector
// -> per-sounding lookupTransform("earth", frame_id, stamp) -> GeoSounding ->
// GeoMapSheet) but writes marine_bathymetry_store draft tiles instead of a GeoTIFF.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cube_bathymetry/angular_response_curve.h"
#include "cube_bathymetry/detections_projector.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/store_import.h"
#include "geometry_msgs/msg/point_stamped.hpp"
#include "marine_acoustic_msgs/msg/sonar_detections.hpp"
#include "marine_autonomy/gz4d_geo.h"
#include "nav_msgs/msg/odometry.hpp"
#include "marine_bathymetry_store/bathymetry_store.hpp"
#include "marine_bathymetry_store/registry.hpp"
#include "marine_bathymetry_store/tile_io.hpp"
#include "marine_mbes_backscatter_store/mbes_store.hpp"
#include "marine_mbes_backscatter_store/registry.hpp"
#include "rclcpp/rclcpp.hpp"
#include "rosbag2_transport/reader_writer_factory.hpp"
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "tf2_msgs/msg/tf_message.hpp"
#include "tf2/time.h"
#include "tf2_ros/buffer.h"

[[noreturn]] void usage()
{
  std::cout << "usage: import_bag [options] -o <store_dir> "
    "-d <detections_topic> <bag> [<bag> ...]\n";
  std::cout << "  --bathy-layer draft|processed: bathymetry store layer for this "
    "import (default processed: the off-boat CUBE re-run is authoritative; the live "
    "node writes draft. #85)\n";
  std::cout << "  -o <store_dir>: Output bathymetry-store directory (created if "
    "needed)\n";
  std::cout << "  --bs-store <dir>: Also write an MBES backscatter store layer "
    "(Processed) from the same CUBE pass (optional; surfaces the co-estimated "
    "intensity -- uncorrected by default, angle-corrected when "
    "--backscatter-correction empirical is set, cube#81)\n";
  std::cout << "  -d <detections_topic>: marine_acoustic_msgs/SonarDetections "
    "topic to replay through CUBE (required)\n";
  std::cout << "  --odom-topic <topic>: nav_msgs/Odometry topic for per-ping "
    "vessel speed-over-ground (optional; without it the speed-dependent "
    "horizontal-TPU terms are floored to 0)\n";
  std::cout << "  -r <meters>: Grid resolution (nominal; snapped to GGGS). "
    "Default 1.0\n";
  std::cout << "  --iho-order <order>: CUBE IHO order (default order1a)\n";
  std::cout << "  --backscatter-correction none|empirical: per-beam angular-response "
    "correction at node-output (default none = identity). 'empirical' subtracts the "
    "per-sonar curve from --backscatter-curve (cube#81)\n";
  std::cout << "  --backscatter-curve <file>: empirical angular-response curve CSV "
    "(abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir). Required for "
    "--backscatter-correction empirical; empty -> correction is a no-op\n";
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

// Speed-over-ground (m/s) nearest @p ns in the odometry timeline. Returns NaN
// when the timeline is empty OR the nearest sample is more than kSpeedMaxAgeNs
// away (an odom dropout) -- the error model then floors the speed-dependent
// horizontal-TPU terms to 0, the same as having no odometry at all, rather than
// applying an arbitrarily stale speed (cube_bathymetry#63 review).
float speedAt(const std::map<int64_t, double> & by_ns, int64_t ns)
{
  // Max staleness for a usable speed sample. Generous vs typical odom rates
  // (tens of Hz) so normal jitter never trips it; trips only on a real dropout.
  constexpr int64_t kSpeedMaxAgeNs = 5LL * 1000000000LL;
  if (by_ns.empty()) {
    return std::nanf("");
  }
  auto it = by_ns.lower_bound(ns);
  int64_t nearest_ns;
  double nearest_speed;
  if (it == by_ns.end()) {
    nearest_ns = std::prev(it)->first;
    nearest_speed = std::prev(it)->second;
  } else if (it == by_ns.begin()) {
    nearest_ns = it->first;
    nearest_speed = it->second;
  } else {
    auto prev = std::prev(it);
    const bool prev_closer = (ns - prev->first <= it->first - ns);
    nearest_ns = prev_closer ? prev->first : it->first;
    nearest_speed = prev_closer ? prev->second : it->second;
  }
  if (std::llabs(ns - nearest_ns) > kSpeedMaxAgeNs) {
    return std::nanf("");
  }
  return static_cast<float>(nearest_speed);
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
  std::string bs_store_dir;  // optional: MBES backscatter store output (#80)
  std::string bathy_layer_str = "processed";  // bathy target layer (#85): draft|processed
  std::string detections_topic;  // required
  std::string odom_topic;  // optional: nav_msgs/Odometry for per-ping vessel speed
  double resolution = 1.0;
  std::string iho_order = "order1a";
  int ping_count_limit = 0;
  // Backscatter angular-response correction (cube#81). Default none = identity.
  std::string backscatter_correction_str = "none";
  std::string backscatter_curve_file;

  // Registry provenance fields for the imported cells.
  marine_bathymetry_store::SourceRecord source_record;
  source_record.source_id = "cube-replay";

  // DetectionsProjector configuration for the offline path. Defaults match
  // detections_to_pointcloud's node defaults so a detections-only bag projects
  // the same way the live node would.
  cube::ProjectorParams projector_params;

  // Consume the value token following a value-taking flag, bounds-checked: a
  // flag given as the last argument (no value after it) would otherwise advance
  // `arg` past end() and dereference it (UB). usage() is [[noreturn]], so on the
  // missing-value error this never falls through to the ++arg/deref. `arg` is
  // declared before the lambda so the by-reference capture binds the loop cursor.
  auto arg = arguments.begin();
  auto next_value = [&](const char * flag) -> const std::string & {
      if (std::next(arg) == arguments.end()) {
        std::cerr << "error: option '" << flag << "' requires a value\n";
        usage();
      }
      ++arg;
      return *arg;
    };

  for (; arg != arguments.end(); arg++) {
    if (*arg == "-h") {
      usage();
    } else if (*arg == "-o") {
      store_dir = next_value("-o");
    } else if (*arg == "--bathy-layer") {
      bathy_layer_str = next_value("--bathy-layer");
    } else if (*arg == "--bs-store") {
      bs_store_dir = next_value("--bs-store");
    } else if (*arg == "-d") {
      detections_topic = next_value("-d");
    } else if (*arg == "--odom-topic") {
      odom_topic = next_value("--odom-topic");
    } else if (*arg == "-r") {
      resolution = std::stod(next_value("-r"));
    } else if (*arg == "--iho-order") {
      iho_order = next_value("--iho-order");
    } else if (*arg == "--backscatter-correction") {
      backscatter_correction_str = next_value("--backscatter-correction");
    } else if (*arg == "--backscatter-curve") {
      backscatter_curve_file = next_value("--backscatter-curve");
    } else if (*arg == "-l") {
      ping_count_limit = std::stoi(next_value("-l"));
    } else if (*arg == "--source-id") {
      source_record.source_id = next_value("--source-id");
    } else if (*arg == "--platform") {
      source_record.platform = next_value("--platform");
    } else if (*arg == "--sensor") {
      source_record.sensor = next_value("--sensor");
    } else if (*arg == "--sensor-class") {
      source_record.sensor_class = next_value("--sensor-class");
    } else if (*arg == "--campaign") {
      source_record.campaign = next_value("--campaign");
    } else if (*arg == "--base-link-frame") {
      projector_params.base_link_frame = next_value("--base-link-frame");
    } else if (*arg == "--level-frame") {
      projector_params.level_frame = next_value("--level-frame");
    } else if (*arg == "--tide-frame") {
      projector_params.tide_frame = next_value("--tide-frame");
    } else if (*arg == "--minimum-range") {
      projector_params.minimum_range = std::stod(next_value("--minimum-range"));
    } else if (*arg == "--maximum-range") {
      projector_params.maximum_range = std::stod(next_value("--maximum-range"));
    } else if (!arg->empty() && (*arg)[0] == '-' && *arg != "-") {
      // An unrecognized flag would otherwise be silently treated as a bag path
      // and fail later with a confusing "cannot open bag". Reject it up front.
      std::cerr << "error: unknown option '" << *arg << "'";
      if (*arg == "-e") {
        std::cerr << " -- the -e <epoch> argument was removed in "
          "cube_bathymetry#69; the store no longer uses per-day epochs, so the "
          "bag now imports into a single fused draft grid with no date label";
      }
      std::cerr << "\n";
      usage();
    } else {
      bagfile_names.push_back(*arg);
    }
  }

  if (store_dir.empty() || detections_topic.empty() || bagfile_names.empty()) {
    std::cerr << "error: -o <store_dir>, -d <detections_topic>, and "
      "at least one bag are all required\n";
    usage();
  }

  // Bathy target layer (#85). The off-boat CUBE re-run is the authoritative product
  // (the live node writes Draft), so this defaults to Processed; --bathy-layer draft
  // overrides (e.g. to seed a draft from a bag).
  marine_bathymetry_store::SourceLayer bathy_layer =
    marine_bathymetry_store::SourceLayer::Processed;
  if (bathy_layer_str == "draft") {
    bathy_layer = marine_bathymetry_store::SourceLayer::Draft;
  } else if (bathy_layer_str != "processed") {
    std::cerr << "error: --bathy-layer must be 'draft' or 'processed' (got '"
              << bathy_layer_str << "')\n";
    usage();
  }

  std::cout << "Detections topic: " << detections_topic
            << " (offline projection, vessel_speed = NaN)" << std::endl;
  std::cout << "Store dir: " << store_dir << std::endl;

  cube::DetectionsProjector projector(projector_params);

  // Accumulated offline-projection diagnostics, surfaced at the end so a
  // misconfigured-frames or over-tight-range run is diagnosable rather than a
  // silently sparse/empty import (the failure mode #43 exists to kill).
  size_t proj_pings = 0;
  size_t proj_soundings = 0;
  size_t proj_filtered_range = 0;
  size_t proj_missing_attitude = 0;
  size_t proj_missing_heave = 0;
  size_t proj_dropped_georef = 0;  // pings with no earth transform at their stamp

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

  // Bounded TF cache: the offline projection only needs the transforms
  // bracketing each ping's stamp, so a small rolling window keeps tf2's
  // std::list-backed TimeCache short. Whole-bag buffering made every
  // lookupTransform an O(n) linear list walk (~7 pings/s; cube_bathymetry#63).
  // The guard is how far the TF frontier must advance past a ping before we
  // project it, so both bracketing transforms are present (no frontier drops).
  constexpr double kCacheWindowSec = 60.0;
  constexpr double kGuardSec = 3.0;
  const int64_t kGuardNs = static_cast<int64_t>(kGuardSec * 1e9);

  auto clock = std::make_shared<rclcpp::Clock>();
  tf2_ros::Buffer tfBuffer(clock, tf2::durationFromSec(kCacheWindowSec));

  cube::GeoMapSheet geo_map_sheet(resolution, iho_order);
  std::cout << "requested resolution: " << resolution << " nominal used: "
            << geo_map_sheet.nominalCellSizeMeters() << std::endl;

  // Backscatter angular-response correction (cube#81). The setter must run AFTER
  // the sheet is constructed (its grids hold a const ref to the sheet Parameters).
  cube::BackscatterAngleCorrection backscatter_mode =
    cube::BackscatterAngleCorrection::None;
  if (!cube::parseBackscatterAngleCorrection(
      backscatter_correction_str, backscatter_mode))
  {
    std::cerr << "error: --backscatter-correction must be 'none' or 'empirical' "
              << "(got '" << backscatter_correction_str << "')\n";
    usage();
  }
  std::vector<std::pair<float, float>> backscatter_curve;
  if (backscatter_mode == cube::BackscatterAngleCorrection::Empirical &&
    !backscatter_curve_file.empty())
  {
    backscatter_curve = cube::loadAngularResponseCurve(backscatter_curve_file);
  }
  if (backscatter_mode == cube::BackscatterAngleCorrection::Empirical &&
    backscatter_curve.empty())
  {
    // Loud, not silent: enabled but no curve loaded -> correction is a no-op.
    std::cerr << "warning: --backscatter-correction empirical but no curve was "
      "loaded from --backscatter-curve '" << backscatter_curve_file
              << "' -- the correction is ENABLED but a NO-OP (intensity emitted "
      "uncorrected). Provide a valid curve CSV.\n";
  } else if (backscatter_mode == cube::BackscatterAngleCorrection::Empirical) {
    std::cout << "Backscatter angular-response correction: empirical, "
              << backscatter_curve.size() << "-point curve from "
              << backscatter_curve_file << std::endl;
  }
  geo_map_sheet.setBackscatterCorrection(
    backscatter_mode, std::move(backscatter_curve));

  std::cout << "reading messages..." << std::endl;

  int ping_count = 0;
  auto last_report_time = std::chrono::system_clock::now();
  const std::chrono::seconds report_interval(1);

  // SINGLE INTERLEAVED PASS over the chronological message stream. TF is fed
  // into the bounded-window buffer as it arrives; each detection waits in a
  // short FIFO until the TF frontier has advanced a guard interval past its
  // stamp, so its bracketing transforms (both sides) are present before we
  // project -- the correctness the old whole-bag 2-pass bought, but without the
  // O(n) lookup cost of a 100k-entry-per-frame TimeCache (cube_bathymetry#63).
  auto phase_tp = std::chrono::steady_clock::now();
  auto phase_secs = [&phase_tp]() {
      auto now = std::chrono::steady_clock::now();
      double s = std::chrono::duration<double>(now - phase_tp).count();
      phase_tp = now;
      return s;
    };

  const int64_t begin_ns = cell_timestamp_ns;
  const int64_t total_ns =
    std::chrono::duration_cast<std::chrono::nanoseconds>(total_duration).count();

  std::map<int64_t, double> speed_by_ns;
  size_t tf_count = 0;
  int64_t tf_frontier_ns = std::numeric_limits<int64_t>::min();

  // Detections awaiting their bracketing TF (ping stamp ns -> message).
  std::deque<std::pair<int64_t, marine_acoustic_msgs::msg::SonarDetections>> pending;

  bool limit_reached = false;

  // Project + georeference one ping into the GeoMapSheet.
  auto process_detection =
    [&](const marine_acoustic_msgs::msg::SonarDetections & detections, int64_t ping_ns) {
      // Real speed-over-ground from odom (Fix B) if available, else NaN -- the
      // error model floors the speed-dependent horizontal-TPU terms to 0 (Fix A).
      const float vessel_speed = speedAt(speed_by_ns, ping_ns);

      auto projection = projector.project(detections, tfBuffer, vessel_speed);
      ++proj_pings;
      proj_soundings += projection.soundings.size();
      proj_filtered_range += projection.diagnostics.filtered_range;
      proj_missing_attitude += projection.diagnostics.missing_attitude;
      proj_missing_heave += projection.diagnostics.missing_heave;

      try {
        auto transform = tfBuffer.lookupTransform(
          "earth", detections.header.frame_id, detections.header.stamp);

        // Per-ping geodetic reference: convert the sensor origin ECEF ->
        // lat/long ONCE (the single iterative ECEF->geodetic solve per ping),
        // then map each sounding's ECEF position through a local-ENU tangent
        // plane (a cheap 4x4 matvec) + a first-order radii-of-curvature
        // linearization. This collapses the per-sounding iterative solves into
        // one per ping. At swath scale (tens of metres about the reference) the
        // linearization error is well under a millimetre -- far below the GGGS
        // cell size and the soundings' own TPU.
        const auto & origin = transform.transform.translation;
        gz4d::GeoPointLatLongDegrees ref_ll(
          gz4d::GeoPointECEF(origin.x, origin.y, origin.z));
        gz4d::LocalENU enu(ref_ll);

        const double ref_lat_deg = ref_ll.latitude();
        const double ref_lon_deg = ref_ll.longitude();
        const double ref_height = ref_ll.altitude();
        constexpr double kDeg2Rad = M_PI / 180.0;
        constexpr double kRad2Deg = 180.0 / M_PI;
        constexpr double kA = 6378137.0;             // WGS84 semi-major axis
        constexpr double kF = 1.0 / 298.257223563;   // WGS84 flattening
        constexpr double kE2 = kF * (2.0 - kF);       // first eccentricity^2
        const double lat0_rad = ref_lat_deg * kDeg2Rad;
        const double sin_lat0 = std::sin(lat0_rad);
        const double cos_lat0 = std::cos(lat0_rad);
        const double w = std::sqrt(1.0 - kE2 * sin_lat0 * sin_lat0);
        const double prime_vertical = kA / w;                     // N(lat0)
        const double meridional = kA * (1.0 - kE2) / (w * w * w);  // M(lat0)

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

          // ECEF -> local ENU (East, North, Up), then linearize ENU -> geodetic
          // delta about the per-ping reference latitude.
          const gz4d::Point<double> local = enu.toLocal(gz4d::GeoPointECEF(
            sounding_ecef.point.x, sounding_ecef.point.y, sounding_ecef.point.z));
          const double lat_deg = ref_lat_deg + (local[1] / meridional) * kRad2Deg;
          const double lon_deg =
            ref_lon_deg + (local[0] / (prime_vertical * cos_lat0)) * kRad2Deg;
          const double height = ref_height + local[2];

          cube::GeoSounding gs(gz4d::GeoPointLatLongDegrees(lat_deg, lon_deg, height));
          gs.sounding.vertical_error = s.vertical_error;
          gs.sounding.horizontal_error = s.horizontal_error;
          // Carry the {raw intensity, beam angle} sufficient-stats pair so the
          // CUBE node co-estimates backscatter (#54) on the winning depth
          // hypothesis -- without this every beam has NaN intensity and the
          // backscatter store (--bs-store, #80) accumulates nothing. node.cpp
          // emits the value UNCORRECTED; the angle correction is cube#81.
          gs.sounding.intensity = s.intensity;
          gs.sounding.beam_angle = s.beam_angle;
          soundings.push_back(gs);
        }
        geo_map_sheet.addSoundings(soundings);
        ping_count++;
      } catch (const tf2::TransformException & e) {
        // A ping with no earth transform in the (bounded) buffer at its stamp --
        // e.g. before the first earth fix, or a TF gap wider than the cache
        // window. Counted (not just logged) so an empty or sparse import is
        // diagnosable rather than silently dropped.
        ++proj_dropped_georef;
        if (proj_dropped_georef <= 5) {
          std::cerr << "Transform Exception: " << e.what() << std::endl;
        }
      }
    };

  // Drain detections whose bracketing TF is now present (frontier advanced a
  // guard interval past the ping stamp). With flush=true, project whatever
  // remains at end-of-stream against the available coverage.
  auto drain_pending = [&](bool flush) {
      while (!pending.empty() && !limit_reached) {
        const int64_t ping_ns = pending.front().first;
        if (!flush && (tf_frontier_ns == std::numeric_limits<int64_t>::min() ||
          ping_ns > tf_frontier_ns - kGuardNs))
        {
          break;
        }
        process_detection(pending.front().second, ping_ns);
        pending.pop_front();
        if (ping_count_limit > 0 && ping_count >= ping_count_limit) {
          limit_reached = true;
        }
      }
    };

  std::cout << "projecting detections (single interleaved pass)..." << std::endl;
  for (auto message = bag_readers.next(); message && !limit_reached;
    message = bag_readers.next())
  {
    const bool is_tf = message->data_type == "tf2_msgs/msg/TFMessage";
    const bool is_odom = !odom_topic.empty() &&
      message->data_type == "nav_msgs/msg/Odometry" &&
      message->message->topic_name == odom_topic;
    const bool is_detection = message->data_type == "marine_acoustic_msgs/msg/SonarDetections" &&
      message->message->topic_name == detections_topic;

    if (is_tf) {
      const bool is_static = ends_with(message->message->topic_name, "/tf_static");
      if (!is_static && !ends_with(message->message->topic_name, "/tf")) {
        continue;
      }
      try {
        rclcpp::SerializedMessage sm(*message->message->serialized_data);
        tf2_msgs::msg::TFMessage tf_message;
        rclcpp::Serialization<tf2_msgs::msg::TFMessage>().deserialize_message(&sm, &tf_message);
        for (const auto & t : tf_message.transforms) {
          tfBuffer.setTransform(t, "", is_static);
          ++tf_count;
          if (!is_static) {
            const int64_t ns = static_cast<int64_t>(t.header.stamp.sec) * 1000000000LL +
              t.header.stamp.nanosec;
            tf_frontier_ns = std::max(tf_frontier_ns, ns);
          }
        }
      } catch (const std::exception & e) {
        std::cerr << e.what() << '\n';
      }
      drain_pending(false);
    } else if (is_odom) {
      try {
        rclcpp::SerializedMessage sm(*message->message->serialized_data);
        nav_msgs::msg::Odometry odom;
        rclcpp::Serialization<nav_msgs::msg::Odometry>().deserialize_message(&sm, &odom);
        const int64_t ns = static_cast<int64_t>(odom.header.stamp.sec) * 1000000000LL +
          odom.header.stamp.nanosec;
        speed_by_ns[ns] = std::hypot(odom.twist.twist.linear.x, odom.twist.twist.linear.y);
      } catch (const std::exception & e) {
        std::cerr << e.what() << '\n';
      }
    } else if (is_detection) {
      try {
        rclcpp::SerializedMessage sm(*message->message->serialized_data);
        marine_acoustic_msgs::msg::SonarDetections detections;
        rclcpp::Serialization<marine_acoustic_msgs::msg::SonarDetections>().deserialize_message(
          &sm, &detections);
        const int64_t ping_ns = static_cast<int64_t>(detections.header.stamp.sec) * 1000000000LL +
          detections.header.stamp.nanosec;
        pending.emplace_back(ping_ns, std::move(detections));
      } catch (const std::exception & e) {
        std::cerr << e.what() << '\n';
      }
    }

    auto now = std::chrono::system_clock::now();
    if (now >= last_report_time + report_interval && total_ns > 0 &&
      tf_frontier_ns != std::numeric_limits<int64_t>::min())
    {
      double progress = (tf_frontier_ns - begin_ns) / static_cast<double>(total_ns);
      std::cout << "\r  " << static_cast<int>(100 * progress) << "%  " << ping_count
                << " pings (" << proj_dropped_georef << " dropped, " << pending.size()
                << " pending)      " << std::flush;
      last_report_time = now;
    }
  }
  // Flush detections still pending at end-of-stream against available coverage.
  drain_pending(true);
  if (limit_reached) {
    std::cout << "\nPing count limit of " << ping_count_limit << " reached" << std::endl;
  }
  std::cout << "\n  buffered " << tf_count << " transforms";
  if (!odom_topic.empty()) {std::cout << ", " << speed_by_ns.size() << " odometry samples";}
  std::cout << "; projected " << ping_count << " pings in " << phase_secs() << "s." << std::endl;

  std::cout << "\ndone." << std::endl;
  std::cout << "Offline projection: " << proj_pings << " pings projected, "
            << ping_count << " georeferenced into the grid, " << proj_dropped_georef
            << " dropped (no earth TF); " << proj_soundings << " soundings ("
            << proj_filtered_range << " range-filtered, " << proj_missing_attitude
            << " missing attitude, " << proj_missing_heave << " missing heave)" << std::endl;
  if (proj_pings > 0 && proj_soundings == 0) {
    std::cerr << "WARNING: projected 0 soundings from " << proj_pings
              << " pings -- check the --*-frame overrides match the bag's "
              << "namespaced frames (see README 'Configuring frames per platform')."
              << std::endl;
  }
  if (ping_count == 0 && proj_dropped_georef > 0) {
    std::cerr << "WARNING: every ping was dropped for lack of an earth transform -- "
              << "check that the bag has a localization chain to the 'earth' frame."
              << std::endl;
  }

  std::cout << "Building store tiles..." << std::endl;

  // The GeoMapSheet picks a GGGS level from the requested cell size; build the
  // store at the matching default level. importTiles is multi-level, so the
  // GridIndex on each tile carries the authoritative level regardless.
  marine_bathymetry_store::BathymetryStore store =
    marine_bathymetry_store::BathymetryStore::fromCellSize(
    static_cast<float>(geo_map_sheet.nominalCellSizeMeters()));

  marine_bathymetry_store::SourceRegistry registry;
  const uint16_t source_index = registry.registerSource(source_record);

  auto tiles = cube::mapSheetToTiles(geo_map_sheet, cell_timestamp_ns, source_index);
  std::cout << "Tiles with data: " << tiles.size() << " (build: " << phase_secs() << "s)"
            << std::endl;

  if (tiles.empty()) {
    std::cerr << "WARNING: no tiles had finite data -- nothing imported. Check "
      "the projector frame overrides and the detections topic." << std::endl;
  }

  // The off-boat full-bag CUBE replay is the authoritative product, so by default it
  // lands in the `Processed` layer (#85; --bathy-layer overrides). The live node
  // writes the `Draft` layer instead. Single fused grid per layer
  // (unh_marine_autonomy#221 — no per-day epochs, newest value wins per cell).
  store.importTiles(bathy_layer, std::move(tiles));

  std::size_t written = marine_bathymetry_store::save(store, store_dir, &registry);
  std::cout << "Saved " << written << " tiles to " << store_dir << " ("
            << bathy_layer_str << " layer)." << std::endl;

  // Optional: surface the co-estimated backscatter into an MBES backscatter store
  // layer from the SAME CUBE pass (#80), written to the Processed layer (the
  // off-boat CUBE re-run is the authoritative product). By default the value is
  // UNCORRECTED; --backscatter-correction empirical (with a --backscatter-curve)
  // applies the per-beam angular-response correction at node-output (cube#81),
  // which corrects both this offline layer and the live tile (#78). Bathy (above)
  // defaults to the same Processed layer (#85).
  if (!bs_store_dir.empty()) {
    std::cout << "Building backscatter store tiles..." << std::endl;

    // Match the bathy store's GGGS level so both products tile identically.
    marine_mbes_backscatter_store::MbesBackscatterStore bs_store =
      marine_mbes_backscatter_store::MbesBackscatterStore::fromCellSize(
      static_cast<float>(geo_map_sheet.nominalCellSizeMeters()));

    // Provenance: register the same physical source in the backscatter registry,
    // tagged with the backscatter sensor class. Every cell carries this index +
    // the import timestamp, so the Processed product is not source/time-blank.
    marine_mbes_backscatter_store::SourceRegistry bs_registry;
    marine_mbes_backscatter_store::SourceRecord bs_source_record;
    bs_source_record.source_id = source_record.source_id;
    bs_source_record.platform = source_record.platform;
    bs_source_record.sensor = source_record.sensor;
    bs_source_record.sensor_class = "mbes-backscatter";
    bs_source_record.campaign = source_record.campaign;
    const uint16_t bs_source_index = bs_registry.registerSource(bs_source_record);

    // Surface the co-estimated intensity cell-by-cell (no bulk-import API is added
    // to the separate marine_mbes_backscatter_store package; #80 stays in-repo).
    const std::map<gggs::CellIndex, marine_mbes_backscatter_store::MbesCell>
    bs_cells = cube::mapSheetToBackscatterCells(
      geo_map_sheet, cell_timestamp_ns, bs_source_index);
    std::cout << "Backscatter cells with data: " << bs_cells.size()
              << " (build: " << phase_secs() << "s)" << std::endl;

    if (bs_cells.empty()) {
      std::cerr << "WARNING: no cells had finite backscatter -- nothing written to "
        "the backscatter store. Check that the detections carry intensities."
                << std::endl;
    }

    for (const auto & cell : bs_cells) {
      bs_store.set(
        marine_mbes_backscatter_store::SourceLayer::Processed, cell.first, cell.second);
    }

    std::size_t bs_written =
      marine_mbes_backscatter_store::save(bs_store, bs_store_dir, &bs_registry);
    std::cout << "Saved " << bs_written << " backscatter tiles to " << bs_store_dir
              << "." << std::endl;
  }

  std::cout << "done!" << std::endl;
  return 0;
}
