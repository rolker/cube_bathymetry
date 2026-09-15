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

#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cube_bathymetry/angular_response_curve.h"
#include "cube_bathymetry/coverage_refresh.h"
#include "cube_bathymetry/detections_projector.h"
#include "cube_bathymetry/projection_summary.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/quantize_tile.h"
#include "cube_bathymetry/build_fingerprint.h"
#include "cube_bathymetry/level_plan.h"
#include "cube_bathymetry/multi_level_accumulator.h"
#include "cube_bathymetry/recon.h"
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
#include "marine_interfaces/msg/sonar_visualization_tile.hpp"
#include "rclcpp/serialization.hpp"

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
    "lazily, per tile on first touch, from this store's `reference` (prior) layer "
    "so blunder rejection drops false-deep detections (#89, #96). Tiles at the survey "
    "GGGS level gate cell-for-cell; a coarser (multi-level) prior gates via a "
    "level-walk fallback that resamples the finest coarser tile (#115). Predicted-"
    "only: the coarse prior is NEVER settled as "
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
  std::cout << "  --tile-size-report <csv>: measure the live coverage stream's "
    "per-message size instead of guessing it (cube_bathymetry#112). Every "
    "--tile-report-interval seconds of BAG time, each dirty tile is quantized "
    "BOTH ways -- whole tile and dirty sub-window -- from the same dirty set, "
    "serialized, and compressed with zlib at the default level, which is what "
    "udp_bridge does to every packet before fragmenting it (packet.cpp). The "
    "compressed column is the one that matters: a whole tile is mostly nodata, "
    "and nodata compresses to almost nothing, so the cell-count saving and the "
    "wire saving are NOT the same number. Writes one CSV row per tile per "
    "cycle; the dirty set is then cleared exactly as the node clears it.\n";
  std::cout << "  --tile-report-interval <seconds>: report cadence, default 5.0 "
    "to match the live node's own coverage publish cadence.\n";
  std::cout << "  --tile-refresh-interval <seconds>: the whole-tile refresh "
    "interval the report MODELS (default 60.0, the node's own default for "
    "subwindow_refresh_interval). The node does not send every dirty tile as a "
    "sub-window -- a tile whose last whole send has aged past this goes out "
    "whole, and a tile that has gone quiet still owing one is re-sent whole by "
    "the refresh drain. Both are real traffic and the second is the dominant "
    "cost at 60 s, so the source,sent,sent_serialized,sent_compressed columns "
    "are what the boat would actually put on the wire; the full_* and window_* "
    "columns remain the two extremes for comparison. 0 models the heal off.\n";
  std::cout << "  --tile-refresh-budget <tiles>: whole tiles the modelled "
    "refresh drain may re-send per cycle (default 2, the node's own default "
    "for subwindow_refresh_tiles_per_cycle). 0 models the heal off.\n";
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
  std::cout << "  --capture-spacing-scale <k>: spacing term of the node capture "
    "distance, as a multiple of the node spacing (default 0.71 = half the cell "
    "diagonal). The gate is max(0.05*|depth|, k*spacing); CUBE's old fixed 0.5 m "
    "floor is gone (cube#143).\n";
  std::cout << "  --depth-adaptive: write a MULTI-LEVEL store (cube#143). A recon pass "
    "over the bags first counts every sounding into a fine count grid and spills "
    "the projected soundings to scratch; the level plan then gives each tile the "
    "COARSER of the level the depth ladder requires (uma#369: cell = scale*depth) "
    "and the level the data achieves (Calder's level of aggregation: the finest "
    "spacing at which each node still gathers --min-obs-per-node soundings); "
    "phase two replays the spill into one CUBE accumulator per level. Parents "
    "stay estimated under their children. Off by default; the live/draft path is "
    "unaffected. --max-resident-tiles is the store-wide total across levels.\n";
  std::cout << "    --depth-adaptive-scale <f> (0.05), --depth-adaptive-coarsest <L> (8), "
    "--depth-adaptive-finest <L> (14; must be <= 14, the survey index's footprint "
    "level -- ADR-0002), --count-level <L> (= finest; finest <= L <= 20), "
    "--min-obs-per-node <N> (5), --blunder-allowance <f> (0.2), "
    "--decision-depth-percentile <p> (2: percentile of a level-14 grid's shallowest "
    "soundings that decides its depth, the flier guard; 0 < p <= 100, read from a "
    "per-grid histogram of WATER DEPTH UNDER THE TRANSDUCER -- not the stored "
    "ellipsoidal height, which the geoid offsets by tens of metres), "
    "--achieved-percentile <p> "
    "(95: percentile of the level of aggregation over a tile that decides its "
    "achieved level).\n";
  std::cout << "    --scratch-dir <dir>: where the recon spill goes (default: beside "
    "the -o store, never /tmp -- it is often tmpfs). The count grid spills "
    "there too, so the free-space check run before the pass starts budgets the "
    "projected sounding spill plus an allowance for the count-tile spill (that "
    "term scales with the ground covered, which is not knowable up front, so it "
    "is an allowance and not a bound).\n";
  std::cout << "    --count-spill-allowance <factor> (1.0): size of that count-tile "
    "allowance, as a multiple of the projected sounding spill. The default "
    "doubles what the preflight check demands, which can refuse a dense survey "
    "over little ground whose spill would have fit; lower it (0 removes the "
    "allowance) or raise it for a wide, sparse survey. Only the preflight check "
    "is affected -- nothing about the pass itself changes.\n";
  std::cout << "    --count-resident-tiles <N> (256, minimum 16): recon count tiles held "
    "in RAM; colder ones are written to the scratch dir and reloaded on demand. "
    "A level-14 count tile is 1.8 MB, so the default budget is ~460 MB. The "
    "minimum is the 3x3 level-of-aggregation neighbourhood plus headroom -- a "
    "smaller budget would thrash one tile per box evaluation. The plan report "
    "states the resident peak.\n";
  std::cout << "    --level-plan-out <file>: RECON ONLY -- write the level plan (JSON) "
    "and print its report (tiles, area and storage per level, the coverage "
    "deficit, the ground stored coarser than level 10), then exit without "
    "estimating. Inspect and approve before committing a multi-hour import.\n";
  std::cout << "    --level-plan <file>: reuse a plan written by --level-plan-out "
    "instead of computing one (the recon pass still runs, for the spill).\n";
  std::cout << "    --count-grid-out <dir>: also persist the recon count grid "
    "(UInt16 GeoTIFF tiles), mergeable into a later run's counts.\n";
  std::cout << "  -l <count>: Stop after this many pings (debugging)\n";
  std::cout << "  Exit codes: 0 = done; 1 = failed, the store may be incomplete; "
    "2 = the store is complete but build_fingerprint.json could not be written, "
    "so a later incremental regen must do a FULL regen (ADR-0003); "
    "3 = the store and its fingerprint are both complete, only the "
    "--tile-size-report CSV could not be written (a diagnostic, not the data).\n";
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

  /// Messages recorded on @p topic across every bag (from the bag metadata).
  uint64_t messageCount(const std::string & topic) const
  {
    uint64_t n = 0;
    for (const auto & reader : readers_) {
      for (const auto & t : reader.second.reader->get_metadata().topics_with_message_count) {
        if (t.topic_metadata.name == topic) {
          n += t.message_count;
        }
      }
    }
    return n;
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


namespace
{

/// Coverage-message size report (cube_bathymetry#112).
///
/// Quantizes each dirty tile BOTH ways from the SAME dirty set -- whole tile
/// and dirty sub-window -- and records serialized and zlib-compressed sizes,
/// then clears the dirty set exactly as cube_bathymetry_node::publishDirtyTiles
/// does. The compressed column is the one that matters: udp_bridge compresses
/// every packet before fragmenting it, so the cell-count saving and the wire
/// saving are different numbers.
///
/// It also MODELS THE SHIPPED POLICY rather than only the two extremes. The
/// node does not send every dirty tile as a sub-window: a tile whose last whole
/// send has aged past subwindow_refresh_interval goes out whole instead, and a
/// tile that has gone quiet still owing a whole send is re-sent whole by the
/// refresh drain. Both are real traffic, the second is the dominant cost at a
/// 60 s interval, and a report that omits them answers a question nobody is
/// asking -- "what would pure sub-window mode cost?" -- while the operator
/// needs the cost of the mode that actually ships. The `sent_*` columns are
/// that number, produced by the SAME CoverageRefreshTracker the node runs.
///
/// The drain is modelled once per report cycle, which is exact at the default
/// --tile-report-interval of 5.0 s because that is also the node's drain tick.
class TileSizeReporter
{
public:
  TileSizeReporter(
    const std::string & path, double interval_s,
    double refresh_interval_s, std::size_t refresh_tiles_per_cycle)
  : out_(path), interval_ns_(static_cast<int64_t>(interval_s * 1e9))
  {
    refresh_.configure(refresh_interval_s, refresh_tiles_per_cycle);
    if (out_) {
      out_ << "bag_time_s,level,tile_row,tile_col,dirty_rows,dirty_cols,"
           << "dirty_cells,full_cells,full_serialized,full_compressed,"
           << "window_serialized,window_compressed,"
           << "source,sent,sent_serialized,sent_compressed\n";
    }
    checkStream("writing the header");
  }

  bool good() const {return static_cast<bool>(out_);}

  /// Did every write reach the file? A truncated CSV is worse than none: it is
  /// the evidence base for a fleet-wide default, and a short file still parses,
  /// still plots, and still looks like a complete survey.
  bool ok() const {return !failed_;}

  /// Flush and close, reporting a failure that only surfaces at close (a full
  /// disk usually does).
  bool finish()
  {
    out_.flush();
    checkStream("flushing");
    out_.close();
    if (out_.fail() && !failed_) {
      failed_ = true;
      std::cerr << "error: --tile-size-report could not be closed cleanly; "
                << "the CSV is incomplete" << std::endl;
    }
    return !failed_;
  }

  /// Report and clear, if a report interval of BAG time has elapsed.
  void maybeReport(cube::GeoMapSheet & sheet, int64_t ping_ns)
  {
    if (last_ns_ != std::numeric_limits<int64_t>::min() &&
      ping_ns - last_ns_ < interval_ns_)
    {
      return;
    }
    last_ns_ = ping_ns;

    builtin_interfaces::msg::Time stamp;
    stamp.sec = static_cast<int32_t>(ping_ns / 1000000000LL);
    stamp.nanosec = static_cast<uint32_t>(ping_ns % 1000000000LL);
    // BAG time drives the refresh policy, exactly as the node's clock drives
    // it live. Wall time here would make the heal fire on how long the import
    // took rather than on how long the survey was.
    const double bag_s = static_cast<double>(ping_ns) * 1e-9;

    std::set<gggs::GridIndex> published_this_cycle;
    for (const auto & index : sheet.publishDirtyGrids()) {
      auto grid = sheet.gridAt(index);
      if (!grid) {
        continue;
      }
      const cube::CellBox box = grid->publishDirtyCells();
      const auto full = cube::quantizeTile(*grid, stamp);
      const auto window = cube::quantizeTileWindow(*grid, stamp, box);
      if (!full || !window) {
        // Nothing displayable this cycle; the node publishes neither.
        continue;
      }
      std::size_t full_serialized = 0;
      std::size_t full_compressed = 0;
      std::size_t window_serialized = 0;
      std::size_t window_compressed = 0;
      measure(*full, full_serialized, full_compressed);
      measure(*window, window_serialized, window_compressed);

      // What the node would ACTUALLY send for this tile this cycle, decided by
      // the same tracker on the same rule: a tile due a refresh -- including a
      // tile that has never been sent whole -- goes out whole even though it
      // is dirty, because doing it here costs one message instead of two.
      const bool whole = refresh_.refreshDue(index, bag_s);
      refresh_.notePublished(index, bag_s, whole);
      published_this_cycle.insert(index);

      writeRow(
        bag_s, *full, box, full_serialized, full_compressed,
        window_serialized, window_compressed, "dirty",
        whole ? "whole" : "window",
        whole ? full_serialized : window_serialized,
        whole ? full_compressed : window_compressed);
    }

    // The quiet-tile drain: tiles no longer changing that still owe a whole
    // send. The node runs this on its own 5 s timer; modelled once per report
    // cycle, which is that same tick at the default --tile-report-interval.
    // Without it the report omits the dominant cost of sub-window mode.
    std::size_t dropped = 0;
    const auto due = refresh_.dueForRefresh(
      published_this_cycle, bag_s,
      [&sheet](const gggs::GridIndex & index) {
        return static_cast<bool>(sheet.gridAt(index));
      },
      &dropped);
    unpayable_ += dropped;
    for (const auto & index : due) {
      auto grid = sheet.gridAt(index);
      if (!grid) {
        continue;
      }
      const auto full = cube::quantizeTile(*grid, stamp);
      if (!full) {
        refresh_.forget(index);
        continue;
      }
      std::size_t serialized = 0;
      std::size_t compressed = 0;
      measure(*full, serialized, compressed);
      refresh_.notePublished(index, bag_s, true);
      // A heal re-sends the WHOLE tile and has no sub-window alternative, so
      // the dirty-box columns are empty rather than zero: zero rows would read
      // as a measured empty window.
      writeRow(
        bag_s, *full, std::nullopt, serialized, compressed, 0, 0,
        "heal", "whole", serialized, compressed);
    }
    sheet.clearPublishDirtyGrids();
  }

  /// Tiles that left RAM still owing a whole-tile refresh over the whole run.
  /// Their gap is unpayable, so the modelled traffic is a LOWER bound by
  /// exactly that many whole tiles -- worth stating rather than absorbing.
  std::size_t unpayableRefreshes() const {return unpayable_;}

private:
  /// One CSV row. `box` is absent for a heal, whose whole-tile re-send has no
  /// dirty window to report.
  void writeRow(
    double bag_s,
    const marine_interfaces::msg::SonarVisualizationTile & tile,
    const std::optional<cube::CellBox> & box,
    std::size_t full_serialized, std::size_t full_compressed,
    std::size_t window_serialized, std::size_t window_compressed,
    const char * source, const char * sent,
    std::size_t sent_serialized, std::size_t sent_compressed)
  {
    // Level via the message's uint8 mirror: gggs::Level has no numeric
    // stream operator and silently writes an empty field.
    out_ << std::fixed << std::setprecision(3)
         << bag_s << ','
         << static_cast<unsigned>(tile.index.level) << ','
         << tile.index.row << ',' << tile.index.col << ',';
    if (box) {
      out_ << box->rows() << ',' << box->columns() << ','
           << (static_cast<std::size_t>(box->rows()) * box->columns()) << ',';
    } else {
      out_ << ",,,";
    }
    out_ << (static_cast<std::size_t>(tile.width) * tile.height) << ','
         << full_serialized << ',' << full_compressed << ',';
    if (box) {
      out_ << window_serialized << ',' << window_compressed << ',';
    } else {
      out_ << ",,";
    }
    out_ << source << ',' << sent << ','
         << sent_serialized << ',' << sent_compressed << '\n';
    checkStream("writing a row");
  }

  /// Say it the first time the stream goes bad, once, and remember. A CSV that
  /// stops mid-survey is indistinguishable from a survey that stopped.
  void checkStream(const char * doing)
  {
    if (out_ || failed_) {
      return;
    }
    failed_ = true;
    std::cerr << "error: --tile-size-report failed while " << doing
              << "; the CSV is incomplete and must not be used as evidence"
              << std::endl;
  }

  /// Serialized size, and the size udp_bridge would actually put on the wire.
  /// zlib's ::compress() at the default level is exactly what packet.cpp does,
  /// and udp_bridge keeps the compressed form only when it is smaller -- so a
  /// payload that does not compress is charged at its serialized size.
  void measure(
    const marine_interfaces::msg::SonarVisualizationTile & tile,
    std::size_t & serialized, std::size_t & compressed)
  {
    rclcpp::SerializedMessage serialized_msg;
    serializer_.serialize_message(&tile, &serialized_msg);
    const auto & rcl = serialized_msg.get_rcl_serialized_message();
    serialized = rcl.buffer_length;
    uLongf bound = compressBound(static_cast<uLong>(serialized));
    std::vector<Bytef> buffer(bound);
    if (::compress(
        buffer.data(), &bound, rcl.buffer, static_cast<uLong>(serialized)) == Z_OK)
    {
      compressed = std::min<std::size_t>(serialized, bound);
    } else {
      compressed = serialized;
    }
  }

  std::ofstream out_;
  int64_t interval_ns_;
  int64_t last_ns_ = std::numeric_limits<int64_t>::min();
  bool failed_ = false;
  std::size_t unpayable_ = 0;
  cube::CoverageRefreshTracker refresh_;
  rclcpp::Serialization<marine_interfaces::msg::SonarVisualizationTile> serializer_;
};

/// Build the reporter, or report why it could not be built. Returns nullopt
/// both when no report was asked for and when the file could not be opened;
/// the caller distinguishes them by whether a path was given.
std::optional<TileSizeReporter> makeTileReporter(
  const std::string & path, double interval_s,
  double refresh_interval_s, int refresh_tiles_per_cycle)
{
  if (path.empty()) {
    return std::nullopt;
  }
  // Validated here rather than at the parse site so the two modelled-policy
  // options stay one concern in one place: 0 is legal for both and means "model
  // the heal switched off", which is a configuration the node supports and an
  // operator may well want to measure.
  if (!(refresh_interval_s >= 0.0) || refresh_tiles_per_cycle < 0) {
    std::cerr << "error: --tile-refresh-interval and --tile-refresh-budget must "
      "be >= 0 (0 models the heal switched off)" << std::endl;
    return std::nullopt;
  }
  TileSizeReporter reporter(
    path, interval_s, refresh_interval_s,
    static_cast<std::size_t>(refresh_tiles_per_cycle));
  if (!reporter.good()) {
    std::cerr << "error: cannot write --tile-size-report " << path << std::endl;
    return std::nullopt;
  }
  return reporter;
}

/// What the pass actually wrote, and the two empty-output cases that are worth
/// a warning rather than a silent zero.
///
/// The on-disk store is the union of tiles evicted during the pass and tiles
/// still resident at finalize (cube#92), so the counts are reported together:
/// an import that evicted heavily and one that never evicted produce identical
/// stores, and only this line distinguishes them when a build looks slow.
template<typename AccumulatorT>
void reportPersisted(
  const AccumulatorT & accumulator, const std::string & store_dir,
  const std::string & bs_store_dir, std::size_t evicted_count,
  std::size_t resident_before_final, double build_secs)
{
  std::cout << "Persisted " << accumulator.bathyTilesPersisted()
            << " bathy tile(s) to " << store_dir << " (survey layer; "
            << evicted_count << " evicted mid-pass, "
            << resident_before_final << " resident at end; build: "
            << build_secs << "s)." << std::endl;

  if (accumulator.bathyTilesPersisted() == 0) {
    std::cerr << "WARNING: no tiles had finite data -- nothing imported. Check "
      "the projector frame overrides and the detections topic." << std::endl;
  }

  // The co-estimated backscatter was surfaced into the --bs-store layer (#80)
  // from the SAME CUBE pass, incrementally under eviction (newest-finite-wins
  // merge, cube#92). By default UNCORRECTED; --backscatter-correction empirical
  // applies the per-beam angular-response correction at node-output (cube#81).
  if (!bs_store_dir.empty()) {
    std::cout << "Persisted " << accumulator.backscatterTilesPersisted()
              << " backscatter tile(s) to " << bs_store_dir << "." << std::endl;
    if (accumulator.backscatterTilesPersisted() == 0) {
      std::cerr << "WARNING: no cells had finite backscatter -- nothing written "
        "to the backscatter store. Check that the detections carry intensities."
                << std::endl;
    }
  }
}

/// Close the size report and say what it is worth. False means the run failed:
/// the report is the reason a --tile-size-report run was asked for, and a CSV
/// that could not be written completely still parses, still plots, and still
/// looks like a complete survey that happened to be quieter -- which is how a
/// bad number becomes a fleet-wide default.
bool finishTileReport(
  std::optional<TileSizeReporter> & reporter, const std::string & path,
  double refresh_interval_s, int refresh_tiles_per_cycle)
{
  if (!reporter) {
    return true;
  }
  if (reporter->unpayableRefreshes() > 0) {
    std::cerr << "NOTE: " << reporter->unpayableRefreshes()
              << " modelled tile(s) left RAM still owing a whole-tile "
      "refresh; their re-sends are absent from the CSV, so the "
      "modelled traffic is a lower bound by that many whole tiles."
              << std::endl;
  }
  if (!reporter->finish()) {
    return false;
  }
  std::cout << "Wrote coverage size report to " << path
            << " (modelled refresh: " << refresh_interval_s << "s, "
            << refresh_tiles_per_cycle << " tile(s)/cycle)." << std::endl;
  return true;
}


/// Georeference one projected ping into GeoSoundings (cube#63/#107 hot path).
/// Extracted verbatim from main()'s per-ping lambda so main() stays within the
/// cpplint function-size limit (cube#143); the arithmetic is unchanged.
std::vector<cube::GeoSounding> georeferencePing(
  const cube::ProjectionResult & projection,
  const geometry_msgs::msg::TransformStamped & transform)
{
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
    // The sonar-frame position, carried through so the depth-adaptive recon
    // can read the WATER DEPTH UNDER THE TRANSDUCER off `z` (cube#143): the
    // georeferenced `depth` above is a WGS84 ellipsoidal height (uma ADR-0002
    // D4) and the geoid offset would coarsen every tile by one to two levels.
    // Left unread by the estimator itself (batch_regen.cpp:55).
    gs.sounding.sonar_relative_position = s.sonar_relative_position;
    soundings.push_back(gs);
  }
  return soundings;
}


/// Resolve the backscatter angular-response correction mode and curve
/// (cube#81/#102): explicit file wins, else the bags' SonarInfo pre-pass.
/// Returns false on an unparseable mode (the caller exits via usage()).
/// Extracted from main() for cpplint's function-size limit (cube#143).
bool resolveBackscatterCorrection(
  const std::string & backscatter_correction_str, const std::string & backscatter_curve_file,
  const std::string & sonar_info_topic, const std::string & detections_topic,
  const std::vector<std::string> & bagfile_names,
  cube::BackscatterAngleCorrection & backscatter_mode,
  cube::AngularResponseCurve & backscatter_curve)
{
  if (!cube::parseBackscatterAngleCorrection(
      backscatter_correction_str, backscatter_mode))
  {
    std::cerr << "error: --backscatter-correction must be 'none', 'empirical' "
              << "or 'auto' (got '" << backscatter_correction_str << "')\n";
    return false;
  }
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
  return true;
}


/// Depth-adaptive option validation (cube#143), run ONCE before any bag is
/// opened -- a policy throw is a misconfiguration identical for every tile, so
/// it is fatal here rather than hours into the pass. Exits via usage() on error.
void validateDepthAdaptiveOptions(
  bool depth_adaptive, cube::LevelPlanPolicy & level_policy, bool count_level_given,
  bool count_resident_tiles_given, bool count_spill_allowance_given,
  const std::string & level_plan_out, const std::string & level_plan_in,
  const std::string & count_grid_out, const std::string & scratch_dir,
  const std::string & tile_size_report_path)
{
  // The plan-side flags without --depth-adaptive are a contradiction worth
  // refusing rather than ignoring.
  if (!depth_adaptive &&
    (!level_plan_out.empty() || !level_plan_in.empty() || !count_grid_out.empty() ||
    !scratch_dir.empty() || count_level_given || count_resident_tiles_given ||
    count_spill_allowance_given))
  {
    std::cerr << "error: --level-plan-out/--level-plan/--count-grid-out/--scratch-dir/"
      "--count-level/--count-resident-tiles/--count-spill-allowance need "
      "--depth-adaptive\n";
    usage();
  }
  if (depth_adaptive) {
    if (!count_level_given) {
      level_policy.count_level = level_policy.depth.finest_level;
    }
    try {
      level_policy.validate();
    } catch (const std::invalid_argument & e) {
      std::cerr << "error: " << e.what() << "\n";
      usage();
    }
    if (!tile_size_report_path.empty()) {
      std::cerr << "error: --tile-size-report models the live single-sheet coverage "
        "stream and is not supported with --depth-adaptive\n";
      usage();
    }
    if (!level_plan_out.empty() && !level_plan_in.empty()) {
      std::cerr << "error: --level-plan-out (recon only) and --level-plan (reuse a "
        "plan) are exclusive\n";
      usage();
    }
  }
}


/// Warn about `.recon_spill_<pid>` directories a killed run left behind
/// (cube#143). They are never swept automatically: a concurrent import may own
/// one, and the spill of an aborted multi-hour run can be gigabytes the
/// operator should see before anything deletes it.
void warnAboutOrphanedSpills(const std::string & spill_root, const std::string & mine)
{
  std::error_code ec;
  if (!std::filesystem::is_directory(spill_root, ec)) {
    return;
  }
  std::vector<std::string> orphans;
  for (std::filesystem::directory_iterator it(spill_root, ec), end; it != end && !ec;
    it.increment(ec))
  {
    const std::string name = it->path().filename().string();
    if (it->is_directory(ec) && name.rfind(".recon_spill_", 0) == 0 &&
      it->path().string() != mine)
    {
      orphans.push_back(it->path().string());
    }
  }
  if (orphans.empty()) {
    return;
  }
  std::cerr << "warning: " << orphans.size() << " leftover recon spill director"
            << (orphans.size() == 1 ? "y" : "ies") << " under " << spill_root
            << " (an aborted run, or a concurrent import). Nothing is deleted "
    "automatically; remove them by hand once no import is using them:"
            << std::endl;
  for (const auto & path : orphans) {
    std::cerr << "  " << path << std::endl;
  }
}

/// A count option with a real floor (cube#143). Negative is the dangerous case
/// -- cast to std::size_t it becomes SIZE_MAX, which for --count-resident-tiles
/// silently restores the unbounded count grid the resident LRU replaced -- but
/// the floor is checked here too, so a value the ReconCollector constructor
/// would refuse is refused at parse time rather than after the orphan warning
/// and the spill banner have already printed. Reports the option error and
/// exits, like every other option failure.
std::size_t requireAtLeast(const char * flag, int value, std::size_t minimum)
{
  if (value < 0 || static_cast<std::size_t>(value) < minimum) {
    std::cerr << "error: option '" << flag << "' expects a count >= " << minimum
              << ", got '" << value << "'\n";
    usage();
  }
  return static_cast<std::size_t>(value);
}

/// A factor option that must be finite and non-negative (cube#143). Reports the
/// option error and exits, like every other option failure.
double requireFiniteFactor(const char * flag, double value)
{
  if (!(value >= 0.0) || !std::isfinite(value)) {
    std::cerr << "error: option '" << flag << "' expects a finite factor >= 0, got '"
              << value << "'\n";
    usage();
  }
  return value;
}

/// Print the span the bags cover (cube#143: lifted out of main(), which is at
/// the cpplint function-size limit).
void reportBagTimeSpan(
  const std::chrono::system_clock::time_point & begin_time,
  const std::chrono::system_clock::time_point & end_time)
{
  auto start_time_t = std::chrono::system_clock::to_time_t(begin_time);
  std::cout << "start time: "
            << std::put_time(std::gmtime(&start_time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
  auto end_time_t = std::chrono::system_clock::to_time_t(end_time);
  std::cout << "end time: "
            << std::put_time(std::gmtime(&end_time_t), "%Y-%m-%d %H:%M:%S") << std::endl;
  std::cout << "total time: "
            << std::chrono::duration_cast<std::chrono::seconds>(end_time - begin_time).count()
            << " seconds" << std::endl;
}

/// Report what tiling the run will use, before the pass starts (cube#143:
/// lifted out of main(), which is at the cpplint function-size limit).
void reportTilingChoice(
  bool depth_adaptive, const cube::LevelPlanPolicy & level_policy,
  const cube::GeoMapSheet & geo_map_sheet, double resolution, float capture_spacing_scale)
{
  if (depth_adaptive) {
    std::cout << "Depth-adaptive multi-level store (cube#143): levels "
              << static_cast<int>(level_policy.depth.coarsest_level) << ".."
              << static_cast<int>(level_policy.depth.finest_level)
              << ", count level " << static_cast<int>(level_policy.count_level)
              << ", n_req " << level_policy.requiredObservations()
              << "; the -r resolution is not used (each level's sheet is built at "
      "its own cell size)." << std::endl;
  } else {
    std::cout << "requested resolution: " << resolution << " nominal used: "
              << geo_map_sheet.nominalCellSizeMeters() << std::endl;
  }
  std::cout << "Capture distance: max(" << geo_map_sheet.parameters().capture_distance_scale
            << " x |depth|, " << capture_spacing_scale << " x node spacing)" << std::endl;
}

/// Build the recon collector (cube#143). Spill scratch beside the output store
/// unless --scratch-dir says otherwise (never temp_directory_path(): it is often
/// tmpfs, and a day's spill is gigabytes). The free-space check uses the bags'
/// detections message count as the ping count and 256 beams per ping as an
/// upper bound, plus @p count_spill_allowance times that for the count-tile
/// spill. Null on a free-space shortfall (already reported).
std::unique_ptr<cube::ReconCollector> makeRecon(
  const cube::LevelPlanPolicy & level_policy, const std::string & scratch_dir,
  const std::string & store_dir, uint64_t projected_pings, std::size_t count_resident_tiles,
  double count_spill_allowance)
{
  const std::string spill_root = scratch_dir.empty() ? store_dir : scratch_dir;
  // Keyed on pid AND start time: a pid-reuse collision with a dead run's
  // orphaned directory would otherwise surface as an "already exists" refusal
  // attributable to a process that has nothing to do with this one.
  const auto started_at = std::chrono::system_clock::now().time_since_epoch();
  const std::string spill_dir = spill_root + "/.recon_spill_" +
    std::to_string(::getpid()) + "_" +
    std::to_string(std::chrono::duration_cast<std::chrono::seconds>(started_at).count());
  warnAboutOrphanedSpills(spill_root, spill_dir);
  const uint64_t projected_bytes =
    projected_pings * 256ull * cube::ReconCollector::kBytesPerSpilledSounding;
  // The count grid spills its cold tiles into the same directory (1.8 MB per
  // level-14 tile, ~630 MB per km^2 of ground), so budgeting the sounding
  // spill alone under-counts what the pass will write. That term scales with
  // the ground COVERED, which nothing knows before the pass -- the surveyed
  // area cannot be inferred from a ping count -- so it is budgeted as an
  // allowance of a share of the sounding spill rather than left out entirely.
  // It is an allowance, not a bound, in BOTH directions: a wide, sparse survey
  // covers more ground per sounding and can still exceed it, and a dense one
  // over little ground can be refused a run that would have fit. Hence
  // --count-spill-allowance: the operator who knows the survey can lower the
  // share (0 removes it) or raise it, rather than being stuck with a doubling.
  const uint64_t count_spill_bytes =
    static_cast<uint64_t>(static_cast<double>(projected_bytes) * count_spill_allowance);
  const uint64_t projected_total_bytes = projected_bytes + count_spill_bytes;
  std::cout << "Recon spill: " << spill_dir << " (~" << projected_bytes / (1024 * 1024)
            << " MB projected for " << projected_pings << " pings at 256 beams, "
            << cube::ReconCollector::kBytesPerSpilledSounding << " B/sounding, plus a ~"
            << count_spill_bytes / (1024 * 1024)
            << " MB allowance for the count-tile spill at --count-spill-allowance "
            << count_spill_allowance << ")" << std::endl;
  try {
    cube::ReconCollector::requireFreeSpace(spill_root, projected_total_bytes);
  } catch (const std::exception & e) {
    std::cerr << "error: " << e.what() << std::endl;
    std::cerr << "Both terms are upper bounds, not measurements: 256 beams per ping and a "
      "count-tile allowance of " << count_spill_allowance << " x the sounding spill. If "
      "this survey is denser than it is wide, lower the allowance with "
      "--count-spill-allowance <factor> (0 removes it); otherwise point --scratch-dir at "
      "a larger device." << std::endl;
    return nullptr;
  }
  try {
    return std::make_unique<cube::ReconCollector>(
      level_policy, spill_dir, count_resident_tiles);
  } catch (const std::exception & e) {
    std::cerr << "error: " << e.what() << std::endl;
    return nullptr;
  }
}

/// Write the ADR-0003 fingerprint for a finished import (cube#143). The store
/// itself is already written, so a failure here is not fatal to the data -- but
/// it is not nothing either: a store with no fingerprint cannot be told stale,
/// so the next `batch_regen --incremental` must fall back to a full regen and
/// the operator has to know. Returns false on failure; the callers exit 2
/// ("the store is complete, the fingerprint is not") rather than print "done!".
bool writeFingerprint(
  const std::string & store_dir, cube::BuildFingerprint::Mode mode, double cell_size_m,
  const cube::Parameters & parameters, const std::string & iho_order,
  const cube::LevelPlanPolicy * policy, const std::set<uint8_t> & levels_used)
{
  cube::BuildFingerprint f;
  f.mode = mode;
  if (mode == cube::BuildFingerprint::Mode::Fixed) {
    f.cell_size_m = cell_size_m;
  }
  f.iho_order = iho_order;
  f.policy.capture_distance_scale = parameters.capture_distance_scale;
  f.policy.capture_spacing_scale = parameters.capture_spacing_scale;
  if (policy) {
    // Every input the level plan rests on: the ladder, its bounds, the count
    // level, the observation requirement and both percentiles.
    f.policy.depth_adaptive_scale = policy->depth.capture_distance_scale;
    f.policy.coarsest_level = policy->depth.coarsest_level;
    f.policy.finest_level = policy->depth.finest_level;
    f.policy.count_level = policy->count_level;
    f.policy.min_obs_per_node = policy->min_obs_per_node;
    f.policy.blunder_allowance = policy->blunder_allowance;
    f.policy.decision_depth_percentile = policy->decision_depth_percentile;
    f.policy.achieved_percentile = policy->achieved_percentile;
  }
  f.levels_used = levels_used;
  try {
    f.write(store_dir);
    std::cout << "Wrote " << cube::BuildFingerprint::kFilename << " (" << cube::toString(mode)
              << ")." << std::endl;
    return true;
  } catch (const std::exception & e) {
    std::cerr << "error: the store in " << store_dir << " is complete, but "
              << cube::BuildFingerprint::kFilename << " could not be written (" << e.what()
              << "). Without it the store cannot be told stale: a later "
      "batch_regen --incremental must fall back to a FULL regen (ADR-0003)."
              << std::endl;
    return false;
  }
}

/// Abort a run whose accumulation did not complete (cube#143) -- a failed or
/// short spill replay, a finalize that threw, or an I/O fault mid-pass on the
/// fixed-level path -- and report what that leaves on disk. The accumulator has
/// been evicting tiles
/// into the real `-o` store since the first batch
/// (`MultiLevelAccumulator::evictToBudget` -> `persistAndDrop`), so by the time
/// a short or failed replay is detected the destination already holds
/// partial-coverage tiles: a re-run over them double-counts the soundings in
/// every tile written twice -- the hazard `build_bathy_store.sh` guards with
/// `--fresh`. Nothing is finalized and no fingerprint is written for THIS run,
/// and a fingerprint left by an earlier complete build is deleted here rather
/// than left describing tiles this run has since mutated (a later
/// `batch_regen --incremental` would otherwise trust it). Returns the exit code.
int abortDirtyReplay(const std::string & store_dir, const std::string & detail)
{
  std::cerr << "error: " << detail << std::endl;
  std::cerr << "The store in " << store_dir
            << " is NOT empty and NOT complete: every batch accumulated before the failure "
    "was already evicted into it, so it holds partial-coverage tiles. Nothing is "
    "finalized and no " << cube::BuildFingerprint::kFilename
            << " is written for this run. Archive or remove that store (or re-import "
    "with --fresh) before re-running -- importing again over these tiles "
    "double-counts their soundings." << std::endl;
  const std::filesystem::path fingerprint =
    std::filesystem::path(store_dir) / cube::BuildFingerprint::kFilename;
  std::error_code ec;
  if (std::filesystem::exists(fingerprint, ec) && !ec) {
    if (std::filesystem::remove(fingerprint, ec) && !ec) {
      std::cerr << "Removed the pre-existing " << fingerprint.string()
                << ": it described the store as it was BEFORE this aborted run mutated it."
                << std::endl;
    } else {
      std::cerr << "warning: could not remove the pre-existing " << fingerprint.string()
                << " (" << ec.message()
                << "). Delete it by hand: it describes the store as it was BEFORE this "
        "aborted run mutated it, and a later batch_regen --incremental would trust it."
                << std::endl;
    }
  }
  return 1;
}

/// Depth-adaptive finish (cube#143): plan, report, recon-only exit, phase-two
/// replay into one accumulator per level, finalize, fingerprint. Returns the
/// process exit code.
int cube_depth_adaptive_finish(
  cube::ReconCollector & recon, const cube::LevelPlanPolicy & policy,
  const std::string & level_plan_in, const std::string & level_plan_out,
  const std::string & count_grid_out, const std::string & store_dir,
  const std::string & reference_store_dir, const std::string & bs_store_dir,
  std::size_t max_resident_tiles, const std::string & iho_order,
  float capture_spacing_scale, cube::BackscatterAngleCorrection backscatter_mode,
  const cube::AngularResponseCurve & backscatter_curve,
  const marine_bathymetry_store::StoreMetadata & store_metadata,
  const marine_mbes_backscatter_store::StoreMetadata & bs_metadata, double recon_secs)
{
  std::cout << "Recon pass: " << recon.soundingsSeen() << " soundings counted, "
            << recon.soundingsSpilled() << " spilled, " << recon.counts().tileCount()
            << " count tile(s) in " << recon_secs << "s." << std::endl;

  // The level decision needs the range below the transducer, not the stored
  // (ellipsoidal) depth -- cube#143. A sounding with no usable sonar-frame z
  // is counted but cannot decide a level; when that is ALL of them the plan
  // would be built from nothing and would ask for the finest level everywhere,
  // so it is refused rather than written.
  if (recon.soundingsWithoutRange() > 0) {
    std::cerr << "WARNING: " << recon.soundingsWithoutRange() << " of "
              << recon.soundingsSeen() << " soundings carried no sonar-frame range "
              << "(z non-finite or zero), so they did not decide a level." << std::endl;
    if (recon.soundingsWithoutRange() >= recon.soundingsSeen()) {
      std::cerr << "error: no sounding carried a range below the transducer, so no "
                << "water depth could be measured and no level plan can be made."
                << std::endl;
      return 1;
    }
  }

  if (!count_grid_out.empty()) {
    try {
      const std::size_t n = recon.counts().saveTo(count_grid_out);
      std::cout << "Wrote " << n << " count tile(s) to " << count_grid_out << std::endl;
    } catch (const std::exception & e) {
      std::cerr << "error: could not write --count-grid-out: " << e.what() << std::endl;
      return 1;
    }
  }

  std::shared_ptr<cube::LevelPlan> plan;
  if (!level_plan_in.empty()) {
    std::ifstream in(level_plan_in);
    if (!in) {
      std::cerr << "error: cannot read --level-plan " << level_plan_in << std::endl;
      return 1;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    try {
      plan = std::make_shared<cube::LevelPlan>(cube::LevelPlan::fromJson(buffer.str()));
    } catch (const std::exception & e) {
      std::cerr << "error: --level-plan " << level_plan_in << ": " << e.what() << std::endl;
      return 1;
    }
    std::cout << "Reusing level plan " << level_plan_in << " (" << plan->tiles().size()
              << " tiles)." << std::endl;
  } else {
    std::cout << "Computing the level plan..." << std::endl;
    plan = std::make_shared<cube::LevelPlan>(recon.plan());
  }
  std::cout << plan->report();
  std::cout << "Recon count grid: " << recon.counts().tileCount() << " tile(s), resident peak "
            << recon.counts().residentPeak() << " of "
            << (recon.counts().residentBudget() == 0 ?
  std::string("unbounded") : std::to_string(recon.counts().residentBudget()))
            << " (~" << (recon.counts().residentPeak() * 2ull * cube::CountGrid::kEdge *
  cube::CountGrid::kEdge) / (1024 * 1024)
            << " MB peak), " << recon.counts().spilledTileCount()
            << " currently spilled to disk." << std::endl;

  if (!level_plan_out.empty()) {
    std::ofstream out(level_plan_out);
    out << plan->toJson() << "\n";
    if (!out) {
      std::cerr << "error: could not write --level-plan-out " << level_plan_out << std::endl;
      return 1;
    }
    std::cout << "Wrote level plan to " << level_plan_out
              << ". Recon only: nothing estimated, nothing written to " << store_dir
              << ". Re-run with --level-plan " << level_plan_out << " to import." << std::endl;
    return 0;
  }
  if (plan->tiles().empty()) {
    std::cerr << "error: the level plan emits no tiles -- no ground with data; nothing "
      "to import." << std::endl;
    return 1;
  }

  std::cout << "Phase two: replaying the spill into " << plan->levels().size()
            << " per-level accumulator(s)..." << std::endl;
  cube::MultiLevelAccumulatorConfig cfg;
  cfg.store_dir = store_dir;
  cfg.reference_store_dir = reference_store_dir;
  cfg.bs_store_dir = bs_store_dir;
  cfg.max_resident_tiles = max_resident_tiles;
  cfg.iho_order = iho_order;
  cfg.capture_spacing_scale = capture_spacing_scale;
  cfg.backscatter_mode = backscatter_mode;
  cfg.backscatter_curve = backscatter_curve.points;
  cfg.backscatter_tl_removed = backscatter_curve.tl_removed;
  cfg.backscatter_absorption_db_per_m = backscatter_curve.absorption_db_per_m;
  cube::MultiLevelAccumulator accumulator(plan, cfg);

  auto phase_tp = std::chrono::steady_clock::now();
  uint64_t replayed = 0;
  // Replay the one spill file front to back, in ping-sized chunks: the file is
  // in arrival order, so every accumulator sees the soundings in the order the
  // fixed-level path saw them (CUBE's sliding-median pre-filter is
  // order-dependent), and a 256-sounding chunk is one swath's worth, so routing
  // and eviction run at the same granularity as the fixed path.
  constexpr std::size_t kChunk = 256;
  std::vector<cube::GeoSounding> chunk;
  chunk.reserve(kChunk);
  uint64_t read_back = 0;
  // forEachSpilled throws on a failed flush or close of the spill file and on a
  // partial trailing record -- the disk-full tail this whole check exists for.
  // It is the one fallible call on this path, and main() has no catch of its
  // own, so an escaping throw would end the run in std::terminate (SIGABRT)
  // instead of the `error: ...` + exit 1 its silent-short-count sibling below
  // produces for the very same condition.
  try {
    read_back = recon.forEachSpilled([&](const cube::GeoSounding & s) {
          chunk.push_back(s);
          if (chunk.size() >= kChunk) {
            accumulator.addBatch(chunk);
            replayed += chunk.size();
            chunk.clear();
          }
      });
    if (!chunk.empty()) {
      accumulator.addBatch(chunk);
      replayed += chunk.size();
      chunk.clear();
    }
  } catch (const std::exception & e) {
    return abortDirtyReplay(
      store_dir, std::string(e.what()) + ". Replayed " + std::to_string(replayed) + " of " +
      std::to_string(recon.soundingsSpilled()) + " spilled sounding(s) before the failure.");
  }
  const double replay_secs =
    std::chrono::duration<double>(std::chrono::steady_clock::now() - phase_tp).count();
  // The spill is the only copy of the projected soundings, so a short replay is
  // a truncated survey, not a detail: fail before finalize() rather than write
  // the store and fingerprint it as a complete build (ADR-0003) -- a later
  // `batch_regen --incremental` would then trust it and never rebuild it. What
  // is already on disk is NOT nothing, though: addBatch evicts to the real -o
  // store as it goes, so abortDirtyReplay() says so and clears any stale
  // fingerprint sitting over the mutated tiles.
  if (read_back != replayed || replayed != recon.soundingsSpilled()) {
    return abortDirtyReplay(
      store_dir, "phase one spilled " + std::to_string(recon.soundingsSpilled()) +
      " soundings but phase two replayed only " + std::to_string(replayed) +
      " (read back " + std::to_string(read_back) + ") from " + recon.spillPath() +
      ". The spill is short -- a full scratch device is the usual cause; free space "
      "(or pass --scratch-dir) before the re-run.");
  }
  std::cout << "Replayed " << replayed << " soundings in " << replay_secs <<
    "s; batches per level:";
  for (const auto & [level, n] : accumulator.batchesPerLevel()) {
    std::cout << " L" << static_cast<int>(level) << "=" << n;
  }
  std::cout << std::endl;

  std::cout << "Building store tiles..." << std::endl;
  const std::size_t resident_before_final = accumulator.residentTileCount();
  const std::size_t evicted_count = accumulator.evictedTileCount();
  accumulator.finalize(
    store_metadata.empty() ? nullptr : &store_metadata,
    (bs_store_dir.empty() || bs_metadata.empty()) ? nullptr : &bs_metadata);
  reportPersisted(
    accumulator, store_dir, bs_store_dir, evicted_count, resident_before_final,
    std::chrono::duration<double>(std::chrono::steady_clock::now() - phase_tp).count());
  // Only now is the spill expendable: until finalize() has persisted the
  // resident tiles, it is the only copy of the projected soundings, and a
  // crash in between would cost the whole (multi-hour) projection pass.
  recon.cleanup();
  const bool fingerprinted = writeFingerprint(
    store_dir, cube::BuildFingerprint::Mode::DepthAdaptive, 0.0,
    accumulator.sheetAt(*plan->levels().begin()).parameters(), iho_order, &policy,
    plan->levels());
  if (!fingerprinted) {
    return 2;
  }
  std::cout << "done!" << std::endl;
  return 0;
}

}  // namespace

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
  // Coverage-message size report (cube_bathymetry#112). Off unless a path is
  // given; measures what the live coverage stream would put on the operator
  // link, whole-tile against dirty-sub-window, on the SAME dirty set.
  std::string tile_size_report_path;
  // Matches the live node's coverage publish cadence: cube_bathymetry_node
  // republishes the dirty set when a ping's stamp is more than 5 s past the
  // last publish, so a report on any other interval would measure a dirty
  // region the boat never actually sends as one message.
  double tile_report_interval_s = 5.0;
  // The refresh policy the report MODELS. Defaults track the node's own
  // parameter defaults (subwindow_refresh_interval,
  // subwindow_refresh_tiles_per_cycle), so the CSV describes the shipped
  // configuration unless it is told otherwise -- and can be re-run at other
  // values to choose one from measurement rather than from the cost table.
  double tile_refresh_interval_s = 60.0;
  int tile_refresh_tiles_per_cycle = 2;
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
  // Node capture distance, spacing term (cube#143): max(0.05*|depth|, k*spacing).
  float capture_spacing_scale = 0.71f;
  // Depth-adaptive multi-level store (cube#143). Off by default.
  bool depth_adaptive = false;
  cube::LevelPlanPolicy level_policy;
  bool count_level_given = false;
  std::size_t count_resident_tiles = cube::CountGrid::kDefaultResidentTiles;
  bool count_resident_tiles_given = false;
  // Count-tile spill allowance, as a multiple of the projected sounding spill
  // (cube#143). 1.0 doubles the preflight requirement; the operator who knows
  // the survey can lower or raise it.
  double count_spill_allowance = 1.0;
  bool count_spill_allowance_given = false;
  std::string scratch_dir;
  std::string level_plan_out;
  std::string level_plan_in;
  std::string count_grid_out;

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
    } else if (*arg == "--tile-size-report") {
      tile_size_report_path = next_value("--tile-size-report");
    } else if (*arg == "--tile-report-interval") {
      tile_report_interval_s = parse_double(
        "--tile-report-interval", next_value("--tile-report-interval"));
      if (!(tile_report_interval_s > 0.0)) {
        std::cerr << "error: --tile-report-interval must be > 0\n";
        usage();
      }
    } else if (*arg == "--tile-refresh-interval") {
      tile_refresh_interval_s = parse_double(
        "--tile-refresh-interval", next_value("--tile-refresh-interval"));
    } else if (*arg == "--tile-refresh-budget") {
      tile_refresh_tiles_per_cycle = parse_int(
        "--tile-refresh-budget", next_value("--tile-refresh-budget"));
    } else if (*arg == "--capture-spacing-scale") {
      capture_spacing_scale = static_cast<float>(
        parse_double("--capture-spacing-scale", next_value("--capture-spacing-scale")));
      if (!(capture_spacing_scale > 0.0f) || !std::isfinite(capture_spacing_scale)) {
        std::cerr << "error: --capture-spacing-scale must be a positive number\n";
        usage();
      }
    } else if (*arg == "--depth-adaptive") {
      depth_adaptive = true;
    } else if (*arg == "--depth-adaptive-scale") {
      level_policy.depth.capture_distance_scale =
        parse_double("--depth-adaptive-scale", next_value("--depth-adaptive-scale"));
    } else if (*arg == "--depth-adaptive-coarsest") {
      level_policy.depth.coarsest_level = static_cast<uint8_t>(
        parse_int("--depth-adaptive-coarsest", next_value("--depth-adaptive-coarsest")));
    } else if (*arg == "--depth-adaptive-finest") {
      level_policy.depth.finest_level = static_cast<uint8_t>(
        parse_int("--depth-adaptive-finest", next_value("--depth-adaptive-finest")));
    } else if (*arg == "--count-level") {
      level_policy.count_level = static_cast<uint8_t>(
        parse_int("--count-level", next_value("--count-level")));
      count_level_given = true;
    } else if (*arg == "--min-obs-per-node") {
      level_policy.min_obs_per_node = static_cast<uint32_t>(
        parse_int("--min-obs-per-node", next_value("--min-obs-per-node")));
    } else if (*arg == "--blunder-allowance") {
      level_policy.blunder_allowance =
        parse_double("--blunder-allowance", next_value("--blunder-allowance"));
    } else if (*arg == "--decision-depth-percentile") {
      level_policy.decision_depth_percentile = parse_double(
        "--decision-depth-percentile", next_value("--decision-depth-percentile")) / 100.0;
    } else if (*arg == "--achieved-percentile") {
      level_policy.achieved_percentile = parse_double(
        "--achieved-percentile", next_value("--achieved-percentile")) / 100.0;
    } else if (*arg == "--count-resident-tiles") {
      count_resident_tiles = requireAtLeast(
        "--count-resident-tiles",
        parse_int("--count-resident-tiles", next_value("--count-resident-tiles")),
        cube::CountGrid::kMinResidentTiles);
      count_resident_tiles_given = true;
    } else if (*arg == "--count-spill-allowance") {
      count_spill_allowance = requireFiniteFactor(
        "--count-spill-allowance",
        parse_double("--count-spill-allowance", next_value("--count-spill-allowance")));
      count_spill_allowance_given = true;
    } else if (*arg == "--scratch-dir") {
      scratch_dir = next_value("--scratch-dir");
    } else if (*arg == "--level-plan-out") {
      level_plan_out = next_value("--level-plan-out");
    } else if (*arg == "--level-plan") {
      level_plan_in = next_value("--level-plan");
    } else if (*arg == "--count-grid-out") {
      count_grid_out = next_value("--count-grid-out");
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

  validateDepthAdaptiveOptions(
    depth_adaptive, level_policy, count_level_given, count_resident_tiles_given,
    count_spill_allowance_given, level_plan_out, level_plan_in, count_grid_out, scratch_dir,
    tile_size_report_path);

  std::cout << "Detections topic: " << detections_topic
            << " (offline projection, vessel_speed = NaN)" << std::endl;
  std::cout << "Store dir: " << store_dir << std::endl;

  cube::DetectionsProjector projector(projector_params);

  // Accumulated offline-projection diagnostics, surfaced at the end so a
  // misconfigured-frames or over-tight-range run is diagnosable rather than a
  // silently sparse/empty import (the failure mode #43 exists to kill).
  // Whole-run projection totals; cube::report_projection_summary prints them.
  cube::ProjectionRunTotals proj_totals;

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
  reportBagTimeSpan(begin_time, end_time);

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
  geo_map_sheet.setCaptureSpacingScale(capture_spacing_scale);
  reportTilingChoice(
    depth_adaptive, level_policy, geo_map_sheet, resolution, capture_spacing_scale);

  // Backscatter angular-response correction (cube#81). The setter must run AFTER
  // the sheet is constructed (its grids hold a const ref to the sheet Parameters).
  cube::BackscatterAngleCorrection backscatter_mode =
    cube::BackscatterAngleCorrection::None;
  cube::AngularResponseCurve backscatter_curve;
  if (!resolveBackscatterCorrection(
      backscatter_correction_str, backscatter_curve_file, sonar_info_topic,
      detections_topic, bagfile_names, backscatter_mode, backscatter_curve))
  {
    usage();
  }
  geo_map_sheet.setBackscatterCorrection(
    backscatter_mode, backscatter_curve.points,
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

  std::unique_ptr<cube::ReconCollector> recon;
  if (depth_adaptive) {
    recon = makeRecon(level_policy, scratch_dir, store_dir,
        bag_readers.messageCount(detections_topic), count_resident_tiles,
        count_spill_allowance);
    if (!recon) {
      return 1;
    }
  }

  if (!reference_store_dir.empty()) {
    std::cout << "Reference-prior seeding from " << reference_store_dir
              << " (lazy per-tile; predicted-only blunder gate, #96)." << std::endl;
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
    std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - begin_time).count();

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
  // A fatal I/O failure raised from inside the per-ping work (cube#143 triage):
  // the recon's spill/count-tile writes and the accumulator's eviction writes
  // both throw on a full device, and NEITHER main() nor the per-ping handler
  // (which catches only tf2::TransformException) would have caught it -- the
  // run would end in std::terminate (SIGABRT), skipping ~ReconCollector and
  // leaving a multi-GB spill dir behind for the next run's orphan warning.
  // Recorded here instead, which stops the pass and lets the normal return path
  // report it and unwind every destructor. Swept at BOTH sites of the class:
  // the recon path and the fixed-level accumulator path.
  std::string fatal_error;

  auto tile_reporter = makeTileReporter(
    tile_size_report_path, tile_report_interval_s, tile_refresh_interval_s,
    tile_refresh_tiles_per_cycle);
  if (!tile_size_report_path.empty() && !tile_reporter) {
    return 1;
  }

  // Project + georeference one ping into the GeoMapSheet.
  auto process_detection =
    [&](const marine_acoustic_msgs::msg::SonarDetections & detections, int64_t ping_ns) {
      // Real speed-over-ground from odom (Fix B) if available, else NaN -- the
      // error model floors the speed-dependent horizontal-TPU terms to 0 (Fix A).
      const float vessel_speed = speedAt(speed_by_ns, ping_ns);

      auto projection = projector.project(detections, tfBuffer, vessel_speed);
      ++proj_totals.pings;
      proj_totals.soundings += projection.soundings.size();
      proj_totals.beams += projection.diagnostics.total;
      proj_totals.filtered_range += projection.diagnostics.filtered_range;
      proj_totals.missing_attitude += projection.diagnostics.missing_attitude;
      proj_totals.missing_heave += projection.diagnostics.missing_heave;
      proj_totals.default_beamwidth_beams += projection.diagnostics.default_beamwidth_beams;
      proj_totals.missing_rx_angle_beams += projection.diagnostics.missing_rx_angle_beams;

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
        const std::vector<cube::GeoSounding> soundings =
          georeferencePing(projection, transform);
        if (recon) {
          // Recon pass (cube#143): count, reservoir and spill; no estimation.
          recon->add(soundings, geo_map_sheet.parameters());
        } else {
          // Accumulate through the bounded-RAM accumulator (cube#92): adds the
          // batch, reloads any evicted tile this ping revisits, then evicts cold
          // tiles back to the budget. With --max-resident-tiles 0 this is a plain
          // addSoundings (no eviction).
          accumulator.addBatch(soundings);
        }
        ping_count++;
        if (tile_reporter) {
          tile_reporter->maybeReport(geo_map_sheet, ping_ns);
        }
      } catch (const tf2::TransformException & e) {
        // A ping with no earth transform in the (bounded) buffer at its stamp --
        // e.g. before the first earth fix, or a TF gap wider than the cache
        // window. Counted (not just logged) so an empty or sparse import is
        // diagnosable rather than silently dropped.
        ++proj_totals.dropped_georef;
        if (proj_totals.dropped_georef <= 5) {
          std::cerr << "Transform Exception: " << e.what() << std::endl;
        }
      } catch (const std::exception & e) {
        // Everything else from the per-ping work is an I/O fault, not a
        // per-ping condition: the recon's spill write or count-tile spill, or
        // the accumulator's eviction write into the -o store. Both mean the
        // device is full (or gone), so the next ping would fail the same way.
        // Record and stop rather than let it escape to std::terminate; the
        // caller reports it below and every destructor still runs.
        // (tf2::TransformException derives from std::runtime_error, so this
        // handler must stay BELOW the one above.)
        fatal_error = e.what();
      }
    };

  // Drain detections whose bracketing TF is now present (frontier advanced a
  // guard interval past the ping stamp). With flush=true, project whatever
  // remains at end-of-stream against the available coverage.
  auto drain_pending = [&](bool flush) {
      int64_t last_drained_ns = std::numeric_limits<int64_t>::min();
      while (!pending.empty() && !limit_reached && fatal_error.empty()) {
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
  for (auto message = bag_readers.next(); message && !limit_reached && fatal_error.empty();
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
                << " pings (" << proj_totals.dropped_georef << " dropped, " << pending.size()
                << " pending)      " << std::flush;
      last_report_time = now;
    }
  }
  // Flush detections still pending at end-of-stream against available coverage.
  drain_pending(true);
  if (!fatal_error.empty()) {
    if (recon) {
      // Phase one: nothing has been estimated and nothing written to -o. The
      // spill (and the spilled count tiles) go with ~ReconCollector as this
      // returns, which is the whole point of not terminating here.
      std::cerr << "error: the recon pass failed after " << ping_count << " ping(s): "
                << fatal_error << std::endl;
      std::cerr << "Nothing was estimated and nothing was written to " << store_dir
                << "; the recon spill is removed as this run unwinds. A full scratch "
        "device is the usual cause -- free space (or pass --scratch-dir) before the "
        "re-run." << std::endl;
      return 1;
    }
    // Fixed level: the accumulator has been evicting tiles into the real -o
    // store since the first batch, so the destination is neither empty nor
    // complete -- exactly the state abortDirtyReplay() exists to report.
    return abortDirtyReplay(
      store_dir, fatal_error + ". The import stopped after " + std::to_string(ping_count) +
      " ping(s); a full device is the usual cause.");
  }
  if (limit_reached) {
    std::cout << "\nPing count limit of " << ping_count_limit << " reached" << std::endl;
  }
  std::cout << "\n  buffered " << tf_count << " transforms";
  if (!odom_topic.empty()) {std::cout << ", " << odom_samples_total << " odometry samples";}
  // One reading, used twice: this is the projection pass, which for a
  // depth-adaptive run IS the recon pass. Calling phase_secs() again below
  // would restart the clock and time the handful of statements in between
  // (the dry run reported the recon as 2.6e-05 s, cube#143 review).
  const double projection_secs = phase_secs();
  std::cout << "; projected " << ping_count << " pings in " << projection_secs << "s."
            << std::endl;

  std::cout << "\ndone." << std::endl;
  proj_totals.georeferenced_pings = static_cast<size_t>(ping_count);
  cube::report_projection_summary(proj_totals, std::cout, std::cerr);

  if (recon) {
    return cube_depth_adaptive_finish(
      *recon, level_policy, level_plan_in, level_plan_out, count_grid_out, store_dir,
      reference_store_dir, bs_store_dir, max_resident_tiles, iho_order,
      capture_spacing_scale, backscatter_mode, backscatter_curve, store_metadata,
      bs_metadata, projection_secs);
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
  reportPersisted(
    accumulator, store_dir, bs_store_dir, evicted_count,
    resident_before_final, phase_secs());
  // Build fingerprint (ADR-0003 schema 2, tiling only -- cube#143): a fixed-level
  // store records its mode, the requested cell size and the capture policy.
  const bool fingerprinted = writeFingerprint(
    store_dir, cube::BuildFingerprint::Mode::Fixed, resolution, geo_map_sheet.parameters(),
    iho_order, nullptr, {geo_map_sheet.gridLevel().level()});

  // Exit codes rank by what is wrong with the DATA: 1 says the store may be
  // incomplete, so a failed diagnostic CSV must not borrow it -- that run's
  // store and fingerprint are both fine. A missing fingerprint (2) outranks a
  // missing report (3).
  const bool tile_report_written = finishTileReport(
    tile_reporter, tile_size_report_path, tile_refresh_interval_s,
    tile_refresh_tiles_per_cycle);
  if (!fingerprinted) {
    return 2;
  }
  if (!tile_report_written) {
    return 3;
  }

  std::cout << "done!" << std::endl;
  return 0;
}
