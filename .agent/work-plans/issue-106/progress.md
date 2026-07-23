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
