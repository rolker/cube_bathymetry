---
issue: 133
---

# Issue #133 — cube_bathymetry: retarget writers from SourceLayer::Survey to Draft/Processed

## Issue Review
**Status**: complete
**When**: 2026-08-20 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #133
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Lockstep breaking change: retarget three `cube_bathymetry` writers from the
retired `SourceLayer::Survey` enum value to the new `Draft` (live CUBE node)
and `Processed` (`store_import`, `batch_regen`) values introduced in
`rolker/unh_marine_autonomy#308`. The store-side split is **done and published**
(uma#313, head 1707ea2). This PR cannot build or merge independently — it
must be co-landed with the store.

**Affected source files:**
- `cube_bathymetry_node.cpp` — 6 `Survey` references (lines 327, 330, 351,
  1027, 1240, 1287); all → `Draft`. Lines 327–351 are the write path; lines
  1027, 1240, 1287 are read-back / scratch-tile walks that follow the rename.
- `store_import.cpp` — 4 `Survey` references (lines 258, 458, 490, 592, 815)
  → `Processed`; also picks up the per-cell draft-clearing call that lands
  store-side.
- `batch_regen.cpp` — 1 `Survey` reference (line 258) → `Processed`.

**Affected test files (must update in same PR):**
- `test_store_import.cpp` (lines 245, 249, 279 use `Survey`)
- `test_batch_regen.cpp` (line 139 uses `marine_bathymetry_store::SourceLayer::Survey`)
- `test_anti_entropy_disk_serve.cpp` (lines 98, 170, 210, 270)
- `test_persistence.cpp` (lines 79, 115, 144, 202, 231, 294)
- `test_tile_eviction_rss.cpp` (line 126)
- `test_import_eviction.cpp` (line 127, 382)

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Human control and transparency | OK | Draft vs Processed semantics well-explained in issue; operator coverage view rationale clear |
| Enforcement over documentation | OK | Build-time enforcement: compile break against renamed enum prevents partial merges |
| Capture decisions, not just implementations | OK | Design rationale captured in issue and referenced ADR-0010 D8 |
| A change includes its consequences | Watch | Test files referencing `Survey` (enumerated above) must update in same PR; issue body doesn't enumerate them explicitly |
| Only what's needed | OK | Tightly scoped to enum substitution |
| Improve incrementally | OK | Correctly broken out from the store-side split |
| Test what breaks | Watch | Test files listed above cover the write/read paths — confirm each is updated to use `Draft` or `Processed` as appropriate for the operation being tested |
| Workspace vs. project separation | OK | Change is correctly in the project repo |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0001 — Adopt ADRs | No | Decision rationale already captured in ADR-0010 D8 |
| ADR-0002 — Worktree isolation | Yes | Satisfied — `feature/issue-133` worktree exists |
| ADR-0008 — ROS 2 conventions | Yes | Package modification; no deviations from ROS 2 conventions expected |
| ADR-0013 — progress.md vocabulary | Yes | This entry |

### Consequences

- **Test files**: All `SourceLayer::Survey` references in test files must be updated in the same PR (see list above). Tests for the live-node path → `Draft`; tests for import/batch-regen paths → `Processed`.
- **Backscatter store**: `marine_mbes_backscatter_store::SourceLayer::Survey` appears in `test_batch_regen.cpp` (line 170) and `test_import_eviction.cpp` (line 171). If the backscatter store also underwent a similar split in uma#308, those references need updating here too. Verify against uma#313 and clarify scope.
- **Build verification**: The worktree's core underlay is `main-tree jazzy` (pre-split). The plan must address how to verify the build against the feature/issue-308 store (uma#313) — options: temporarily point the core underlay at the issue-308 built `core_ws`, or defer full build verification to a combined host-side check.

### Recommendations

- Clarify whether `marine_mbes_backscatter_store::SourceLayer::Survey` in the test files is in scope — if the backscatter store also split in uma#308, it should be handled here.
- The plan should include an explicit build-verification strategy against the feature/issue-308 store before marking the PR ready.
- Enumerate the scratch-tile-walk read sites in `cube_bathymetry_node.cpp` (lines 1027, 1240, 1287) explicitly in the plan so they're not missed as "just read sites."

### Actions
- [ ] Update all test files that reference `SourceLayer::Survey` in the same PR (see list in Consequences)
- [ ] Confirm backscatter store `SourceLayer::Survey` scope — update or explicitly exclude in plan
- [ ] Plan must include build-verification strategy against the uma#313 / feature/issue-308 store
- [ ] Include scratch-tile-walk read sites (cube_bathymetry_node.cpp lines 1027, 1240, 1287) explicitly in the implementation plan
