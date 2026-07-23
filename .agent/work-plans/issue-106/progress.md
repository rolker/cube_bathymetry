---
issue: 106
---

# Issue #106 — Anti-entropy phase 2: catalog + serving from disk

## Issue Review
**Status**: complete
**When**: 2026-07-23 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #106
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue #106 closes the second half of the anti-entropy protocol (uma ADR-0008 D4):
the catalog must be a **complete** snapshot of all servable tiles (not just
RAM-resident), and the TileRequest path must be able to serve evicted tiles from
disk without LRU churn. The 4 tasks are cohesive and can fit a single PR. All live
in `cube_bathymetry` (project repo, correct placement).

**Key code context verified:**
- `publishCatalog()` (L699–726): builds catalog from `geo_map_sheet_->grids()` —
  evicted tiles absent, breaking D4 completeness.
- `tileRequestCallback()` (L728–762): serves resident tiles only; "from-disk
  catch-up is a follow-up" (the present issue).
- `reloadEvictedTile()` (L876): exists but inserts into resident set — cannot be
  used as the task-2 read-only serve path.
- Startup prime ordering bug (L257–278): `trimResidentToBudget()` runs BEFORE the
  catalog-seeding loop, so tiles evicted at startup never enter `catalog_builder_`.

### Actions
- [ ] Update in-code comment at L706–711 (the "D4-complete against the
  resident-serving TileRequest path / evicted tiles drop out" rationale) — it
  will be wrong after task 1 and must be updated in the same PR to keep
  code-comment intent in sync with the new semantics.
- [ ] Add an ADR addendum (to project ADR-0001 or a new standalone note) capturing
  the "disk-serving is read-only load → quantize → publish → drop" invariant and
  why `reloadEvictedTile()` is NOT reused for this path (it inserts into the
  resident set; the catch-up path must not churn LRU).
- [ ] Fix the startup-prime ordering: seed the catalog from ALL store tiles (before
  OR after trimming, covering both resident and just-evicted ones) so a restart
  advertises the full on-disk set. Verify that tiles evicted by
  `trimResidentToBudget()` at startup are still tracked in `catalog_builder_`.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Burst-bounding params configurable; no-store fallback explicitly preserved; operator can tune `tiles_per_tick` / queue depth. |
| Enforcement over documentation | OK | Task 4 requires hermetic integration test; acceptance criteria are concrete. |
| Capture decisions, not just implementations | Action needed | The "disk-serving is read-only, no LRU churn" invariant is a non-obvious design choice; code comments and ADR need updating alongside the implementation. |
| A change includes its consequences | Watch | Startup-prime ordering bug (catalog seeded after trim) is a pre-existing gap this issue must close; the acceptance criteria don't call it out explicitly. |
| Only what's needed | OK | Drain queue justified by concrete 2/s rate-limit constraint; scope is contained. |
| Improve incrementally | OK | Focused on one protocol gap; existing behavior (no `draft_dir`) unchanged. |
| Test what breaks | OK | Task 4 specifies end-to-end loop: populate past cap → evict → cold consumer → convergence check (catalog completeness + disk serving + no LRU churn). |
| Workspace vs. project separation | OK | All changes in `cube_bathymetry` project repo. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| uma ADR-0008 D4 (anti-entropy) | Yes — core requirement | This issue directly implements D4 completeness; ensure implementation notes the from-disk path satisfies D4. |
| cube ADR-0001 (eviction + incremental publish) | Yes — semantics change | The D4 note in code (L709) and ADR text will be wrong post-implementation; an addendum is needed. |
| workspace ADR-0008 (ROS 2 conventions) | Yes | New ROS parameters for drain queue must use `snake_case`, `declare_parameter` with defaults; follows existing pattern. |
| workspace ADR-0002 (worktree isolation) | OK | Already in the correct worktree. |

### Consequences

- `publishCatalog()` comment at L706–711 must be updated (will be wrong after task 1).
- `tileRequestCallback()` debug log at L759 must be updated ("from-disk catch-up is
  a follow-up" becomes the active path).
- Startup-prime catalog-seeding loop (L275–278) must include evicted tiles or move
  before the trim so the full on-disk set is advertised.
- Project ADR-0001 should receive an addendum documenting the new serving semantics
  (or a new project ADR-0002 if the scope warrants it).
- CAMP (#121) and udp_bridge (#19) are downstream consumers of this protocol but
  require no code changes here — the change is transparent to them via the same
  `TileCatalog` / `TileRequest` / `SonarVisualizationTile` interface.

### Recommendations

- During `tileRequestCallback()` disk-serve, acquire a scratch
  `marine_bathymetry_store::loadWindow()` (the same primitive `reloadEvictedTile`
  uses) but do NOT call `evicted_indices_.erase()` or insert into `geo_map_sheet_` —
  comment this constraint explicitly so a future refactor doesn't accidentally merge
  the two paths.
- The drain queue (task 3) should use an existing `rclcpp::TimerBase` tick already
  in the maintenance cycle rather than a new timer if timing requirements allow, to
  keep the timer count bounded.
- Bag-replay acceptance check should specifically confirm the 2026-07-21 Gabby
  sonar sessions no longer show the stuck-tile symptom described in #104.

## Plan Authored
**Status**: complete
**When**: 2026-07-23 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-106/plan.md` at `04620fd`
**Branch**: feature/issue-106 at `04620fd`
**Phases**: single

### Open questions
- [ ] No open questions — plan is review-plan-ready.

## Plan Review
**Status**: complete
**When**: 2026-07-23 14:13 +00:00
**By**: Claude Code Agent (Claude Opus)

**Plan**: `.agent/work-plans/issue-106/plan.md` at `04620fd`
**PR**: PR-less (`--issue 106`, layer worktree)
**Verdict**: approve-with-suggestions

<!-- Independence: Plan Authored is stamped "Claude Code Agent (Claude Sonnet)";
     this review is "Claude Code Agent (Claude Opus)" from a separate fresh-context
     dispatch. The name portion collides (shared bot identity) but the model and
     context differ, so this is a genuinely independent review, not an author
     self-review — no self-review annotation applied. -->

Plan is technically sound and its load-bearing code claims were verified against
source: `trimResidentToBudget()` drops via `dropTile()` and never calls
`catalog_builder_.remove()` (so evicted versions persist in the builder — the
premise of Steps 1–2 holds); `TileCatalogBuilder::buildCatalog(generation_time)`,
`TileCatalogEntry{index, version}`, and the `TileCatalogReconciler`
`reconcile`/`markHave`/`to_request` API used by Steps 2 and 6 all exist as the
plan describes; `reloadEvictedTile()` confirms the `fromCellSize` → `loadWindow`
→ `primeFromTile` scratch-serve primitive Step 3 reuses. Scope fits one PR;
review-issue's three actions (comment L706–711, ADR addendum, startup-prime
reorder) are all addressed. Suggestions below are non-blocking.

### Findings
- [x] (suggestion) Step 6 test guards the *invariant* via library primitives, not
  the *changed node methods*. `CubeBathymetry::publishCatalog()`,
  `tileRequestCallback()`, `trimResidentToBudget()`, the startup prime, and the new
  drain queue are all `private` in `cube_bathymetry_node.cpp` (after `private:` at
  L423) with no header — unreachable from tests (`test_node.cpp` includes a
  different `node.h`). A pure-library replica test can pass while the node wiring
  regresses (e.g. someone re-introduces the `grids()`-based catalog). Acknowledge
  this limitation in the plan/test, or add a launch_testing assertion on the real
  node path. — `plan.md` Step 6 (L157–178)
- [x] (suggestion) Step 3 adds a new `disk_serve_timer_`, but review-issue
  explicitly recommended reusing an existing maintenance-cycle tick "if timing
  requirements allow" to keep the timer count bounded. The plan neither adopts nor
  justifies the divergence — add a one-line rationale (independent rate control)
  or reuse the catalog/maintenance tick. — `plan.md` Step 3 (L124–128)
- [x] (suggestion) review-issue recommended a bag-replay acceptance check against
  the 2026-07-21 Gabby sessions (#104 stuck-tile symptom). The hermetic test does
  not cover field validation and the plan omits any manual acceptance step — note
  it as post-merge validation or explicitly defer. — `plan.md` Step 6
- [x] (suggestion, minor) Precision: Step 3's "New members" block omits an
  interval member though `disk_serve_interval` is declared in prose (L106), and
  Step 6 says "call `trimResidentToBudget(max=2)`" — a node-private method the
  library test cannot call; it must replicate the trim via
  `GeoMapSheet::coldTiles(max)` / `dropTile()`. Align the member list and the test
  wording. — `plan.md` L90–106, L170–172

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-23 14:50 +00:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: approved

**Branch**: feature/issue-106 at `f549027`
**Mode**: pre-push
**Depth**: Deep (reason: ~500 LoC touching concurrency/timers, lifecycle, anti-entropy protocol semantics, ADR addendum)
**Must-fix**: 0 | **Suggestions**: 5
**Round**: 1 | **Ship**: recommended — no must-fix; concurrency/lifecycle/security verified sound, 27/27 tests + all linters pass; suggestions non-blocking

### Findings
- [ ] (suggestion) Restart re-stamps all persisted tiles with fresh `prime_version=now()` → warm consumer re-requests the whole store each reboot (broadened blast radius); persist versions or document the trade — `cube_bathymetry_node.cpp:271`
- [ ] (suggestion) Drain per-tick budget counts published tiles, not disk loads; empty/absent tiles let one tick issue up to queue-depth `loadWindow` reads — pace by attempts — `cube_bathymetry_node.cpp:882`
- [ ] (suggestion) Disk-serve I/O shares the single executor thread with sounding ingest and has no result cache; consider a served-tile LRU if field profiling shows starvation — `cube_bathymetry_node.cpp:882`
- [ ] (suggestion) Test "no LRU churn" guard is near-trivially true — disk-serve loop never touches `sheet`, so it documents intent but doesn't guard the node's live-sheet behavior — `test_anti_entropy_disk_serve.cpp:224`
- [ ] (suggestion) `on_configure` resets `evicted_indices_`/`catalog_builder_` but not `disk_serve_queue_`/`disk_serve_queued_` (defense-in-depth; deactivate+cleanup already cover normal flow) — `cube_bathymetry_node.cpp:103`
