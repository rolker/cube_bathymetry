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

#include "cube_bathymetry/gain_declaration.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace cube
{

namespace
{

constexpr const char * kFormatLine = "# format: gain-state-declaration v1";
constexpr const char * kFormatPrefix = "# format: gain-state-declaration";
constexpr const char * kColumns =
  "segment,start_utc,end_utc,offset_db,provenance,provenance_note";
constexpr std::size_t kColumnCount = 6;
constexpr std::uintmax_t kMaxBytes = 64 * 1024;
constexpr double kMaxAbsOffsetDb = 60.0;
constexpr std::size_t kMaxQuoted = 200;

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

std::string clip(const std::string & s)
{
  return s.size() <= kMaxQuoted ? s : s.substr(0, kMaxQuoted) + "...";
}

bool isRegularFile(const std::string & path)
{
  std::error_code ec;
  const auto status = std::filesystem::status(path, ec);
  return !ec && std::filesystem::is_regular_file(status);
}

/// Read at most @p limit + 1 bytes; more than @p limit means too large.
bool readBounded(std::istream & in, std::uintmax_t limit, std::string & out)
{
  out.clear();
  char buffer[4096];
  while (out.size() <= limit && (in.read(buffer, sizeof(buffer)) || in.gcount() > 0)) {
    out.append(buffer, static_cast<std::size_t>(in.gcount()));
  }
  return !in.bad();
}

/// An identifier: [A-Za-z0-9_.-]+
bool isIdentifier(const std::string & s)
{
  if (s.empty()) {
    return false;
  }
  return std::all_of(
    s.begin(), s.end(), [](unsigned char c) {
      return std::isalnum(c) || c == '_' || c == '.' || c == '-';
    });
}

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

bool allDigits(const std::string & s, std::size_t from, std::size_t count)
{
  if (from + count > s.size()) {
    return false;
  }
  for (std::size_t i = from; i < from + count; ++i) {
    if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
      return false;
    }
  }
  return true;
}

int num(const std::string & s, std::size_t from, std::size_t count)
{
  return std::stoi(s.substr(from, count));
}

/// Days since 1970-01-01 of a proleptic Gregorian date (Hinnant's algorithm).
int64_t daysFromCivil(int64_t y, int64_t m, int64_t d)
{
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const int64_t yoe = y - era * 400;
  const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + doe - 719468;
}

bool isLeap(int y)
{
  return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

int daysInMonth(int y, int m)
{
  static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return (m == 2 && isLeap(y)) ? 29 : kDays[m - 1];
}

std::string offsetText(double offset_db)
{
  char buf[64];
  // Negative zero prints as zero: the digest must not depend on it.
  std::snprintf(buf, sizeof(buf), "%.6f", offset_db == 0.0 ? 0.0 : offset_db);
  return buf;
}

}  // namespace

bool parseUtcNs(const std::string & text, int64_t & out)
{
  // YYYY-MM-DDTHH:MM:SS[.f{1,9}]Z
  if (text.size() < 20 || !allDigits(text, 0, 4) || text[4] != '-' || !allDigits(text, 5, 2) ||
    text[7] != '-' || !allDigits(text, 8, 2) || text[10] != 'T' || !allDigits(text, 11, 2) ||
    text[13] != ':' || !allDigits(text, 14, 2) || text[16] != ':' || !allDigits(text, 17, 2))
  {
    return false;
  }
  std::size_t pos = 19;
  int64_t frac_ns = 0;
  if (text[pos] == '.') {
    ++pos;
    std::size_t digits = 0;
    int64_t scale = 100000000;
    while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
      if (++digits > 9) {
        return false;
      }
      frac_ns += (text[pos] - '0') * scale;
      scale /= 10;
      ++pos;
    }
    if (digits == 0) {
      return false;
    }
  }
  if (pos + 1 != text.size() || text[pos] != 'Z') {
    return false;
  }
  const int y = num(text, 0, 4);
  const int mo = num(text, 5, 2);
  const int d = num(text, 8, 2);
  const int h = num(text, 11, 2);
  const int mi = num(text, 14, 2);
  const int s = num(text, 17, 2);
  if (y < 1970 || y > 2200 || mo < 1 || mo > 12 || d < 1 || d > daysInMonth(y, mo) || h > 23 ||
    mi > 59 || s > 59)
  {
    return false;
  }
  const int64_t days = daysFromCivil(y, mo, d);
  out = ((days * 24 + h) * 60 + mi) * 60 * 1000000000LL + static_cast<int64_t>(s) * 1000000000LL +
    frac_ns;
  return true;
}

const GainSegment * GainDeclaration::segmentAt(int64_t stamp_ns) const
{
  // First segment whose start is after the stamp; the one before it is the
  // only candidate (segments are sorted and do not overlap).
  auto it = std::upper_bound(
    segments.begin(), segments.end(), stamp_ns,
    [](int64_t t, const GainSegment & s) {return t < s.start_ns;});
  if (it == segments.begin()) {
    return nullptr;
  }
  --it;
  return stamp_ns < it->end_ns ? &*it : nullptr;
}

double GainDeclaration::offsetDbAt(int64_t stamp_ns) const
{
  const GainSegment * segment = segmentAt(stamp_ns);
  return segment ? segment->offset_db : 0.0;
}

std::string GainDeclaration::canonicalForm() const
{
  std::string out = "gain-state-declaration v1\nreference_state=" + reference_state + "\n";
  for (const GainSegment & s : segments) {
    out += std::to_string(s.start_ns) + "," + std::to_string(s.end_ns) + "," +
      offsetText(s.offset_db) + "\n";
  }
  return out;
}

bool isGainDeclaration(const std::string & path)
{
  if (path.empty() || !isRegularFile(path)) {
    return false;
  }
  std::ifstream file(path, std::ios::binary);
  std::string head;
  if (!file || !readBounded(file, kMaxBytes, head)) {
    return false;
  }
  std::istringstream in(head);
  std::string line;
  std::size_t line_no = 0;
  return firstContentLine(in, line, line_no) &&
         line.compare(0, std::char_traits<char>::length(kFormatPrefix), kFormatPrefix) == 0;
}

GainDeclaration loadGainDeclaration(
  const std::string & path, std::string & error, std::string * bytes_out)
{
  error.clear();
  if (bytes_out) {
    bytes_out->clear();
  }
  auto fail = [&](std::size_t line_no, const std::string & why) {
      error = path + ":" + std::to_string(line_no) + ": " + why;
      return GainDeclaration();
    };
  std::error_code ec;
  const auto status = std::filesystem::status(path, ec);
  if (status.type() == std::filesystem::file_type::not_found) {
    error = path + ": cannot open (no such file)";
    return GainDeclaration();
  }
  if (ec || !std::filesystem::is_regular_file(status)) {
    error = path + ": not a regular file (a declaration is read from a file, never a FIFO, "
      "device or directory)";
    return GainDeclaration();
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    error = path + ": cannot open";
    return GainDeclaration();
  }
  std::string content;
  if (!readBounded(file, kMaxBytes, content)) {
    error = path + ": read error";
    return GainDeclaration();
  }
  if (content.size() > kMaxBytes) {
    error = path + ": more than " + std::to_string(kMaxBytes) +
      " bytes, more than a declaration can be";
    return GainDeclaration();
  }

  std::istringstream in(content);
  std::string line;
  std::size_t line_no = 0;
  if (!firstContentLine(in, line, line_no)) {
    return fail(line_no, "empty file, not a gain-state declaration");
  }
  if (line != kFormatLine) {
    if (line.compare(0, std::char_traits<char>::length(kFormatPrefix), kFormatPrefix) == 0) {
      return fail(
        line_no, "unsupported declaration format '" + clip(line) + "' (this loader reads '" +
        kFormatLine + "')");
    }
    return fail(
      line_no, std::string("not a gain-state declaration (first line must be '") + kFormatLine +
      "')");
  }

  GainDeclaration declaration;
  declaration.path = path;
  bool reference_seen = false;
  bool columns_seen = false;
  while (std::getline(in, line)) {
    ++line_no;
    line = stripCr(line);
    if (line.empty()) {
      continue;
    }
    if (line[0] == '#') {
      const std::string body = trim(line.substr(1));
      const auto colon = body.find(':');
      if (colon == std::string::npos) {
        continue;
      }
      const std::string key = trim(body.substr(0, colon));
      const std::string value = trim(body.substr(colon + 1));
      if (key == "format") {
        return fail(line_no, "a second '# format:' line");
      }
      if (key == "reference_state") {
        if (reference_seen) {
          return fail(line_no, "'# reference_state:' given twice");
        }
        if (!isIdentifier(value)) {
          return fail(
            line_no, "'# reference_state:' must be a non-empty label of [A-Za-z0-9_.-], got '" +
            clip(value) + "'");
        }
        declaration.reference_state = value;
        reference_seen = true;
      } else if (key == "sonar") {
        declaration.sonar = value;
      }
      continue;
    }
    if (line != kColumns) {
      return fail(line_no, "expected the column line '" + std::string(kColumns) + "'");
    }
    columns_seen = true;
    break;
  }
  if (!reference_seen) {
    return fail(line_no, "header lacks '# reference_state:'");
  }
  if (!columns_seen) {
    return fail(line_no, "no column line");
  }

  std::set<std::string> ids;
  while (std::getline(in, line)) {
    ++line_no;
    line = stripCr(line);
    if (line.empty() || line[0] == '#') {
      continue;
    }
    if (line.find('"') != std::string::npos) {
      return fail(line_no, "a quote character: fields are never quoted");
    }
    std::vector<std::string> f;
    {
      std::string field;
      for (char c : line) {
        if (c == ',') {
          f.push_back(trim(field));
          field.clear();
        } else {
          field.push_back(c);
        }
      }
      f.push_back(trim(field));
    }
    if (f.size() != kColumnCount) {
      return fail(
        line_no, "expected " + std::to_string(kColumnCount) + " fields, got " +
        std::to_string(f.size()) + " (a field may not contain a comma)");
    }
    GainSegment s;
    s.id = f[0];
    if (!isIdentifier(s.id)) {
      return fail(line_no, "segment id '" + clip(s.id) + "' must be non-empty [A-Za-z0-9_.-]");
    }
    if (!ids.insert(s.id).second) {
      return fail(line_no, "duplicate segment id '" + s.id + "'");
    }
    if (!parseUtcNs(f[1], s.start_ns)) {
      return fail(
        line_no, "start_utc '" + clip(f[1]) +
        "' is not a UTC stamp YYYY-MM-DDTHH:MM:SS[.fff]Z");
    }
    if (!parseUtcNs(f[2], s.end_ns)) {
      return fail(
        line_no, "end_utc '" + clip(f[2]) + "' is not a UTC stamp YYYY-MM-DDTHH:MM:SS[.fff]Z");
    }
    if (!(s.start_ns < s.end_ns)) {
      return fail(line_no, "start_utc must be before end_utc (the interval is [start, end))");
    }
    {
      char * end = nullptr;
      errno = 0;
      const double v = std::strtod(f[3].c_str(), &end);
      if (f[3].empty() || end == f[3].c_str() || *end != '\0' || errno == ERANGE ||
        !std::isfinite(v))
      {
        return fail(line_no, "offset_db '" + clip(f[3]) + "' is not a finite number");
      }
      if (std::abs(v) > kMaxAbsOffsetDb) {
        return fail(
          line_no, "offset_db " + f[3] + " is beyond +-" +
          std::to_string(static_cast<int>(kMaxAbsOffsetDb)) + " dB (a typo?)");
      }
      s.offset_db = v;
    }
    if (f[4] != "settings_file" && f[4] != "measured" && f[4] != "operator_reported") {
      return fail(
        line_no, "provenance '" + clip(f[4]) +
        "' must be settings_file, measured or operator_reported");
    }
    s.provenance = f[4];
    if (f[5].empty()) {
      return fail(line_no, "provenance_note is mandatory: say how the offset was obtained");
    }
    s.note = f[5];
    declaration.segments.push_back(std::move(s));
  }

  std::sort(
    declaration.segments.begin(), declaration.segments.end(),
    [](const GainSegment & a, const GainSegment & b) {return a.start_ns < b.start_ns;});
  for (std::size_t i = 1; i < declaration.segments.size(); ++i) {
    const GainSegment & a = declaration.segments[i - 1];
    const GainSegment & b = declaration.segments[i];
    if (a.end_ns > b.start_ns) {
      error = path + ": segments '" + a.id + "' and '" + b.id +
        "' overlap (touching is fine, overlapping is not)";
      return GainDeclaration();
    }
  }
  if (declaration.segments.empty()) {
    declaration.warnings.push_back(
      "declares only the reference state '" + declaration.reference_state +
      "' and no segment: every ping is taken to be at the reference");
  }
  if (bytes_out) {
    *bytes_out = std::move(content);
  }
  return declaration;
}

}  // namespace cube
