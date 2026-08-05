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

// batch_regen: offline detections-bag -> CUBE GeoMapSheet -> bathymetry-store
// EXACT rebuild (cube_bathymetry#96). Shares import_bag's projection pipeline but
// scatters each projected sounding to a per-tile bucket on disk, then gathers each
// tile in a single unbounded pass (no eviction) so the output is BIT-EXACT vs a
// whole-survey-in-RAM build -- the authoritative off-boat rebuild path.
//
// Mirrors the bag_to_geotiff `-d` offline-projection chain (rosbag2
// SequentialReader -> tf2::BufferCore from /tf + /tf_static -> DetectionsProjector
// -> per-sounding lookupTransform("earth", frame_id, stamp) -> GeoSounding ->
// GeoMapSheet) but writes marine_bathymetry_store survey tiles instead of a GeoTIFF.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "cube_bathymetry/angular_response_curve.h"
#include "cube_bathymetry/batch_regen.h"
#include "cube_bathymetry/detections_projector.h"
#include "cube_bathymetry/geo_map_sheet.h"
#include "cube_bathymetry/geo_sounding.h"
#include "cube_bathymetry/store_import.h"
#include "cube_bathymetry/survey_index_query.h"   // --index-db dirty-tile query (#111)
#include "marine_survey_index/schema.hpp"          // openIndexDb (#111)
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
  std::cout << "usage: batch_regen [options] -o <store_dir> "
    "-d <detections_topic> <bag> [<bag> ...]\n";
  std::cout << "  Exact rebuild: scatters soundings to per-tile buckets on disk, "
    "then gathers each tile in one unbounded pass (no eviction), so the output is "
    "bit-exact vs a whole-survey-in-RAM build (cube#96). Use this for the "
    "authoritative off-boat product; use import_bag for the bounded-RAM path.\n";
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
  std::cout << "  --backscatter-correction none|empirical: per-beam angular-response "
    "correction at node-output (default none = identity). 'empirical' subtracts the "
    "per-sonar curve from --backscatter-curve (cube#81). NOTE: import_bag's "
    "'auto' (SonarInfo, cube#102) is NOT supported here -- match import_bag's "
    "effective correction explicitly for bit-exact regeneration\n";
  std::cout << "  --backscatter-curve <file>: empirical angular-response curve CSV "
    "(abs_angle_deg_center,mean_bs_db,n,db_relative_to_nadir). Required for "
    "--backscatter-correction empirical; empty -> correction is a no-op. A tier-2 "
    "curve (header '# tl_removed: true' + '# absorption_db_per_m: <a>') also makes "
    "the estimator remove per-beam 2-way TL 40*log10(R)+2*alpha*R (cube#87)\n";
  std::cout << "  --index-db <path>: DRY-RUN dirty-tile query (cube#111, ADR-0002). "
    "Given the marine_survey_index sidecar (survey_index.db) and the bags treated "
    "as newly-added, prints the store-level (L10) tiles a tile-scoped rebuild would "
    "touch (footprint + one-tile margin, rolled up from the L14 index) and their "
    "contributing bags/pass-intervals -- as a human-readable summary plus a "
    "'DIRTY_TILES_JSON:' line. Builds NOTHING and ignores -o. If the DB file is "
    "absent, reports that a real run falls back to full regen. Without --index-db "
    "the normal full-regen path is unchanged.\n";
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
      const rosbag2_cpp::Reader & reader)
    : message(message)
    {
      for (const auto & topic_info : reader.get_all_topics_and_types()) {
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


// Minimal JSON string escaper for the machine-parseable dry-run line (bag paths
// may contain characters that must be escaped). Handles the JSON control set.
std::string jsonEscape(const std::string & s)
{
  std::string out;
  out.reserve(s.size() + 2);
  for (const char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

// DRY-RUN dirty-tile query (cube#111, ADR-0002). Opens the survey index and
// reports the store-level (L10) tiles a tile-scoped rebuild would touch for the
// given bags (treated as newly-added), plus their contributing bags/intervals.
// Builds nothing. Returns a process exit code. The survey_index.db file is a
// SOFT dependency: when it is absent the function reports that a real run falls
// back to full regen (the same graceful degradation PR2's rebuild path uses).
//
// Machine contract for consumers parsing stdout: the presence of a single
// `DIRTY_TILES_JSON:` line is AUTHORITATIVE — it is emitted only on a successful
// query that found an indexed footprint, and it then carries the complete dirty
// set. Its ABSENCE means "fall back to full regen" (the index was absent, could
// not be opened/queried, was not a valid index, or answered with an EMPTY dirty
// set for a non-empty bag list — an index miss; see the guard below). Every one
// of those outcomes still exits 0 (a soft-dependency miss is not a failure), so
// consumers must key off the marker line, not the exit code.
int dirtyTileDryRun(
  const std::string & index_db_path,
  const std::vector<std::string> & bagfile_names,
  double resolution,
  const std::string & iho_order)
{
  // Store GGGS level, derived exactly as the real build derives it: the sheet
  // snaps the requested resolution to a nominal cell size, and the store tiles
  // at Level::fromCellSize of that snapped size (see main()/store_import.cpp).
  cube::GeoMapSheet sheet(static_cast<float>(resolution), iho_order);
  const gggs::Level store_level =
    gggs::Level::fromCellSize(static_cast<float>(sheet.nominalCellSizeMeters()));

  std::cout << "Dry-run dirty-tile query (--index-db " << index_db_path
            << "); nothing will be built." << std::endl;
  std::cout << "Store level: L" << static_cast<int>(store_level.level())
            << " (nominal cell " << sheet.nominalCellSizeMeters() << " m)" << std::endl;
  std::cout << "New bags (" << bagfile_names.size() << "):" << std::endl;
  for (const auto & bag : bagfile_names) {
    std::cout << "  " << bag << std::endl;
  }

  // Use the non-throwing std::error_code overload here: this check sits OUTSIDE
  // the soft-dep try/catch below, so the throwing overload would let a path error
  // (EACCES/ELOOP on the index path) abort the process uncaught -- a nonzero exit
  // with no DIRTY_TILES_JSON marker, violating the exit-0 "index unavailable =>
  // full regen" contract. Treat any such error the same as an absent index.
  std::error_code exists_ec;
  if (!std::filesystem::exists(index_db_path, exists_ec) || exists_ec) {
    // "unavailable", not "not found": this branch also fires when the path
    // check itself failed (EACCES/ELOOP), where the index may well exist.
    // The appended ec.message() names the actual cause.
    std::cerr << "note: survey index '" << index_db_path << "' unavailable"
              << (exists_ec ? " (" + exists_ec.message() + ")" : "")
              << " -- a real incremental run would fall back to FULL regen "
      "(index-absent contract, ADR-0002)." << std::endl;
    return 0;
  }

  std::vector<cube::DirtyTile> dirty;
  try {
    sqlite3 * db = marine_survey_index::openIndexDb(index_db_path);
    try {
      dirty = cube::dirtyL10Tiles(db, bagfile_names, store_level);
    } catch (...) {
      // close_v2 on the throw path: an exception can escape mid-query with a
      // statement still live, and plain sqlite3_close would then return
      // SQLITE_BUSY and LEAK the handle. close_v2 defers the free until the
      // last statement finalizes, so the handle is always reclaimed.
      sqlite3_close_v2(db);
      throw;
    }
    // Checked on the success path: SQLITE_BUSY here means the query left a
    // prepared statement unfinalized (the leak StmtGuard exists to prevent).
    // It does not invalidate the dirty set already computed, so warn rather
    // than change the marker contract -- but never fail silently.
    const int close_rc = sqlite3_close(db);
    if (close_rc != SQLITE_OK) {
      std::cerr << "warning: survey index did not close cleanly ("
                << sqlite3_errstr(close_rc)
                << ") -- a prepared statement was leaked by the query; the "
        "dirty-tile result below is still valid." << std::endl;
    }
  } catch (const std::exception & e) {
    std::cerr << "note: could not query survey index (" << e.what() << ") -- a real "
      "incremental run would fall back to FULL regen." << std::endl;
    return 0;
  }

  // Index-miss guard (cube#111). An EMPTY dirty set for a NON-EMPTY new-bag list
  // means the index answered with no footprint at all for the given bags.
  // `dirtyL10Tiles` matches `bags.path` EXACTLY, so a bag that was never indexed
  // -- or whose path is merely spelled differently than it was at index time
  // (relative vs absolute, a symlinked mount, a trailing slash) -- produces zero
  // rows, and is INDISTINGUISHABLE here from the legitimate "bag is indexed but
  // recorded no passes" case. Emitting the marker with `dirty_tile_count: 0`
  // would tell a PR2 consumer "nothing to rebuild" in the index-miss case, where
  // a FULL regen is actually required -- a silent correctness failure, since the
  // marker is documented as authoritative. Suppress the marker instead: its
  // absence is the documented "fall back to FULL regen" signal, which is
  // conservative in both cases (correct on an index miss, merely a wasted
  // rebuild for a genuinely pass-less indexed bag). Exit stays 0 -- this is a
  // soft-dependency miss, not a failure.
  if (dirty.empty() && !bagfile_names.empty()) {
    std::cerr << "note: the survey index reports no dirty tiles for the given "
      "bag(s) -- they are not in the index (paths are matched exactly) or "
      "recorded no passes. No DIRTY_TILES_JSON marker is emitted; a real "
      "incremental run would fall back to FULL regen (ADR-0002)." << std::endl;
    return 0;
  }

  // Human-readable summary.
  std::set<std::string> contributing_bags;
  std::cout << "\nDirty L" << static_cast<int>(store_level.level())
            << " tiles: " << dirty.size() << std::endl;
  for (const auto & dt : dirty) {
    std::cout << "  tile L" << static_cast<int>(dt.tile.level())
              << " row=" << dt.tile.row() << " col=" << dt.tile.column()
              << " [" << dt.tile.southLatitude() << "," << dt.tile.westLongitude()
              << " .. " << dt.tile.northLatitude() << "," << dt.tile.eastLongitude()
              << "]  (" << dt.passes.size() << " contributing pass(es))" << std::endl;
    for (const auto & p : dt.passes) {
      contributing_bags.insert(p.bag_path);
      std::cout << "      " << p.bag_path << "  " << p.sensor_type << "  " << p.topic
                << "  [" << p.t_start_ns << ".." << p.t_end_ns << "]  pings="
                << p.ping_count << std::endl;
    }
  }
  std::cout << "Contributing bags (distinct): " << contributing_bags.size() << std::endl;
  for (const auto & bag : contributing_bags) {
    std::cout << "  " << bag << std::endl;
  }

  // Machine-parseable one-line JSON (prefixed marker so a consumer can grep it).
  // Classic locale (no locale-dependent digit grouping / decimal comma) and full
  // round-trippable double precision so the emitted tile lat/lon reproduce the
  // computed bounds exactly rather than truncating to the default 6 sig-figs.
  std::ostringstream json;
  json.imbue(std::locale::classic());
  json << std::setprecision(17);
  json << "{\"store_level\":" << static_cast<int>(store_level.level())
       << ",\"dirty_tile_count\":" << dirty.size() << ",\"dirty_tiles\":[";
  bool first_tile = true;
  for (const auto & dt : dirty) {
    if (!first_tile) {json << ",";}
    first_tile = false;
    json << "{\"level\":" << static_cast<int>(dt.tile.level())
         << ",\"row\":" << dt.tile.row() << ",\"col\":" << dt.tile.column()
         << ",\"south\":" << dt.tile.southLatitude()
         << ",\"west\":" << dt.tile.westLongitude()
         << ",\"north\":" << dt.tile.northLatitude()
         << ",\"east\":" << dt.tile.eastLongitude() << ",\"passes\":[";
    bool first_pass = true;
    for (const auto & p : dt.passes) {
      if (!first_pass) {json << ",";}
      first_pass = false;
      json << "{\"bag\":\"" << jsonEscape(p.bag_path)
           << "\",\"sensor\":\"" << jsonEscape(p.sensor_type)
           << "\",\"topic\":\"" << jsonEscape(p.topic)
           << "\",\"t_start_ns\":" << p.t_start_ns
           << ",\"t_end_ns\":" << p.t_end_ns
           << ",\"ping_count\":" << p.ping_count << "}";
    }
    json << "]}";
  }
  json << "]}";
  std::cout << "DIRTY_TILES_JSON: " << json.str() << std::endl;
  return 0;
}


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
  // optional: marine_survey_index sidecar -> DRY-RUN dirty-tile query (#111)
  std::string index_db_path;
  double resolution = 1.0;
  std::string iho_order = "order1a";
  int ping_count_limit = 0;
  // Backscatter angular-response correction (cube#81). Default none = identity.
  std::string backscatter_correction_str = "none";
  std::string backscatter_curve_file;

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

  // Guarded numeric parses: std::stod/std::stoi THROW on a non-numeric value and,
  // uncaught in main, would std::terminate the process with an opaque message
  // (mirrors the hardening import_bag still lacks). Catch and route to usage() with
  // a clear diagnostic instead. usage() is [[noreturn]], so these never fall through.
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
    } else if (*arg == "--index-db") {
      index_db_path = next_value("--index-db");
    } else if (*arg == "-r") {
      resolution = parse_double("-r", next_value("-r"));
    } else if (*arg == "--iho-order") {
      iho_order = next_value("--iho-order");
    } else if (*arg == "--backscatter-correction") {
      backscatter_correction_str = next_value("--backscatter-correction");
    } else if (*arg == "--backscatter-curve") {
      backscatter_curve_file = next_value("--backscatter-curve");
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
      projector_params.minimum_range =
        parse_double("--minimum-range", next_value("--minimum-range"));
    } else if (*arg == "--maximum-range") {
      projector_params.maximum_range =
        parse_double("--maximum-range", next_value("--maximum-range"));
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

  // DRY-RUN dirty-tile query gate (cube#111): with --index-db we only report the
  // tiles a tile-scoped rebuild would touch and exit -- no store is written, so
  // -o / -d are not required. Placed before the full-regen argument checks so the
  // query can run standalone. The full-regen path below is unchanged when
  // --index-db is absent.
  if (!index_db_path.empty()) {
    if (bagfile_names.empty()) {
      std::cerr << "error: --index-db requires at least one bag (the newly-added "
        "bags to query)\n";
      usage();
    }
    if (!(resolution > 0.0)) {
      std::cerr << "error: -r resolution must be > 0 (got " << resolution << ")\n";
      usage();
    }
    // Validate --iho-order up front, like -r above. dirtyTileDryRun builds a
    // GeoMapSheet (and its Parameters) BEFORE the soft-dependency try/catch, and
    // Parameters' ctor throws std::invalid_argument on an unknown order. Left
    // unvalidated that would abort the dry-run uncaught -- std::terminate, nonzero
    // exit, and no DIRTY_TILES_JSON marker -- which a consumer keying off the
    // marker's absence would misread as the exit-0 "index absent, full regen"
    // soft-dep path. A bad --iho-order is a user error, so route it to a clean
    // usage() error here instead. (GeoMapSheet's ctor is cheap: Parameters + Level,
    // no grid allocation.)
    try {
      cube::GeoMapSheet probe(static_cast<float>(resolution), iho_order);
      (void)probe;
    } catch (const std::exception & e) {
      std::cerr << "error: " << e.what() << "\n";
      usage();
    }
    return dirtyTileDryRun(index_db_path, bagfile_names, resolution, iho_order);
  }

  if (store_dir.empty() || detections_topic.empty() || bagfile_names.empty()) {
    std::cerr << "error: -o <store_dir>, -d <detections_topic>, and "
      "at least one bag are all required\n";
    usage();
  }

  // Resolution drives the GGGS level (Level::fromCellSize); a non-positive value is
  // nonsensical and would produce a degenerate/garbage level rather than fail cleanly.
  if (!(resolution > 0.0)) {
    std::cerr << "error: -r resolution must be > 0 (got " << resolution << ")\n";
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
  if (backscatter_mode == cube::BackscatterAngleCorrection::Auto) {
    // The shared parser accepts 'auto' (cube#102), but batch_regen has no
    // SonarInfo source to auto-load a curve from -- accepting it would be a
    // silent identity. Bit-exactness vs import_bag is preserved by passing
    // the same explicit none/empirical+curve flags to both tools.
    std::cerr << "error: --backscatter-correction auto (SonarInfo delivery, "
      "cube#102) is not supported by batch_regen; pass 'none' or "
      "'empirical' with --backscatter-curve\n";
    usage();
  }
  cube::AngularResponseCurve backscatter_curve;
  if (backscatter_mode == cube::BackscatterAngleCorrection::Empirical &&
    !backscatter_curve_file.empty())
  {
    backscatter_curve = cube::loadAngularResponseCurveWithHeader(backscatter_curve_file);
  }
  if (backscatter_mode == cube::BackscatterAngleCorrection::Empirical &&
    backscatter_curve.points.empty())
  {
    // Loud, not silent: enabled but no curve loaded -> correction is a no-op.
    std::cerr << "warning: --backscatter-correction empirical but no curve was "
      "loaded from --backscatter-curve '" << backscatter_curve_file
              << "' -- the correction is ENABLED but a NO-OP (intensity emitted "
      "uncorrected). Provide a valid curve CSV.\n";
  } else if (backscatter_mode == cube::BackscatterAngleCorrection::Empirical) {
    std::cout << "Backscatter angular-response correction: empirical, "
              << backscatter_curve.points.size() << "-point curve from "
              << backscatter_curve_file;
    if (backscatter_curve.tl_removed) {
      // tier-2 (cube#87): the curve is a TL-removed residual; the estimator
      // also removes 40*log10(R) + 2*alpha*R per beam.
      std::cout << " [tier-2: TL-removed, alpha="
                << backscatter_curve.absorption_db_per_m << " dB/m]";
    }
    std::cout << std::endl;
  }
  geo_map_sheet.setBackscatterCorrection(
    backscatter_mode, backscatter_curve.points,
    backscatter_curve.tl_removed, backscatter_curve.absorption_db_per_m);

  // Sheet factory (#96): batch-regen builds one routing sheet + one gather sheet
  // per tile, all of which MUST be configured identically to this projection sheet
  // (cell size, IHO order, backscatter correction) for the rebuild to be exact.
  // Capture the correction settings by value so the factory can build many sheets.
  cube::BatchRegen::SheetFactory make_sheet =
    [resolution, iho_order, backscatter_mode,
      curve_points = backscatter_curve.points,
      tl_removed = backscatter_curve.tl_removed,
      absorption = backscatter_curve.absorption_db_per_m]() {
      auto sheet = std::make_unique<cube::GeoMapSheet>(resolution, iho_order);
      sheet->setBackscatterCorrection(
        backscatter_mode, curve_points, tl_removed, absorption);
      return sheet;
    };

  // Reference-prior seeding (#89, #96) is LAZY, per tile on first touch, driven by
  // the gather accumulator's seedNewTile: a `reference/` tile primes the CUBE
  // predicted surface only (seed_settled=false) so the blunder gate turns on WITHOUT
  // settling coarse prior depths as measured data. A reference tile at the survey
  // GGGS level gates cell-for-cell; a COARSER (multi-level) prior gates via the
  // seedNewTile level-walk fallback that resamples the finest coarser tile (#115).

  // Store-level provenance (uma#248 StoreMetadata), written once at finalize.
  // Backscatter provenance mirrors the bathy platform/sensor with an MBES-specific
  // calibration ref (empty until a beam-pattern calibration exists).
  marine_mbes_backscatter_store::StoreMetadata bs_metadata;
  bs_metadata.platform = store_metadata.platform;
  bs_metadata.sensor = store_metadata.sensor;
  bs_metadata.survey = store_metadata.survey;
  bs_metadata.date = store_metadata.date;

  cube::ImportAccumulatorConfig regen_config;
  regen_config.store_dir = store_dir;
  regen_config.reference_store_dir = reference_store_dir;
  // Match the store level to the sheet's actual (GGGS-snapped) cell size so the
  // scatter routing + gather scratch stores tile identically.
  regen_config.cell_size_m =
    static_cast<float>(geo_map_sheet.nominalCellSizeMeters());
  regen_config.bs_store_dir = bs_store_dir;
  cube::BatchRegen regen(make_sheet, regen_config);

  if (!reference_store_dir.empty()) {
    std::cout << "Reference-prior seeding from " << reference_store_dir
              << " (lazy per-tile; predicted-only blunder gate, #96)." << std::endl;
  }
  std::cout << "Exact rebuild: scatter to per-tile buckets, then gather each tile "
    "in one unbounded pass (cube#96)." << std::endl;

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
          // Per-beam slant range R = twtt*c/2 (set in the Sounding detections
          // ctor) for the tier-2 TL correction (cube#87).
          gs.sounding.slant_range = s.slant_range;
          soundings.push_back(gs);
        }
        // Scatter this ping's soundings to their per-tile buckets on disk (cube#96).
        // Nothing accumulates in RAM here; the gather (finalize) builds each tile.
        regen.addBatch(soundings);
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

  std::cout << "Gathering per-tile buckets (exact rebuild)..." << std::endl;

  // Gather every scattered tile bucket: each tile is rebuilt in a single unbounded
  // pass over the complete set of soundings that touch it (no eviction), then
  // written once to the `survey` layer (uma#248 collapsed draft/processed). Store-
  // level metadata is written once here. The scatter scratch dir is cleaned up.
  const std::size_t tile_buckets = regen.tileCount();
  regen.finalize(
    store_metadata.empty() ? nullptr : &store_metadata,
    (bs_store_dir.empty() || bs_metadata.empty()) ? nullptr : &bs_metadata);
  std::cout << "Persisted " << regen.bathyTilesPersisted()
            << " bathy tile(s) to " << store_dir << " (survey layer; "
            << tile_buckets << " tile bucket(s) gathered; build: "
            << phase_secs() << "s)." << std::endl;

  if (regen.bathyTilesPersisted() == 0) {
    std::cerr << "WARNING: no tiles had finite data -- nothing imported. Check "
      "the projector frame overrides and the detections topic." << std::endl;
  }

  // The co-estimated backscatter was surfaced into the --bs-store `survey` layer
  // (#80) from the SAME CUBE pass. By default UNCORRECTED; --backscatter-correction
  // empirical applies the per-beam angular-response correction at node-output (#81).
  if (!bs_store_dir.empty()) {
    std::cout << "Persisted " << regen.backscatterTilesPersisted()
              << " backscatter tile(s) to " << bs_store_dir << "." << std::endl;
    if (regen.backscatterTilesPersisted() == 0) {
      std::cerr << "WARNING: no cells had finite backscatter -- nothing written to "
        "the backscatter store. Check that the detections carry intensities."
                << std::endl;
    }
  }

  std::cout << "done!" << std::endl;
  return 0;
}
