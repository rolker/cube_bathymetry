# Plan: Anti-entropy phase 2: catalog + serving from disk

## Issue

https://github.com/rolker/cube_bathymetry/issues/106

## Context

The live-coverage anti-entropy protocol (cube ADR-0001 D4 / uma ADR-0008)
has two halves.  The first half (CAMP-side reconcile, cube#78) is built.
The second half — the boat must advertise **all** servable tiles and serve
**any** requested tile — is not.  Two gaps remain in
`cube_bathymetry_node.cpp`:

1. **`publishCatalog()` (L699–726)** iterates `geo_map_sheet_->grids()`
   (RAM-resident only), so evicted tiles drop out of the catalog even though
   their versions are still in `catalog_builder_`.
2. **Startup prime ordering (L257–279)**: `trimResidentToBudget()` runs
   before the catalog-seeding loop, so tiles evicted at startup are never
   seeded into `catalog_builder_`, and restart advertises only the
   post-trim resident window.
3. **`tileRequestCallback()` (L728–762)** serves resident tiles only; a
   request for an evicted tile is silently dropped.  `reloadEvictedTile()`
   cannot be reused here — it inserts into the resident set (LRU churn).

Consequence: once a survey exceeds `max_resident_tiles`, a cold or
restarted CAMP can never catch up on evicted coverage, and CAMP's
gen-time-gated prune deletes those tiles — the operator view erodes toward
the boat's resident window.  This is the standing prime suspect for the
2026-07-21 stuck-tiles symptom (#104).

## Approach

### Step 1 — Fix startup catalog prime (L257–279)

Seed `catalog_builder_` from the **full** `draft_tiles` map (all on-disk
tiles) before calling `trimResidentToBudget()`, rather than from
`geo_map_sheet_->grids()` after the trim.

```cpp
// replace the post-trim grid loop with:
const std::int64_t prime_version = now().nanoseconds();
for (const auto & [idx, tile] : draft_tiles) {
  catalog_builder_.update(idx, prime_version);
}
trimResidentToBudget();
// (grid loop removed — catalog already covers all store tiles)
```

This ensures a node restart advertises the full on-disk set immediately,
not just the tiles that fit within the resident budget.

### Step 2 — Fix `publishCatalog()` to use `catalog_builder_`

Replace the resident-grid iteration with `catalog_builder_.buildCatalog()`.
`TileCatalogBuilder::update()` is already called at eviction time (L690)
and never calls `remove()` for evicted tiles, so evicted versions are
already in the registry — the catalog just isn't publishing them.

```cpp
void publishCatalog() {
  const rclcpp::Time gen = now();
  marine_interfaces::msg::TileCatalog msg;
  msg.header.stamp = gen;
  msg.header.frame_id = "gggs";
  const auto cat = catalog_builder_.buildCatalog(gen.nanoseconds());
  for (const auto & e : cat.entries) {
    marine_interfaces::msg::TileCatalogEntry entry;
    entry.index.level = e.index.level();
    entry.index.row   = e.index.row();
    entry.index.col   = e.index.column();
    entry.version.sec    = static_cast<int32_t>(e.version / 1'000'000'000LL);
    entry.version.nanosec = static_cast<uint32_t>(e.version % 1'000'000'000LL);
    msg.entries.push_back(entry);
  }
  tile_catalog_publisher_->publish(msg);
}
```

Update the in-code comment at L706–711 (it describes D4-completeness
against the resident-only view; after this change the catalog is complete
against the full store, so the comment is wrong and misleading).

### Step 3 — Disk-serve drain queue

Add a **bounded, rate-limited drain queue** so a cold CAMP's burst of
TileRequests trickles out on spare bandwidth rather than flooding the
best-effort topic.

**New members** (plan-review S4: interval member included):
```cpp
std::deque<gggs::GridIndex>  disk_serve_queue_;
std::set<gggs::GridIndex>    disk_serve_queued_;  // dedup companion
rclcpp::TimerBase::SharedPtr disk_serve_timer_;
std::size_t disk_serve_tiles_per_tick_{4};
std::size_t disk_serve_queue_max_depth_{64};
double      disk_serve_interval_{0.5};  // s, drain timer period
```

**Why a separate timer, not the catalog/maintenance tick** (plan-review S2):
the drain cadence is a bandwidth-pacing knob — `disk_serve_interval` ×
`disk_serve_tiles_per_tick` is the catch-up rate an operator tunes against
the link budget (bridge rate limit is 2 tiles/s). Coupling it to
`catalog_interval` (5 s) would tie catch-up throughput to the catalog
cadence: 4 tiles per 5 s = 0.8 tiles/s ceiling, and any catalog retune
would silently change catch-up rate. Independent rate control justifies
the extra timer; the node's timer count stays small (catalog, save,
drain).

**New ROS parameters** (declared in `on_configure`, with
`declare_parameter` + descriptor):
- `disk_serve_tiles_per_tick` (default 4) — tiles drained per tick
- `disk_serve_queue_max_depth` (default 64) — max pending entries; excess
  requests are dropped (WARN once per overflow) so a malicious or buggy
  consumer cannot OOM the boat.
- `disk_serve_interval` (default 0.5 s) — drain timer period; reuses
  `disk_serve_timer_`.

**New method `drainDiskServeQueue()`:**
```
for up to disk_serve_tiles_per_tick_ entries from disk_serve_queue_:
  pop front, remove from disk_serve_queued_
  if tile is now resident (no longer in evicted_indices_):
    serve from geo_map_sheet_ (quantizeTile), publish, continue
  scratch_store = BathymetryStore::fromCellSize(cell_size_)
  loadWindow(scratch_store, draft_dir_, sw, ne, nullptr)
  scratch_sheet = GeoMapSheet(cell_size_)
  primeFromTile(tile, scratch_sheet)     // settled depth only
  grid = scratch_sheet.grids() for this index
  if grid && quantizeTile(*grid, stamp):
    sonar_tile_publisher_->publish(vt)
  // scratch_sheet/store dropped here — NOT inserted into geo_map_sheet_
```

**Lifecycle wiring** (same pattern as `catalog_timer_`):
- Create `disk_serve_timer_` in `on_activate()`, only when `!draft_dir_.empty()`
- Cancel + reset in `on_deactivate()` and `on_cleanup()`
- `tileRequestCallback()` already gates on ACTIVE state, so requests only
  arrive while the node is active.

### Step 4 — Update `tileRequestCallback()`

After the existing resident-serve loop, for each unserved tile index:

```cpp
if (evicted_indices_.count(ti_idx) &&
    disk_serve_queue_.size() < disk_serve_queue_max_depth_ &&
    !disk_serve_queued_.count(ti_idx))
{
  disk_serve_queue_.push_back(ti_idx);
  disk_serve_queued_.insert(ti_idx);
}
```

Update the comment ("from-disk catch-up is a follow-up") and the debug log
to reflect that evicted tiles are now queued rather than silently dropped.

### Step 5 — ADR-0001 addendum

Add a project ADR-0001 addendum (§ Anti-entropy phase 2 — disk-serve,
#106) documenting:
- The "catalog reflects the full store, not RAM" invariant (step 1+2).
- Why `reloadEvictedTile()` is NOT reused for the disk-serve path:
  it inserts into `geo_map_sheet_` (LRU churn); the catch-up path must
  use a scratch sheet and discard it.
- The drain-queue contract: rate-limited, bounded, dedup'd.

### Step 6 — Integration test (`test_anti_entropy_disk_serve.cpp`)

Pure library test (no ROS node spin).  Tests the complete loop:

**Known limitation (plan-review S1, acknowledged):** the changed node
methods (`publishCatalog`, `tileRequestCallback`, the startup prime, the
drain queue) are private to `cube_bathymetry_node.cpp` with no test
harness, so this test guards the *invariants* via the same library
primitives the node composes — it cannot catch a regression in the node
wiring itself (e.g. re-introducing a `grids()`-based catalog). A
launch_testing assertion on the real node path is out of proportion for
this PR; the limitation is stated in the test's header comment, and the
node-wiring acceptance is the post-merge field validation (below) plus
review. Revisit if the node grows a testable seam.

1. **Populate**: insert soundings into `GeoMapSheet` for N > `max_resident`
   tiles; save all (via the same `saveDirtyTiles` helper pattern as
   `test_persistence.cpp`).
2. **Trim**: replicate the trim via the library primitives the node uses —
   `GeoMapSheet::coldTiles(max=2)` + `dropTile()` (plan-review S4:
   `trimResidentToBudget()` is node-private and not callable here).
3. **Catalog prime (fix 1+2)**: seed `catalog_builder_` from full
   `draft_tiles` (before trim in production, replicated here).  Verify
   `catalog_builder_.size() == N` (all tiles, not just residents).
4. **Reconcile**: build `TileCatalog`; run
   `TileCatalogReconciler::reconcile()` against an empty consumer.  Verify
   `to_request` contains all N evicted tile indices.
5. **Disk serve (no LRU churn)**: for each requested index:
   - Record resident count before.
   - `loadWindow` → scratch store; `primeFromTile` → scratch sheet; get
     `GeoGrid*`; verify it has finite cells.
   - Assert resident count of main sheet is **unchanged** (no LRU churn).
6. **Consumer convergence**: mark all requested tiles as `markHave()` in
   the reconciler; reconcile again; verify `to_request` is empty.

### Step 7 — Post-merge field validation (plan-review S3; tracked under #104, not this PR)

The hermetic test cannot validate against the field symptom. After merge
and the gabby rebuild:
1. **Bag-replay check**: replay the 2026-07-21 gabby sonar sessions
   through the fixed node; verify the catalog advertises the full store
   (count vs draft dir) across eviction.
2. **Cold-CAMP catch-up**: against a store populated past
   `max_resident_tiles`, enable a fresh CAMP source (empty cache) and
   verify it converges to the full tile set (no erosion to the resident
   window).
Record results on #104; that issue stays open until this validation
passes.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/src/cube_bathymetry_node.cpp` | Steps 1–4: fix startup prime, `publishCatalog()`, drain queue, `tileRequestCallback()` |
| `cube_bathymetry/docs/decisions/0001-tile-eviction-and-incremental-publish.md` | Step 5: ADR addendum |
| `cube_bathymetry/test/test_anti_entropy_disk_serve.cpp` | Step 6: new integration test |
| `cube_bathymetry/CMakeLists.txt` | Register `test_anti_entropy_disk_serve` |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | New params (`disk_serve_tiles_per_tick`, `disk_serve_queue_max_depth`, `disk_serve_interval`) are configurable with explicit defaults; no-store fallback preserved (drain only starts if `draft_dir` is set). |
| Enforcement over documentation | Step 6 test is hermetic and machine-enforced; the "no LRU churn" invariant is asserted, not just commented. |
| Capture decisions, not just implementations | Step 5 ADR addendum documents *why* `reloadEvictedTile()` is not reused and what the read-only scratch path buys. |
| A change includes its consequences | In-code comment at L706–711 updated in the same PR (it would be wrong after step 2). Debug log in `tileRequestCallback()` updated (the "follow-up" wording becomes stale). |
| Only what's needed | No new abstraction beyond what the issue requires; drain queue reuses existing `BathymetryStore` / `GeoMapSheet` primitives. |
| Improve incrementally | Single focused PR; no behavior change when `draft_dir` is unset. |
| Test what breaks | Test covers: catalog completeness, disk-serve with no LRU churn, consumer convergence — the three failure modes that caused field erosion. |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| cube ADR-0001 (eviction + incremental publish) | Yes — semantics change | In-code comment (L706–711) corrected; addendum added in step 5 |
| uma ADR-0008 D4 (anti-entropy) | Yes — core requirement | Steps 1–4 close D4 completeness; catalog includes full store |
| workspace ADR-0008 (ROS 2 conventions) | Yes | New params use `snake_case`, `declare_parameter` with defaults and descriptors |
| workspace ADR-0002 (worktree isolation) | OK | Already in the correct worktree |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `publishCatalog()` comment at L706–711 | Updated in step 2 | Yes |
| `tileRequestCallback()` "follow-up" comment + debug log | Updated in step 4 | Yes |
| Startup catalog prime order | `catalog_builder_` seeded before trim | Yes |
| `disk_serve_timer_` lifecycle | `on_activate` / `on_deactivate` / `on_cleanup` | Yes |
| CAMP (#121), udp_bridge (#19) | No code changes — transparent via same interface | N/A |

## Open Questions

- [ ] No open questions — plan is review-plan-ready.

## Estimated Scope

Single PR.
