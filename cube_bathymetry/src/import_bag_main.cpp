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
#include <cstddef>
#include <deque>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cube_bathymetry/angular_response_curve.h"
#include "cube_bathymetry/detections_projector.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/store_import.h"
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
#include "cube_bathymetry/sonar_info_curve.h"
#include "marine_interfaces/msg/sonar_info.hpp"

namespace
{

/// Default sonar_info topic beside the detections topic (#102): replace the
/// last path element, e.g. /bizzy/sensors/m3/detections ->
/// /bizzy/sensors/m3/sonar_info. A topic with no '/' just becomes
/// "sonar_info" (same namespace).
std::string deriveSonarInfoTopic(const std::string & detections_topic)
{
  const auto slash = detections_topic.rfind('/');
  if (slash == std::string::npos) {
    return "sonar_info";
  }
  return detections_topic.substr(0, slash + 1) + "sonar_info";
}

/// Pre-pass over the bags' sonar_info topic for the FIRST valid
/// angular-response curve (latch-first -- heartbeats republish the same
/// message; a mid-survey curve change is not adopted, matching the live
/// node). Returns false with `last_reject` set when non-empty curves were
/// seen but all rejected; false with it empty when the topic carried no
/// curve at all (normal for pre-SonarInfo bags). Open errors are ignored
/// here -- the main pass reports them.
bool loadCurveFromBagSonarInfo(
  const std::vector<std::string> & bags, const std::string & topic,
  cube::AngularResponseCurve & out, std::string & last_reject)
{
  rclcpp::Serialization<marine_interfaces::msg::SonarInfo> serialization;
  for (const auto & bag : bags) {
    rosbag2_storage::StorageOptions storage_options;
    storage_options.uri = bag;
    auto reader = rosbag2_transport::ReaderWriterFactory::make_reader(storage_options);
    try {
      reader->open(storage_options);
    } catch (const std::exception &) {
      continue;
    }
    rosbag2_storage::StorageFilter filter;
    filter.topics = {topic};
    reader->set_filter(filter);
    while (reader->has_next()) {
      const auto message = reader->read_next();
      rclcpp::SerializedMessage sm(*message->serialized_data);
      marine_interfaces::msg::SonarInfo info;
      try {
        serialization.deserialize_message(&sm, &info);
      } catch (const std::exception &) {
        continue;  // corrupt record: skip, keep scanning
      }
      std::string reject;
      if (cube::curveFromSonarInfo(info, out, reject)) {
        return true;
      }
      if (!info.angular_response_angle_deg.empty() ||
        !info.angular_response_db_rel_nadir.empty())
      {
        last_reject = reject;
      }
    }
  }
  return false;
}

}  // namespace
#include "tf2_geometry_msgs/tf2_geometry_msgs.hpp"
#include "tf2_msgs/msg/tf_message.hpp"
#include "tf2/time.h"
#include "tf2_ros/buffer.h"
#include "kdl/frames.hpp"  // KDL::Frame/Vector for the per-ping transform hoist

[[noreturn]] void usage()
{
  std::cout << "usage: import_bag [options] -o <store_dir> "
    "-d <detections_topic> <bag> [<bag> ...]\n";
  std::cout << "  -o <store_dir>: Output bathymetry-store directory (created if "
    "needed). The off-boat CUBE re-run is authoritative, so it always writes the "
    "`survey` layer (uma#248 collapsed the draft/processed split into one)\n";
  std::cout << "  --reference-store <store_dir>: seed the CUBE predicted surface "
    "lazily, per tile on first touch, from BOTH of this store's prior layers -- "
    "`chart` (official chart/ENC products) and `reference` (prior/contour) -- so "
    "blunder rejection drops false-deep detections (#89, #96, #119). `chart` primes "
    "first and `reference` overwrites where both cover a cell. Tiles at the survey "
    "GGGS level gate cell-for-cell; a coarser (multi-level) prior gates via a "
    "level-walk fallback that resamples the finest CONTAINING coarser tile holding "
    "data -- since #137 for BOTH layers, which matters because an ENC product is "
    "built on the chart scale ladder and essentially never has a survey-level tile. "
    "Predicted-only: the coarse prior is NEVER settled as "
    "measured data and seeds no backscatter. NOTE: a coarse/shallow-biased prior "
    "can also reject LEGITIMATE deeper-than-charted returns; the rejection margin "
    "is tunable via the blunder_* params. (A `survey` tile already in -o takes "
    "precedence and is warm-started as measured data instead.)\n";
  std::cout << "  --bs-store <dir>: Also write an MBES backscatter store `survey` "
    "layer from the same CUBE pass (optional; surfaces the co-estimated "
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
  std::cout << "  --sonar-info-topic <topic>: SonarInfo topic scanned for the "
    "angular-response curve (auto mode, cube#102). Default: the detections "
    "topic's sibling 'sonar_info'. An explicit --backscatter-curve wins.\n";
  std::cout << "  --backscatter-correction none|empirical|auto: per-beam angular-response "
    "correction at node-output (default auto = a valid curve in the bag's "
    "SonarInfo enables it; no curve = identity, cube#102). 'empirical' subtracts the "
    "per-sonar curve from --backscatter-curve (cube#81)\n";
  std::cout << "  --backscatter-curve <file>: empirical angular-response curve CSV "
    "(abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir). Required for "
    "--backscatter-correction empirical; empty -> correction is a no-op. A tier-2 "
    "curve (header '# tl_removed: true' + '# absorption_db_per_m: <a>') also makes "
    "the estimator remove per-beam 2-way TL 40*log10(R)+2*alpha*R (cube#87)\n";
  std::cout << "  --max-resident-tiles <N>: bound resident-tile RAM (cube#92). When "
    "the in-memory GGGS tile count exceeds N, the coldest tiles are persisted to "
    "the -o store (and their backscatter to --bs-store), their per-cell intensity "
    "Welford spilled to a temp scratch, and dropped from RAM. A revisit reloads the "
    "tile (settled depth + restored Welford) BEFORE the new soundings, so "
    "backscatter blends LOSSLESSLY with the pre-eviction beams; only the bathy "
    "depth UNCERTAINTY is re-derived (the depth value stays faithful). Default 256 "
    "(generous; disk is local/fast offline). 0 = unbounded (whole survey in RAM). "
    "NOTE: per-cell intensity memory is O(1) regardless of beam count (cube#93), so "
    "a small heavily-oversampled survey no longer OOMs even without eviction\n";
  std::cout << "  -l <count>: Stop after this many pings (debugging)\n";
  std::cout << "  --platform / --sensor / --campaign <str>: store-level provenance "
    "written once to <store>/registry.json (uma#248 StoreMetadata; --campaign maps "
    "to the survey/campaign id). Per-cell source interning was retired for the "
    "single-platform deployment\n";
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
      const std::unordered_map<std::string, std::string> & topic_types)
    : message(message)
    {
      // Look the type up in the bag's cached topic->type map (built once per bag
      // in Bag::open). Previously this called reader.get_all_topics_and_types()
      // -- which walks and copies the whole metadata topic list -- for EVERY
      // message popped (cube#107).
      auto it = topic_types.find(message->topic_name);
      if (it != topic_types.end()) {
        data_type = it->second;
      }
    }
  };

  BagReaders(
    const std::vector<std::string> & bagfile_names,
    const std::vector<std::string> & filter_topics)
  {
    for (const auto & bagfile_name : bagfile_names) {
      readers_[bagfile_name].open(bagfile_name, filter_topics);
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
    // topic -> type for this bag, built once at open (cube#107).
    std::unordered_map<std::string, std::string> topic_types_;

    void open(
      const std::string & file_name,
      const std::vector<std::string> & filter_topics)
    {
      rosbag2_storage::StorageOptions storage_options;
      storage_options.uri = file_name;
      reader = rosbag2_transport::ReaderWriterFactory::make_reader(storage_options);
      reader->open(storage_options);
      for (const auto & topic_info : reader->get_all_topics_and_types()) {
        topic_types_[topic_info.name] = topic_info.type;
      }
      // Restrict reads to the topics the import consumes: the requested exact
      // topics (detections / odom) plus every /tf and /tf_static topic present
      // in THIS bag. The tf topics are matched by namespaced suffix because the
      // exact names (e.g. /bizzy/tf, /bizzy/tf_static) can't be predicted; the
      // filter is built from the bag's real topic list so transient-local
      // /tf_static survives. This skips the sidescan imagery that is the bulk of
      // bag bytes (cube#107). Set only when non-empty -- an empty StorageFilter
      // means "read everything" in rosbag2, the correct fallback for a bag that
      // has none of the wanted topics (it contributes nothing to the import).
      rosbag2_storage::StorageFilter filter;
      for (const auto & entry : topic_types_) {
        const std::string & name = entry.first;
        if (ends_with(name, "/tf") || ends_with(name, "/tf_static") ||
          std::find(filter_topics.begin(), filter_topics.end(), name) !=
          filter_topics.end())
        {
          filter.topics.push_back(name);
        }
      }
      if (!filter.topics.empty()) {
        reader->set_filter(filter);
      }
      if (reader->has_next()) {
        next_message = std::make_shared<Message>(reader->read_next(), topic_types_);
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
        next_message = std::make_shared<Message>(reader->read_next(), topic_types_);
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
  // optional: reference-prior store to seed the predicted surface lazily (#89, #96)
  std::string reference_store_dir;
  std::string bs_store_dir;  // optional: MBES backscatter store output (#80)
  std::string detections_topic;  // required
  std::string odom_topic;  // optional: nav_msgs/Odometry for per-ping vessel speed
  double resolution = 1.0;
  std::string iho_order = "order1a";
  int ping_count_limit = 0;
  // Bounded-RAM eviction budget (cube#92). Default 256 resident tiles: generous
  // for offline (each ~960x960-cell GeoGrid + CUBE state is the heavy object), so
  // small/medium surveys never evict (identical output to the old path) while a
  // large multi-day survey stays bounded. 0 = unbounded (old behavior).
  std::size_t max_resident_tiles = 256;
  // Backscatter angular-response correction (cube#81, #102). Default auto:
  // a valid curve in the bag's SonarInfo enables it; none in the bag =
  // identity, exactly the old default for pre-SonarInfo bags.
  std::string backscatter_correction_str = "auto";
  std::string backscatter_curve_file;
  std::string sonar_info_topic;  // default: derived from detections_topic

  // Store-level provenance (uma#248 StoreMetadata, written once at finalize).
  marine_bathymetry_store::StoreMetadata store_metadata;

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

  // Guarded numeric parses: std::stod/std::stoi/std::stoll THROW on a non-numeric value
  // and, uncaught in main, would std::terminate the process with an opaque message.
  // Catch and route to usage() with a clear diagnostic instead (mirrors the sibling
  // batch_regen_main). usage() is [[noreturn]], so these never fall through.
  auto parse_double = [&](const char * flag, const std::string & value) -> double {
      try {
        std::size_t consumed = 0;
        const double parsed = std::stod(value, &consumed);
        if (consumed != value.size()) {
          throw std::invalid_argument("trailing characters");
        }
        return parsed;
      } catch (const std::exception &) {
        std::cerr << "error: option '" << flag << "' expects a number, got '"
                  << value << "'\n";
        usage();
      }
    };
  auto parse_int = [&](const char * flag, const std::string & value) -> int {
      try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(value, &consumed);
        if (consumed != value.size()) {
          throw std::invalid_argument("trailing characters");
        }
        return parsed;
      } catch (const std::exception &) {
        std::cerr << "error: option '" << flag << "' expects an integer, got '"
                  << value << "'\n";
        usage();
      }
    };
  auto parse_long = [&](const char * flag, const std::string & value) -> int64_t {
      try {
        std::size_t consumed = 0;
        const int64_t parsed = std::stoll(value, &consumed);
        if (consumed != value.size()) {
          throw std::invalid_argument("trailing characters");
        }
        return parsed;
      } catch (const std::exception &) {
        std::cerr << "error: option '" << flag << "' expects an integer, got '"
                  << value << "'\n";
        usage();
      }
    };

  for (; arg != arguments.end(); arg++) {
    if (*arg == "-h") {
      usage();
    } else if (*arg == "-o") {
      store_dir = next_value("-o");
    } else if (*arg == "--reference-store") {
      reference_store_dir = next_value("--reference-store");
    } else if (*arg == "--bs-store") {
      bs_store_dir = next_value("--bs-store");
    } else if (*arg == "-d") {
      detections_topic = next_value("-d");
    } else if (*arg == "--odom-topic") {
      odom_topic = next_value("--odom-topic");
    } else if (*arg == "-r") {
      resolution = parse_double("-r", next_value("-r"));
    } else if (*arg == "--iho-order") {
      iho_order = next_value("--iho-order");
    } else if (*arg == "--backscatter-correction") {
      backscatter_correction_str = next_value("--backscatter-correction");
    } else if (*arg == "--backscatter-curve") {
      backscatter_curve_file = next_value("--backscatter-curve");
    } else if (*arg == "--sonar-info-topic") {
      sonar_info_topic = next_value("--sonar-info-topic");
    } else if (*arg == "--max-resident-tiles") {
      const int64_t v = parse_long("--max-resident-tiles", next_value("--max-resident-tiles"));
      if (v < 0) {
        std::cerr << "error: --max-resident-tiles must be >= 0 (0 = unbounded)\n";
        usage();
      }
      max_resident_tiles = static_cast<std::size_t>(v);
    } else if (*arg == "-l") {
      ping_count_limit = parse_int("-l", next_value("-l"));
    } else if (*arg == "--platform") {
      store_metadata.platform = next_value("--platform");
    } else if (*arg == "--sensor") {
      store_metadata.sensor = next_value("--sensor");
    } else if (*arg == "--campaign") {
      store_metadata.survey = next_value("--campaign");
    } else if (*arg == "--base-link-frame") {
      projector_params.base_link_frame = next_value("--base-link-frame");
    } else if (*arg == "--level-frame") {
      projector_params.level_frame = next_value("--level-frame");
    } else if (*arg == "--tide-frame") {
      projector_params.tide_frame = next_value("--tide-frame");
    } else if (*arg == "--minimum-range") {
      projector_params.minimum_range = parse_double("--minimum-range",
        next_value("--minimum-range"));
    } else if (*arg == "--maximum-range") {
      projector_params.maximum_range = parse_double("--maximum-range",
        next_value("--maximum-range"));
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

  // Main-pass read filter (cube#107): restrict each reader to the topics the
  // projection consumes. /tf and /tf_static are added per-bag inside Bag::open
  // by namespaced suffix; here we pass the exact detections/odom topic names.
  std::vector<std::string> filter_topics;
  filter_topics.push_back(detections_topic);
  if (!odom_topic.empty()) {
    filter_topics.push_back(odom_topic);
  }
  BagReaders bag_readers(bagfile_names, filter_topics);

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
    std::cerr << "error: --backscatter-correction must be 'none', 'empirical' "
              << "or 'auto' (got '" << backscatter_correction_str << "')\n";
    usage();
  }
  cube::AngularResponseCurve backscatter_curve;
  std::string backscatter_curve_source = backscatter_curve_file;
  if (backscatter_mode == cube::BackscatterAngleCorrection::None) {
    std::cout << "--backscatter-correction none: any SonarInfo "
      "angular-response curve in the bag will be ignored." << std::endl;
  } else if (!backscatter_curve_file.empty()) {
    // Explicit file wins over SonarInfo (the reprocessing override, #102).
    backscatter_curve = cube::loadAngularResponseCurveWithHeader(backscatter_curve_file);
    if (backscatter_curve.points.empty()) {
      // Loud in EVERY mode: the explicit file also suppresses the SonarInfo
      // pre-pass (it stays the operator's chosen source), so a failed load
      // must never vanish silently (#102 r1).
      std::cerr << "warning: --backscatter-curve '" << backscatter_curve_file
                << "' yielded an EMPTY curve (missing/unparseable) -- no "
        "correction from it, and the bag's SonarInfo curves stay IGNORED "
        "because an explicit file was given. Fix or drop the flag.\n";
    }
  } else {
    // SonarInfo pre-pass (#102): scan the bags' sonar_info topic for the
    // first valid curve (latch-first; heartbeats republish the same one).
    const std::string topic = sonar_info_topic.empty() ?
      deriveSonarInfoTopic(detections_topic) : sonar_info_topic;
    std::string last_reject;
    if (loadCurveFromBagSonarInfo(
        bagfile_names, topic, backscatter_curve, last_reject))
    {
      backscatter_curve_source = "SonarInfo topic '" + topic + "'";
    } else if (!last_reject.empty()) {
      std::cerr << "warning: SonarInfo on '" << topic
                << "' carried an angular-response curve, but it was rejected: "
                << last_reject << "\n";
    }
  }
  if (backscatter_mode == cube::BackscatterAngleCorrection::Empirical &&
    backscatter_curve.points.empty())
  {
    // Loud, not silent: explicitly enabled but no curve loaded -> no-op.
    // (auto with no curve is quiet by design: identity is its fallback.)
    std::cerr << "warning: --backscatter-correction empirical but no curve was "
      "loaded from --backscatter-curve '" << backscatter_curve_file
              << "' or the bag's SonarInfo -- the correction is ENABLED but a "
      "NO-OP (intensity emitted uncorrected). Provide a valid curve CSV.\n";
  } else if (!backscatter_curve.points.empty()) {
    std::cout << "Backscatter angular-response correction: "
              << backscatter_curve.points.size() << "-point curve from "
              << backscatter_curve_source;
    if (backscatter_curve.tl_removed) {
      // tier-2 (cube#87): the curve is a TL-removed residual; the estimator
      // also removes 40*log10(R) + 2*alpha*R per beam.
      std::cout << " [tier-2: TL-removed, alpha="
                << backscatter_curve.absorption_db_per_m << " dB/m]";
    }
    std::cout << std::endl;
  }
  geo_map_sheet.setBackscatterCorrection(
    backscatter_mode, std::move(backscatter_curve.points),
    backscatter_curve.tl_removed, backscatter_curve.absorption_db_per_m);

  // Reference-prior seeding (#89, #96) is now LAZY, per tile on first touch, driven
  // by the accumulator's seedNewTile (see ImportAccumulatorConfig::reference_store_dir
  // below): a `reference/` tile primes the CUBE predicted surface only (seed_settled=
  // false) so the blunder gate turns on WITHOUT settling coarse prior depths as
  // measured data. A reference tile at the survey GGGS level gates cell-for-cell; a
  // COARSER reference tile (a multi-level prior) gates via the seedNewTile level-walk
  // fallback, which resamples the finest coarser tile onto the fine survey cells
  // (#115 -- before that fix a coarser prior was a silent no-op and the gate stayed
  // off). The pre-#96 upfront whole-store loadIntoSheet was
  // removed: it defeated the bounded-RAM eviction by loading the entire prior into
  // the sheet at once.

  // Store-level provenance (uma#248 StoreMetadata) + bounded-RAM accumulator
  // (cube#92). The accumulator owns the persist-then-drop eviction + lossless
  // reload-on-revisit that bounds resident RAM by tile COUNT (not surveyed AREA)
  // and the two-rung seed precedence (#96). registry.json is written once at
  // finalize(). Backscatter provenance mirrors the bathy platform/sensor with an
  // MBES-specific calibration ref (empty until a beam-pattern calibration exists).
  marine_mbes_backscatter_store::StoreMetadata bs_metadata;
  bs_metadata.platform = store_metadata.platform;
  bs_metadata.sensor = store_metadata.sensor;
  bs_metadata.survey = store_metadata.survey;
  bs_metadata.date = store_metadata.date;

  cube::ImportAccumulatorConfig accumulator_config;
  accumulator_config.store_dir = store_dir;
  accumulator_config.reference_store_dir = reference_store_dir;
  // Match the store level to the sheet's actual (GGGS-snapped) cell size so the
  // reload/seed/merge scratch stores tile identically.
  accumulator_config.cell_size_m =
    static_cast<float>(geo_map_sheet.nominalCellSizeMeters());
  accumulator_config.bs_store_dir = bs_store_dir;
  accumulator_config.max_resident_tiles = max_resident_tiles;
  cube::ImportAccumulator accumulator(geo_map_sheet, accumulator_config);

  if (!reference_store_dir.empty()) {
    std::cout << "Prior seeding (chart + reference layers) from "
              << reference_store_dir
              << " (lazy per-tile; predicted-only blunder gate, #96). A run that "
      "primes nothing WARNS at the end (#137) -- this banner alone does not mean "
      "the gate engaged." << std::endl;
  }

  if (max_resident_tiles > 0) {
    std::cout << "Bounded resident tiles: " << max_resident_tiles
              << " (cold tiles persist to the -o store and drop from RAM; "
      "revisits reload losslessly, cube#92)." << std::endl;
  } else {
    std::cout << "Unbounded resident tiles (--max-resident-tiles 0): the whole "
      "survey stays in RAM." << std::endl;
  }

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

  // Speed-over-ground samples, pruned to a rolling window as pings drain (see
  // drain_pending) so the map stays bounded across a long multi-bag run instead
  // of retaining every odom sample for the whole survey (cube#107).
  std::map<int64_t, double> speed_by_ns;
  // Total odom samples ingested; speed_by_ns is pruned, so its size() no longer
  // reflects the total for the end-of-run diagnostic.
  size_t odom_samples_total = 0;
  // Drop speed samples older than this behind the drain frontier. Generously
  // larger than speedAt's kSpeedMaxAgeNs (5 s) staleness gate, so a prune can
  // never remove a sample speedAt would still pick as the nearest for a current
  // or future (chronological) ping -- the pruning is output-neutral.
  constexpr int64_t kSpeedStalenessNs = 30LL * 1000000000LL;
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

        // Hoist the quaternion->matrix conversion out of the per-sounding loop:
        // tf2::doTransform(PointStamped, ...) rebuilds the KDL::Frame (quaternion
        // -> rotation matrix) from `transform` for EVERY sounding. Build it once
        // per ping and apply the frame as a matvec. Bit-identical to the
        // per-sounding doTransform -- the exact same KDL::Frame * KDL::Vector,
        // just hoisted (cube#107). This MUST stay on the KDL path
        // (tf2::gmTransformToKDL), NOT a tf2::Transform matvec: doTransform for a
        // point is KDL-based, and a different rotation build would perturb the
        // ECEF output in its low bits and shift boundary soundings between cells.
        const KDL::Frame ping_frame = tf2::gmTransformToKDL(transform);

        std::vector<cube::GeoSounding> soundings;
        soundings.reserve(projection.soundings.size());
        for (const auto & s : projection.soundings) {
          const KDL::Vector sounding_ecef = ping_frame * KDL::Vector(
            s.sonar_relative_position.x,
            s.sonar_relative_position.y,
            s.sonar_relative_position.z);

          // ECEF -> local ENU (East, North, Up), then linearize ENU -> geodetic
          // delta about the per-ping reference latitude.
          const gz4d::Point<double> local = enu.toLocal(gz4d::GeoPointECEF(
            sounding_ecef.x(), sounding_ecef.y(), sounding_ecef.z()));
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
          // Per-beam slant range R = twtt*c/2 (set in the Sounding detections
          // ctor) for the tier-2 TL correction (cube#87).
          gs.sounding.slant_range = s.slant_range;
          soundings.push_back(gs);
        }
        // Accumulate through the bounded-RAM accumulator (cube#92): adds the
        // batch, reloads any evicted tile this ping revisits, then evicts cold
        // tiles back to the budget. With --max-resident-tiles 0 this is a plain
        // addSoundings (no eviction).
        accumulator.addBatch(soundings);
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
      int64_t last_drained_ns = std::numeric_limits<int64_t>::min();
      while (!pending.empty() && !limit_reached) {
        const int64_t ping_ns = pending.front().first;
        if (!flush && (tf_frontier_ns == std::numeric_limits<int64_t>::min() ||
          ping_ns > tf_frontier_ns - kGuardNs))
        {
          break;
        }
        process_detection(pending.front().second, ping_ns);
        last_drained_ns = ping_ns;
        pending.pop_front();
        if (ping_count_limit > 0 && ping_count >= ping_count_limit) {
          limit_reached = true;
        }
      }
      // Prune speed samples that can no longer be the nearest for any current or
      // future ping (pings drain chronologically, and speedAt only ever looks
      // within kSpeedMaxAgeNs of a ping). Bounds speed_by_ns across a multi-bag
      // run without affecting output (cube#107).
      if (last_drained_ns != std::numeric_limits<int64_t>::min()) {
        const int64_t cutoff = last_drained_ns - kSpeedStalenessNs;
        speed_by_ns.erase(speed_by_ns.begin(), speed_by_ns.lower_bound(cutoff));
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
        ++odom_samples_total;
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
  if (!odom_topic.empty()) {std::cout << ", " << odom_samples_total << " odometry samples";}
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

  // Persist the still-resident tiles and write the store-level metadata. Tiles
  // evicted during the pass were already written to disk (bathy in the -o store,
  // their backscatter to --bs-store); finalize() writes whatever is still in RAM,
  // so the on-disk store is the union of evicted + resident — identical to an
  // unbounded build (cube#92). The off-boat full-bag CUBE replay is the
  // authoritative product, so it always writes the `survey` layer (uma#248
  // collapsed draft/processed). Single fused grid per layer (uma#221).
  const std::size_t resident_before_final = accumulator.residentTileCount();
  const std::size_t evicted_count = accumulator.evictedIndices().size();
  accumulator.finalize(
    store_metadata.empty() ? nullptr : &store_metadata,
    (bs_store_dir.empty() || bs_metadata.empty()) ? nullptr : &bs_metadata);
  std::cout << "Persisted " << accumulator.bathyTilesPersisted()
            << " bathy tile(s) to " << store_dir << " (survey layer; "
            << evicted_count << " evicted mid-pass, "
            << resident_before_final << " resident at end; build: "
            << phase_secs() << "s)." << std::endl;

  if (accumulator.bathyTilesPersisted() == 0) {
    std::cerr << "WARNING: no tiles had finite data -- nothing imported. Check "
      "the projector frame overrides and the detections topic." << std::endl;
  }

  // The co-estimated backscatter was surfaced into the --bs-store layer (#80) from
  // the SAME CUBE pass, incrementally under eviction (newest-finite-wins merge,
  // cube#92). By default UNCORRECTED; --backscatter-correction empirical applies
  // the per-beam angular-response correction at node-output (cube#81).
  if (!bs_store_dir.empty()) {
    std::cout << "Persisted " << accumulator.backscatterTilesPersisted()
              << " backscatter tile(s) to " << bs_store_dir << "." << std::endl;
    if (accumulator.backscatterTilesPersisted() == 0) {
      std::cerr << "WARNING: no cells had finite backscatter -- nothing written to "
        "the backscatter store. Check that the detections carry intensities."
                << std::endl;
    }
  }

  std::cout << "done!" << std::endl;
  return 0;
}
