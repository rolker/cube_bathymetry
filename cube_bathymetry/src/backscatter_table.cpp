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


#include "cube_bathymetry/backscatter_table.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace cube
{

namespace
{

constexpr const char * kFormatLine = "# format: angular-response-table v1";
/// Any version of the format: a file whose first line starts with this is a
/// table, and one this loader cannot read fails loudly rather than being
/// handed to the single-curve loader (which reads it as an empty curve).
constexpr const char * kFormatPrefix = "# format: angular-response-table";
constexpr const char * kColumns =
  "mode,term,x,angle_lo_deg,angle_hi_deg,value_db,n,var_db2,db_relative_to_nadir,"
  "se_db,offset_db_vs_ref,offset_se_db,offset_anchored,declared_tvg_model,"
  "declared_tvg_absorption_db_per_km";
constexpr std::size_t kColumnCount = 15;

std::string stripCr(std::string line)
{
  while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
    line.pop_back();
  }
  return line;
}

std::string trim(const std::string & s)
{
  const auto b = s.find_first_not_of(" \t");
  if (b == std::string::npos) {
    return "";
  }
  const auto e = s.find_last_not_of(" \t");
  return s.substr(b, e - b + 1);
}

std::vector<std::string> splitCsv(const std::string & line)
{
  std::vector<std::string> out;
  std::string field;
  for (char c : line) {
    if (c == ',') {
      out.push_back(field);
      field.clear();
    } else {
      field.push_back(c);
    }
  }
  out.push_back(field);
  return out;
}

/// Parse a decimal field. Returns false on a malformed value; an empty field
/// is NaN (and true) so the caller decides whether empty is allowed.
bool parseNumber(const std::string & field, double & out)
{
  const std::string t = trim(field);
  if (t.empty()) {
    out = std::numeric_limits<double>::quiet_NaN();
    return true;
  }
  if (t == "nan" || t == "NaN") {
    out = std::numeric_limits<double>::quiet_NaN();
    return true;
  }
  char * end = nullptr;
  errno = 0;
  const double v = std::strtod(t.c_str(), &end);
  if (end == t.c_str() || *end != '\0' || errno == ERANGE) {
    return false;
  }
  out = v;
  return true;
}

bool parseCount(const std::string & field, uint64_t & out)
{
  const std::string t = trim(field);
  if (t.empty()) {
    out = 0;
    return true;
  }
  if (t.find_first_not_of("0123456789") != std::string::npos) {
    return false;
  }
  try {
    out = std::stoull(t);
  } catch (const std::exception &) {
    return false;
  }
  return true;
}

bool parseBool(const std::string & field, bool & out)
{
  const std::string t = trim(field);
  if (t == "true") {
    out = true;
    return true;
  }
  if (t == "false") {
    out = false;
    return true;
  }
  return false;
}

/// Key/value tokens of a `# mode_summary:` line (`pings=1457065`).
std::map<std::string, std::string> tokens(const std::string & text)
{
  std::map<std::string, std::string> out;
  std::istringstream in(text);
  std::string tok;
  while (in >> tok) {
    const auto eq = tok.find('=');
    if (eq != std::string::npos) {
      out[tok.substr(0, eq)] = tok.substr(eq + 1);
    }
  }
  return out;
}

/// Parse `# range_angle_groups_deg:` (`10-30,30-50,50-90`, the writer's `%g-%g`
/// list): every declared group, ascending and non-overlapping, inside
/// [0, 90] degrees. False (with @p why) on anything else.
bool parseAngleGroups(
  const std::string & text, std::vector<std::pair<double, double>> & out, std::string & why)
{
  out.clear();
  for (const auto & raw : splitCsv(text)) {
    const std::string item = trim(raw);
    const char * begin = item.c_str();
    char * end = nullptr;
    errno = 0;
    const double lo = std::strtod(begin, &end);
    if (end == begin || *end != '-' || errno == ERANGE) {
      why = "group '" + item + "' is not <lo>-<hi>";
      return false;
    }
    const char * hi_begin = end + 1;
    const double hi = std::strtod(hi_begin, &end);
    if (end == hi_begin || *end != '\0' || errno == ERANGE) {
      why = "group '" + item + "' is not <lo>-<hi>";
      return false;
    }
    if (!std::isfinite(lo) || !std::isfinite(hi) || !(lo >= 0.0) || !(lo < hi) ||
      !(hi <= 90.0))
    {
      why = "group '" + item + "' needs 0 <= lo < hi <= 90";
      return false;
    }
    if (!out.empty() && lo < out.back().second) {
      why = "group '" + item + "' overlaps or precedes the one before it";
      return false;
    }
    out.emplace_back(lo, hi);
  }
  if (out.empty()) {
    why = "no groups";
    return false;
  }
  return true;
}

/// True when two group bounds written with the same `%g` are the same group.
bool sameBounds(const std::pair<double, double> & a, const std::pair<double, double> & b)
{
  return std::abs(a.first - b.first) <= 1e-9 && std::abs(a.second - b.second) <= 1e-9;
}

/// The first non-blank line of @p in, with a UTF-8 byte-order mark, the line
/// ending and surrounding blanks removed; @p line_no counts every line read.
/// False when the stream holds no such line.
bool firstContentLine(std::istream & in, std::string & out, std::size_t & line_no)
{
  std::string line;
  bool first = true;
  while (std::getline(in, line)) {
    ++line_no;
    if (first && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
      line.erase(0, 3);
    }
    first = false;
    line = trim(stripCr(line));
    if (!line.empty()) {
      out = line;
      return true;
    }
  }
  return false;
}

bool hasFormatPrefix(const std::string & line)
{
  return line.compare(0, std::char_traits<char>::length(kFormatPrefix), kFormatPrefix) == 0;
}

/// Per-mode scalar columns as raw text, compared verbatim between rows.
struct ModeScalars
{
  std::string offset_db, offset_se_db, anchored, tvg_model, tvg_absorption;
  bool operator==(const ModeScalars & o) const
  {
    return offset_db == o.offset_db && offset_se_db == o.offset_se_db &&
           anchored == o.anchored && tvg_model == o.tvg_model &&
           tvg_absorption == o.tvg_absorption;
  }
};

struct ModeBuild
{
  ModeEntry entry;
  ModeScalars scalars;
  /// (lo, hi) -> nodes, in order of first appearance.
  std::vector<std::pair<std::pair<double, double>, std::vector<RangeNode>>> groups;
  /// Index into `groups` by bounds, and the keys already seen, so the
  /// duplicate checks stay O(log n) per row (an operator file of any size
  /// must not stall a configure).
  std::map<std::pair<double, double>, std::size_t> group_index;
  std::set<double> angle_bins;
  std::set<std::tuple<double, double, double>> range_nodes;
};

/// Bounds on what the loader reads: a real table is a few thousand rows of
/// short lines (modes x angle bins + modes x groups x nodes), so these leave
/// orders of magnitude of headroom and only stop a wrong or runaway file.
constexpr std::uintmax_t kMaxTableBytes = 64ull * 1024 * 1024;
constexpr std::size_t kMaxTableRows = 1000000;
constexpr std::size_t kMaxLineLength = 4096;
/// The format probe reads no further than this looking for the first line.
constexpr std::size_t kMaxProbeBytes = 64 * 1024;
/// How much of a line an error message quotes.
constexpr std::size_t kMaxQuotedLength = 200;

/// @p line as an error message quotes it: whole up to kMaxQuotedLength
/// characters, else its start and its length. The first line is read before
/// the per-line length bound applies, so it can be the whole file (review
/// round 3).
std::string quotedLine(const std::string & line)
{
  if (line.size() <= kMaxQuotedLength) {
    return line;
  }
  return line.substr(0, kMaxQuotedLength) + "... (" + std::to_string(line.size()) +
         " characters)";
}

/// True when @p path names a regular file (symlinks followed). Checked
/// before opening: opening a FIFO for reading blocks until a writer comes,
/// and a device such as /dev/zero has no size to bound the read by.
bool isRegularFile(const std::string & path)
{
  std::error_code ec;
  const auto status = std::filesystem::status(path, ec);
  return !ec && std::filesystem::is_regular_file(status);
}

/// Read at most @p limit + 1 bytes of @p in into @p out: more than @p limit
/// means the file is larger than allowed. False on a read error.
bool readBounded(std::istream & in, std::uintmax_t limit, std::string & out)
{
  out.clear();
  char buffer[1 << 16];
  while (out.size() <= limit &&
    (in.read(buffer, sizeof(buffer)) || in.gcount() > 0))
  {
    out.append(buffer, static_cast<std::size_t>(in.gcount()));
  }
  return !in.bad();
}

}  // namespace

std::string keyNumber(double value, double scale)
{
  if (!std::isfinite(value) || value <= 0.0) {
    return "?";
  }
  const double v = value * scale;
  if (!std::isfinite(v) || v <= 0.0) {
    return "?";
  }
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.2e", v);
  const std::string s(buf);
  const auto e = s.find('e');
  if (e == std::string::npos) {
    return "?";  // unreachable for a finite positive v
  }
  std::string digits;
  for (char c : s.substr(0, e)) {
    if (c != '.') {
      digits.push_back(c);
    }
  }
  const int exponent = std::stoi(s.substr(e + 1));
  const int shift = exponent - 2;  // value = digits * 10^shift
  if (shift >= 0) {
    return digits + std::string(static_cast<std::size_t>(shift), '0');
  }
  const std::size_t width = static_cast<std::size_t>(1 - shift);  // >= 1 integer digit
  std::string padded = digits.size() < width ?
    std::string(width - digits.size(), '0') + digits : digits;
  const std::size_t split = static_cast<std::size_t>(static_cast<int>(padded.size()) + shift);
  std::string out = padded.substr(0, split) + "." + padded.substr(split);
  while (!out.empty() && out.back() == '0') {
    out.pop_back();
  }
  if (!out.empty() && out.back() == '.') {
    out.pop_back();
  }
  return out;
}

std::string canonicalModeKey(
  const std::vector<float> & pulse_lengths_s,
  const std::vector<float> & bandwidths_hz,
  const std::vector<uint8_t> & tx_signal_types,
  float frequency_hz)
{
  const std::size_t n_sectors = std::max<std::size_t>(
    {pulse_lengths_s.size(), bandwidths_hz.size(), tx_signal_types.size(), 1});
  std::string key;
  for (std::size_t i = 0; i < n_sectors; ++i) {
    if (i > 0) {
      key += "+";
    }
    const char * kind = "UNK";
    if (i < tx_signal_types.size()) {
      switch (tx_signal_types[i]) {
        case 1: kind = "CW"; break;
        case 2: kind = "FMU"; break;
        case 3: kind = "FMD"; break;
        default: kind = "UNK"; break;
      }
    }
    const std::string pulse = i < pulse_lengths_s.size() ?
      keyNumber(static_cast<double>(pulse_lengths_s[i]), 1e6) : "?";
    const std::string width = i < bandwidths_hz.size() ?
      keyNumber(static_cast<double>(bandwidths_hz[i]), 1.0) : "?";
    key += std::string(kind) + "_" + pulse + "us_" + width + "Hz";
  }
  return key + "_" + keyNumber(static_cast<double>(frequency_hz), 1e-3) + "kHz";
}

const char * toString(DeclaredTvg tvg)
{
  switch (tvg) {
    case DeclaredTvg::None: return "NONE";
    case DeclaredTvg::Log20R: return "20LOGR";
    case DeclaredTvg::Log40R: return "40LOGR";
    case DeclaredTvg::Custom: return "CUSTOM";
    case DeclaredTvg::Mixed: return "MIXED";
    case DeclaredTvg::Unknown:
    default: return "UNKNOWN";
  }
}

bool parseDeclaredTvg(const std::string & text, DeclaredTvg & out)
{
  const std::string t = trim(text);
  if (t == "UNKNOWN") {out = DeclaredTvg::Unknown; return true;}
  if (t == "NONE") {out = DeclaredTvg::None; return true;}
  if (t == "20LOGR") {out = DeclaredTvg::Log20R; return true;}
  if (t == "40LOGR") {out = DeclaredTvg::Log40R; return true;}
  if (t == "CUSTOM") {out = DeclaredTvg::Custom; return true;}
  if (t == "MIXED") {out = DeclaredTvg::Mixed; return true;}
  return false;
}

DeclaredTvg declaredTvgFromMessageValue(uint8_t tvg_model)
{
  switch (tvg_model) {
    case 1: return DeclaredTvg::None;
    case 2: return DeclaredTvg::Log20R;
    case 3: return DeclaredTvg::Log40R;
    case 4: return DeclaredTvg::Custom;
    default: return DeclaredTvg::Unknown;
  }
}

bool DeclaredTvgState::operator==(const DeclaredTvgState & other) const
{
  if (model != other.model) {
    return false;
  }
  const bool a_nan = std::isnan(absorption_db_per_km);
  const bool b_nan = std::isnan(other.absorption_db_per_km);
  if (a_nan || b_nan) {
    return a_nan && b_nan;
  }
  // An infinite declaration (the loader refuses one; a SonarInfo may carry
  // it) equals only the same infinity: the relative test below would call
  // inf equal to any finite value, and inf - inf is NaN.
  if (std::isinf(absorption_db_per_km) || std::isinf(other.absorption_db_per_km)) {
    return absorption_db_per_km == other.absorption_db_per_km;
  }
  // The table writes the absorption with %.6g, so a declared float32 comes
  // back rounded to 6 significant digits (relative error up to 5e-6): equal
  // within a relative 1e-5, never compared bit for bit.
  const double a = absorption_db_per_km, b = other.absorption_db_per_km;
  return std::abs(a - b) <= 1e-5 * std::max(std::abs(a), std::abs(b));
}

std::string DeclaredTvgState::toString() const
{
  std::string s = cube::toString(model);
  if (!std::isnan(absorption_db_per_km)) {
    std::ostringstream os;
    os << s << "/" << absorption_db_per_km << " dB/km";
    return os.str();
  }
  return s;
}

double RangeTermGroup::at(double x) const
{
  if (nodes.empty()) {
    return 0.0;
  }
  if (nodes.size() == 1 || x <= nodes.front().x) {
    return nodes.front().value_db;
  }
  if (x >= nodes.back().x) {
    return nodes.back().value_db;
  }
  for (std::size_t i = 1; i < nodes.size(); ++i) {
    if (x <= nodes[i].x) {
      const double x0 = nodes[i - 1].x, x1 = nodes[i].x;
      const double span = x1 - x0;
      if (span <= 0.0) {
        return nodes[i].value_db;
      }
      const double t = (x - x0) / span;
      return nodes[i - 1].value_db + t * (nodes[i].value_db - nodes[i - 1].value_db);
    }
  }
  return nodes.back().value_db;  // unreachable
}

double ModeEntry::relativeDb(double abs_angle_deg) const
{
  if (angle.empty()) {
    return 0.0;
  }
  if (abs_angle_deg <= angle.front().abs_angle_deg) {
    return angle.front().db_relative_to_nadir;
  }
  if (abs_angle_deg >= angle.back().abs_angle_deg) {
    return angle.back().db_relative_to_nadir;
  }
  for (std::size_t i = 1; i < angle.size(); ++i) {
    if (abs_angle_deg <= angle[i].abs_angle_deg) {
      const double a0 = angle[i - 1].abs_angle_deg, a1 = angle[i].abs_angle_deg;
      const double span = a1 - a0;
      if (span <= 0.0) {
        return angle[i].db_relative_to_nadir;
      }
      const double t = (abs_angle_deg - a0) / span;
      return angle[i - 1].db_relative_to_nadir +
             t * (angle[i].db_relative_to_nadir - angle[i - 1].db_relative_to_nadir);
    }
  }
  return angle.back().db_relative_to_nadir;  // unreachable
}

const AngleBin * ModeEntry::binFor(double abs_angle_deg) const
{
  const AngleBin * best = nullptr;
  double best_distance = std::numeric_limits<double>::infinity();
  for (const auto & bin : angle) {
    const double d = std::abs(bin.abs_angle_deg - abs_angle_deg);
    if (d < best_distance) {  // strict: a tie keeps the lower bin
      best_distance = d;
      best = &bin;
    }
  }
  return best;
}

const RangeTermGroup * ModeEntry::groupFor(double abs_angle_deg) const
{
  if (groups.empty()) {
    return nullptr;
  }
  if (abs_angle_deg < groups.front().angle_lo_deg) {
    return &groups.front();
  }
  for (const auto & g : groups) {
    if (g.angle_lo_deg <= abs_angle_deg && abs_angle_deg < g.angle_hi_deg) {
      return &g;
    }
  }
  return nullptr;
}

std::optional<uint16_t> BackscatterTable::find(const std::string & key) const
{
  const auto it = index_.find(key);
  if (it == index_.end()) {
    return std::nullopt;
  }
  return it->second;
}

const ModeEntry * BackscatterTable::entry(uint16_t correction_idx) const
{
  if (correction_idx == kCorrectionUnresolved || correction_idx > kMaxCorrectionEntries) {
    return nullptr;
  }
  const std::size_t k = static_cast<std::size_t>(correction_idx) - 1;
  if (k >= entries.size()) {
    return nullptr;
  }
  return &entries[k];
}

bool isAngularResponseTable(const std::string & path)
{
  if (path.empty() || !isRegularFile(path)) {
    return false;
  }
  std::ifstream file(path, std::ios::binary);
  std::string head;
  if (!file || !readBounded(file, kMaxProbeBytes, head)) {
    return false;
  }
  std::istringstream in(head);
  std::string line;
  std::size_t line_no = 0;
  return firstContentLine(in, line, line_no) && hasFormatPrefix(line);
}

std::string explicitBackscatterFileProblem(const std::string & path)
{
  std::error_code ec;
  const auto status = std::filesystem::status(path, ec);
  if (status.type() == std::filesystem::file_type::not_found) {
    return "";
  }
  if (ec) {
    return "cannot be read (" + ec.message() + ")";
  }
  if (!std::filesystem::is_regular_file(status)) {
    return "is not a regular file (a curve or table is read from a file, never a FIFO, "
           "device or directory)";
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return "cannot be opened for reading";
  }
  return "";
}

BackscatterTable loadBackscatterTable(
  const std::string & path, std::string & error, std::string * bytes_out)
{
  BackscatterTable table;
  table.path = path;
  error.clear();
  if (bytes_out) {
    bytes_out->clear();
  }
  auto fail = [&](std::size_t line_no, const std::string & why) {
      error = path + ":" + std::to_string(line_no) + ": " + why;
      return BackscatterTable();
    };

  if (!std::filesystem::exists(path)) {
    error = path + ": cannot open";
    return BackscatterTable();
  }
  if (!isRegularFile(path)) {
    error = path + ": not a regular file (a table is read from a file, never a FIFO or "
      "device)";
    return BackscatterTable();
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = path + ": cannot open";
    return BackscatterTable();
  }
  // One bounded read: the size check, the parse and the caller's hash all
  // see these bytes, never a second read of a file that may have changed.
  std::string content;
  if (!readBounded(file, kMaxTableBytes, content)) {
    error = path + ": read error";
    return BackscatterTable();
  }
  if (content.size() > kMaxTableBytes) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(path, ec);
    error = path + ": " + (ec ? std::string("over ") + std::to_string(kMaxTableBytes) :
      std::to_string(size)) + " bytes, more than a table can be (" +
      std::to_string(kMaxTableBytes) + ")";
    return BackscatterTable();
  }
  std::istringstream in(content);
  std::string line;
  std::size_t line_no = 0;
  if (!firstContentLine(in, line, line_no)) {
    return fail(line_no, "empty file, not an angular-response table");
  }
  if (line != kFormatLine) {
    if (hasFormatPrefix(line)) {
      return fail(line_no, "unsupported table format '" + quotedLine(line) +
               "' (this loader reads '" +
               kFormatLine + "')");
    }
    return fail(line_no, std::string("not an angular-response table (first line must be '") +
             kFormatLine + "')");
  }

  // Header comments, then the column line.
  std::map<std::string, std::string> header;
  /// (line number, text): an error names the summary's own line.
  std::vector<std::pair<std::size_t, std::string>> summaries;
  bool columns_seen = false;
  while (std::getline(in, line)) {
    ++line_no;
    line = stripCr(line);
    if (line.size() > kMaxLineLength) {
      return fail(line_no, "line longer than " + std::to_string(kMaxLineLength) + " characters");
    }
    if (line.empty()) {
      continue;
    }
    if (line[0] == '#') {
      const std::string body = trim(line.substr(1));
      const auto colon = body.find(':');
      if (colon == std::string::npos) {
        continue;  // a free comment
      }
      const std::string key = trim(body.substr(0, colon));
      const std::string value = trim(body.substr(colon + 1));
      if (key == "mode_summary") {
        summaries.emplace_back(line_no, value);
      } else if (!header.count(key)) {
        header[key] = value;
      } else if (key == "gain_declaration_sha256" || key == "gain_reference_state") {
        return fail(line_no, "'# " + key + ":' given twice");
      }
      continue;
    }
    if (line != kColumns) {
      return fail(line_no, "unexpected column line '" + line + "'");
    }
    columns_seen = true;
    break;
  }
  if (!columns_seen) {
    return fail(line_no, "no column line");
  }

  auto headerNumber = [&](const char * key, double & out, bool required) -> bool {
      const auto it = header.find(key);
      if (it == header.end()) {
        return !required;
      }
      return parseNumber(it->second, out) && std::isfinite(out);
    };
  if (!header.count("reference_mode")) {
    return fail(line_no, "header lacks '# reference_mode:'");
  }
  table.reference_mode = header["reference_mode"];
  if (!headerNumber("range_ref_m", table.range_ref_m, true) || !(table.range_ref_m > 0.0)) {
    return fail(line_no, "header lacks a finite positive '# range_ref_m:'");
  }
  if (!headerNumber("range_node_spacing_decades", table.range_node_spacing_decades, false)) {
    return fail(line_no, "malformed '# range_node_spacing_decades:'");
  }
  if (!headerNumber("angle_bin_width_deg", table.angle_bin_width_deg, false)) {
    return fail(line_no, "malformed '# angle_bin_width_deg:'");
  }
  // The writer's contract (derive_angular_response.py refuses --bin-width-deg
  // outside 0 < w <= 90). A zero or negative width here would turn the
  // outside-bin test off without a word, so a beam past the outermost bin got
  // the clamped edge value instead of the fallback (PR #175 review). An
  // absent header stays allowed: the loader warns about it elsewhere.
  if (std::isfinite(table.angle_bin_width_deg) &&
    !(table.angle_bin_width_deg > 0.0 && table.angle_bin_width_deg <= 90.0))
  {
    return fail(
      line_no, "'# angle_bin_width_deg:' must be > 0 and <= 90 degrees (the writer's "
      "contract), got " + header["angle_bin_width_deg"]);
  }
  if (!headerNumber("specular_exclusion_deg", table.specular_exclusion_deg, false)) {
    return fail(line_no, "malformed '# specular_exclusion_deg:'");
  }
  if (header.count("source_bags")) {
    table.source_bags = header["source_bags"];
  }
  // The gain-state record is both lines or neither, each with a value: one
  // without the other (or an empty one) would read as "no record" or as half
  // a record, and either passes the declaration check wrongly (#176).
  if (header.count("gain_declaration_sha256") != header.count("gain_reference_state")) {
    return fail(
      line_no, "'# gain_declaration_sha256:' and '# gain_reference_state:' must be given "
      "together (a gain-state record is both or neither)");
  }
  if (header.count("gain_declaration_sha256")) {
    table.gain_declaration_sha256 = header["gain_declaration_sha256"];
    table.gain_reference_state = header["gain_reference_state"];
    if (table.gain_declaration_sha256.empty() || table.gain_reference_state.empty()) {
      return fail(
        line_no, "'# gain_declaration_sha256:' / '# gain_reference_state:' must not be empty");
    }
  }
  // The declared range-term groups (cube#172 review): the writer omits the
  // rows of a group it did not fit (its zero term), so the rows alone cannot
  // say where the first group starts. With this line every mode carries every
  // declared group, empty ones as zero terms, and the specular band takes the
  // first DECLARED group; without it (a hand-made table) the groups are the
  // ones the rows name.
  std::vector<std::pair<double, double>> declared_groups;
  if (header.count("range_angle_groups_deg")) {
    std::string why;
    if (!parseAngleGroups(header["range_angle_groups_deg"], declared_groups, why)) {
      return fail(line_no, "malformed '# range_angle_groups_deg:' (" + why + ")");
    }
  }

  // Per-mode summaries: pings and beams.
  std::map<std::string, std::pair<uint64_t, uint64_t>> summary_counts;
  for (const auto & [summary_line, s] : summaries) {
    std::istringstream is(s);
    std::string key;
    is >> key;
    const auto kv = tokens(s);
    uint64_t pings = 0, beams = 0;
    // The ping count decides the ping floor (--backscatter-mode-min-pings):
    // a summary without it would read as 0 and quietly fall under any floor.
    if (!kv.count("pings")) {
      return fail(summary_line, "'# mode_summary: " + s + "' has no pings=");
    }
    if (!parseCount(kv.at("pings"), pings) || trim(kv.at("pings")).empty()) {
      return fail(summary_line, "malformed pings in '# mode_summary: " + s + "'");
    }
    if (kv.count("beams") && !parseCount(kv.at("beams"), beams)) {
      return fail(summary_line, "malformed beams in '# mode_summary: " + s + "'");
    }
    if (summary_counts.count(key)) {
      return fail(summary_line, "two '# mode_summary:' lines for '" + key + "'");
    }
    summary_counts[key] = {pings, beams};
  }

  // Rows.
  std::vector<ModeBuild> builds;
  std::map<std::string, std::size_t> build_index;
  std::size_t rows = 0;
  while (std::getline(in, line)) {
    ++line_no;
    line = stripCr(line);
    if (line.size() > kMaxLineLength) {
      return fail(line_no, "line longer than " + std::to_string(kMaxLineLength) + " characters");
    }
    // Every line counts against the bound, comments included.
    if (++rows > kMaxTableRows) {
      return fail(line_no, "more than " + std::to_string(kMaxTableRows) + " lines");
    }
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const auto f = splitCsv(line);
    if (f.size() != kColumnCount) {
      return fail(line_no, "expected " + std::to_string(kColumnCount) + " fields, got " +
               std::to_string(f.size()));
    }
    const std::string mode = trim(f[0]);
    if (mode.empty() || !std::isalpha(static_cast<unsigned char>(mode[0]))) {
      return fail(line_no, "mode key '" + mode + "' must start with a letter");
    }
    const std::string term = trim(f[1]);
    double x = 0.0, lo = 0.0, hi = 0.0, value = 0.0, var = 0.0, rel = 0.0, se = 0.0;
    uint64_t n = 0;
    if (!parseNumber(f[2], x) || !std::isfinite(x)) {
      return fail(line_no, "malformed x '" + f[2] + "'");
    }
    if (!parseNumber(f[3], lo) || !parseNumber(f[4], hi)) {
      return fail(line_no, "malformed angle bounds");
    }
    if (!parseNumber(f[5], value) || !std::isfinite(value)) {
      return fail(line_no, "malformed value_db '" + f[5] + "'");
    }
    if (!parseCount(f[6], n)) {
      return fail(line_no, "malformed n '" + f[6] + "'");
    }
    if (!parseNumber(f[7], var) || !parseNumber(f[8], rel) || !parseNumber(f[9], se)) {
      return fail(line_no, "malformed var_db2 / db_relative_to_nadir / se_db");
    }
    ModeScalars scalars{trim(f[10]), trim(f[11]), trim(f[12]), trim(f[13]), trim(f[14])};

    auto it = build_index.find(mode);
    if (it == build_index.end()) {
      if (builds.size() >= kMaxCorrectionEntries) {
        return fail(line_no, "more than " + std::to_string(kMaxCorrectionEntries) + " modes");
      }
      ModeBuild b;
      b.entry.key = mode;
      b.scalars = scalars;
      bool anchored = false;
      if (!parseBool(scalars.anchored, anchored)) {
        return fail(line_no, "malformed offset_anchored '" + scalars.anchored + "'");
      }
      b.entry.offset_anchored = anchored;
      double offset = 0.0, offset_se = 0.0;
      if (!parseNumber(scalars.offset_db, offset) || !parseNumber(scalars.offset_se_db,
          offset_se))
      {
        return fail(line_no, "malformed offset columns");
      }
      if (anchored && !std::isfinite(offset)) {
        return fail(line_no, "anchored mode '" + mode + "' has no finite offset_db_vs_ref");
      }
      b.entry.offset_db_vs_ref = anchored ? offset : std::numeric_limits<double>::quiet_NaN();
      b.entry.offset_se_db = anchored ? offset_se : std::numeric_limits<double>::quiet_NaN();
      if (!parseDeclaredTvg(scalars.tvg_model, b.entry.declared.model)) {
        return fail(line_no, "malformed declared_tvg_model '" + scalars.tvg_model + "'");
      }
      double alpha = 0.0;
      // A number or `nan` (MIXED / undeclared); never an infinity, and
      // never a value float32 cannot hold (it would become one).
      if (!parseNumber(scalars.tvg_absorption, alpha) || std::isinf(alpha) ||
        (std::isfinite(alpha) && std::abs(alpha) > std::numeric_limits<float>::max()))
      {
        return fail(line_no, "malformed declared_tvg_absorption_db_per_km '" +
                 scalars.tvg_absorption + "' (a finite number or nan)");
      }
      b.entry.declared.absorption_db_per_km = static_cast<float>(alpha);
      const auto sc = summary_counts.find(mode);
      if (sc != summary_counts.end()) {
        b.entry.pings = sc->second.first;
        b.entry.beams = sc->second.second;
      }
      build_index[mode] = builds.size();
      builds.push_back(std::move(b));
      it = build_index.find(mode);
    } else if (!(builds[it->second].scalars == scalars)) {
      return fail(line_no, "per-mode columns of '" + mode + "' disagree with an earlier row");
    }
    ModeBuild & b = builds[it->second];

    if (term == "angle") {
      if (!std::isfinite(rel)) {
        return fail(line_no, "angle row without db_relative_to_nadir");
      }
      // The writer centres bin j at (j + 0.5) * width for j < ceil(90 / width),
      // so a width that does not divide 90 (4, 8, 20, 40 deg ...) puts the
      // last centre at or past 90 deg: that bin's lower edge is still under 90
      // and it holds real beams. Beams at or beyond 90 deg are uncorrectable
      // whatever the bins say (tableBeamStatus), so only a centre no writer
      // can produce (negative, or >= 180 deg, past any width <= 90) is refused.
      if (!(x >= 0.0 && x < 180.0)) {
        return fail(line_no, "angle bin centre " + f[2] + " is outside [0, 180) deg");
      }
      if (!b.angle_bins.insert(x).second) {
        return fail(line_no, "duplicate angle bin " + f[2] + " for '" + mode + "'");
      }
      AngleBin bin;
      bin.abs_angle_deg = x;
      bin.value_db = value;
      bin.db_relative_to_nadir = rel;
      bin.var_db2 = (std::isfinite(var) && var > 0.0) ?
        var : std::numeric_limits<double>::quiet_NaN();
      bin.se_db = se;
      bin.n = n;
      if (!bin.weighable()) {
        ++b.entry.unweighable_bins;
      }
      b.entry.angle.push_back(bin);
    } else if (term == "range") {
      if (!std::isfinite(lo) || !std::isfinite(hi) || !(lo < hi)) {
        return fail(line_no, "range row needs angle_lo_deg < angle_hi_deg");
      }
      if (!(lo >= 0.0 && hi <= 90.0)) {
        return fail(line_no, "range row group " + trim(f[3]) + "-" + trim(f[4]) +
                 " is outside [0, 90] deg");
      }
      RangeNode node;
      node.x = x;
      node.value_db = value;
      node.se_db = se;
      node.n = n;
      if (!b.range_nodes.insert(std::make_tuple(lo, hi, x)).second) {
        return fail(line_no, "duplicate range node " + f[2] + " for '" + mode + "'");
      }
      auto slot = b.group_index.find({lo, hi});
      if (slot == b.group_index.end()) {
        slot = b.group_index.emplace(std::make_pair(lo, hi), b.groups.size()).first;
        b.groups.push_back({{lo, hi}, {}});
      }
      b.groups[slot->second].second.push_back(node);
    } else {
      return fail(line_no, "unknown term '" + term + "'");
    }
  }
  if (in.bad()) {
    error = path + ": read error";
    return BackscatterTable();
  }

  for (auto & b : builds) {
    if (b.entry.angle.empty()) {
      error = path + ": mode '" + b.entry.key + "' has no angle rows";
      return BackscatterTable();
    }
    std::sort(
      b.entry.angle.begin(), b.entry.angle.end(),
      [](const AngleBin & l, const AngleBin & r) {return l.abs_angle_deg < r.abs_angle_deg;});
    if (!declared_groups.empty()) {
      // Every declared group, in declared order; a written group must be one
      // of them (a row naming another group is a table this loader cannot
      // place, so it refuses rather than guessing).
      std::vector<std::pair<std::pair<double, double>, std::vector<RangeNode>>> placed;
      for (const auto & d : declared_groups) {
        placed.push_back({d, {}});
      }
      // Written groups are bucketed by exact bounds but matched within 1e-9,
      // so two near-identical written groups (a hand edit) can land on one
      // declared group: refused, never one silently overwriting the other.
      std::vector<bool> filled(placed.size(), false);
      for (auto & g : b.groups) {
        auto slot = std::find_if(
          placed.begin(), placed.end(), [&](const auto & p) {
            return sameBounds(p.first, g.first);
          });
        if (slot == placed.end()) {
          std::ostringstream os;
          os << path << ": mode '" << b.entry.key << "' has range rows for the group "
             << g.first.first << "-" << g.first.second
             << ", which '# range_angle_groups_deg:' does not declare";
          error = os.str();
          return BackscatterTable();
        }
        const std::size_t at = static_cast<std::size_t>(slot - placed.begin());
        if (filled[at]) {
          std::ostringstream os;
          os << path << ": mode '" << b.entry.key << "' has range rows for two groups that "
             << "both match the declared group " << slot->first.first << "-"
             << slot->first.second;
          error = os.str();
          return BackscatterTable();
        }
        filled[at] = true;
        slot->second = std::move(g.second);
      }
      b.groups = std::move(placed);
    }
    std::sort(
      b.groups.begin(), b.groups.end(),
      [](const auto & l, const auto & r) {return l.first.first < r.first.first;});
    for (std::size_t i = 1; i < b.groups.size(); ++i) {
      if (b.groups[i].first.first < b.groups[i - 1].first.second) {
        error = path + ": mode '" + b.entry.key + "' has overlapping range-term groups";
        return BackscatterTable();
      }
    }
    for (auto & g : b.groups) {
      RangeTermGroup group;
      group.angle_lo_deg = g.first.first;
      group.angle_hi_deg = g.first.second;
      group.nodes = std::move(g.second);
      std::sort(
        group.nodes.begin(), group.nodes.end(),
        [](const RangeNode & l, const RangeNode & r) {return l.x < r.x;});
      b.entry.groups.push_back(std::move(group));
    }
    table.index_[b.entry.key] = static_cast<uint16_t>(table.entries.size() + 1);
    table.entries.push_back(std::move(b.entry));
  }
  if (table.entries.empty()) {
    error = path + ": no table rows";
    return BackscatterTable();
  }
  // Without the bin width the OutsideAngleBins test cannot run, so a beam
  // far past the outermost written bin silently gets that bin's clamped
  // value (review round 3). Every table derive_angular_response.py writes
  // carries the header; a hand-written one may not. One bin per entry is a
  // flat curve on purpose and is not warned about.
  if (!std::isfinite(table.angle_bin_width_deg) &&
    std::any_of(
      table.entries.begin(), table.entries.end(),
      [](const ModeEntry & e) {return e.angle.size() > 1;}))
  {
    table.warnings.push_back(
      "the table has no '# angle_bin_width_deg:' header, so a beam beyond the outermost "
      "written angle bin cannot be told from one inside it: it gets that bin's value "
      "(clamped) instead of the fallback. Tables written by derive_angular_response.py "
      "carry the header.");
  }
  if (bytes_out) {
    *bytes_out = std::move(content);
  }
  return table;
}

const char * toString(BackscatterModeFallback fallback)
{
  return fallback == BackscatterModeFallback::Exclude ? "exclude" : "identity";
}

bool parseBackscatterModeFallback(const std::string & text, BackscatterModeFallback & out)
{
  std::string lower;
  for (char c : text) {
    lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  if (lower == "identity") {
    out = BackscatterModeFallback::Identity;
    return true;
  }
  if (lower == "exclude") {
    out = BackscatterModeFallback::Exclude;
    return true;
  }
  return false;
}

const char * toString(TableBeamStatus status)
{
  switch (status) {
    case TableBeamStatus::Corrected: return "corrected";
    case TableBeamStatus::NoEntry: return "no table entry";
    case TableBeamStatus::Unanchored: return "unanchored entry";
    case TableBeamStatus::NoFiniteAngle: return "no finite beam angle";
    case TableBeamStatus::AngleAtOrBeyond90: return "beam angle at or beyond 90 deg";
    case TableBeamStatus::OutsideAngleBins: return "beam angle outside the written angle bins";
    case TableBeamStatus::NoFiniteRange: return "no finite positive slant range";
    case TableBeamStatus::UnweighableBin: return "unweighable angle bin";
    default: return "?";
  }
}

double beamAbsAngleDeg(float beam_angle_rad)
{
  return std::isnan(beam_angle_rad) ? std::numeric_limits<double>::quiet_NaN() :
         std::abs(static_cast<double>(beam_angle_rad)) * 180.0 / M_PI;
}

TableBeamStatus tableBeamStatus(
  double abs_angle_deg, double range_m, const BackscatterTable & table,
  uint16_t correction_idx)
{
  const ModeEntry * entry = table.entry(correction_idx);
  if (!entry) {
    return TableBeamStatus::NoEntry;
  }
  if (!entry->offset_anchored) {
    return TableBeamStatus::Unanchored;
  }
  if (!std::isfinite(abs_angle_deg)) {
    return TableBeamStatus::NoFiniteAngle;
  }
  if (abs_angle_deg >= 90.0) {
    return TableBeamStatus::AngleAtOrBeyond90;
  }
  const double half_width = 0.5 * table.angle_bin_width_deg;
  // A relative 1e-9 on the half width: a beam on the outer edge of the
  // outermost bin is inside it (the centres round-trip with 10 digits).
  if (std::isfinite(half_width) && half_width > 0.0 && !entry->angle.empty() &&
    (abs_angle_deg > entry->angle.back().abs_angle_deg + half_width * (1.0 + 1e-9) ||
    abs_angle_deg < entry->angle.front().abs_angle_deg - half_width * (1.0 + 1e-9)))
  {
    return TableBeamStatus::OutsideAngleBins;
  }
  if (!std::isfinite(range_m) || range_m <= 0.0 || !(table.range_ref_m > 0.0)) {
    return TableBeamStatus::NoFiniteRange;
  }
  const AngleBin * bin = entry->binFor(abs_angle_deg);
  if (!bin || !bin->weighable()) {
    return TableBeamStatus::UnweighableBin;
  }
  return TableBeamStatus::Corrected;
}

double correctWithTable(
  double raw_db, double abs_angle_deg, double range_m,
  const BackscatterTable & table, uint16_t correction_idx,
  BackscatterModeFallback fallback)
{
  if (tableBeamStatus(abs_angle_deg, range_m, table, correction_idx) !=
    TableBeamStatus::Corrected)
  {
    return fallback == BackscatterModeFallback::Exclude ?
           std::numeric_limits<double>::quiet_NaN() : raw_db;
  }
  const ModeEntry * entry = table.entry(correction_idx);
  const double rel = entry->relativeDb(abs_angle_deg);
  const RangeTermGroup * group = entry->groupFor(abs_angle_deg);
  const double r = group ? group->at(std::log10(range_m / table.range_ref_m)) : 0.0;
  return raw_db - rel - r - entry->offset_db_vs_ref;
}

}  // namespace cube
