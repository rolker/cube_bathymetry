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

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include <nlohmann/json.hpp>

#include "cube_bathymetry/build_fingerprint.h"
#include "cube_bathymetry/gain_declaration.h"

namespace cube
{

namespace
{

std::string writeTemp(const std::string & tag, const std::string & content)
{
  const std::string path = (std::filesystem::temp_directory_path() /
    ("cube_gain_decl_" + tag + "_" + std::to_string(::getpid()) + ".csv")).string();
  std::ofstream out(path, std::ios::binary);
  out << content;
  return path;
}

const char * kHeader =
  "# format: gain-state-declaration v1\n"
  "# reference_state: pre_step\n"
  "segment,start_utc,end_utc,offset_db,provenance,provenance_note\n";

/// Load @p content; returns the error ("" on success) and the declaration.
std::string tryLoad(
  const std::string & tag, const std::string & content, GainDeclaration & out,
  std::string * bytes = nullptr)
{
  const std::string path = writeTemp(tag, content);
  std::string error;
  out = loadGainDeclaration(path, error, bytes);
  std::filesystem::remove(path);
  return error;
}

constexpr int64_t kSecond = 1000000000LL;

GainDeclaration fixture()
{
  std::string error;
  GainDeclaration d = loadGainDeclaration(GAIN_DECLARATION_FIXTURE_FILE, error);
  EXPECT_EQ(error, "");
  return d;
}

nlohmann::json vectors()
{
  std::ifstream in(GAIN_DECLARATION_VECTORS_FILE);
  EXPECT_TRUE(in) << GAIN_DECLARATION_VECTORS_FILE;
  return nlohmann::json::parse(in);
}

}  // namespace

TEST(GainDeclaration, LoadsFixture)
{
  const GainDeclaration d = fixture();
  ASSERT_TRUE(d.loaded());
  EXPECT_EQ(d.reference_state, "pre_step");
  ASSERT_EQ(d.segments.size(), 1u);
  EXPECT_EQ(d.segments[0].id, "gain_step");
  EXPECT_EQ(d.segments[0].provenance, "measured");
  EXPECT_DOUBLE_EQ(d.segments[0].offset_db, 23.0);
  EXPECT_FALSE(d.segments[0].note.empty());
  EXPECT_TRUE(d.warnings.empty());
  int64_t start = 0, end = 0;
  ASSERT_TRUE(parseUtcNs("2026-08-26T20:17:30Z", start));
  ASSERT_TRUE(parseUtcNs("2026-08-29T00:00:00Z", end));
  EXPECT_EQ(d.segments[0].start_ns, start);
  EXPECT_EQ(d.segments[0].end_ns, end);
}

// The sign is pinned: the offset is ADDED to the reported level. A segment
// whose sonar read 23 dB low is +23, and 40 dB reported becomes 63 dB in the
// reference state. A flipped sign would double the split (the #172
// candidate-D failure).
TEST(GainDeclaration, SignIsAdded)
{
  const GainDeclaration d = fixture();
  int64_t inside = 0;
  ASSERT_TRUE(parseUtcNs("2026-08-27T12:00:00Z", inside));
  const double reported = 40.0;
  EXPECT_DOUBLE_EQ(reported + d.offsetDbAt(inside), 63.0);
}

TEST(GainDeclaration, StepBoundaryHalfOpen)
{
  const GainDeclaration d = fixture();
  const int64_t start = d.segments[0].start_ns;
  const int64_t end = d.segments[0].end_ns;
  EXPECT_DOUBLE_EQ(d.offsetDbAt(start - 1), 0.0);
  EXPECT_DOUBLE_EQ(d.offsetDbAt(start), 23.0);
  EXPECT_DOUBLE_EQ(d.offsetDbAt(end - 1), 23.0);
  EXPECT_DOUBLE_EQ(d.offsetDbAt(end), 0.0);
  EXPECT_EQ(d.segmentAt(start - 1), nullptr);
  ASSERT_NE(d.segmentAt(start), nullptr);
  EXPECT_EQ(d.segmentAt(start)->id, "gain_step");
}

TEST(GainDeclaration, UndeclaredTimeIsReference)
{
  const GainDeclaration d = fixture();
  EXPECT_DOUBLE_EQ(d.offsetDbAt(0), 0.0);
  EXPECT_DOUBLE_EQ(d.offsetDbAt(d.segments[0].end_ns + 100 * 86400 * kSecond), 0.0);
}

TEST(GainDeclaration, OverlapRejected)
{
  GainDeclaration d;
  const std::string error = tryLoad(
    "overlap", std::string(kHeader) +
    "a,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,5.0,measured,n\n"
    "b,2026-01-01T12:00:00Z,2026-01-03T00:00:00Z,6.0,measured,n\n", d);
  EXPECT_NE(error.find("overlap"), std::string::npos) << error;
  EXPECT_FALSE(d.loaded());
}

TEST(GainDeclaration, TouchingSegmentsAccepted)
{
  GainDeclaration d;
  const std::string error = tryLoad(
    "touch", std::string(kHeader) +
    "b,2026-01-02T00:00:00Z,2026-01-03T00:00:00Z,6.0,measured,n\n"
    "a,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,5.0,measured,n\n", d);
  EXPECT_EQ(error, "");
  ASSERT_EQ(d.segments.size(), 2u);
  EXPECT_EQ(d.segments[0].id, "a");  // sorted on load
  int64_t boundary = 0;
  ASSERT_TRUE(parseUtcNs("2026-01-02T00:00:00Z", boundary));
  EXPECT_DOUBLE_EQ(d.offsetDbAt(boundary - 1), 5.0);
  EXPECT_DOUBLE_EQ(d.offsetDbAt(boundary), 6.0);
}

TEST(GainDeclaration, StartNotBeforeEndRejected)
{
  GainDeclaration d;
  const std::string error = tryLoad(
    "empty_interval", std::string(kHeader) +
    "a,2026-01-02T00:00:00Z,2026-01-02T00:00:00Z,5.0,measured,n\n", d);
  EXPECT_NE(error.find("before end_utc"), std::string::npos) << error;
}

TEST(GainDeclaration, NonUtcStampRejected)
{
  GainDeclaration d;
  const std::string error = tryLoad(
    "nonutc", std::string(kHeader) +
    "a,2026-01-01T00:00:00+01:00,2026-01-02T00:00:00Z,5.0,measured,n\n", d);
  EXPECT_NE(error.find("UTC stamp"), std::string::npos) << error;
}

TEST(GainDeclaration, MissingProvenanceRejected)
{
  GainDeclaration d;
  const std::string error = tryLoad(
    "noprov", std::string(kHeader) +
    "a,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,5.0,,n\n", d);
  EXPECT_NE(error.find("provenance"), std::string::npos) << error;
  EXPECT_FALSE(d.loaded());
}

TEST(GainDeclaration, UnknownProvenanceRejected)
{
  GainDeclaration d;
  const std::string error = tryLoad(
    "badprov", std::string(kHeader) +
    "a,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,5.0,guessed,n\n", d);
  EXPECT_NE(error.find("operator_reported"), std::string::npos) << error;
}

TEST(GainDeclaration, EmptyNoteRejected)
{
  GainDeclaration d;
  const std::string error = tryLoad(
    "nonote", std::string(kHeader) +
    "a,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,5.0,measured,\n", d);
  EXPECT_NE(error.find("provenance_note"), std::string::npos) << error;
}

TEST(GainDeclaration, DuplicateSegmentIdRejected)
{
  GainDeclaration d;
  const std::string error = tryLoad(
    "dupid", std::string(kHeader) +
    "a,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,5.0,measured,n\n"
    "a,2026-02-01T00:00:00Z,2026-02-02T00:00:00Z,5.0,measured,n\n", d);
  EXPECT_NE(error.find("duplicate segment id"), std::string::npos) << error;
}

TEST(GainDeclaration, OffsetNotFiniteOrHugeRejected)
{
  for (const char * bad : {"nan", "inf", "-inf", "230", "-61", "abc", ""}) {
    GainDeclaration d;
    const std::string error = tryLoad(
      "offset", std::string(kHeader) + "a,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z," + bad +
      ",measured,n\n", d);
    EXPECT_NE(error, "") << "offset '" << bad << "' was accepted";
    EXPECT_FALSE(d.loaded());
  }
  GainDeclaration d;
  EXPECT_EQ(
    tryLoad(
      "offset60", std::string(kHeader) +
      "a,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,-60,measured,n\n"
      "b,2026-01-03T00:00:00Z,2026-01-04T00:00:00Z,0.0,measured,explicit reference\n", d), "");
}

TEST(GainDeclaration, FractionalSecondsToNs)
{
  int64_t whole = 0, frac = 0;
  ASSERT_TRUE(parseUtcNs("2026-08-26T20:17:30Z", whole));
  ASSERT_TRUE(parseUtcNs("2026-08-26T20:17:30.123456789Z", frac));
  EXPECT_EQ(frac - whole, 123456789);
  ASSERT_TRUE(parseUtcNs("2026-08-26T20:17:30.5Z", frac));
  EXPECT_EQ(frac - whole, 500000000);
  EXPECT_FALSE(parseUtcNs("2026-08-26T20:17:30.1234567891Z", frac));
}

TEST(GainDeclaration, QuotedFieldRejected)
{
  GainDeclaration d;
  const std::string error = tryLoad(
    "quoted", std::string(kHeader) +
    "a,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,5.0,measured,\"n\"\n", d);
  EXPECT_NE(error.find("quote"), std::string::npos) << error;
}

TEST(GainDeclaration, ZeroSegmentsLoadsWithWarning)
{
  GainDeclaration d;
  EXPECT_EQ(tryLoad("zero", kHeader, d), "");
  EXPECT_TRUE(d.loaded());
  EXPECT_TRUE(d.segments.empty());
  ASSERT_EQ(d.warnings.size(), 1u);
  EXPECT_NE(d.warnings[0].find("pre_step"), std::string::npos);
  EXPECT_DOUBLE_EQ(d.offsetDbAt(12345), 0.0);
}

TEST(GainDeclaration, BytesAreTheParsedBuffer)
{
  GainDeclaration d;
  std::string bytes;
  const std::string content = std::string(kHeader) +
    "a,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,5.0,measured,n\n";
  EXPECT_EQ(tryLoad("bytes", content, d, &bytes), "");
  EXPECT_EQ(bytes, content);
  // A failed load hands back no bytes.
  tryLoad("bytes_bad", "garbage\n", d, &bytes);
  EXPECT_TRUE(bytes.empty());
}

TEST(GainDeclaration, NonRegularFileRefused)
{
  std::string error;
  const GainDeclaration d = loadGainDeclaration("/dev/null", error);
  EXPECT_FALSE(d.loaded());
  EXPECT_NE(error.find("not a regular file"), std::string::npos) << error;
  loadGainDeclaration(std::filesystem::temp_directory_path().string(), error);
  EXPECT_NE(error.find("not a regular file"), std::string::npos) << error;
  loadGainDeclaration("/nonexistent/declaration.csv", error);
  EXPECT_NE(error.find("cannot open"), std::string::npos) << error;
}

TEST(GainDeclaration, IsGainDeclarationProbe)
{
  EXPECT_TRUE(isGainDeclaration(GAIN_DECLARATION_FIXTURE_FILE));
  const std::string other = writeTemp("probe", "# format: angular-response-table v1\n");
  EXPECT_FALSE(isGainDeclaration(other));
  std::filesystem::remove(other);
  EXPECT_FALSE(isGainDeclaration("/dev/null"));
  EXPECT_FALSE(isGainDeclaration(""));
}

TEST(GainDeclaration, CanonicalFormIgnoresNotesAndOrder)
{
  GainDeclaration a, b;
  ASSERT_EQ(
    tryLoad(
      "canon_a", std::string(kHeader) +
      "x,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,5.0,measured,first note\n"
      "y,2026-02-01T00:00:00Z,2026-02-02T00:00:00Z,-0.0,measured,n\n", a), "");
  ASSERT_EQ(
    tryLoad(
      "canon_b", "\xEF\xBB\xBF# format: gain-state-declaration v1\r\n"
      "# a free comment\r\n# sonar: another sonar\r\n# reference_state: pre_step\r\n"
      "segment,start_utc,end_utc,offset_db,provenance,provenance_note\r\n"
      "# between rows\r\n"
      "q,2026-02-01T00:00:00Z,2026-02-02T00:00:00Z,0,settings_file,other\r\n"
      "p,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,5,operator_reported,edited\r\n", b), "");
  EXPECT_EQ(a.canonicalForm(), b.canonicalForm());
  EXPECT_EQ(gainDeclarationSha256(a), gainDeclarationSha256(b));
  // An offset or a stamp or the reference label does change it.
  GainDeclaration c;
  ASSERT_EQ(
    tryLoad(
      "canon_c", std::string(kHeader) +
      "x,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,5.5,measured,first note\n", c), "");
  EXPECT_NE(a.canonicalForm(), c.canonicalForm());
}

// The same file and expectations the Python loader is tested on.
TEST(GainDeclaration, SharedVectors)
{
  const auto doc = vectors();
  for (const auto & t : doc.at("timestamps")) {
    int64_t ns = 0;
    const bool ok = parseUtcNs(t.at("text").get<std::string>(), ns);
    if (t.at("ns").is_null()) {
      EXPECT_FALSE(ok) << t.at("text");
    } else {
      ASSERT_TRUE(ok) << t.at("text");
      EXPECT_EQ(ns, t.at("ns").get<int64_t>()) << t.at("text");
    }
  }
  const GainDeclaration d = fixture();
  const auto & fx = doc.at("fixture");
  EXPECT_EQ(d.reference_state, fx.at("reference_state").get<std::string>());
  EXPECT_EQ(d.canonicalForm(), fx.at("canonical").get<std::string>());
  EXPECT_EQ(gainDeclarationSha256(d), fx.at("canonical_sha256").get<std::string>());
  for (const auto & o : fx.at("offsets")) {
    EXPECT_DOUBLE_EQ(d.offsetDbAt(o.at("stamp_ns").get<int64_t>()),
        o.at("offset_db").get<double>());
  }
  for (const auto & b : doc.at("bad_files")) {
    GainDeclaration bad;
    const std::string error = tryLoad(b.at("name"), b.at("text"), bad);
    EXPECT_NE(error, "") << b.at("name");
    EXPECT_NE(error.find(b.at("error_contains").get<std::string>()), std::string::npos)
      << b.at("name") << ": " << error;
    EXPECT_FALSE(bad.loaded()) << b.at("name");
  }
  for (const auto & g : doc.at("ok_files")) {
    GainDeclaration good;
    EXPECT_EQ(tryLoad(g.at("name"), g.at("text"), good), "") << g.at("name");
    EXPECT_TRUE(good.loaded());
  }
}

// cube_bathymetry#176 (pre-push round 1): the offset grammar, rounding and
// file framing are pinned for both loaders on the same cases.
namespace
{
std::string offsetFile(const std::string & offset)
{
  return std::string(kHeader) +
         "a,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z," + offset + ",measured,n\n";
}
}  // namespace

TEST(GainDeclaration, OffsetGrammarAndCanonicalRounding)
{
  const auto doc = vectors();
  for (const auto & c : doc.at("offset_cases")) {
    GainDeclaration d;
    const std::string text = c.at("text");
    const std::string error = tryLoad("grammar", offsetFile(text), d);
    ASSERT_EQ(error, "") << text;
    // The file header of this helper says reference_state pre_step.
    const std::string expected = "gain-state-declaration v1\nreference_state=pre_step\n" +
      std::to_string(d.segments[0].start_ns) + "," + std::to_string(d.segments[0].end_ns) + "," +
      c.at("canonical_offset").get<std::string>() + "\n";
    EXPECT_EQ(d.canonicalForm(), expected) << text;
    // What is applied is what the digest hashes, with no negative zero.
    const double applied = std::strtod(c.at("canonical_offset").get<std::string>().c_str(),
        nullptr);
    EXPECT_EQ(d.segments[0].offset_db, applied) << text;
    EXPECT_EQ(std::signbit(d.segments[0].offset_db), applied < 0.0) << text;
  }
  for (const auto & text : doc.at("offset_rejects")) {
    GainDeclaration d;
    const std::string error = tryLoad("grammar_bad", offsetFile(text.get<std::string>()), d);
    EXPECT_NE(error, "") << "offset '" << text.get<std::string>() << "' was accepted";
    EXPECT_FALSE(d.loaded());
  }
}

TEST(GainDeclaration, AppliedOffsetIsTheCanonicalSixDecimalValue)
{
  // 0.0000004 and -0 share the digest of 0, so they must apply as 0 too, and
  // the report must not print "+-" for a zero that carries a sign.
  for (const char * text : {"0.0000004", "-0.0000004", "-0", "-0.0", "-0e5"}) {
    GainDeclaration d;
    ASSERT_EQ(tryLoad("zero", offsetFile(text), d), "") << text;
    const double offset = d.segments[0].offset_db;
    EXPECT_EQ(offset, 0.0) << text;
    EXPECT_FALSE(std::signbit(offset)) << text;
    GainStateResolver resolver(&d);
    EXPECT_EQ(resolver.apply(d.segments[0].start_ns), 0.0) << text;
    const std::string report = resolver.report();
    EXPECT_EQ(report.find("+-"), std::string::npos) << report;
    EXPECT_NE(report.find("+0.000000 dB"), std::string::npos) << report;
  }
  // A small negative offset is applied at 6 decimals and printed at 6, so it
  // does not read as a negative zero.
  GainDeclaration tiny;
  ASSERT_EQ(tryLoad("tiny", offsetFile("-0.0004"), tiny), "");
  GainStateResolver tiny_resolver(&tiny);
  tiny_resolver.apply(tiny.segments[0].start_ns);
  const std::string tiny_report = tiny_resolver.report();
  EXPECT_NE(tiny_report.find(") -0.000400 dB ("), std::string::npos) << tiny_report;
  EXPECT_EQ(tiny_report.find("-0.000 dB"), std::string::npos) << tiny_report;
  GainDeclaration d;
  ASSERT_EQ(tryLoad("round", offsetFile("-0.4000004"), d), "");
  EXPECT_EQ(d.segments[0].offset_db, -0.4);
  GainStateResolver resolver(&d);
  resolver.apply(d.segments[0].start_ns);
  EXPECT_EQ(resolver.report().find("+-"), std::string::npos);
}

TEST(GainDeclaration, NonUtf8BytesAreRefusedLikeThePythonLoader)
{
  const std::string good = std::string(kHeader) +
    "a,2026-01-01T00:00:00Z,2026-01-02T00:00:00Z,5.0,measured,";
  GainDeclaration d;
  EXPECT_EQ(tryLoad("utf8_ok", good + "caf\xC3\xA9 \xE2\x82\xAC \xF0\x9F\x8C\x8A\n", d), "");
  for (const char * bad : {"\xFF", "\xC3", "\xC0\x80", "\xE0\x80\x80", "\xED\xA0\x80",
      "\xF4\x90\x80\x80", "\x80", "ab\xE2\x82"})
  {
    const std::string error = tryLoad("utf8_bad", good + bad + "\n", d);
    EXPECT_NE(error.find("not UTF-8"), std::string::npos) << error;
    EXPECT_FALSE(d.loaded());
  }
}

TEST(GainDeclaration, FileFramingVariantsHaveTheSameCanonicalForm)
{
  const auto doc = vectors();
  for (const auto & c : doc.at("canonical_files")) {
    GainDeclaration d;
    const std::string name = c.at("name");
    ASSERT_EQ(tryLoad(name, c.at("text"), d), "") << name;
    // The vector files name reference state "r".
    EXPECT_EQ(d.canonicalForm(), c.at("canonical").get<std::string>()) << name;
  }
}

// strtod and snprintf follow LC_NUMERIC; the loader and the canonical form must
// not. Needs a locale with a decimal comma installed; skipped with a message
// when none is.
TEST(GainDeclaration, ParseAndCanonicalFormIgnoreLcNumeric)
{
  const char * previous = std::setlocale(LC_NUMERIC, nullptr);
  const std::string saved = previous ? previous : "C";
  const char * candidates[] = {"de_DE.UTF-8", "de_DE.utf8", "fr_FR.UTF-8", "fr_FR.utf8",
    "nl_NL.UTF-8", "ru_RU.UTF-8", "es_ES.UTF-8", "it_IT.UTF-8", "pt_BR.UTF-8"};
  const char * chosen = nullptr;
  for (const char * name : candidates) {
    if (std::setlocale(LC_NUMERIC, name)) {
      char probe[16];
      std::snprintf(probe, sizeof(probe), "%.1f", 1.5);
      if (std::string(probe).find(',') != std::string::npos) {
        chosen = name;
        break;
      }
    }
  }
  if (!chosen) {
    std::setlocale(LC_NUMERIC, saved.c_str());
    GTEST_SKIP() << "no locale with a decimal comma is installed (tried de_DE, fr_FR, nl_NL, "
      "ru_RU, es_ES, it_IT, pt_BR); the check did not run";
  }
  GainDeclaration d;
  const std::string error = tryLoad("locale", offsetFile("23.5"), d);
  const std::string canonical = d.canonicalForm();
  GainStateResolver resolver(&d);
  resolver.apply(d.segments.empty() ? 0 : d.segments[0].start_ns);
  const std::string report = resolver.report();
  std::setlocale(LC_NUMERIC, saved.c_str());
  EXPECT_EQ(error, "") << chosen;
  EXPECT_NE(canonical.find(",23.500000\n"), std::string::npos) << canonical;
  EXPECT_NE(report.find("+23.500000 dB"), std::string::npos) << report;
}

}  // namespace cube
