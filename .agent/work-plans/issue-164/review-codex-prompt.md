# Adversarial Code Review

## Your Role

You are an independent adversarial reviewer. Your job is to find issues that
other reviewers missed: edge cases, security implications, incorrect
assumptions, subtle bugs, and logic errors.

Review the diff below with fresh eyes. Do not assume previous reviewers caught
everything. Focus on:

- **Edge cases**: What inputs or states could break this code?
- **Security**: Are there injection, auth, or data exposure risks?
- **Assumptions**: What does the code assume that might not hold?
- **Subtle bugs**: Off-by-one, race conditions, resource leaks, null/undefined
- **Logic errors**: Does the code actually do what the PR title claims?

## PR Under Review

**Title**: Local branch feature/issue-164 at fcf0c66
**Branch**: feature/issue-164
**Base**: 0ab490e
**HEAD**: fcf0c66

## Diff

```diff
diff --git a/README.md b/README.md
index a44b2f3..b0c92de 100644
--- a/README.md
+++ b/README.md
@@ -155,8 +155,13 @@ none of the previous window's cells (`test_node_lifecycle`).
   is refused the save timer is stopped and nothing retries the write: the tiles stay in
   RAM until cleanup is requested again. A `shutdown` transition (from `unconfigured`,
   `inactive` or `active`) makes one last attempt to write them and then completes
-  whether or not it succeeded; a process killed without either transition loses
-  whatever the periodic save has not written.
+  whether or not it succeeded, releasing the node's timers, subscriptions and
+  publishers as cleanup does. A process stopped with SIGINT or SIGTERM (Ctrl-C, or
+  `ros2 launch` stopping its nodes) makes the same last attempt after it stops
+  spinning, and logs `Stopping: wrote the N tile(s)...` (or, on failure, how many
+  were lost). Only a hard stop (SIGKILL, a crash, power loss) loses whatever the
+  periodic save has not written; `ros2 launch` sends SIGKILL to a node that has not
+  exited after its SIGINT and SIGTERM timeouts, so a flush slower than that is cut off.
 - **Nothing is saved when `draft_dir` is empty.** That is what makes the sheet fresh,
   and it also means the reset **permanently discards the in-RAM survey**: the old
   window exists nowhere afterwards. Save or export what you want to keep first. With
diff --git a/cube_bathymetry/CMakeLists.txt b/cube_bathymetry/CMakeLists.txt
index e6af4d0..a62959c 100644
--- a/cube_bathymetry/CMakeLists.txt
+++ b/cube_bathymetry/CMakeLists.txt
@@ -404,10 +404,11 @@ if(BUILD_TESTING)
   set_tests_properties(test_node_lifecycle PROPERTIES
     ENVIRONMENT "${CUBE_ISOLATED_TEST_ENV}")
 
-  # reset_cube_nodes.sh against a stub `ros2` on PATH (#164), 10 case groups:
+  # reset_cube_nodes.sh against a stub `ros2` on PATH (#164), 11 case groups:
   # consumer ordering, refusal on a non-empty draft_dir, fail-fast, preflight,
   # flag parsing, the never-configured probe, timeouts, stderr noise,
-  # `timeout --foreground` / SIGHUP / SIGPIPE, and probe-failure reporting. The
+  # `timeout --foreground` / SIGHUP / SIGPIPE, probe-failure reporting, and each
+  # node named once. The
   # stub never talks to DDS; the environment is blanked anyway so a future case
   # that does cannot reach live boats (same variables as the mru_transform test).
   add_test(NAME test_reset_cube_nodes
diff --git a/cube_bathymetry/scripts/reset_cube_nodes.sh b/cube_bathymetry/scripts/reset_cube_nodes.sh
index fae6067..ccbbcf1 100755
--- a/cube_bathymetry/scripts/reset_cube_nodes.sh
+++ b/cube_bathymetry/scripts/reset_cube_nodes.sh
@@ -16,6 +16,8 @@
 #                         while every CUBE node is quiesced, so it cannot absorb a
 #                         stale window from a node that has not been reset yet.
 #                         May be repeated.
+#   Each node is named once: a node given twice, or as both NODE and --consumer
+#   (`/a/b`, `a/b` and `/a/b/` are the same node), is a usage error.
 #   --allow-persistence   reset even though a node has a non-empty `draft_dir`.
 #                         Saved draft tiles are RELOADED on configure, so the sheet
 #                         is then NOT fresh; without this flag the script refuses.
@@ -82,6 +84,24 @@ while [ $# -gt 0 ]; do
 done
 [ ${#nodes[@]} -gt 0 ] || { echo "$prog: no cube_bathymetry node given" >&2; usage; exit 2; }
 
+# Each node once. A CUBE node also given as a consumer would be reactivated in
+# step 2 and publish `grid` while the remaining consumers are still being reset,
+# which is exactly what the ordering exists to prevent; a node given twice would
+# be cycled twice. Compared fully qualified, as ROS names the node.
+qualified() { local n=${1%/}; [ "${n#/}" = "$n" ] && n="/$n"; echo "$n"; }
+declare -A listed_as=()
+list_once() {
+  local key
+  key=$(qualified "$1")
+  if [ -n "${listed_as[$key]:-}" ]; then
+    echo "$prog: $key is given twice (as ${listed_as[$key]} and as $2); name each node once. Nothing was changed." >&2
+    exit 2
+  fi
+  listed_as[$key]=$2
+}
+for node in "${nodes[@]}"; do list_once "$node" "a CUBE node"; done
+for node in "${consumers[@]}"; do list_once "$node" "a --consumer"; done
+
 # A dropped ssh session (SIGHUP) or a closed pipe (SIGPIPE) would otherwise kill
 # the script mid-reset, leaving boats without `grid` and nothing printed.
 trap '' HUP PIPE
diff --git a/cube_bathymetry/src/cube_bathymetry_node.cpp b/cube_bathymetry/src/cube_bathymetry_node.cpp
index 4a700ee..86c888d 100644
--- a/cube_bathymetry/src/cube_bathymetry_node.cpp
+++ b/cube_bathymetry/src/cube_bathymetry_node.cpp
@@ -29,9 +29,7 @@ int main(int argc, char **argv)
   auto cube = std::make_shared<CubeBathymetry>();
 
   rclcpp::executors::SingleThreadedExecutor exe;
-  exe.add_node(cube->get_node_base_interface());
-  exe.spin();
-
+  spinUntilStopped(cube, exe);
 
   rclcpp::shutdown();
   return 0;
diff --git a/cube_bathymetry/src/cube_bathymetry_node.hpp b/cube_bathymetry/src/cube_bathymetry_node.hpp
index 2aa6e85..f6d778a 100644
--- a/cube_bathymetry/src/cube_bathymetry_node.hpp
+++ b/cube_bathymetry/src/cube_bathymetry_node.hpp
@@ -396,6 +396,14 @@ public:
       "with bag recording. See #74.";
     save_interval_s_ =
       declareOnce<double>("save_interval", 30.0, save_interval_desc);
+    // A <= 0 or non-finite interval would make the save timer fire continuously
+    // and starve the executor (as for disk_serve_interval below).
+    if (!(save_interval_s_ > 0.0) || !std::isfinite(save_interval_s_)) {
+      RCLCPP_WARN(get_logger(),
+        "save_interval=%g is not a positive finite duration; "
+        "using default 30s", save_interval_s_);
+      save_interval_s_ = 30.0;
+    }
 
     if (!draft_dir_.empty()) {
       // On startup, prime the fresh GeoMapSheet from the persisted draft grid so
@@ -551,6 +559,14 @@ public:
     // COMPLETE snapshot that drives anti-entropy reconciliation, and
     // `~/coverage_requests` lets a consumer ask for tiles it is missing/stale on.
     catalog_interval_s_ = declareOnce<double>("catalog_interval", 5.0);
+    // A <= 0 or non-finite interval would make the wall timer fire continuously
+    // and starve the executor (as for disk_serve_interval below).
+    if (!(catalog_interval_s_ > 0.0) || !std::isfinite(catalog_interval_s_)) {
+      RCLCPP_WARN(get_logger(),
+        "catalog_interval=%g is not a positive finite duration; "
+        "using default 5s", catalog_interval_s_);
+      catalog_interval_s_ = 5.0;
+    }
     // Disk-serve drain queue (#106): rate-limited from-disk catch-up of
     // evicted tiles requested by a consumer. interval x tiles_per_tick is the
     // catch-up throughput knob an operator tunes against the link budget
@@ -848,9 +864,9 @@ public:
     // Final flush so the last accumulation since the previous periodic save is
     // not stranded when the sheet is released below. (This is the cleanup path
     // only: a shutdown requested from INACTIVE or ACTIVE goes through on_shutdown,
-    // and a process killed without either transition loses whatever the periodic
-    // save has not yet written.) The save timer is normally cancelled
-    // on_deactivate, but guard here too in case cleanup is reached by another path.
+    // and a process stopped by SIGINT/SIGTERM flushes in spinUntilStopped(). Only
+    // a hard stop -- SIGKILL, a crash, power loss -- loses whatever the periodic
+    // save has not yet written.)
     saveDirtyTiles();
     // saveDirtyTiles() logs a write failure and KEEPS the dirty set so the next
     // save retries. Cleanup is the last chance: the next configure replaces the
@@ -872,6 +888,58 @@ public:
                CallbackReturn::FAILURE;
       }
     }
+    releaseConfiguredEntities();
+    return LifecycleNode::on_cleanup(state);
+  }
+
+  /// Shutdown from UNCONFIGURED, INACTIVE or ACTIVE (`ros2 lifecycle set shutdown`).
+  /// Unlike cleanup it cannot be refused, so a failed flush is logged and the
+  /// transition still completes; the tiles that could not be written are lost with
+  /// the process, and the error says how many. Everything configure/activate
+  /// created is then released, as on cleanup: a FINALIZED node must not keep
+  /// publishing its catalog or serving tiles from its timers.
+  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
+  on_shutdown(const rclcpp_lifecycle::State & state)
+  {
+    flushForStop();
+    releaseConfiguredEntities();
+    return LifecycleNode::on_shutdown(state);
+  }
+
+  /// Write every tile accumulated since the last periodic save, for a stop that
+  /// cannot be refused: the shutdown transition, and the process stopping on
+  /// SIGINT/SIGTERM (spinUntilStopped(), after the executor has stopped). Logs
+  /// how many tiles could not be written; they are lost with the process.
+  /// Nothing to do when no sheet is configured or draft_dir is empty.
+  void flushForStop()
+  {
+    if (draft_dir_.empty() || !geo_map_sheet_) {
+      return;
+    }
+    const std::size_t pending = geo_map_sheet_->dirtyGrids().size();
+    saveDirtyTiles();
+    const std::size_t unsaved = geo_map_sheet_->dirtyGrids().size();
+    if (unsaved > 0) {
+      RCLCPP_ERROR(get_logger(),
+        "Stopping with %zu tile(s) that could not be written to %s; they are "
+        "lost with the process.", unsaved, draft_dir_.c_str());
+    } else if (pending > 0) {
+      // saveDirtyTiles' own report is throttled; the last flush must be visible.
+      RCLCPP_INFO(get_logger(),
+        "Stopping: wrote the %zu tile(s) changed since the last save to %s.",
+        pending, draft_dir_.c_str());
+    }
+  }
+
+private:
+  /// Release everything on_configure and on_activate created, so nothing of the
+  /// old configuration can run afterwards. Shared by on_cleanup (after its final
+  /// flush has succeeded) and on_shutdown (which cannot refuse). Safe to call
+  /// with any of them already released.
+  void releaseConfiguredEntities()
+  {
+    // The timers are normally cancelled on_deactivate; a shutdown from ACTIVE, or
+    // a cleanup reached by another path, arrives with them still running.
     if (save_timer_) {
       save_timer_->cancel();
       save_timer_.reset();
@@ -884,8 +952,6 @@ public:
       disk_serve_timer_->cancel();
       disk_serve_timer_.reset();
     }
-    // on_deactivate cancels this one too; the guard covers a cleanup reached by
-    // another path, like the other timers above.
     if (refresh_timer_) {
       refresh_timer_->cancel();
       refresh_timer_.reset();
@@ -912,36 +978,15 @@ public:
     // touch a destroyed buffer.
     tf_listener_.reset();
     tf_buffer_.reset();
-    // The sheet can hold up to max_resident_tiles 960x960 grids. The final flush
-    // above has succeeded (or there is no draft_dir, so nothing was ever going to
-    // be saved), so release it and the version registry now instead of holding
-    // them until the next configure replaces them (#164). Every reader of either
-    // is a subscription or timer released above.
+    // The sheet can hold up to max_resident_tiles 960x960 grids. The caller has
+    // flushed it (cleanup refuses when that failed; shutdown has logged what it
+    // could not write), so release it and the version registry now instead of
+    // holding them until the next configure replaces them (#164). Every reader of
+    // either is a subscription or timer released above.
     geo_map_sheet_.reset();
     catalog_builder_ = marine_tiled_raster_store::TileCatalogBuilder{};
-    return LifecycleNode::on_cleanup(state);
   }
 
-  /// Shutdown from UNCONFIGURED, INACTIVE or ACTIVE (`ros2 lifecycle set shutdown`).
-  /// Unlike cleanup it cannot be refused, so a failed flush is logged and the
-  /// transition still completes; the tiles that could not be written are lost with
-  /// the process, and the error says how many.
-  rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn
-  on_shutdown(const rclcpp_lifecycle::State & state)
-  {
-    saveDirtyTiles();
-    if (!draft_dir_.empty() && geo_map_sheet_) {
-      const std::size_t unsaved = geo_map_sheet_->dirtyGrids().size();
-      if (unsaved > 0) {
-        RCLCPP_ERROR(get_logger(),
-          "Shutting down with %zu tile(s) that could not be written to %s; they are "
-          "lost with the process.", unsaved, draft_dir_.c_str());
-      }
-    }
-    return LifecycleNode::on_shutdown(state);
-  }
-
-private:
   /// Declare a parameter the first time only and return its current value.
   ///
   /// Parameters outlive cleanup, so a second on_configure must not declare them
@@ -2321,4 +2366,19 @@ private:
   }
 };
 
+/// Spin the node until the context shuts down, then flush its unsaved tiles.
+///
+/// SIGINT/SIGTERM (a terminal Ctrl-C, or `ros2 launch` stopping its nodes) shuts
+/// the context down and makes spin() return without any lifecycle transition, so
+/// on_shutdown never runs; before this, every normal stop lost what the periodic
+/// save had not yet written (#164). The flush runs here, after the executor has
+/// stopped, so no callback can touch the sheet at the same time.
+inline void spinUntilStopped(
+  const std::shared_ptr<CubeBathymetry> & node, rclcpp::Executor & executor)
+{
+  executor.add_node(node->get_node_base_interface());
+  executor.spin();
+  node->flushForStop();
+}
+
 #endif  // CUBE_BATHYMETRY_NODE_HPP_
diff --git a/cube_bathymetry/test/test_node_lifecycle.cpp b/cube_bathymetry/test/test_node_lifecycle.cpp
index 819bf54..f1294c9 100644
--- a/cube_bathymetry/test/test_node_lifecycle.cpp
+++ b/cube_bathymetry/test/test_node_lifecycle.cpp
@@ -52,6 +52,7 @@
 #include <vector>
 
 #include "lifecycle_msgs/msg/state.hpp"
+#include "marine_interfaces/msg/tile_catalog.hpp"
 #include "rclcpp/rclcpp.hpp"
 #include "tf2_msgs/msg/tf_message.hpp"
 
@@ -90,6 +91,9 @@ struct CubeBathymetryTestAccess
     n.catalog_builder_.update(probeIndex(), 5);
   }
 
+  static double catalogInterval(const CubeBathymetry & n) {return n.catalog_interval_s_;}
+  static double saveInterval(const CubeBathymetry & n) {return n.save_interval_s_;}
+
   /// Names of the fields that are NOT back at their fresh-node value.
   static std::vector<std::string> staleFields(const CubeBathymetry & n)
   {
@@ -470,6 +474,103 @@ TEST_F(NodeLifecycle, ShutdownCompletesWhenTheFlushFails)
   std::filesystem::remove_all(dir);
 }
 
+// A shutdown from ACTIVE releases what configure/activate created, as cleanup
+// does. Before, the timers outlived it and a FINALIZED node went on publishing
+// its coverage catalog every catalog_interval. Spun in an executor so the timers
+// can actually fire; the control phase proves the listener would see them.
+TEST_F(NodeLifecycle, ShutdownFromActiveStopsAllOutput)
+{
+  auto node = std::make_shared<CubeBathymetry>(
+    withParams({rclcpp::Parameter("catalog_interval", 0.1)}));
+  auto listener = std::make_shared<rclcpp::Node>("catalog_listener");
+  int catalogs = 0;
+  auto sub = listener->create_subscription<marine_interfaces::msg::TileCatalog>(
+    std::string(node->get_fully_qualified_name()) + "/coverage_catalog",
+    rclcpp::QoS(10).reliable(),
+    [&catalogs](marine_interfaces::msg::TileCatalog::ConstSharedPtr) {++catalogs;});
+  rclcpp::executors::SingleThreadedExecutor exe;
+  exe.add_node(node->get_node_base_interface());
+  exe.add_node(listener);
+  // Spin until `done` or the deadline, on this thread: transitions below run
+  // between spins, never concurrently with a callback.
+  auto spinUntil = [&exe](std::chrono::milliseconds limit, auto done) {
+      const auto end = std::chrono::steady_clock::now() + limit;
+      while (std::chrono::steady_clock::now() < end && !done()) {
+        exe.spin_some(std::chrono::milliseconds(10));
+        std::this_thread::sleep_for(std::chrono::milliseconds(5));
+      }
+    };
+  auto never = [] {return false;};
+
+  ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
+  ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
+  spinUntil(std::chrono::milliseconds(5000), [&catalogs] {return catalogs >= 3;});
+  ASSERT_GE(catalogs, 3) << "control: the listener must see the catalog while ACTIVE";
+
+  ASSERT_EQ(node->shutdown().id(), State::PRIMARY_STATE_FINALIZED);
+  spinUntil(std::chrono::milliseconds(200), never);  // drain anything in flight
+  catalogs = 0;
+  spinUntil(std::chrono::milliseconds(1000), never);  // ten catalog intervals
+  EXPECT_EQ(catalogs, 0) << "a FINALIZED node must not keep publishing its catalog";
+  EXPECT_FALSE(node->sheet()) << "shutdown must release the sheet, as cleanup does";
+}
+
+// SIGINT/SIGTERM (Ctrl-C, or ros2 launch stopping its nodes) shuts the context
+// down and returns from spin() with no lifecycle transition, so on_shutdown never
+// runs. spinUntilStopped() -- what main() runs -- must still write the tiles
+// accumulated since the last periodic save. The context is shut down directly,
+// which is what rclcpp's signal handler does; a private context keeps the suite's
+// global one alive for the other tests.
+TEST_F(NodeLifecycle, ContextShutdownFlushesUnsavedTiles)
+{
+  const auto dir = makeTempDir();
+  {
+    auto context = std::make_shared<rclcpp::Context>();
+    context->init(0, nullptr);
+    auto node = std::make_shared<CubeBathymetry>(
+      withParams({rclcpp::Parameter("draft_dir", dir.string())}).context(context));
+    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
+    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
+    addPatch(node->sheet(), -12.5);
+    ASSERT_FALSE(node->sheet()->dirtyGrids().empty());
+    ASSERT_FALSE(std::filesystem::exists(dir / "draft")) << "test setup: nothing saved yet";
+
+    rclcpp::ExecutorOptions options;
+    options.context = context;
+    rclcpp::executors::SingleThreadedExecutor exe(options);
+    std::thread spinner([&node, &exe] {spinUntilStopped(node, exe);});
+    std::this_thread::sleep_for(std::chrono::milliseconds(300));
+    context->shutdown("test: what the SIGINT handler does");
+    spinner.join();
+
+    EXPECT_TRUE(std::filesystem::exists(dir / "draft") &&
+      !std::filesystem::is_empty(dir / "draft"))
+      << "stopping on a signal must write the tiles accumulated since the last save";
+    EXPECT_TRUE(node->sheet()->dirtyGrids().empty());
+  }
+  std::filesystem::remove_all(dir);
+}
+
+// A zero, negative or non-finite timer interval would make its wall timer fire
+// continuously and starve the executor; configure falls back to the default.
+TEST_F(NodeLifecycle, NonPositiveTimerIntervalsFallBackToDefaults)
+{
+  const auto dir = makeTempDir();
+  {
+    auto node = std::make_shared<CubeBathymetry>(
+      withParams({
+      rclcpp::Parameter("draft_dir", dir.string()),
+      rclcpp::Parameter("catalog_interval", 0.0),
+      rclcpp::Parameter("save_interval", -1.0)}));
+    ASSERT_EQ(node->configure().id(), State::PRIMARY_STATE_INACTIVE);
+    EXPECT_EQ(CubeBathymetryTestAccess::catalogInterval(*node), 5.0);
+    EXPECT_EQ(CubeBathymetryTestAccess::saveInterval(*node), 30.0);
+    ASSERT_EQ(node->activate().id(), State::PRIMARY_STATE_ACTIVE);
+    EXPECT_EQ(node->shutdown().id(), State::PRIMARY_STATE_FINALIZED);
+  }
+  std::filesystem::remove_all(dir);
+}
+
 // Cleanup releases the sheet and the catalog instead of holding them until the
 // next configure: a reset on a boat that has run for hours should not keep the
 // old survey in RAM while the new one is built.
diff --git a/cube_bathymetry/test/test_reset_cube_nodes.sh b/cube_bathymetry/test/test_reset_cube_nodes.sh
index e29339a..e0dd43f 100755
--- a/cube_bathymetry/test/test_reset_cube_nodes.sh
+++ b/cube_bathymetry/test/test_reset_cube_nodes.sh
@@ -20,6 +20,8 @@
 #      do not kill the script mid-reset.
 #  10. a failing probe is not reported as a failed reset; a CUBE node that is not
 #      active is warned about in the preflight.
+#  11. a node given twice, or as both a CUBE node and a consumer (in any spelling
+#      of its name), is a usage error before anything is read or changed.
 #
 # Usage: test_reset_cube_nodes.sh /path/to/reset_cube_nodes.sh
 
@@ -273,6 +275,24 @@ case "$out" in *"no CUBE node was reset"*"/boat4/cube_bathymetry"*) ;; *) fail "
 case "$out" in *"warning: /boat4/cube_bathymetry is 'unconfigured'"*) ;; *) fail "case 10a: no preflight warning: $out" ;; esac
 [ "$(cat "$work/state/_boat1_cube_bathymetry")" = active ] || fail "case 10a: another node was reset"
 
+# --- 11. each node named once -----------------------------------------------------
+setup_nodes
+out=$("$script" --consumer /boat2/cube_bathymetry "${cubes[@]}" 2>&1); rc=$?
+[ "$rc" -eq 2 ] || fail "case 11a: CUBE node also a consumer: expected exit 2, got $rc: $out"
+case "$out" in *"/boat2/cube_bathymetry"*"twice"*"Nothing was changed"*) ;; *) fail "case 11a: message: $out" ;; esac
+[ ! -s "$work/log" ] || fail "case 11a: transitions ran: $(cat "$work/log")"
+setup_nodes
+out=$("$script" --consumer boat2/cube_bathymetry/ "${cubes[@]}" 2>&1); rc=$?
+[ "$rc" -eq 2 ] || fail "case 11b: same node in another spelling: expected exit 2, got $rc: $out"
+[ ! -s "$work/log" ] || fail "case 11b: transitions ran: $(cat "$work/log")"
+setup_nodes
+out=$("$script" "${cubes[@]}" /boat1/cube_bathymetry 2>&1); rc=$?
+[ "$rc" -eq 2 ] || fail "case 11c: CUBE node given twice: expected exit 2, got $rc: $out"
+setup_nodes
+out=$("$script" --consumer /coverage_assessment --consumer coverage_assessment "${cubes[@]}" 2>&1); rc=$?
+[ "$rc" -eq 2 ] || fail "case 11d: consumer given twice: expected exit 2, got $rc: $out"
+[ ! -s "$work/log" ] || fail "case 11d: transitions ran: $(cat "$work/log")"
+
 if [ "$failures" -ne 0 ]; then
   echo "$failures failure(s)"
   exit 1
```

## Plan Context

Below is the `## Approach` section of the plan this change is meant
to implement. It is context, not the subject of the review: flag
divergences between the diff and this plan, but do not review the
plan itself.

```markdown

### PR (a): fresh sheet without a restart (no message dependency)

1. **Pure refactor, no behaviour change.** `git mv src/cube_bathymetry_node.cpp src/cube_bathymetry_node.hpp`
   (class definition, header guard `CUBE_BATHYMETRY__CUBE_BATHYMETRY_NODE_H_`), new thin `src/cube_bathymetry_node.cpp`
   (`main()` only). The header lives in `src/`, **not** `include/`: it is not installed and is not public API; the test adds `src/` to its private
   include path. CMake: an INTERFACE target `cube_bathymetry_node_link` carrying the node's current link list, used by the
   executable and by the new test, so the two cannot drift. `git mv` keeps blame.
2. **Test seam.** Two public const accessors, `std::shared_ptr<cube::GeoMapSheet> sheet() const` and `std::size_t catalogSize() const` (documented "diagnostics and tests"; the
   second is needed to assert that the tile catalog is empty after a fresh start), and a defaulted `rclcpp::NodeOptions` constructor argument so a test can supply parameter
   overrides before the first configure (the executable passes the default). No other test hooks; the transitions are the public `configure()/activate()/deactivate()/cleanup()` of `LifecycleNode`.
   *(As implemented, `db74bf8`..`dc9fdca`.)*
3. **Red test first.** `test/test_node_lifecycle.cpp` (rclcpp initialised in `SetUpTestSuite`; `set_tests_properties ... ENVIRONMENT` gives this test its **own
   `ROS_DOMAIN_ID`** and `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST` (jazzy; `ROS_LOCALHOST_ONLY` is deprecated) so parallel colcon tests, live boats and the CI container cannot
   cross-talk): configure -> activate -> deactivate -> cleanup -> configure, and assert the current state is INACTIVE after the second configure.
   Run it on the unfixed code (after the pure refactor, before the guard) and record in progress.md whether it fails
   (confirming or refuting the `ParameterAlreadyDeclaredException` reading). This is the reproduction work item.
4. **Guard.** Add `declareOnce<T>(name, default, descriptor)` as in unh_marine_autonomy helm_manager (PR#401): declare only when `!has_parameter`,
   then return the current value. Replace **every** `declare_parameter` in `on_configure` (the ~18 sites, including those with `read_only` descriptors, and `map_frame`).
   Consequence documented: a value set at runtime between cycles is what the next configure reads. (helm_manager's `declareReadOnlyString` undeclare/redeclare dance exists for
   `automatically_declare_parameters_from_overrides`, which this node does not enable; a comment says so.)
5. **Reset leftover state on configure and release the listener safely in cleanup**, found by auditing the members: `last_grid_publish_time_`, `have_publish_tf_`/`last_publish_tf_`,
   `have_vessel_pos_`/`last_vessel_*`. In `on_cleanup`, release `tf_listener_` **before** `tf_buffer_` (the old listener holds a reference to the buffer and
   can touch it after destruction at `on_configure`'s reassignment, where the unique_ptr buffer is replaced before the shared_ptr listener) and the publishers/subscriptions that are recreated, symmetric with `sonar_info_subscription_`.
   *(As implemented: `on_cleanup` resets the ping and tile-request subscriptions, the four publishers, then the listener, then the buffer, then the sheet and the catalog
   registry once the final flush has succeeded. After the pre-push review: `on_configure` also resets a leftover `tf_listener_` before assigning `tf_buffer_`, because a configure that throws
   returns to UNCONFIGURED without `on_cleanup`; `on_cleanup` returns FAILURE, before releasing anything, when `draft_dir` is set and dirty grids remain after a failed
   `saveDirtyTiles()` (ADR-0001: a refused cleanup, not lost tiles); `refresh_timer_` is cancelled there too, and `drainRefreshQueue`, `publishCatalog` and `drainDiskServeQueue` return early when the publisher or sheet is gone. After round 2: an `on_shutdown` flushes the dirty tiles (from UNCONFIGURED, INACTIVE or ACTIVE) and always completes, logging how many tiles it could not write, since a shutdown cannot be refused; the cleanup comment no longer claims tiles are safe on shutdown, and the README says tiles stay in RAM while a cleanup is refused and that a killed process loses what the periodic save has not written.)*
6. **Empty-start tests** (same file): with `draft_dir` and `prior_store_dir` empty, add soundings through `sheet()`, deactivate, cleanup, configure;
   assert a new `GeoMapSheet` object with no grids and an empty catalog. A characterization test with `draft_dir` set to a temp dir: after the same cycle
   the saved tiles reload (the sheet is NOT empty), pinning the behaviour the README will state. A test that a parameter set between cycles is honoured.
   *(After the pre-push review, also: `FailedConfigureCanBeRetried`, `CleanupFailsWhenTheFinalFlushFails`, `CleanupReleasesTheSheetAndCatalog`, `ReadOnlyOverridesSurviveACycle` and
   `ReconfigureResetsTheSessionState`, which dirties the cursor/cache/queue/tracker/catalog state through a `friend struct CubeBathymetryTestAccess` and checks each is fresh after the next configure.
   Round 2 added `FailedConfigureCanBeRetriedWhileTfIsPublished` (300 failed configures while a second node publishes `/tf`; it pins the `tf_listener_.reset()` only under AddressSanitizer, the comment says so),
   `on_shutdown` (see item 5) with `ShutdownFromActiveFlushesUnsavedTiles` and `ShutdownCompletesWhenTheFlushFails`.
   The test's `ROS_DOMAIN_ID` is 87, not 164: above 101 the DDS port formula overlaps the ephemeral range.)*
7. **Stop condition.** If the empty-start assertion fails for a reason the guard does not fix, the plan stops here: record the cause in progress.md and
   hand it to the owner. No `clear` service is built before the event (owner decision); it stays a named follow-up.
8. **Elevation sign test and docs.** In `test_publish_equivalence.cpp` add `ElevationIsEllipsoidHeightPositiveUp`: a sounding at known lat/lon/altitude
   projects to an `elevation` equal to that altitude (a seabed is *below the water surface's ellipsoid height*: negative near sea level, positive on a lake whose surface is above the ellipsoid, e.g. Lake Massabesic at +48.88 m, so the test has both cases). README: the `grid` message's `elevation` is ellipsoid height, positive up (cites the existing
   ellipsoidal-heights statement and `ssp_ray_tracer.h`), 95 % (1.96 sigma) uncertainty, how to get depth below the water surface (the surface's ellipsoid height minus `elevation`),
   and the d^2/2R caveat on the tide-derived surface height.
9. **Reset command sequence as a script.** `scripts/reset_cube_nodes.sh [--allow-persistence] [--consumer NODE]... NODE...` (installed to `lib/cube_bathymetry`).
   Order: for each CUBE node, walk to unconfigured (`ros2 lifecycle get`, then deactivate/cleanup as needed, so an already-inactive node is not an error);
   then each `--consumer` node (anything accumulating state derived from `grid`, e.g. the assessor) is walked to unconfigured and back to active;
   then each CUBE node is configured and activated. Quiescing CUBE first means no stale window reaches a reset consumer. It reads `draft_dir` and `prior_store_dir`
   with `ros2 param get` and **refuses** if `draft_dir` is non-empty unless `--allow-persistence` (a non-empty `prior_store_dir` only prints a note: it seeds a predicted surface, not coverage).
   A node that was never configured has no readable `draft_dir` (declared at configure), so after every other check has passed it is configured, read and put back to unconfigured, one node at a time.
   Fails fast with the node and transition named, prints final states. *(After the pre-push review: step 0 first checks that every node and consumer answers and is `active`/`inactive`/`unconfigured`, before any transition;
   a failure after the first change, or SIGINT/SIGTERM, prints every node's state and a re-run hint; every `ros2` call is under `timeout` (`RESET_CUBE_GET_TIMEOUT` 30 s, `RESET_CUBE_SET_TIMEOUT` 300 s) and parsed output is stdout only; `--consumer` rejects a value starting with `-`. After round 2: calls run under `timeout --foreground` so Ctrl-C reaches `ros2`; `trap '' HUP PIPE` plus a README line about tmux/nohup (the final report returns 0 even if stdout is closed); the preflight warns for each CUBE node that is not `active`; a failing probe of a never-configured node is reported as a failed probe ("no CUBE node was reset"), not a failed reset, via a `probing` flag instead of `changed`; the refusal advises a NEW empty `draft_dir` plus `--allow-persistence`, never `draft_dir ''`.)* `package.xml` gains `exec_depend` on `ros2lifecycle` and `ros2param` (the script shells out to both).
   `test/test_reset_cube_nodes.sh` runs it against a stub `ros2` on `PATH` holding per-node state, registered with `add_test`, **three cases at first, eight after round 1, ten case groups after round 2** (adds `timeout --foreground` on every call, SIGHUP and SIGPIPE survival, probe-failure wording and the preflight warning; case 6b now probes last: the never-configured node comes first in the argument order and the `draft_dir` is on the last node): consumer ordering,
   refusal on non-empty `draft_dir`, failing transition naming the node (and the state/re-run output), preflight of a mistyped consumer and a finalized node, `--consumer` not swallowing a flag,
   the never-configured-node probe (three variants), a hung node under the timeout, and stderr noise. The "already inactive" and four-node permutations are covered by the live check (step 11), not by the stub.
   The script must not block PR (a): if it slips, (a) ships with the README command sequence and the script follows.
10. **Docs.** README section "Starting a fresh sheet without restarting": the exact command sequence for four namespaced nodes
    (`/boat1/cube_bathymetry` ... `/boat4/cube_bathymetry`), what is and is not cleared, the `draft_dir` caveat, ordering with consumers. It records the **event configuration
    consequence**: `bizzyboat.yaml` sets `draft_dir: /home/field/data/world/depths` for `/**/cube_bathymetry` and `bizzyboat_sim_core_launch.py` layers that file, so any launch that loads it makes a lifecycle cycle reload old tiles and
    "fresh sheet" false; the demo launch (unh_marine_simulation#88) must leave `draft_dir` empty, which the host has already told that implementer. The script refuses loudly if it is not.
    *(The README also states, after the pre-push review: an empty-`draft_dir` reset permanently discards the in-RAM survey; the named boats publish no `grid` between the quiesce and the bring-up, so it is not for use underway;
    a wrong `read_only` value (`capture_spacing_scale`, `publish_dirty_subwindow`) needs a restart; and a cleanup whose flush failed refuses. After round 2: do not clear `draft_dir` with `''` (no persistence and no eviction, so RAM grows on a long survey, and cleanup flushes against the directory the node was configured with); point it at a new empty directory and pass `--allow-persistence`.)*
11. **Live verification** on a built node (no persistence): run the script against a real `cube_bathymetry_node` (own `ROS_DOMAIN_ID`, localhost discovery), confirm `ros2 lifecycle get` ends `active`
    and the first `grid` after the reset contains none of the previous window's cells; record in progress.md. Full-scope `ci_local.sh` before push.
    *(Done 2026-10-09 with a throwaway publisher feeding two patches 120 m apart; recorded in progress.md `## Implementation`. Full-scope `ci_local.sh` passed before push; the attestation is in `refs/notes/ci-local` and the run is recorded in progress.md.)*
12. **Follow-up named, not built:** a `clear` service (draft below under Follow-ups). Filing the issue needs the owner's yes.

### PR (b): the `coverage_assessment` package (core first, wrapper last; after #406 is on `jazzy`)

**Decisions.**
- **Where:** new colcon package `coverage_assessment/` in this repo next to `cube_bathymetry/`. Generic by name: it reads any grid_map source that carries
  `elevation` + `uncertainty`, not only CUBE. Node `coverage_assessment` (lifecycle), executable `coverage_assessment_node`.
- **Testable:** library + thin main. `coverage_assessment_core` is plain C++ (grid_map_core + std only, no ROS, no message types); `coverage_assessment_ros`
  holds the node class in a header with the wrapper code (in `src/`, not installed); `main()` only spins. The core never sees `SurveyArea` or TF; it has local structs
  (`SurveyStandard{name,a,b}`, `AreaDefinition{id, polygon in raster-frame metres, standard}`, an affine `raster -> source` horizontal mapping) that the wrapper fills.
- **Input design (the issue's open item):** the node keeps a **latched union over time per source**, built from that source's `grid` messages. It does not
  read `coverage_tiles` or the store: the live `grid` is what every source (sim boats and real ones) already publishes, and the union fixes the 200 m window problem.
  Per source: a lattice anchored in the **assessor's raster frame** (cell (i,j) covers `[i*cs,(i+1)*cs)`), paged 64x64 blocks of (elevation, uncertainty) floats.
  Each incoming message is resampled **pull-style**: for every lattice centre whose image in the message's frame falls inside the message's extent, take the message cell containing it.
  Pull resampling is needed because each CUBE message sizes its geometry from its own min/max, so successive messages are not on one lattice, and a push would alias into pinholes.
  **Never-erases applies per field**: a finite elevation overwrites the held elevation; a finite uncertainty overwrites the held uncertainty; a NaN in either field never erases that field
  (a finite elevation with NaN uncertainty, which the sim produces before TF settles, must not wipe a good uncertainty and flip a meeting cell to not meeting).
  Covered = finite elevation. Memory grows with surveyed cells (~8 B/cell plus paging); logged every 10 s.
- **Frames (new).** The raster frame is the `map_frame` parameter. For each message the wrapper does **one** `lookupTransform(message_frame <- map_frame)` (latest available; this is per message,
  not per cell) and hands the core a horizontal affine; lattice centre -> earth -> source frame is exactly that composed transform with z = 0, since only the horizontal placement matters
  (`elevation` is an ellipsoid height, frame independent, so it is never transformed; the z error from tangent-plane curvature does not move the pull lookup measurably at event scale).
  A message whose transform is unavailable is **skipped with a throttled WARN naming the source and frame**, and counted in the node's diagnostics; the source is never silently dropped.
  Polygon vertices are projected with exactly the chain `grid_projection.cpp` uses for cell centres: `GeoPointLatLongDegrees(lat, lon, 0)` -> ECEF -> `map_frame <- earth` affine
  (one `lookupTransform` per assessment, cached per area until the area or the transform changes) -> x,y.
- **Water surface.** `h_surface` = geodetic altitude of the origin of `earth <- water_surface_frame` (with origin height 0 this is `map_tide.z`; going through `earth` keeps one code path for any frame).
  **Which boat's `map_tide`:** exactly one, named by the `water_surface_frame` parameter (e.g. `boat1/map_tide`); the same surface is applied to every source. Consequences, stated rather than discovered:
  (1) a source on a boat far from the tide boat sees a surface that is only as good as the one-surface assumption (README: valid for an area of a few km, same body of water);
  (2) **when that boat's estimator withholds `map_tide`** (stale beyond `water_surface_max_age`, or not published), the assessor stops scoring **everyone**: it skips the assessment with a throttled WARN, keeps
  accumulating grids, and the last assessment stays on the transient_local topic (the scoreboard shows an old value, the log says why). (3) The curvature bound above: the assessor measures d, the distance of the
  tide boat (`water_surface_boat_frame`, e.g. `boat1/base_link`) from the origin of the tide's parent frame, and above `water_surface_max_boat_distance` it **warns and skips scoring**, the same behaviour as (2).
- **Rasterising and judging.** Polygon (raster-frame metres) -> scanline even-odd fill; a cell belongs to the area when its **centre** is inside (documented half-open rule), so
  `area_total` = cells x cell_size^2 exactly as #406 defines. Per cell: covered if any source has a depth; best uncertainty = smallest finite uncertainty across sources
  (ties go to the earlier configured source); depth from that source = `h_surface - elevation`; meets when `u* <= sqrt(a^2+(b*depth)^2)`;
  no finite uncertainty anywhere -> covered, not meeting. Per source: covered and meeting judged on its own grid with its own depth; `area_unique` = cells only that source covers.
  Sources keep the order of `grid_topics` in every message.
- **Negative depths are never clamped silently.** A cell whose depth is negative (seabed "above" the surface) is evaluated with depth 0 in the threshold (so the formula stays defined), but each assessment **counts the cells with depth below about -1 m**
  and logs a throttled WARN with the count and the surface height used, so a wrong sign, datum or stale tide cannot pose as a plausible verdict. The count is also in the node's diagnostics and tested.
- **Output.** `coverage_assessment`: one `CoverageAssessment` per area per assessment, **transient_local** (depth = number of areas held, so a scoreboard that starts late receives every area). `coverage_grid`: one raster over the bounding box of all areas
  (**transient_local**, depth 1), layer `meets_standard` (NaN not covered or outside every area, 0 below standard, 1 meets); where areas overlap a cell is 1 only if it meets the standard of **every** area containing it.
  `header.frame_id` of both outputs is `map_frame`. Assessment runs on a timer, only when a grid, an area, or the water surface (> 1 cm) changed, so `coverage_grid` is republished at most once per `assessment_period`.
- **Robustness decisions.** Invalid `SurveyArea` (< 3 vertices, non-finite coordinates, negative or non-finite `a`/`b`, raster larger than `max_raster_cells`) is rejected with an ERROR and
  an earlier definition of that id is kept; the registry replaces by id only if `stamp >=` the held one. `grid_topics` empty, duplicated, or `source_names` of a different length: configure FAILS with the reason.
  Message conversion errors are caught per message. `survey_area` uses a fixed QoS depth (transient_local, depth 100 hard-coded with a comment; no parameter).

**Parameters** (declared in the constructor with `read_only` descriptors for everything read at configure, so a runtime set is rejected by rclcpp itself and there is no accepted-but-inert set; no custom set-callback):

| Parameter | Default | Meaning |
|---|---|---|
| `grid_topics` | `[]` (required) | CUBE `grid` topics, one per source; order defines `sources` order |
| `source_names` | `[]` | per-source label (platform name); empty entry or empty list falls back to the topic |
| `survey_area_topic` | `survey_area` | area source (`SurveyArea`, transient_local) |
| `assessment_period` | `1.0` s | timer period |
| `cell_size` | `1.0` m | assessment lattice; should equal CUBE's `cell_size`, a source resolution differing by > 1e-6 warns once per source |
| `map_frame` | `map` | the frame of the assessment raster; sources in other frames are transformed into it |
| `water_surface_frame` | `map_tide` | origin of this frame is the water surface; one boat's (e.g. `boat1/map_tide`) |
| `water_surface_boat_frame` | `base_link` | the tide-publishing boat, for the curvature-bound distance |
| `water_surface_max_boat_distance` | `1500.0` m | beyond this the surface is low by > ~18 cm: warn and skip scoring |
| `water_surface_max_age` | `30.0` s | oldest acceptable water-surface transform |
| `max_raster_cells` | `1000000` | per-area raster cap (guards a swapped lat/lon and the `coverage_grid` message, ~4 MB at this size) |

(`earth_frame` is hard-coded `earth`; `survey_area_queue_depth` is hard-coded.)

**Steps (build order). B1 is the first thing done in (b), before any package code; B3-B6 build and pass in CI without #406 on their own, B7 is what needs it.**

- B0. Re-read #406 (merged by then) and compare field names to the plan; until it is merged, create the local, **uncommitted** `upstream.repos` override and a layer worktree of the #406 branch.
- B1. **Datum and frame spike, before any wrapper code.** A throwaway script (not committed; findings recorded in progress.md) on the running sim: for each source, print `h_surface - elevation` (with `h_surface` read from `earth <- map_tide` exactly as the wrapper will) and the source's `header.frame_id`, and measure the
  **pinhole share** inside surveyed swaths of single `grid` messages (NaN share between finite cells; a stationary boat repeats the same phase so pull resampling cannot fill it), comparing `area_covered` from a direct count with what the accumulator will report. Its results decide the frame handling above
  (confirm the `<ns>/map` frames and that `lookupTransform` between them works), the sign of depth, and whether a pinhole fill is needed. If the spike contradicts a decision here, the plan is revised before B2.
- B2. Package skeleton, `package.xml` (explicit dependencies: `grid_map_core`, `grid_map_ros`, `grid_map_msgs`, `rclcpp`, `rclcpp_lifecycle`, `tf2`, `tf2_ros`, `tf2_eigen`, `tf2_geometry_msgs`, `marine_autonomy`, `marine_interfaces`), core library target, and CI plumbing: `.github/ci/Dockerfile` COPY of `coverage_assessment/package.xml` **and** the `--from-paths` argument of both `rosdep check` and `rosdep install` (currently `src cube_bathymetry`, Dockerfile ~45-52), plus the `ci-image.yml` path trigger.
- B3. `polygon_raster` (+ tests): axis-aligned squares at several offsets (exact area), triangle vs analytic area within one boundary row, L-shape, edge on a lattice line, negative coordinates, < 3 vertices, zero-area, oversize rejection.
- B4. `source_accumulator` (+ tests): phase-shifted messages of the same surface give identical coverage; a message in a rotated/translated frame (affine) gives the same coverage as the same message in the raster frame; NaN does not erase, **per field** (finite elevation with NaN uncertainty keeps the held uncertainty); later finite values overwrite; **moving window** (3 windows sweeping a strip) leaves coverage equal to the union and never decreasing; elevation without uncertainty is covered; `clear()` empties.
- B5. `coverage_assessor` + `area_registry` (+ tests): known areas; two overlapping sources (`area_unique`, sums exceed totals); a missing uncertainty does not meet; depth-dependent threshold (same uncertainty fails shallow, meets deep); equality meets; depth taken from the smallest-uncertainty source (two sources, depths straddling the threshold); per-source judged on own grid while the union meets; tie order; water-surface shift changes the verdict; **negative-depth count** (cells below -1 m counted, threshold evaluated at 0); registry replace-by-id, stale stamp ignored, invalid area keeps the old one.
- B6. `assessment_raster` (+ tests): `meets_standard` values, overlapping areas with different standards, bounding box and frame.
- B7. ROS wrapper (needs #406): conversions, polygon projection, per-message source-frame transform (skip + throttled WARN when unavailable), water-surface height with the age and distance refusals, node class + thin main, `read_only` parameters, launch file, install rules, tf2/`ros2` exec dependencies.
- B8. Wrapper tests, **happy path, frame handling, stale tide, reset-empties-union only** (per-test `ROS_DOMAIN_ID`, `ROS_AUTOMATIC_DISCOVERY_RANGE=LOCALHOST`): (i) projection agrees with `cube::geoGridsToGridMap`'s cell placement for the same lat/lon under a known `earth -> map` (polygon and grid share one chain); (ii) node integration over a static TF broadcaster (`earth->map`, `earth->map_tide`) and synthetic `GridMap` messages: exact numbers on `coverage_assessment`/`coverage_grid`; (iii) frame handling: two sources in different `<ns>/map` frames both credited, one with no transform skipped with the WARN; (iv) stale tide, and a tide boat beyond the distance bound, skip scoring; (v) deactivate->cleanup->configure empties the union.
- B9. Docs: `coverage_assessment/README.md` (design decisions above, the d^2/2R bound and the one-surface assumption, which boat's tide is used and what happens when it is withheld, memory note, rasterisation rule), top-level README Nodes row + parameter table, reset section updated to name the assessor as `--consumer`.
- B10. Sim check before the event: run against unh_marine_simulation's CUBE grids and confirm depth below the surface comes out positive and plausible across all four boats' frames (the spike's answer, now end to end). Full-scope `ci_local.sh`; `git diff -- upstream.repos` must be empty; mark ready only after #406 is on `jazzy`.
```

## Output Format

Write your findings to this exact format so they can be parsed:

### Findings

| # | Severity | File | Line | Finding |
|---|----------|------|------|---------|
| 1 | must-fix / suggestion | `path/to/file` | line number | Description of the issue |

If you find no issues, write:

### Findings

No issues found.

### Summary

Write a 1-3 sentence overall assessment after the findings table.
