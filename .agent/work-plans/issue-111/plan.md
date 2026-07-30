# Plan: Tile-scoped incremental regen via the survey index

## Issue

https://github.com/rolker/cube_bathymetry/issues/111

## Context

Full-campaign batch regen (~1.4 h for ~12M pings, #96/#107) rebuilds every tile
from every bag. The cost is proportional to campaign size, not to new data added.
`marine_survey_index` (uma#259) now maps L14 tiles ↔ bags + per-bag pass intervals,
and L14 keys roll up to L10 store tiles via the GGGS parent hierarchy. That provides
the dirty-tile set and contributing-bag set needed to scope rebuilds.

The issue review and operator checkpoint (#111 progress.md) resolved five open design
points and directed a 2-PR sequence. Both project ADRs are authored here as part of
plan-task (operator decision, 2026-07-30).

## Approach

### PR1 — Survey-index integration: dirty-tile query only (no rebuild path yet)

1. **Add `marine_survey_index` dependency** to `package.xml` and `CMakeLists.txt` in
   the `batch_regen` target only. The index is a new soft dep; absence falls back to
   full regen.

2. **Author ADR-0002** (`docs/decisions/0002-dirty-tile-footprint-math.md`): decide
   that the dirty L10 set = survey-index L14 footprint + one-cell L14 margin →
   L14→L10 GGGS parent rollup. Done in plan-task (see committed ADR file).

3. **Add `survey_index_query.h/cpp`**: `dirtyL10Tiles(db, new_bag_paths, level)`
   — opens the index, queries `queryPasses` for each new bag's L14 footprint (with
   one-cell margin from `tilesForBoundingBox`-expanded bounds), returns the distinct
   set of L10 `gggs::GridIndex` values and, for each, all contributing bag paths +
   pass intervals (`PassRow` vectors). No rebuild; pure query.

4. **Add `--index-db <path>` flag to `batch_regen_main`** (dry-run mode): given new
   bags and an index DB path, prints the dirty L10 tiles and contributing bag set
   (human-readable + machine-parseable JSON) without building anything. Gate: if
   `--index-db` is absent → normal full-regen path unchanged.

5. **Add `test_survey_index_query.cpp`**: in-memory SQLite DB, insert synthetic
   passes spanning known L14 tiles, verify L14→L10 rollup and one-cell margin;
   verify no dirty tiles returned when no new bags.

### PR2 — Tile-scoped rebuild + atomic swap + staleness fingerprint + consumer update

6. **Author ADR-0003** (`docs/decisions/0003-staleness-fingerprint.md`): decide
   `build_fingerprint.json` in the store root (format, schema version, staleness
   rules). Done in plan-task (see committed ADR file).

7. **Add `build_fingerprint.h/cpp`**: JSON read/write for the fingerprint struct
   (nlohmann/json or hand-rolled; whichever is already a dep). `computeBagHash(path)`
   reads the bag's metadata YAML SHA-256 (not the full bag, for speed). Atomic write:
   write to `build_fingerprint.json.tmp`, rename into place.

8. **Add time-window bag seeking** in the `BagReaders` helper inside
   `batch_regen_main.cpp`: accept optional `{t_start_ns, t_end_ns}` per-bag interval
   from the contributing-bag query and use `rosbag2`'s `SeekOptions` (or filter) to
   read only those windows. Full-bag read (current behavior) is the fallback when no
   intervals are provided (full regen path).

9. **Add `--incremental` flag to `batch_regen_main`** (requires `--index-db`):
   - Read/create `build_fingerprint.json`; compare against current inputs.
   - Detect non-bag input changes (reference store, correction mode/curve, cell size,
     tool version) → force full regen if any changed.
   - Detect removed/modified bags → force full regen.
   - Compute new-bag set; query dirty L10 tiles + contributing bags + intervals.
   - Scatter: for each contributing bag, read only its pass intervals.
   - Gather: build only dirty L10 tiles (`BatchRegen::finalize` already processes
     only tiles that have scatter buckets — behavior is correct by construction).
   - Atomic swap: write rebuilt tiles to a temp subdirectory, then
     `std::filesystem::rename` each file into the live store.
   - Rewrite `build_fingerprint.json` on success.
   - Full regen (no `--incremental`) never reads the fingerprint; still writes it on
     success so a subsequent incremental run has a baseline.

10. **Add bit-exact A/B test** (`test_batch_regen.cpp` or new file): index a small
    synthetic campaign, run `--fresh` full regen, run `--incremental` with one new
    bag, verify per-tile output is byte-identical to a second `--fresh` full regen
    over all bags. This must be a required CI test, not advisory.

11. **Verify backscatter correction-mode propagation** through the per-tile rebuild:
    `auto` mode is already rejected in `batch_regen` (ADR-0007 addendum); incremental
    path inherits the same `BackscatterAngleCorrection` enum gating — no extra work
    needed, but add an assertion in the incremental code path.

12. **Update `build_bathy_store.sh`** (`unh_echoboats_project11/scripts/`, #382):
    - Default: `batch_regen --incremental --index-db "$INDEX_DB"` (falls through to
      full regen if index absent or fingerprint absent).
    - Add `--fresh` flag that passes `--no-incremental` (or removes `--incremental`)
      to force full regen; behavior today is preserved for `--fresh` callers.

## Files to Change

| File | Change |
|------|--------|
| `cube_bathymetry/package.xml` | Add `<exec_depend>marine_survey_index</exec_depend>` |
| `cube_bathymetry/CMakeLists.txt` | Link `marine_survey_index` into `batch_regen` target |
| `cube_bathymetry/include/cube_bathymetry/survey_index_query.h` | New: `dirtyL10Tiles()` |
| `cube_bathymetry/src/survey_index_query.cpp` | New: implementation |
| `cube_bathymetry/src/batch_regen_main.cpp` | `--index-db` (PR1), `--incremental` + BagReaders seek + atomic swap (PR2) |
| `cube_bathymetry/include/cube_bathymetry/build_fingerprint.h` | New: fingerprint struct + I/O (PR2) |
| `cube_bathymetry/src/build_fingerprint.cpp` | New: implementation (PR2) |
| `cube_bathymetry/test/test_survey_index_query.cpp` | New: query unit tests (PR1) |
| `cube_bathymetry/test/test_batch_regen.cpp` | Add bit-exact A/B test (PR2) |
| `cube_bathymetry/docs/decisions/0002-dirty-tile-footprint-math.md` | New ADR (plan-task) |
| `cube_bathymetry/docs/decisions/0003-staleness-fingerprint.md` | New ADR (plan-task) |
| `unh_echoboats_project11/scripts/build_bathy_store.sh` | Incremental default + `--fresh` (PR2) |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Human control and transparency | `--fresh` always available; absent index ⇒ full regen; fingerprint mismatch ⇒ full regen; dry-run `--index-db` mode |
| Enforcement over documentation | Bit-exact A/B test is required CI; ADRs for both design decisions |
| Capture decisions, not just implementations | ADR-0002 (footprint math) and ADR-0003 (fingerprint) committed here |
| A change includes its consequences | `build_bathy_store.sh` (#382) in scope; backscatter propagation verified |
| Only what's needed | Survey index already merged; no new tooling; reuses BatchRegen scatter-gather |
| Improve incrementally | 2-PR sequence; full regen stays live until PR2 is verified |
| Test what breaks | Bit-exact A/B test required; in-memory SQLite test for query rollup |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| Workspace ADR-0001 (Adopt ADRs) | Yes | ADR-0002 and ADR-0003 authored in plan-task |
| cube_bathymetry ADR-0001 (tile eviction / lossless I/O) | Yes | Atomic swap uses `std::filesystem::rename`; no partial-write risk |
| cube_bathymetry ADR-0007 (backscatter store) | Yes | `auto` mode rejection inherited; correction-mode propagation asserted |
| Workspace ADR-0013 (progress.md) | OK | This entry |

## Consequences

| If we change... | Also update... | Included? |
|---|---|---|
| `batch_regen_main` CLI flags | `build_bathy_store.sh` `#382` | Yes — PR2 |
| Staleness fingerprint format | ADR-0003 schema_version bump on next change | ADR-0003 records the rule |
| `survey_index.db` absent | Fall back to full regen (explicit, logged) | Yes — step 4/9 |
| ADR-0007 full document deferred | Incremental path does not unblock it | Noted; separate follow-up |

## Open Questions

- [ ] No open questions — operator checkpoint accepted all review actions; both ADRs authored; plan is review-plan-ready.

## Estimated Scope

Two PRs under this issue: PR1 (dirty-tile query + dry-run, cube_bathymetry only),
PR2 (incremental rebuild + fingerprint + consumer update, touches cube_bathymetry +
unh_echoboats_project11). Each PR is independently reviewable; full regen remains
the live path until PR2 lands.
