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

/// @file
/// @brief End-to-end smoke test of the depth-adaptive path against a REAL
///        sonar bag (cube_bathymetry#143 / #162).
///
/// Every other test in this package drives the library directly, so nothing
/// reached `import_bag`'s `main()` past argument validation: the per-ping I/O
/// guard, the finalize-inside-the-abort-guard contract and the two-phase
/// spill/replay were all reviewed by inspection only. This test runs the real
/// binaries over a 3,000-ping excerpt of the 2026-06-09 UNH pier M3 bag and
/// checks the numbers the dry run established, byte-identity between
/// `import_bag --depth-adaptive` and `batch_regen_bag --level-plan`, and the
/// two documented abort paths with a fault injected.
///
/// **The excerpt is data, not a fixture**: it lives beside its source bag in
/// the survey archive (18 MB, far too large to commit) and is referenced by
/// path. Where it is unreachable -- a CI container, a machine with no archive
/// mount -- every case here is **SKIPPED with the reason printed**, never
/// passed: a green tick that means "did not look" is worse than a red one.
/// `CUBE_REAL_BAG_EXCERPT` overrides the path.
///
/// Rebuild the excerpt (any host with the source bag) with
/// `scripts/make_bag_excerpt.py`.

#include <gtest/gtest.h>

#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace
{

/// Where the excerpt lives when `CUBE_REAL_BAG_EXCERPT` does not say otherwise.
constexpr const char * kDefaultExcerpt =
  "/mnt/nadata/map2026asv/logs/gabby/logs/bizzy_m3/"
  "bag_2026-06-09T14.51.50_m3_detections_3000ping_excerpt";

/// The dry run's numbers for this window (`.agent/work-plans/issue-143`):
/// 3,000 pings, 2,999 georeferenced, 613,018 soundings counted.
constexpr double kGroundM2 = 705.91;
constexpr double kGroundTolerance = 0.10;
constexpr double kShallowestDecisionDepth = -7.0;
constexpr double kDeepestDecisionDepth = -20.0;

/// The projector needs the bag's namespaced frames or the grid comes out empty.
const char * kProjectorArgs =
  " -d /bizzy/sensors/m3/detections --odom-topic /bizzy/odom"
  " --base-link-frame bizzy/base_link --level-frame bizzy/base_link_north_up"
  " --tide-frame bizzy/map_tide --platform bizzyboat --sensor kongsberg-m3"
  " --campaign massabesic-jun2026";

std::string excerptPath()
{
  const char * env = std::getenv("CUBE_REAL_BAG_EXCERPT");
  return (env != nullptr && *env != '\0') ? std::string(env) : std::string(kDefaultExcerpt);
}

/// Why the excerpt cannot be used, or an empty string when it can. Reported
/// verbatim in the skip message, so it names the path it looked at.
std::string excerptProblem()
{
  const std::string path = excerptPath();
  std::error_code ec;
  if (!std::filesystem::is_directory(path, ec) || ec) {
    return "no bag directory at " + path;
  }
  if (!std::filesystem::exists(std::filesystem::path(path) / "metadata.yaml", ec) || ec) {
    return path + " has no metadata.yaml (not a rosbag2 bag)";
  }
  std::ifstream probe(std::filesystem::path(path) / "metadata.yaml");
  if (!probe) {
    return path + "/metadata.yaml is not readable";
  }
  return "";
}

// The skip is a macro so the SKIP is attributed to the test's own line, and so
// a case that cannot run says why on the console rather than reporting green.
#define SKIP_WITHOUT_EXCERPT() \
  do { \
    const std::string problem_ = excerptProblem(); \
    if (!problem_.empty()) { \
      GTEST_SKIP() << "real-bag smoke test SKIPPED (NOT passed): " << problem_ \
                   << ". Set CUBE_REAL_BAG_EXCERPT to a 3,000-ping excerpt of the " \
                   << "2026-06-09 pier M3 bag (scripts/make_bag_excerpt.py) to run it."; \
    } \
  } while (false)

/// Run @p command; merged stdout+stderr back, exit status through @p status.
std::string run(const std::string & command, int * status)
{
  std::string output;
  FILE * pipe = popen((command + " 2>&1").c_str(), "r");
  if (pipe == nullptr) {
    *status = -1;
    return output;
  }
  std::array<char, 4096> buffer{};
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
  }
  const int rc = pclose(pipe);
  *status = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;
  return output;
}

/// A scratch directory under the test's own temp root, removed on destruction.
class TempDir
{
public:
  explicit TempDir(const std::string & tag)
  : path_(std::filesystem::temp_directory_path() /
      ("cube143_smoke_" + tag + "_" + std::to_string(::getpid())))
  {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TempDir()
  {
    std::error_code ec;
    std::filesystem::permissions(
      path_, std::filesystem::perms::owner_all,
      std::filesystem::perm_options::add, ec);
    std::filesystem::remove_all(path_, ec);
  }
  TempDir(const TempDir &) = delete;
  TempDir & operator=(const TempDir &) = delete;
  std::string sub(const std::string & name) const {return (path_ / name).string();}
  const std::filesystem::path & path() const {return path_;}

private:
  std::filesystem::path path_;
};

/// The value of a top-level numeric key in a plan JSON. Deliberately a text
/// probe: the point is that the file an operator hands to the next run parses
/// as the documented schema, not that our own reader round-trips it.
double jsonNumber(const std::string & text, const std::string & key, bool * found)
{
  const std::string needle = "\"" + key + "\":";
  const std::size_t at = text.find(needle);
  *found = at != std::string::npos;
  if (!*found) {
    return 0.0;
  }
  return std::strtod(text.c_str() + at + needle.size(), nullptr);
}

std::string readFile(const std::string & path)
{
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

/// Every `<level>_<row>_<col>.tif` under @p store, relative to it.
std::vector<std::string> storeTiles(const std::string & store)
{
  std::vector<std::string> tiles;
  std::error_code ec;
  for (const auto & entry : std::filesystem::recursive_directory_iterator(store, ec)) {
    if (entry.is_regular_file() && entry.path().extension() == ".tif") {
      tiles.push_back(std::filesystem::relative(entry.path(), store).string());
    }
  }
  std::sort(tiles.begin(), tiles.end());
  return tiles;
}

/// The recon spill inside @p scratch, or an empty path when it is not there yet.
std::filesystem::path spillFile(const std::string & scratch)
{
  std::error_code ec;
  for (const auto & dir : std::filesystem::directory_iterator(scratch, ec)) {
    if (dir.path().filename().string().rfind(".recon_spill_", 0) != 0) {
      continue;
    }
    if (dir.is_regular_file()) {
      return dir.path();
    }
    for (const auto & inner : std::filesystem::directory_iterator(dir.path(), ec)) {
      if (inner.is_regular_file()) {
        return inner.path();
      }
    }
  }
  return {};
}

/// Run @p command, and once its output carries @p marker, wait @p delay and
/// call @p fault. The marker is the line `main()` prints when it leaves phase
/// one, so the fault lands in the middle of the replay -- the only way to get
/// a fault into a two-phase run of a real bag from outside the process.
std::string runWithFaultAfter(
  const std::string & command, const std::string & marker,
  std::chrono::milliseconds delay, const std::function<void()> & fault, int * status)
{
  std::string output;
  FILE * pipe = popen((command + " 2>&1").c_str(), "r");
  if (pipe == nullptr) {
    *status = -1;
    return output;
  }
  std::thread injector;
  std::array<char, 4096> buffer{};
  while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    output += buffer.data();
    if (!injector.joinable() && output.find(marker) != std::string::npos) {
      injector = std::thread([delay, fault]() {
            std::this_thread::sleep_for(delay);
            fault();
          });
    }
  }
  if (injector.joinable()) {
    injector.join();
  }
  const int rc = pclose(pipe);
  *status = (rc != -1 && WIFEXITED(rc)) ? WEXITSTATUS(rc) : -1;
  return output;
}

/// The dry-run recon, shared by the cases that need a plan.
std::string reconCommand(const TempDir & dir, const std::string & plan)
{
  return std::string(IMPORT_BAG_EXE) + " -o " + dir.sub("store") + kProjectorArgs +
         " -r 1.0 --depth-adaptive --scratch-dir " + dir.sub("scratch") +
         " --level-plan-out " + plan + " " + excerptPath();
}

}  // namespace

// Phase one over a real bag: the numbers the operator approves a multi-hour
// import from. Every one of these was wrong at some point in this PR's review
// rounds -- the areas were tile footprints, the recon timer read 2.6e-05 s for
// a ten-second pass, and the decision depth was an ellipsoidal height.
TEST(RealBagSmoke, DepthAdaptiveReconReportsTheSurveyItActuallyCounted)
{
  SKIP_WITHOUT_EXCERPT();
  TempDir dir("recon");
  std::filesystem::create_directories(dir.sub("store"));
  std::filesystem::create_directories(dir.sub("scratch"));
  const std::string plan = dir.sub("plan.json");

  int status = -1;
  const std::string out = run(reconCommand(dir, plan), &status);
  ASSERT_EQ(status, 0) << out;

  // Not one sounding may lack a range below the transducer: a grid with no
  // depth is never emitted, so that warning means unestimated ground.
  EXPECT_EQ(out.find("no range below the transducer"), std::string::npos) << out;
  EXPECT_NE(out.find("Recon pass:"), std::string::npos) << out;

  // The recon timer measures the pass, not the instant after it (the interval
  // it reported before the fix was 2.6e-05 s for a ten-second pass). Bound it
  // from both sides against the projection the same run reports.
  const std::size_t recon_at = out.find("count tile(s) in ");
  ASSERT_NE(recon_at, std::string::npos) << out;
  const double recon_secs = std::strtod(out.c_str() + recon_at + 17, nullptr);
  EXPECT_GT(recon_secs, 0.1) << "the recon pass cannot have taken a few microseconds\n" << out;
  EXPECT_LT(recon_secs, 600.0) << out;
  const std::size_t proj_at = out.find("pings in ");
  ASSERT_NE(proj_at, std::string::npos) << out;
  const double projection_secs = std::strtod(out.c_str() + proj_at + 9, nullptr);
  EXPECT_NEAR(recon_secs, projection_secs, 0.5 * projection_secs + 1.0)
    << "the recon pass IS the projection pass (one interleaved pass)\n" << out;

  const std::string json = readFile(plan);
  ASSERT_FALSE(json.empty()) << "no plan written to " << plan;
  bool found = false;
  EXPECT_EQ(jsonNumber(json, "schema", &found), 2) << json;
  EXPECT_TRUE(found) << json;
  // Schema 2 carries the capture gate the import ran with; without it a
  // rebuild silently applies its own default (cube#143 triage).
  const double scale = jsonNumber(json, "capture_spacing_scale", &found);
  EXPECT_TRUE(found) << "schema 2 must carry capture_spacing_scale\n" << json;
  EXPECT_NEAR(scale, 0.71, 1e-4) << json;

  const double ground = jsonNumber(json, "ground_m2", &found);
  ASSERT_TRUE(found) << json;
  EXPECT_NEAR(ground, kGroundM2, kGroundTolerance * kGroundM2)
    << "surveyed ground is measured from the count grid's occupied cells\n" << json;

  // Water depth under the transducer, not the stored ellipsoidal height: the
  // pier's geoid separation is ~28 m, so an ellipsoidal decision depth would
  // land near -37 m and coarsen every tile.
  std::size_t at = 0;
  int depths = 0;
  while ((at = json.find("\"d\":", at)) != std::string::npos) {
    const double d = std::strtod(json.c_str() + at + 4, nullptr);
    EXPECT_LE(d, kShallowestDecisionDepth) << "decision depth " << d << " in " << json;
    EXPECT_GE(d, kDeepestDecisionDepth) << "decision depth " << d << " in " << json;
    ++depths;
    at += 4;
  }
  EXPECT_GE(depths, 1) << "the plan emitted no tile\n" << json;
}

// The equivalence README promises, on real soundings rather than a synthetic
// grid: the bounded-RAM import and the exact rebuild must agree byte for byte.
TEST(RealBagSmoke, ImportAndBatchRegenAgreeByteForByteOnTheRealBag)
{
  SKIP_WITHOUT_EXCERPT();
  TempDir dir("equiv");
  std::filesystem::create_directories(dir.sub("store"));
  std::filesystem::create_directories(dir.sub("scratch"));
  const std::string plan = dir.sub("plan.json");
  int status = -1;
  const std::string recon = run(reconCommand(dir, plan), &status);
  ASSERT_EQ(status, 0) << recon;

  std::filesystem::create_directories(dir.sub("imported"));
  std::filesystem::create_directories(dir.sub("scratch2"));
  const std::string imported = run(
    std::string(IMPORT_BAG_EXE) + " -o " + dir.sub("imported") + kProjectorArgs +
    " -r 1.0 --depth-adaptive --scratch-dir " + dir.sub("scratch2") +
    " --level-plan " + plan + " " + excerptPath(), &status);
  ASSERT_EQ(status, 0) << imported;
  EXPECT_NE(imported.find("Wrote build_fingerprint.json"), std::string::npos) << imported;

  std::filesystem::create_directories(dir.sub("regen"));
  const std::string regenerated = run(
    std::string(BATCH_REGEN_EXE) + " -o " + dir.sub("regen") + kProjectorArgs +
    " -r 1.0 --level-plan " + plan + " " + excerptPath(), &status);
  ASSERT_EQ(status, 0) << regenerated;

  const std::vector<std::string> tiles = storeTiles(dir.sub("imported"));
  ASSERT_FALSE(tiles.empty()) << imported;
  EXPECT_EQ(tiles, storeTiles(dir.sub("regen")));
  for (const std::string & tile : tiles) {
    EXPECT_EQ(readFile(dir.sub("imported") + "/" + tile), readFile(dir.sub("regen") + "/" + tile))
      << tile << " differs between import_bag --depth-adaptive and batch_regen --level-plan";
  }

  // The fixed-level path over the same window still runs clean (it gathers at
  // a different capture distance, so its tiles are NOT expected to match).
  std::filesystem::create_directories(dir.sub("fixed"));
  const std::string fixed = run(
    std::string(IMPORT_BAG_EXE) + " -o " + dir.sub("fixed") + kProjectorArgs +
    " -r 1.0 " + excerptPath(), &status);
  ASSERT_EQ(status, 0) << fixed;
  EXPECT_FALSE(storeTiles(dir.sub("fixed")).empty()) << fixed;
}

// The spill is the only copy of the projected soundings. A fault in phase two
// must end the run through `abortDirtyReplay` -- exit 1, the reason, and the
// warning that what is already on disk is partial -- not in std::terminate and
// not in a finalized store fingerprinted as a complete build (ADR-0003).
TEST(RealBagSmoke, ATruncatedSpillAbortsTheReplayAndDeclaresTheStoreDirty)
{
  SKIP_WITHOUT_EXCERPT();
  TempDir dir("truncate");
  std::filesystem::create_directories(dir.sub("store"));
  std::filesystem::create_directories(dir.sub("scratch"));
  const std::string plan = dir.sub("plan.json");
  int status = -1;
  const std::string recon = run(reconCommand(dir, plan), &status);
  ASSERT_EQ(status, 0) << recon;

  const std::string scratch = dir.sub("scratch2");
  std::filesystem::create_directories(scratch);
  std::filesystem::create_directories(dir.sub("imported"));
  bool truncated = false;
  const std::string out = runWithFaultAfter(
    std::string(IMPORT_BAG_EXE) + " -o " + dir.sub("imported") + kProjectorArgs +
    " -r 1.0 --depth-adaptive --scratch-dir " + scratch +
    " --level-plan " + plan + " " + excerptPath(),
    "Phase two:", std::chrono::milliseconds(500),
    [&scratch, &truncated]() {
      const std::filesystem::path spill = spillFile(scratch);
      if (spill.empty()) {
        return;
      }
      std::error_code ec;
      const auto size = std::filesystem::file_size(spill, ec);
      if (ec || size < 64) {
        return;
      }
      // Off a record boundary: a short read at the end is what the replay must
      // refuse to treat as the end of the data.
      std::filesystem::resize_file(spill, size - 17, ec);
      truncated = !ec;
    }, &status);

  if (!truncated) {
    GTEST_SKIP() << "could not truncate the recon spill before the replay finished reading it; "
      "the fault was not injected, so nothing was proven (NOT a pass)\n" << out;
  }
  EXPECT_EQ(status, 1) << "a truncated spill must exit 1, not abort\n" << out;
  EXPECT_NE(out.find("truncated"), std::string::npos) << out;
  EXPECT_NE(out.find("NOT empty and NOT complete"), std::string::npos)
    << "the operator must be told the store on disk is partial\n" << out;
  EXPECT_FALSE(
    std::filesystem::exists(std::filesystem::path(dir.sub("imported")) /
    "build_fingerprint.json"))
    << "an aborted replay must not leave a fingerprint describing a complete build";
}

// The other end of the same contract: finalize() sits INSIDE the replay guard,
// so an I/O failure while persisting the resident tiles unwinds through
// abortDirtyReplay too -- it used to escape main() uncaught, over a store
// eviction had already been writing into since the first batch.
TEST(RealBagSmoke, AFinalizeFailureAbortsThroughTheSameDirtyStoreGuard)
{
  SKIP_WITHOUT_EXCERPT();
  TempDir dir("finalize");
  std::filesystem::create_directories(dir.sub("store"));
  std::filesystem::create_directories(dir.sub("scratch"));
  const std::string plan = dir.sub("plan.json");
  int status = -1;
  const std::string recon = run(reconCommand(dir, plan), &status);
  ASSERT_EQ(status, 0) << recon;

  // A read-only layer directory: the tile writes at the end of the run fail,
  // which is the I/O fault that used to escape main().
  const std::string store = dir.sub("imported");
  const std::filesystem::path layer = std::filesystem::path(store) / "processed";
  std::filesystem::create_directories(layer);
  std::filesystem::permissions(
    layer, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove);
  std::filesystem::create_directories(dir.sub("scratch2"));
  const std::string out = run(
    std::string(IMPORT_BAG_EXE) + " -o " + store + kProjectorArgs +
    " -r 1.0 --depth-adaptive --scratch-dir " + dir.sub("scratch2") +
    " --level-plan " + plan + " " + excerptPath(), &status);
  std::filesystem::permissions(
    layer, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);

  if (out.find("could not be finalized") == std::string::npos && status == 0) {
    GTEST_SKIP() << "the layer directory stayed writable (running as root?), so the fault "
      "was not injected and nothing was proven (NOT a pass)\n" << out;
  }
  EXPECT_EQ(status, 1) << "a failed finalize must exit 1, not abort\n" << out;
  EXPECT_NE(out.find("could not be finalized"), std::string::npos) << out;
  EXPECT_NE(out.find("NOT empty and NOT complete"), std::string::npos)
    << "finalize() must unwind through abortDirtyReplay, not past it\n" << out;
  EXPECT_EQ(out.find("done!"), std::string::npos) << out;
  EXPECT_FALSE(
    std::filesystem::exists(std::filesystem::path(store) / "build_fingerprint.json"))
    << "an aborted run must not leave a fingerprint describing a complete build";
}

// A store that IS complete but whose fingerprint cannot be written is a
// different outcome, and the contract distinguishes them: exit 2, and the
// operator is told the store is usable but cannot be told stale. Reporting it
// as a dirty store would send them to archive a good build.
TEST(RealBagSmoke, AnUnwritableFingerprintIsReportedAsCompleteButUnstampable)
{
  SKIP_WITHOUT_EXCERPT();
  TempDir dir("fingerprint");
  std::filesystem::create_directories(dir.sub("store"));
  std::filesystem::create_directories(dir.sub("scratch"));
  const std::string plan = dir.sub("plan.json");
  int status = -1;
  const std::string recon = run(reconCommand(dir, plan), &status);
  ASSERT_EQ(status, 0) << recon;

  // A directory where the fingerprint file goes: the rename over it fails at
  // the very last step, after the store has been persisted.
  const std::string store = dir.sub("imported");
  std::filesystem::create_directories(
    std::filesystem::path(store) / "build_fingerprint.json");
  std::filesystem::create_directories(dir.sub("scratch2"));
  const std::string out = run(
    std::string(IMPORT_BAG_EXE) + " -o " + store + kProjectorArgs +
    " -r 1.0 --depth-adaptive --scratch-dir " + dir.sub("scratch2") +
    " --level-plan " + plan + " " + excerptPath(), &status);

  EXPECT_EQ(status, 2) << "an unwritable fingerprint is exit 2, not 1 and not 0\n" << out;
  EXPECT_NE(out.find("is complete, but"), std::string::npos) << out;
  EXPECT_NE(out.find("FULL regen"), std::string::npos) << out;
  EXPECT_EQ(out.find("NOT empty and NOT complete"), std::string::npos)
    << "the store IS complete here; calling it dirty would send the operator to "
    "archive a good build\n" << out;
  EXPECT_EQ(out.find("done!"), std::string::npos) << out;
}
