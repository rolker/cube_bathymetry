// Copyright 2025 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
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


// Whole-tile refresh policy for the sub-window coverage push (#112).
//
// The property under test is not "tiles get re-sent" but the reason they must:
// a sub-window patch is best-effort AND its loss can be undiscoverable by the
// consumer. CAMP takes possession from a patch and records the message stamp as
// its held version (camp#121), so its held version sits at or ahead of the
// catalog whether or not the patch it lost ever arrived, and it never
// re-requests -- a dropped patch would be a permanent invisible hole.
// (The producer bumps the catalog only on a whole send, which fixes what
// anti-entropy CAN see; it cannot help a consumer already level with it.)
// The tracker's job is to make every patched tile go out whole within a
// bounded interval WITHOUT the consumer noticing anything.

#include <gtest/gtest.h>

#include <cstddef>
#include <set>
#include <vector>

#include "cube_bathymetry/coverage_refresh.h"
#include "marine_autonomy/gggs.h"

namespace cube
{
namespace
{

gggs::GridIndex tileAt(double lat, double lon)
{
  const gggs::Level level{gggs::Level::fromCellSize(1.0f)};
  return level.gridIndex(lat, lon);
}

// Three distinct tiles, far enough apart to be distinct at 1 m cells.
const gggs::GridIndex kA = tileAt(43.07, -70.76);
const gggs::GridIndex kB = tileAt(43.20, -70.76);
const gggs::GridIndex kC = tileAt(43.30, -70.76);

// Every tile still in memory unless a test says otherwise.
auto allResident() {return [](const gggs::GridIndex &) {return true;};}

CoverageRefreshTracker makeTracker(double interval = 60.0, std::size_t per_cycle = 2)
{
  CoverageRefreshTracker t;
  t.configure(interval, per_cycle);
  return t;
}

}  // namespace

// A tile that has only ever gone out WHOLE owes nothing: there is no patch that
// could have been lost, so re-sending it would be pure cost.
TEST(CoverageRefresh, WholeTileOnlyNeverOwesARefresh)
{
  auto t = makeTracker();
  t.notePublished(kA, 0.0, true);
  EXPECT_FALSE(t.refreshDue(kA, 0.0));
  EXPECT_FALSE(t.refreshDue(kA, 1000.0));
  EXPECT_EQ(t.owedCount(), 0u);
}

// A patch incurs the debt, and it comes due exactly at the interval -- not
// before (that would spend bandwidth early) and not after.
TEST(CoverageRefresh, PatchComesDueAtTheInterval)
{
  auto t = makeTracker(60.0);
  t.notePublished(kA, 0.0, true);
  t.notePublished(kA, 10.0, false);
  EXPECT_EQ(t.owedCount(), 1u);
  EXPECT_FALSE(t.refreshDue(kA, 59.999));
  EXPECT_TRUE(t.refreshDue(kA, 60.0));
  EXPECT_TRUE(t.refreshDue(kA, 600.0));
}

// The interval runs from the last WHOLE send, not the last patch. Otherwise a
// tile patched every cycle would push its own deadline back forever and never
// heal -- which is precisely the tile being surveyed right now.
TEST(CoverageRefresh, ContinuousPatchingCannotPostponeTheHeal)
{
  auto t = makeTracker(60.0);
  t.notePublished(kA, 0.0, true);
  for (double s = 5.0; s <= 55.0; s += 5.0) {
    t.notePublished(kA, s, false);
    EXPECT_FALSE(t.refreshDue(kA, s)) << "not due yet at " << s;
  }
  t.notePublished(kA, 60.0, false);
  EXPECT_TRUE(t.refreshDue(kA, 60.0));
}

// A tile patched by a sheet that has never sent it whole is overdue on sight:
// the consumer may hold nothing but a patch, over a tile it never received.
TEST(CoverageRefresh, PatchedButNeverSentWholeIsImmediatelyDue)
{
  auto t = makeTracker(60.0);
  t.notePublished(kA, 100.0, false);
  EXPECT_TRUE(t.refreshDue(kA, 100.0));
}

// A whole send discharges the debt -- including the refresh itself, which is
// what stops the tracker re-sending the same tile every cycle forever.
TEST(CoverageRefresh, WholeSendDischargesTheDebt)
{
  auto t = makeTracker(60.0);
  t.notePublished(kA, 0.0, false);
  ASSERT_TRUE(t.refreshDue(kA, 0.0));
  t.notePublished(kA, 1.0, true);
  EXPECT_FALSE(t.refreshDue(kA, 1.0));
  EXPECT_EQ(t.owedCount(), 0u);
  // Time alone does not re-incur it: with no patch outstanding there is
  // nothing that could have been lost, so a quiet tile costs nothing forever.
  EXPECT_FALSE(t.refreshDue(kA, 1e6));

  // A NEW patch does -- and its deadline is measured from the last WHOLE send
  // (1.0), not from the patch, so it comes due at 61.0 rather than 90.0.
  t.notePublished(kA, 30.0, false);
  EXPECT_FALSE(t.refreshDue(kA, 60.5));
  EXPECT_TRUE(t.refreshDue(kA, 61.0));
}

// THE CASE THE DRAIN EXISTS FOR: publishDirtyTiles only ever visits tiles that
// are still changing, so a tile the vessel has moved off would never be
// revisited. Its lost patch has to be healed by the drain or not at all.
TEST(CoverageRefresh, QuietTileIsStillHealed)
{
  auto t = makeTracker(60.0);
  t.notePublished(kA, 0.0, true);
  t.notePublished(kA, 5.0, false);   // surveyed, then the vessel moves on
  const std::set<gggs::GridIndex> nothing_published_now;
  EXPECT_TRUE(t.dueForRefresh(nothing_published_now, 50.0, allResident()).empty());
  const auto due = t.dueForRefresh(nothing_published_now, 70.0, allResident());
  ASSERT_EQ(due.size(), 1u);
  EXPECT_EQ(due.front(), kA);
}

// A tile handled in its own dirty cycle must not ALSO be drained: that would
// put the same tile on the wire twice.
TEST(CoverageRefresh, AlreadyPublishedThisCycleIsNotDrainedAgain)
{
  auto t = makeTracker(60.0);
  t.notePublished(kA, 0.0, false);
  const std::set<gggs::GridIndex> published{kA};
  EXPECT_TRUE(t.dueForRefresh(published, 100.0, allResident()).empty());
}

// The drain is bounded, and it spends its budget on the OLDEST debt -- so a
// tile cannot be starved by neighbours that keep coming due alongside it.
TEST(CoverageRefresh, DrainIsBoundedAndTakesTheOldestDebtFirst)
{
  auto t = makeTracker(60.0, 1);
  t.notePublished(kA, 0.0, true);
  t.notePublished(kB, 10.0, true);
  t.notePublished(kC, 20.0, true);
  t.notePublished(kA, 21.0, false);
  t.notePublished(kB, 21.0, false);
  t.notePublished(kC, 21.0, false);

  const std::set<gggs::GridIndex> none;
  auto due = t.dueForRefresh(none, 100.0, allResident());
  ASSERT_EQ(due.size(), 1u) << "budget of one per cycle";
  EXPECT_EQ(due.front(), kA) << "oldest whole-tile send goes first";

  t.notePublished(kA, 100.0, true);
  due = t.dueForRefresh(none, 101.0, allResident());
  ASSERT_EQ(due.size(), 1u);
  EXPECT_EQ(due.front(), kB) << "then the next oldest, not kA again";
}

// A tile that has left RAM cannot be quantized, so its debt is unpayable. It is
// dropped and COUNTED rather than retained forever -- an unbounded set of
// unpayable debts would be a slow leak, and a silent one would hide a real gap
// at the consumer.
TEST(CoverageRefresh, EvictedTileIsDroppedAndReported)
{
  auto t = makeTracker(60.0);
  t.notePublished(kA, 0.0, false);
  t.notePublished(kB, 0.0, false);
  const std::set<gggs::GridIndex> none;
  std::size_t dropped = 99;
  const auto due = t.dueForRefresh(
    none, 100.0, [](const gggs::GridIndex & i) {return i == kB;}, &dropped);
  ASSERT_EQ(due.size(), 1u);
  EXPECT_EQ(due.front(), kB);
  EXPECT_EQ(dropped, 1u);
  EXPECT_EQ(t.owedCount(), 1u) << "kA's unpayable debt is not retained";
}

// interval 0 disables the heal entirely (documented as only safe against a
// consumer that tracks patch possession itself). It must be OFF, not "due
// immediately" -- a zero that meant "refresh every cycle" would silently
// reinstate the whole-tile stream this feature exists to replace.
TEST(CoverageRefresh, ZeroIntervalDisablesTheRefresh)
{
  auto t = makeTracker(0.0);
  // The tile must have gone out WHOLE at least once first: a tile never sent
  // whole is due regardless of the interval (FirstMessageForATileIsAlwaysWhole),
  // and that rule is about possession, not about the periodic heal this test
  // is switching off.
  t.notePublished(kA, 0.0, true);
  t.notePublished(kA, 0.0, false);
  EXPECT_FALSE(t.refreshDue(kA, 1e9));
  const std::set<gggs::GridIndex> none;
  EXPECT_TRUE(t.dueForRefresh(none, 1e9, allResident()).empty());
}

// A zero budget disables only the drain, not the in-cycle refresh: a tile still
// being surveyed heals on its own dirty cycle.
TEST(CoverageRefresh, ZeroBudgetDisablesTheDrainButNotTheDueTest)
{
  auto t = makeTracker(60.0, 0);
  t.notePublished(kA, 0.0, false);
  EXPECT_TRUE(t.refreshDue(kA, 100.0));
  const std::set<gggs::GridIndex> none;
  EXPECT_TRUE(t.dueForRefresh(none, 100.0, allResident()).empty());
}

// clear() drops everything. Carried across a reconfigure, a stale last-whole
// time would claim a tile had been sent whole that the NEW sheet has never sent
// at all -- suppressing the heal for exactly the tiles a restart is most likely
// to have left a consumer stale on.
TEST(CoverageRefresh, ClearForgetsEverything)
{
  auto t = makeTracker(60.0);
  t.notePublished(kA, 0.0, true);
  t.notePublished(kA, 1.0, false);
  ASSERT_EQ(t.owedCount(), 1u);
  t.clear();
  EXPECT_EQ(t.owedCount(), 0u);
  // Due again immediately, and that is the point: after a clear this tracker
  // describes a sheet that has sent NOTHING whole, so the next message for any
  // tile must be the whole tile. The alternative -- treating a cleared tile as
  // satisfied -- would suppress the heal for exactly the tiles a reconfigure is
  // most likely to have left a consumer stale on.
  EXPECT_TRUE(t.refreshDue(kA, 1000.0));
  // And nothing is measured from a whole send the new sheet never made.
  t.notePublished(kA, 2000.0, false);
  EXPECT_TRUE(t.refreshDue(kA, 2000.0));
}

// A tile the tracker has never seen sent WHOLE is due on sight, whatever the
// interval says and whether or not a patch is outstanding. A consumer cannot
// apply a patch to a tile it never received, and with the catalog bumped only
// on whole sends such a tile carries no version for the consumer to reconcile
// against -- so the FIRST message for any tile has to be the whole tile.
TEST(CoverageRefresh, FirstMessageForATileIsAlwaysWhole)
{
  auto t = makeTracker(60.0);
  EXPECT_TRUE(t.refreshDue(kA, 0.0)) << "never sent whole";
  // Still true with the periodic heal switched off: this is a possession
  // requirement, not the periodic heal.
  auto off = makeTracker(0.0);
  EXPECT_TRUE(off.refreshDue(kA, 0.0));
  // And it stops being due once the whole tile has actually gone out.
  t.notePublished(kA, 1.0, true);
  EXPECT_FALSE(t.refreshDue(kA, 1.0));
}

// The backlog signal: true exactly when the outstanding debt is larger than
// the drain can clear in one interval, i.e. when the stated latency is not
// being met and the operator should be told rather than left to infer it.
TEST(CoverageRefresh, BacklogExceedsBudgetReportsAnUnmeetableLatency)
{
  auto t = makeTracker(60.0, 2);          // 2 per tick
  const double cycles_per_interval = 12;  // 60 s / 5 s tick -> clears 24
  EXPECT_FALSE(t.backlogExceedsBudget(cycles_per_interval));

  // 24 owed is exactly clearable; 25 is not.
  std::vector<gggs::GridIndex> tiles;
  for (int i = 0; i < 25; ++i) {
    tiles.push_back(tileAt(43.07 + 0.01 * i, -70.76));
  }
  for (int i = 0; i < 24; ++i) {
    t.notePublished(tiles[i], 0.0, true);
    t.notePublished(tiles[i], 1.0, false);
  }
  EXPECT_FALSE(t.backlogExceedsBudget(cycles_per_interval)) << "24 is clearable";
  t.notePublished(tiles[24], 0.0, true);
  t.notePublished(tiles[24], 1.0, false);
  EXPECT_TRUE(t.backlogExceedsBudget(cycles_per_interval));

  // With the heal disabled there is no latency to fail to meet, so the signal
  // must stay quiet rather than fire permanently.
  auto disabled = makeTracker(0.0, 2);
  disabled.notePublished(kA, 0.0, false);
  EXPECT_FALSE(disabled.backlogExceedsBudget(cycles_per_interval));
  auto no_budget = makeTracker(60.0, 0);
  no_budget.notePublished(kA, 0.0, false);
  EXPECT_FALSE(no_budget.backlogExceedsBudget(cycles_per_interval));
}

// The whole/patch decision is read off the window, which is what the consumer
// sees -- not a producer-side flag that could drift from the payload.
TEST(CoverageRefresh, WholeTileWindowIsDecidedFromTheWindowItself)
{
  EXPECT_TRUE(isWholeTileWindow(0, 0, 960, 960, 960, 960));
  EXPECT_FALSE(isWholeTileWindow(0, 0, 960, 959, 960, 960));
  EXPECT_FALSE(isWholeTileWindow(0, 1, 960, 960, 960, 960));
  EXPECT_FALSE(isWholeTileWindow(1, 0, 960, 960, 960, 960));
  EXPECT_FALSE(isWholeTileWindow(100, 100, 20, 20, 960, 960));
  // A window covering the tile from a non-zero origin cannot exist (the wire
  // contract bounds window_col + window_width by width), but must not be
  // mistaken for whole if it ever did.
  EXPECT_FALSE(isWholeTileWindow(1, 1, 960, 960, 960, 960));
}

// The debt is discharged by a whole send from ANY path, not only the drain's.
// tileRequestCallback serves a freshly quantized whole tile straight from the
// resident grid, so the gap the heal existed to close is already closed; a
// tracker that did not know would re-send the same ~183 kB within the interval,
// on the operator link this mode exists to protect.
TEST(CoverageRefresh, AWholeSendFromAnyPathDischargesTheDebt)
{
  auto t = makeTracker();
  t.notePublished(kA, 0.0, true);
  t.notePublished(kA, 1.0, false);            // patched: now owes a heal
  ASSERT_TRUE(t.owesRefresh(kA));
  ASSERT_TRUE(t.refreshDue(kA, 100.0));

  t.notePublished(kA, 2.0, true);             // a request serve, not the drain
  EXPECT_FALSE(t.owesRefresh(kA));
  EXPECT_FALSE(t.refreshDue(kA, 100.0));
  EXPECT_EQ(t.owedCount(), 0u);
}

// owesRefresh answers for ONE tile, which is what the eviction site needs: it
// is dropping a specific tile and can only report the debt it is making
// unpayable. owedCount (the whole set) cannot tell it that.
TEST(CoverageRefresh, OwesRefreshIsPerTile)
{
  auto t = makeTracker();
  t.notePublished(kA, 0.0, false);
  EXPECT_TRUE(t.owesRefresh(kA));
  EXPECT_FALSE(t.owesRefresh(kB));
  EXPECT_EQ(t.owedCount(), 1u);
}

// forget() must work with the heal DISABLED, because that is the configuration
// in which nothing else reaps: dueForRefresh returns early on interval 0 or
// budget 0 -- both documented values -- so its lazy residency reaping never
// runs and the debt set would grow for the life of the sheet. The node calls
// forget() from the eviction path precisely so the bound does not depend on
// the heal being switched on.
TEST(CoverageRefresh, ForgetReapsEvenWhenTheHealIsDisabled)
{
  auto t = makeTracker(0.0, 0);               // heal off, both ways
  t.notePublished(kA, 0.0, false);
  t.notePublished(kB, 0.0, false);
  ASSERT_EQ(t.owedCount(), 2u);

  // The drain reaps nothing here -- that is the early return under test.
  std::size_t dropped = 0;
  EXPECT_TRUE(t.dueForRefresh({}, 1000.0,
    [](const gggs::GridIndex &) {return false;}, &dropped).empty());
  EXPECT_EQ(dropped, 0u);
  EXPECT_EQ(t.owedCount(), 2u);

  t.forget(kA);
  EXPECT_FALSE(t.owesRefresh(kA));
  EXPECT_EQ(t.owedCount(), 1u);
}

}  // namespace cube
