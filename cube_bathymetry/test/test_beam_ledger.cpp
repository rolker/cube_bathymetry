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
#include <unistd.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cube_bathymetry/beam_ledger.h"
#include "cube_bathymetry/build_fingerprint.h"

/// @file
/// @brief The beam ledger file (cube_bathymetry#173, second part): writer, reader,
///        atomicity, and the golden bytes the Python reader of
///        derive_angular_response.py is tested against.

namespace cube
{
namespace
{

namespace fs = std::filesystem;

std::string freshDir(const std::string & tag)
{
  static int counter = 0;
  const fs::path dir = fs::temp_directory_path() /
    ("cube_ledger_" + tag + "_" + std::to_string(::getpid()) + "_" + std::to_string(counter++));
  fs::remove_all(dir);
  fs::create_directories(dir);
  return dir.string();
}

// Any temporary file of the writer left in @p dir.
bool hasPartial(const std::string & dir)
{
  for (const auto & entry : fs::directory_iterator(dir)) {
    if (entry.path().filename().string().find(".partial") != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::string readFile(const std::string & path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

StoreInputs goldenInputs()
{
  StoreInputs in;
  in.slope_baseline_deg = 5.0;
  in.slope_layers = "processed";
  in.correction = "none";
  in.level = 10;
  return in;
}

std::vector<BagIdentity> goldenBags()
{
  return {{"bag_a", std::string(64, 'a')}, {"bag_b", std::string(64, 'b')}};
}

// The fixed content of test/data/beam_ledger_v1.bin.
void writeGolden(BeamLedgerWriter & w)
{
  w.addPing(1000000000123LL, 0,
    {{0, 2512, 0}, {3, -1234, 0}, {7, 4500, 3}, {9, kLedgerNoIncidence, 7}});
  w.addPing(1000100000000LL, 1, {});
  w.addPing(-5, 1, {{65534, 18000, 1}});
}

}  // namespace

TEST(BeamLedger, IncidenceIsInHundredthsOfADegree)
{
  EXPECT_EQ(ledgerIncidence(0.0f), 0);
  EXPECT_EQ(ledgerIncidence(static_cast<float>(M_PI / 180.0 * 25.12)), 2512);
  EXPECT_EQ(ledgerIncidence(static_cast<float>(M_PI)), 18000);
  EXPECT_EQ(ledgerIncidence(std::nanf("")), kLedgerNoIncidence);
  EXPECT_EQ(ledgerIncidence(1000.0f), kLedgerNoIncidence) << "does not fit int16";
}

TEST(BeamLedger, RoundTripsThroughTheFile)
{
  const std::string dir = freshDir("roundtrip");
  const std::string path = dir + "/ledger.bin";
  std::string error;
  auto w = BeamLedgerWriter::open(path, goldenInputs(), goldenBags(), "/sonar/detections", error);
  ASSERT_TRUE(w) << error;
  writeGolden(*w);
  EXPECT_EQ(w->pings(), 3u);
  EXPECT_FALSE(fs::exists(path)) << "nothing under the final name before finish";
  EXPECT_TRUE(hasPartial(dir));
  w->finish();
  EXPECT_TRUE(fs::exists(path));
  EXPECT_FALSE(hasPartial(dir));

  const BeamLedger ledger = readBeamLedger(path);
  const auto header = nlohmann::json::parse(ledger.header_json);
  EXPECT_EQ(header.at("format"), kBeamLedgerFormat);
  EXPECT_EQ(header.at("detections_topic"), "/sonar/detections");
  EXPECT_EQ(header.at("inputs").at("slope_baseline_deg"), 5.0);
  EXPECT_EQ(header.at("inputs").at("slope_layers"), "processed");
  ASSERT_EQ(header.at("bags").size(), 2u);
  EXPECT_EQ(header.at("bags")[1].at("name"), "bag_b");
  ASSERT_EQ(ledger.pings.size(), 3u);
  EXPECT_EQ(ledger.pings[0].stamp_ns, 1000000000123LL);
  EXPECT_EQ(ledger.pings[0].bag_index, 0u);
  ASSERT_EQ(ledger.pings[0].beams.size(), 4u);
  EXPECT_EQ(ledger.pings[0].beams[1].incidence_centideg, -1234);
  EXPECT_EQ(ledger.pings[0].beams[2].source, 3);
  EXPECT_EQ(ledger.pings[0].beams[3].incidence_centideg, kLedgerNoIncidence);
  EXPECT_TRUE(ledger.pings[1].beams.empty());
  EXPECT_EQ(ledger.pings[2].stamp_ns, -5);
  EXPECT_EQ(ledger.pings[2].beams[0].beam_index, 65534u);
  fs::remove_all(dir);
}

// A run that did not complete leaves no ledger: the partial file goes with the writer.
TEST(BeamLedger, AnUnfinishedLedgerLeavesNothingBehind)
{
  const std::string dir = freshDir("unfinished");
  const std::string path = dir + "/ledger.bin";
  std::string error;
  {
    auto w = BeamLedgerWriter::open(path, goldenInputs(), goldenBags(), "/sonar/detections", error);
    ASSERT_TRUE(w) << error;
    writeGolden(*w);
  }
  EXPECT_FALSE(fs::exists(path));
  EXPECT_FALSE(hasPartial(dir));
  // And one that was finished can be discarded (the run failed after it).
  {
    auto w = BeamLedgerWriter::open(path, goldenInputs(), goldenBags(), "/sonar/detections", error);
    writeGolden(*w);
    w->finish();
    w->discard();
  }
  EXPECT_FALSE(fs::exists(path));
  fs::remove_all(dir);
}

// Sealing leaves the file under its temporary name; only the commit puts it in
// place, and replaces an existing ledger. Two writers never share a temporary file.
TEST(BeamLedger, SealThenCommitAndTemporaryNamesAreUnique)
{
  const std::string dir = freshDir("seal");
  const std::string path = dir + "/ledger.bin";
  {
    std::ofstream(path) << "an earlier ledger";
  }
  std::string error;
  auto w = BeamLedgerWriter::open(path, goldenInputs(), goldenBags(), "/t", error);
  ASSERT_TRUE(w) << error;
  writeGolden(*w);
  const std::string sha = w->seal();
  EXPECT_EQ(sha.size(), 64u);
  EXPECT_EQ(readFile(path), "an earlier ledger") << "sealed, not yet in place";
  w->commit();
  EXPECT_NE(readFile(path), "an earlier ledger");
  EXPECT_FALSE(hasPartial(dir));
  EXPECT_EQ(sha256File(path), sha);
  fs::remove_all(dir);
}

TEST(BeamLedger, APingTooWideForTheFormatIsRefused)
{
  const std::string dir = freshDir("wide");
  std::string error;
  auto w = BeamLedgerWriter::open(dir + "/l.bin", goldenInputs(), goldenBags(), "/t", error);
  ASSERT_TRUE(w) << error;
  EXPECT_THROW(w->addPing(1, 0, std::vector<LedgerBeam>(65536)), std::runtime_error);
  EXPECT_NO_THROW(w->addPing(1, 0, std::vector<LedgerBeam>(65535)));
  fs::remove_all(dir);
}

TEST(BeamLedger, AnUnwritableLocationIsRefusedAtOnce)
{
  std::string error;
  auto w = BeamLedgerWriter::open(
    "/nonexistent_dir_for_ledger/ledger.bin", goldenInputs(), goldenBags(), "/sonar/detections",
      error);
  EXPECT_FALSE(w);
  EXPECT_NE(error.find("cannot create the beam ledger"), std::string::npos) << error;
}

TEST(BeamLedger, TheReaderRefusesWhatIsNotALedger)
{
  const std::string dir = freshDir("bad");
  {
    std::ofstream(dir + "/garbage.bin", std::ios::binary) << "not a ledger at all";
  }
  EXPECT_THROW(readBeamLedger(dir + "/garbage.bin"), std::runtime_error);
  EXPECT_THROW(readBeamLedger(dir + "/missing.bin"), std::runtime_error);
  // Truncated mid-record.
  const std::string path = dir + "/ledger.bin";
  std::string error;
  {
    auto w = BeamLedgerWriter::open(path, goldenInputs(), goldenBags(), "/sonar/detections", error);
    writeGolden(*w);
    w->finish();
  }
  fs::resize_file(path, fs::file_size(path) - 2);
  EXPECT_THROW(readBeamLedger(path), std::runtime_error);
  fs::remove_all(dir);
}

// The bytes the Python reader's test reads. Regenerate with BEAM_LEDGER_WRITE_GOLDEN=1.
TEST(BeamLedger, WriterOutputMatchesTheGoldenFile)
{
  const std::string dir = freshDir("golden");
  const std::string path = dir + "/ledger.bin";
  std::string error;
  auto w = BeamLedgerWriter::open(path, goldenInputs(), goldenBags(), "/sonar/detections", error);
  ASSERT_TRUE(w) << error;
  writeGolden(*w);
  w->finish();
  if (std::getenv("BEAM_LEDGER_WRITE_GOLDEN") != nullptr) {
    fs::copy_file(path, BEAM_LEDGER_GOLDEN_FILE, fs::copy_options::overwrite_existing);
  }
  EXPECT_EQ(readFile(path), readFile(BEAM_LEDGER_GOLDEN_FILE));
  fs::remove_all(dir);
}

}  // namespace cube
