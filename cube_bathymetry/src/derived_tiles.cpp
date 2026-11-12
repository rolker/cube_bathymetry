// Copyright 2026 Center for Coastal and Ocean Mapping and NOAA-UNH Joint
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

#include "cube_bathymetry/derived_tiles.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <regex>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "cube_bathymetry/build_fingerprint.h"
#include "marine_autonomy/gggs.h"

namespace cube
{

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace
{

constexpr const char * kFormat = "derived-tiles v1";
constexpr const char * kPendingFormat = "derived-tiles v1";
constexpr const char * kIncomingPrefix = ".incoming-";
constexpr const char * kRegistryName = "registry.json";

[[noreturn]] void fail(const std::string & what)
{
  throw std::runtime_error(what);
}

[[noreturn]] void failErrno(const std::string & doing, const std::string & path)
{
  fail(doing + " " + path + ": " + std::strerror(errno));
}

/// fsync a file by path (opened read-only: fsync works on any descriptor).
void fsyncFileImpl(const std::string & path)
{
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    failErrno("cannot open for fsync", path);
  }
  const int rc = ::fsync(fd);
  const int err = errno;
  ::close(fd);
  if (rc != 0) {
    errno = err;
    failErrno("fsync failed on", path);
  }
}

/// fsync a directory so a rename in it survives power loss. A filesystem with
/// no directory fsync (EINVAL/ENOTSUP) is not a failed publish: the rename is
/// still atomic, only its durability is unproven.
void fsyncDirectoryImpl(const std::string & path)
{
  const int fd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) {
    failErrno("cannot open directory for fsync", path);
  }
  const int rc = ::fsync(fd);
  const int err = errno;
  ::close(fd);
  if (rc != 0 && err != EINVAL && err != ENOTSUP && err != EROFS) {
    errno = err;
    failErrno("fsync failed on directory", path);
  }
}

/// Write @p text to @p final_path through a temp file beside it: write, fsync,
/// rename, fsync the directory.
void writeDurably(const std::string & final_path, const std::string & text)
{
  const std::string tmp = final_path + ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      fail("cannot create " + tmp);
    }
    out << text;
    out.flush();
    if (!out) {
      fail("cannot write " + tmp);
    }
  }
  fsyncFile(tmp);
  std::error_code ec;
  fs::rename(tmp, final_path, ec);
  if (ec) {
    fail("cannot rename " + tmp + " to " + final_path + ": " + ec.message());
  }
  fsyncDirectory(fs::path(final_path).parent_path().string());
}

std::string readAll(const std::string & path)
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    fail("cannot read " + path);
  }
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (in.bad()) {
    fail("read error on " + path);
  }
  return text;
}

// ---- strict JSON ----------------------------------------------------------

/// Parse @p text refusing a duplicate key anywhere (nlohmann keeps the last).
json parseStrict(const std::string & text, const std::string & what)
{
  std::vector<std::set<std::string>> keys;
  std::string duplicate;
  json::parser_callback_t callback =
    [&](int, json::parse_event_t event, json & parsed) {
      if (event == json::parse_event_t::object_start) {
        keys.emplace_back();
      } else if (event == json::parse_event_t::object_end) {
        if (!keys.empty()) {
          keys.pop_back();
        }
      } else if (event == json::parse_event_t::key && !keys.empty()) {
        if (!keys.back().insert(parsed.get<std::string>()).second && duplicate.empty()) {
          duplicate = parsed.get<std::string>();
        }
      }
      return true;
    };
  json j;
  try {
    j = json::parse(text, callback);
  } catch (const std::exception & e) {
    fail(what + ": not valid JSON (" + e.what() + ")");
  }
  if (!duplicate.empty()) {
    fail(what + ": the key '" + duplicate + "' is given twice");
  }
  return j;
}

void requireObject(const json & j, const std::string & where)
{
  if (!j.is_object()) {
    fail(where + " must be an object");
  }
}

/// The object must hold exactly the required keys plus any of the optional ones.
void checkKeys(
  const json & j, const std::string & where, std::initializer_list<const char *> required,
  std::initializer_list<const char *> optional = {})
{
  requireObject(j, where);
  for (const char * k : required) {
    if (!j.contains(k)) {
      fail(where + " lacks '" + k + "'");
    }
  }
  for (auto it = j.begin(); it != j.end(); ++it) {
    bool known = false;
    for (const char * k : required) {
      known = known || it.key() == k;
    }
    for (const char * k : optional) {
      known = known || it.key() == k;
    }
    if (!known) {
      fail(where + " has an unknown key '" + it.key() + "'");
    }
  }
}

std::string getString(const json & j, const char * key, const std::string & where)
{
  if (!j.at(key).is_string()) {
    fail(where + "." + key + " must be a string");
  }
  return j.at(key).get<std::string>();
}

double getNumber(const json & j, const char * key, const std::string & where)
{
  const json & v = j.at(key);
  if (!v.is_number()) {
    fail(where + "." + key + " must be a number");
  }
  const double d = v.get<double>();
  if (!std::isfinite(d)) {
    fail(where + "." + key + " must be finite");
  }
  return d;
}

std::vector<std::string> getStringList(const json & j, const char * key, const std::string & where)
{
  const json & v = j.at(key);
  if (!v.is_array()) {
    fail(where + "." + key + " must be an array");
  }
  std::vector<std::string> out;
  for (const json & e : v) {
    if (!e.is_string()) {
      fail(where + "." + key + " must hold strings");
    }
    out.push_back(e.get<std::string>());
  }
  return out;
}

bool isHexDigest(const std::string & s)
{
  return s.size() == 64 && std::all_of(
    s.begin(), s.end(), [](char ch) {return std::isxdigit(static_cast<unsigned char>(ch)) != 0;});
}

/// A bare file name: no separators, no dot-dot, not empty.
bool isBareName(const std::string & s)
{
  return !s.empty() && s != "." && s != ".." && s.find('/') == std::string::npos &&
         s.find('\\') == std::string::npos && s.find('\0') == std::string::npos;
}

std::string exactNumber(double v)
{
  std::ostringstream os;
  os.precision(17);
  os << v;
  return os.str();
}

json inputsToJson(const StoreInputs & in)
{
  json j;
  j["angle_basis"] = in.angle_basis;
  j["slope_baseline_deg"] = in.slope_baseline_deg;
  j["slope_layers"] = in.slope_layers;
  j["correction"] = in.correction;
  j["gain_declaration_sha256"] = in.gain_declaration_sha256;
  j["gain_reference_state"] = in.gain_reference_state;
  j["level"] = in.level;
  j["mode_fallback"] = in.mode_fallback;
  j["mode_min_pings"] = in.mode_min_pings;
  j["iho_order"] = in.iho_order;
  j["capture_spacing_scale"] = in.capture_spacing_scale;
  j["reference_store"] = in.reference_store;
  return j;
}

StoreInputs inputsFromJson(const json & j)
{
  const std::string where = "inputs";
  checkKeys(
    j, where, {"angle_basis", "slope_baseline_deg", "slope_layers", "correction",
      "gain_declaration_sha256", "gain_reference_state", "level", "mode_fallback",
      "mode_min_pings", "iho_order", "capture_spacing_scale", "reference_store"});
  StoreInputs in;
  in.angle_basis = getString(j, "angle_basis", where);
  if (in.angle_basis != "incidence") {
    fail("inputs.angle_basis must be 'incidence', not '" + in.angle_basis + "'");
  }
  in.slope_baseline_deg = getNumber(j, "slope_baseline_deg", where);
  in.slope_layers = getString(j, "slope_layers", where);
  in.correction = getString(j, "correction", where);
  if (in.correction != "none" &&
    !(in.correction.rfind("table:", 0) == 0 && isHexDigest(in.correction.substr(6))))
  {
    fail("inputs.correction must be 'none' or 'table:<sha256>', not '" + in.correction + "'");
  }
  in.gain_declaration_sha256 = getString(j, "gain_declaration_sha256", where);
  in.gain_reference_state = getString(j, "gain_reference_state", where);
  if (!in.gain_declaration_sha256.empty() && !isHexDigest(in.gain_declaration_sha256)) {
    fail("inputs.gain_declaration_sha256 must be empty or a sha256");
  }
  if (!j.at("level").is_number_integer()) {
    fail("inputs.level must be an integer");
  }
  in.level = j.at("level").get<int>();
  in.mode_fallback = getString(j, "mode_fallback", where);
  if (!j.at("mode_min_pings").is_number_integer()) {
    fail("inputs.mode_min_pings must be an integer");
  }
  in.mode_min_pings = j.at("mode_min_pings").get<int64_t>();
  in.iho_order = getString(j, "iho_order", where);
  in.capture_spacing_scale = getNumber(j, "capture_spacing_scale", where);
  in.reference_store = getString(j, "reference_store", where);
  if (!in.reference_store.empty() &&
    !(in.reference_store.rfind("sha256:", 0) == 0 && isHexDigest(in.reference_store.substr(7))))
  {
    fail("inputs.reference_store must be empty or 'sha256:<digest>'");
  }
  return in;
}

json tileToJson(const TileRecord & t)
{
  json j;
  json contributors = json::array();
  for (const BagIdentity & b : t.contributors) {
    json c;
    c["name"] = b.name;
    c["metadata_sha256"] = b.metadata_sha256;
    contributors.push_back(c);
  }
  j["contributors"] = contributors;
  json depth = json::array();
  for (const DepthTileRecord & d : t.depth_tiles) {
    json e;
    e["path"] = d.path;
    e["sha256"] = d.sha256;
    depth.push_back(e);
  }
  j["depth_tiles"] = depth;
  j["stencil_margin_m"] = t.stencil_margin_m;
  j["dropped_georef_pings"] = t.dropped_georef_pings;
  j["written"] = t.written;
  j["code_version"] = t.code_version;
  return j;
}

TileRecord tileFromJson(const json & j, const std::string & where)
{
  checkKeys(j, where,
    {"contributors", "depth_tiles", "stencil_margin_m", "dropped_georef_pings", "written",
      "code_version"});
  TileRecord t;
  if (!j.at("contributors").is_array()) {
    fail(where + ".contributors must be an array");
  }
  for (const json & c : j.at("contributors")) {
    checkKeys(c, where + ".contributors[]", {"name", "metadata_sha256"});
    BagIdentity b;
    b.name = getString(c, "name", where + ".contributors[]");
    b.metadata_sha256 = getString(c, "metadata_sha256", where + ".contributors[]");
    if (!isBareName(b.name) || !isHexDigest(b.metadata_sha256)) {
      fail(where + ".contributors[] needs a bare bag name and a sha256");
    }
    t.contributors.push_back(b);
  }
  if (!std::is_sorted(t.contributors.begin(), t.contributors.end()) ||
    std::adjacent_find(t.contributors.begin(), t.contributors.end()) != t.contributors.end())
  {
    fail(where + ".contributors must be sorted and without duplicates");
  }
  if (!j.at("depth_tiles").is_array()) {
    fail(where + ".depth_tiles must be an array");
  }
  for (const json & d : j.at("depth_tiles")) {
    checkKeys(d, where + ".depth_tiles[]", {"path", "sha256"});
    DepthTileRecord r;
    r.path = getString(d, "path", where + ".depth_tiles[]");
    r.sha256 = getString(d, "sha256", where + ".depth_tiles[]");
    if (r.path.empty() || r.path.front() == '/' || r.path.find("..") != std::string::npos ||
      !isHexDigest(r.sha256))
    {
      fail(where + ".depth_tiles[] needs a relative path and a sha256");
    }
    t.depth_tiles.push_back(r);
  }
  if (!std::is_sorted(
      t.depth_tiles.begin(), t.depth_tiles.end(),
      [](const DepthTileRecord & a, const DepthTileRecord & b) {return a.path < b.path;}))
  {
    fail(where + ".depth_tiles must be sorted by path");
  }
  t.stencil_margin_m = getNumber(j, "stencil_margin_m", where);
  if (t.stencil_margin_m < 0.0) {
    fail(where + ".stencil_margin_m must not be negative");
  }
  if (!j.at("dropped_georef_pings").is_number_unsigned()) {
    fail(where + ".dropped_georef_pings must be a non-negative integer");
  }
  t.dropped_georef_pings = j.at("dropped_georef_pings").get<uint64_t>();
  t.written = getString(j, "written", where);
  t.code_version = getString(j, "code_version", where);
  return t;
}

}  // namespace

// ---- bag identity ---------------------------------------------------------

void fsyncFile(const std::string & path) {fsyncFileImpl(path);}
void fsyncDirectory(const std::string & path) {fsyncDirectoryImpl(path);}

std::string storeInputsJson(const StoreInputs & inputs)
{
  return inputsToJson(inputs).dump();
}

std::string normalizedBagName(const std::string & path)
{
  std::string trimmed = path;
  while (trimmed.size() > 1 && trimmed.back() == '/') {
    trimmed.pop_back();
  }
  const std::string name = fs::path(trimmed).filename().string();
  if (name.empty() || name == "." || name == "..") {
    fail("a bag path with no directory name: '" + path + "'");
  }
  return name;
}

BagIdentity bagIdentity(const std::string & path)
{
  BagIdentity id;
  id.name = normalizedBagName(path);
  std::error_code ec;
  if (!fs::is_directory(path, ec)) {
    fail("bag '" + path + "' is not a bag directory (a slope run identifies each bag by its "
         "directory name and the hash of its metadata.yaml)");
  }
  const std::string metadata = (fs::path(path) / "metadata.yaml").string();
  if (!fs::is_regular_file(metadata, ec)) {
    fail("bag '" + path + "' has no metadata.yaml, so it cannot be identified");
  }
  id.metadata_sha256 = sha256File(metadata);
  return id;
}

std::string duplicateBagProblem(const std::vector<BagIdentity> & bags)
{
  std::set<std::string> seen;
  for (const BagIdentity & b : bags) {
    if (!seen.insert(b.name).second) {
      return "two bags are named '" + b.name + "'; a slope run tells bags apart by name (and "
             "metadata.yaml hash), so give each bag a distinct directory name, and give a bag "
             "only once";
    }
  }
  return "";
}

// ---- StoreInputs ----------------------------------------------------------

bool StoreInputs::operator==(const StoreInputs & o) const
{
  return difference(o).empty();
}

std::string StoreInputs::difference(const StoreInputs & o) const
{
  const auto describe = [](const char * what, const std::string & a, const std::string & b) {
      return std::string(what) + " differs (the store records '" + a + "', this run has '" + b +
             "')";
    };
  if (angle_basis != o.angle_basis) {
    return describe("the angle basis", angle_basis, o.angle_basis);
  }
  if (slope_baseline_deg != o.slope_baseline_deg) {
    return describe(
      "the slope baseline angle", exactNumber(slope_baseline_deg),
      exactNumber(o.slope_baseline_deg));
  }
  if (slope_layers != o.slope_layers) {
    return describe("the slope surface layers", slope_layers, o.slope_layers);
  }
  if (correction != o.correction) {
    return describe("the correction", correction, o.correction);
  }
  if (gain_declaration_sha256 != o.gain_declaration_sha256 ||
    gain_reference_state != o.gain_reference_state)
  {
    return describe(
      "the gain declaration", gain_declaration_sha256.empty() ? "none" : gain_declaration_sha256,
      o.gain_declaration_sha256.empty() ? "none" : o.gain_declaration_sha256);
  }
  if (mode_fallback != o.mode_fallback) {
    return describe("the table fallback", mode_fallback, o.mode_fallback);
  }
  if (mode_min_pings != o.mode_min_pings) {
    return describe("the table ping floor", std::to_string(mode_min_pings),
             std::to_string(o.mode_min_pings));
  }
  if (iho_order != o.iho_order) {
    return describe("the IHO order", iho_order, o.iho_order);
  }
  if (capture_spacing_scale != o.capture_spacing_scale) {
    return describe("the capture spacing scale", std::to_string(capture_spacing_scale),
             std::to_string(o.capture_spacing_scale));
  }
  if (reference_store != o.reference_store) {
    return describe(
      "the reference (prior) store", reference_store.empty() ? "none" : reference_store,
      o.reference_store.empty() ? "none" : o.reference_store);
  }
  if (level != o.level) {
    return describe("the store level", std::to_string(level), std::to_string(o.level));
  }
  return "";
}

// ---- DerivedTiles ---------------------------------------------------------

std::string DerivedTiles::toJson() const
{
  json j;
  j["format"] = kFormat;
  j["schema_version"] = kSchemaVersion;
  j["inputs"] = inputsToJson(inputs);
  json tiles = json::object();
  for (const auto & [name, record] : this->tiles) {
    tiles[name] = tileToJson(record);
  }
  j["tiles"] = tiles;
  if (last_publish) {
    json p;
    p["run"] = last_publish->run;
    p["written"] = last_publish->written;
    p["removed"] = last_publish->removed;
    p["registry"] = last_publish->registry;
    if (!last_publish->ledger_sha256.empty()) {
      p["ledger_sha256"] = last_publish->ledger_sha256;
    }
    j["last_publish"] = p;
  }
  return j.dump(2) + "\n";
}

DerivedTiles DerivedTiles::fromJson(const std::string & text)
{
  const json j = parseStrict(text, "derived_tiles.json");
  checkKeys(j, "derived_tiles.json", {"format", "schema_version", "inputs", "tiles"},
    {"last_publish"});
  if (!j.at("schema_version").is_number_integer() ||
    j.at("schema_version").get<int>() != kSchemaVersion)
  {
    fail("derived_tiles.json: unsupported schema_version (this build reads " +
         std::to_string(kSchemaVersion) + ")");
  }
  if (getString(j, "format", "derived_tiles.json") != kPendingFormat) {
    fail("derived_tiles.json: unsupported format '" + getString(j, "format", "") + "'");
  }
  DerivedTiles d;
  d.inputs = inputsFromJson(j.at("inputs"));
  requireObject(j.at("tiles"), "tiles");
  for (auto it = j.at("tiles").begin(); it != j.at("tiles").end(); ++it) {
    if (!isTileFileName(it.key())) {
      fail("tiles: '" + it.key() + "' is not a tile file name (<level>_<row>_<col>.tif)");
    }
    d.tiles[it.key()] = tileFromJson(it.value(), "tiles['" + it.key() + "']");
  }
  if (j.contains("last_publish")) {
    const json & p = j.at("last_publish");
    checkKeys(p, "last_publish", {"run", "written", "removed", "registry"}, {"ledger_sha256"});
    PublishRecord r;
    r.run = getString(p, "run", "last_publish");
    r.written = getStringList(p, "written", "last_publish");
    r.removed = getStringList(p, "removed", "last_publish");
    if (!p.at("registry").is_boolean()) {
      fail("last_publish.registry must be a boolean");
    }
    r.registry = p.at("registry").get<bool>();
    if (p.contains("ledger_sha256")) {
      r.ledger_sha256 = getString(p, "ledger_sha256", "last_publish");
      if (!isHexDigest(r.ledger_sha256)) {
        fail("last_publish.ledger_sha256 must be a sha256");
      }
    }
    if (!isBareName(r.run)) {
      fail("last_publish.run must be a bare name");
    }
    for (const auto * names : {&r.written, &r.removed}) {
      for (const std::string & n : *names) {
        if (!isTileFileName(n)) {
          fail("last_publish names '" + n + "', which is not a tile file name");
        }
      }
    }
    d.last_publish = r;
  }
  return d;
}

std::optional<DerivedTiles> DerivedTiles::load(const std::string & store_dir)
{
  const std::string path = (fs::path(store_dir) / kFilename).string();
  std::error_code ec;
  if (!fs::exists(path, ec)) {
    return std::nullopt;
  }
  try {
    return fromJson(readAll(path));
  } catch (const std::exception & e) {
    fail(path + ": " + e.what());
  }
}

void DerivedTiles::write(const std::string & store_dir) const
{
  fs::create_directories(store_dir);
  writeDurably((fs::path(store_dir) / kFilename).string(), toJson());
}

bool hasDerivedTilesRecord(const std::string & store_dir)
{
  std::error_code ec;
  return fs::exists(fs::path(store_dir) / DerivedTiles::kFilename, ec) ||
         fs::exists(fs::path(store_dir) / DerivedTiles::kPendingFilename, ec);
}

std::string contributorProblem(
  const std::string & tile_name, const TileRecord & recorded,
  const std::vector<BagIdentity> & given)
{
  std::vector<std::string> missing;
  for (const BagIdentity & c : recorded.contributors) {
    const auto same_name = std::find_if(
      given.begin(), given.end(), [&](const BagIdentity & g) {return g.name == c.name;});
    if (same_name == given.end()) {
      missing.push_back(c.name);
    } else if (same_name->metadata_sha256 != c.metadata_sha256) {
      return "tile " + tile_name + " was built from a bag named '" + c.name +
             "' whose metadata.yaml hashes to " + c.metadata_sha256 + ", but the bag of that "
             "name given to this run hashes to " + same_name->metadata_sha256 + ": not the same "
             "bag";
    }
  }
  if (missing.empty()) {
    return "";
  }
  std::string names;
  for (const std::string & m : missing) {
    names += (names.empty() ? "" : ", ") + m;
  }
  return "tile " + tile_name + " was built from " + std::to_string(recorded.contributors.size()) +
         " bag(s), and this run was not given: " + names + ". Rebuilding it from fewer bags "
         "would silently drop their beams; give every bag that touched it. (The bags "
         "a tile is attributed to are the conservative reach of each batch, so near a tile "
         "edge this can name a bag that contributed no cell to the tile.)";
}

std::string utcTimestamp()
{
  const std::time_t now = std::time(nullptr);
  std::tm utc{};
  gmtime_r(&now, &utc);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
  return buffer;
}

// ---- tiles and the surface ------------------------------------------------

bool isTileFileName(const std::string & name)
{
  static const std::regex pattern("^[0-9]+_[0-9]+_[0-9]+\\.tif$");
  return std::regex_match(name, pattern);
}

std::optional<TileBounds> tileBoundsFromName(const std::string & file_name)
{
  static const std::regex pattern("^([0-9]+)_([0-9]+)_([0-9]+)\\.tif$");
  std::smatch m;
  if (!std::regex_match(file_name, m, pattern)) {
    return std::nullopt;
  }
  uint64_t level = 0, row = 0, col = 0;
  try {
    level = std::stoull(m[1]);
    row = std::stoull(m[2]);
    col = std::stoull(m[3]);
  } catch (const std::exception &) {
    return std::nullopt;
  }
  if (level >= gggs::levels.size()) {
    return std::nullopt;
  }
  const gggs::LevelSpecs & spec = gggs::levels[level];
  if (row >= spec.row_count || col >= spec.columnCount(static_cast<uint32_t>(row))) {
    return std::nullopt;
  }
  const double lon_span = spec.gridLongitudinalSpan(static_cast<uint32_t>(row));
  TileBounds b;
  b.south = std::clamp(-96.0 + static_cast<double>(row) * spec.grid_angular_span, -90.0, 90.0);
  b.north = std::clamp(-96.0 + static_cast<double>(row + 1) * spec.grid_angular_span, -90.0, 90.0);
  b.west = -180.0 + static_cast<double>(col) * lon_span;
  b.east = -180.0 + static_cast<double>(col + 1) * lon_span;
  return b;
}

SurfaceTileIndex::SurfaceTileIndex(
  const std::string & surface_root, const std::vector<std::string> & layers, bool eager)
{
  for (const std::string & layer : layers) {
    const fs::path dir = fs::path(surface_root) / layer;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
      continue;
    }
    for (const auto & entry : fs::directory_iterator(dir, ec)) {
      const std::string name = entry.path().filename().string();
      const auto bounds = tileBoundsFromName(name);
      if (!bounds || !entry.is_regular_file(ec)) {
        continue;  // overviews/, sidecars, anything that is not a fine tile
      }
      Entry e;
      e.relative = layer + "/" + name;
      e.path = entry.path().string();
      e.bounds = *bounds;
      entries_.push_back(e);
    }
  }
  std::sort(
    entries_.begin(), entries_.end(),
    [](const Entry & a, const Entry & b) {return a.relative < b.relative;});
  if (eager) {
    for (Entry & e : entries_) {
      e.sha256 = sha256File(e.path);
      e.hashed = true;
      ++hashed_;
    }
  }
}

std::string SurfaceTileIndex::verifyUnchanged()
{
  for (Entry & e : entries_) {
    if (!e.hashed) {
      continue;
    }
    std::string now;
    try {
      now = sha256File(e.path);
    } catch (const std::exception & ex) {
      return "surface tile " + e.relative + " can no longer be read (" + ex.what() + ")";
    }
    if (now != e.sha256) {
      return "surface tile " + e.relative + " changed while the run was reading it";
    }
  }
  return "";
}

std::vector<DepthTileRecord> SurfaceTileIndex::tilesNear(const TileBounds & tile, double margin_m)
{
  // Conservative degrees for the margin: the smallest metres-per-degree on the
  // ellipsoid, so a tile is never left out for a rounding of the conversion.
  const double lat_margin = margin_m / 110000.0;
  const double mid_lat = 0.5 * (tile.south + tile.north);
  const double cos_lat = std::max(0.05, std::cos(mid_lat * M_PI / 180.0));
  const double lon_margin = margin_m / (110000.0 * cos_lat);
  std::vector<DepthTileRecord> out;
  for (Entry & e : entries_) {
    const bool intersects =
      e.bounds.north >= tile.south - lat_margin && e.bounds.south <= tile.north + lat_margin &&
      e.bounds.east >= tile.west - lon_margin && e.bounds.west <= tile.east + lon_margin;
    if (!intersects) {
      continue;
    }
    if (!e.hashed) {
      e.sha256 = sha256File(e.path);
      e.hashed = true;
      ++hashed_;
    }
    out.push_back({e.relative, e.sha256});
  }
  return out;
}

std::string referenceStoreDigest(const std::string & dir)
{
  if (dir.empty()) {
    return "";
  }
  std::vector<std::pair<std::string, std::string>> files;
  std::error_code ec;
  for (const char * layer : {"reference", "chart"}) {
    const fs::path layer_dir = fs::path(dir) / layer;
    if (!fs::is_directory(layer_dir, ec)) {
      continue;
    }
    fs::directory_iterator it(layer_dir, ec);
    if (ec) {
      fail("cannot read " + layer_dir.string() + ": " + ec.message());
    }
    for (const auto & entry : it) {
      const std::string name = entry.path().filename().string();
      if (isTileFileName(name) && entry.is_regular_file(ec)) {
        files.emplace_back(std::string(layer) + "/" + name, sha256File(entry.path().string()));
      }
    }
  }
  std::sort(files.begin(), files.end());
  std::string listing;
  for (const auto & [path, sha] : files) {
    listing += path + ":" + sha + "\n";
  }
  return "sha256:" + sha256Text(listing);
}

// ---- lock -----------------------------------------------------------------

std::unique_ptr<StoreLock> StoreLock::acquire(const std::string & store_dir, std::string & error)
{
  error.clear();
  std::error_code ec;
  fs::create_directories(store_dir, ec);
  if (ec) {
    error = "cannot create " + store_dir + ": " + ec.message();
    return nullptr;
  }
  const std::string path = (fs::path(store_dir) / DerivedTiles::kLockFilename).string();
  const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
  if (fd < 0) {
    error = "cannot open the lock file " + path + ": " + std::strerror(errno);
    return nullptr;
  }
  if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
    const int err = errno;
    ::close(fd);
    error = err == EWOULDBLOCK ?
      "another run (an import or a slope run) holds " + path +
      " (it is writing this store); a second run is "
      "refused, not queued" :
      "cannot lock " + path + ": " + std::strerror(err);
    return nullptr;
  }
  return std::unique_ptr<StoreLock>(new StoreLock(fd));
}

StoreLock::~StoreLock()
{
  if (fd_ >= 0) {
    ::flock(fd_, LOCK_UN);
    ::close(fd_);
  }
}

// ---- publish and recovery -------------------------------------------------

namespace
{

std::string incomingDirFor(const std::string & store_dir, const std::string & run)
{
  return (fs::path(store_dir) / "survey" / (std::string(kIncomingPrefix) + run)).string();
}

void renameOver(const std::string & from, const std::string & to)
{
  std::error_code ec;
  fs::rename(from, to, ec);
  if (ec) {
    fail("cannot rename " + from + " to " + to + ": " + ec.message());
  }
}

/// The moves of steps 3 and 4, shared by a publish and by the roll-forward of
/// an interrupted one. Idempotent: an incoming file that is already gone was
/// moved by the run that was interrupted.
void completePublish(
  const std::string & store_dir, const PublishRecord & record, const PublishFailpoint & failpoint)
{
  const fs::path survey = fs::path(store_dir) / "survey";
  const fs::path incoming = incomingDirFor(store_dir, record.run);
  const auto point = [&](const std::string & name) {
      if (failpoint) {
        failpoint(name);
      }
    };
  for (std::size_t i = 0; i < record.written.size(); ++i) {
    const fs::path from = incoming / record.written[i];
    const fs::path to = survey / record.written[i];
    std::error_code ec;
    if (fs::exists(from, ec)) {
      renameOver(from.string(), to.string());
    } else if (!fs::exists(to, ec)) {
      fail("the pending publish names tile " + record.written[i] + ", which is in neither " +
           incoming.string() + " nor " + survey.string());
    }
    point("tile-moved:" + std::to_string(i));
  }
  if (record.registry) {
    const fs::path from = incoming / kRegistryName;
    const fs::path to = fs::path(store_dir) / kRegistryName;
    std::error_code ec;
    if (fs::exists(from, ec)) {
      renameOver(from.string(), to.string());
    } else if (!fs::exists(to, ec)) {
      fail("the pending publish installs " + std::string(kRegistryName) +
           ", which is in neither place");
    }
    point("registry-moved");
  }
  for (const std::string & name : record.removed) {
    std::error_code ec;
    fs::remove(survey / name, ec);  // already gone is fine
    if (ec) {
      fail("cannot delete " + (survey / name).string() + ": " + ec.message());
    }
  }
  point("tiles-deleted");
  fsyncDirectory(survey.string());
  fsyncDirectory(store_dir);
  const fs::path pending = fs::path(store_dir) / DerivedTiles::kPendingFilename;
  renameOver(pending.string(), (fs::path(store_dir) / DerivedTiles::kFilename).string());
  fsyncDirectory(store_dir);
  point("manifest-moved");
  std::error_code ec;
  fs::remove_all(incoming, ec);  // a leftover empty directory is harmless; never fail on it
}

}  // namespace

std::string RecoveryReport::text() const
{
  std::ostringstream os;
  if (rolled_forward) {
    os << "Recovered an interrupted slope-run publish (run " << rolled_run
       << "): it had committed, so its remaining moves were completed from "
       << DerivedTiles::kPendingFilename << ".\n";
  }
  for (std::size_t i = 0; i < removed_incoming.size(); ++i) {
    if (i < incoming_was_empty.size() && incoming_was_empty[i]) {
      os << "Removed " << removed_incoming[i] << ": empty, left by a publish that had already "
        "completed.\n";
    } else {
      os << "Removed " << removed_incoming[i] << ": a slope-run publish that never committed; "
        "the store is as it was before that run.\n";
    }
  }
  return os.str();
}

RecoveryReport recoverInterruptedPublish(const std::string & store_dir)
{
  RecoveryReport report;
  std::error_code ec;
  const fs::path root(store_dir);
  const fs::path survey = root / "survey";
  // Temp files of an interrupted durable write never carry a commit.
  for (const char * leftover : {"derived_tiles.json.tmp", "derived_tiles.json.pending.tmp"}) {
    fs::remove(root / leftover, ec);
  }
  const fs::path pending = root / DerivedTiles::kPendingFilename;
  if (fs::exists(pending, ec)) {
    DerivedTiles manifest;
    try {
      manifest = DerivedTiles::fromJson(readAll(pending.string()));
    } catch (const std::exception & e) {
      fail(pending.string() + ": " + e.what() + ". It is the commit record of an interrupted "
           "publish and cannot be read, so the store cannot be repaired automatically");
    }
    if (!manifest.last_publish) {
      fail(pending.string() + " has no last_publish record, so the publish it commits cannot be "
           "completed");
    }
    completePublish(store_dir, *manifest.last_publish, {});
    report.rolled_forward = true;
    report.rolled_run = manifest.last_publish->run;
  }
  if (fs::is_directory(survey, ec)) {
    std::vector<fs::path> leftovers;
    for (const auto & entry : fs::directory_iterator(survey, ec)) {
      if (entry.is_directory(ec) &&
        entry.path().filename().string().rfind(kIncomingPrefix, 0) == 0)
      {
        leftovers.push_back(entry.path());
      }
    }
    std::sort(leftovers.begin(), leftovers.end());
    for (const fs::path & dir : leftovers) {
      report.incoming_was_empty.push_back(fs::is_empty(dir, ec));
      fs::remove_all(dir, ec);
      if (ec) {
        fail("cannot remove the abandoned publish directory " + dir.string() + ": " +
             ec.message());
      }
      report.removed_incoming.push_back(dir.string());
    }
  }
  return report;
}

void publishDerived(const PublishPlan & plan, const PublishFailpoint & failpoint)
{
  const auto point = [&](const std::string & name) {
      if (failpoint) {
        failpoint(name);
      }
    };
  const fs::path survey = fs::path(plan.store_dir) / "survey";
  const fs::path incoming = incomingDirFor(plan.store_dir, plan.run_id);
  if (!isBareName(plan.run_id)) {
    fail("publish: the run id must be a bare name");
  }
  for (const std::string & name : plan.put_tiles) {
    if (!isTileFileName(name)) {
      fail("publish: '" + name + "' is not a tile file name");
    }
  }
  for (const std::string & name : plan.delete_tiles) {
    if (!isTileFileName(name)) {
      fail("publish: '" + name + "' is not a tile file name");
    }
  }
  for (const std::string & name : plan.delete_tiles) {
    if (std::find(plan.put_tiles.begin(), plan.put_tiles.end(), name) != plan.put_tiles.end()) {
      fail("publish: '" + name + "' is both installed and deleted");
    }
  }
  std::error_code ec;
  fs::create_directories(incoming, ec);
  if (ec) {
    fail("cannot create " + incoming.string() + ": " + ec.message());
  }

  // 1. Copy into the incoming directory (same filesystem as the final place).
  for (const std::string & name : plan.put_tiles) {
    const fs::path from = fs::path(plan.scratch_survey_dir) / name;
    const fs::path to = incoming / name;
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      fail("cannot copy " + from.string() + " to " + to.string() + ": " + ec.message());
    }
    fsyncFile(to.string());
  }
  PublishRecord record;
  record.run = plan.run_id;
  record.written = plan.put_tiles;
  record.removed = plan.delete_tiles;
  record.ledger_sha256 = plan.ledger_sha256;
  if (!plan.registry_source.empty()) {
    const fs::path to = incoming / kRegistryName;
    fs::copy_file(plan.registry_source, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
      fail("cannot copy " + plan.registry_source + " to " + to.string() + ": " + ec.message());
    }
    fsyncFile(to.string());
    record.registry = true;
  }
  fsyncDirectory(incoming.string());
  fsyncDirectory(survey.string());
  point("incoming-copied");

  // 2. The commit point: the complete new manifest, under its pending name.
  DerivedTiles manifest = plan.manifest;
  manifest.last_publish = record;
  writeDurably((fs::path(plan.store_dir) / DerivedTiles::kPendingFilename).string(),
    manifest.toJson());
  point("pending-written");

  // 3 and 4. Moves, then the manifest takes its name.
  completePublish(plan.store_dir, record, failpoint);
}

}  // namespace cube
