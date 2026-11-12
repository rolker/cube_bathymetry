# ADR-0003: Build fingerprint for incremental regen staleness detection

## Status

Accepted.

## Context

Incremental regen (cube_bathymetry#111) rebuilds only the dirty tiles introduced by
new bags. But a changed reference layer, backscatter curve, cell size, or tool version
also invalidates previously-built tiles that new bags did not touch. Without a
persistent record of what inputs the current store was built from, any of these changes
would silently produce a mixed store (some tiles from the new build, some from the old).

The store already writes `registry.json` (uma#248 `StoreMetadata`) for provenance, but
that records descriptive fields (platform, sensor, campaign), not the exact input set
needed to detect staleness.

## Decision

Maintain a JSON sidecar file `build_fingerprint.json` in the store root directory
alongside `registry.json`. On every successful `batch_regen` run (full or incremental)
it is atomically written (temp file + rename) with the following schema:

```json
{
  "schema_version": 1,
  "tool_version": "<package.xml <version>>",
  "cell_size_m": 0.25,
  "bags": [
    {"path": "/abs/path/to/bag", "sha256": "<hex>"}
  ],
  "reference_store": {"path": "/abs/path/to/ref", "metadata_sha256": "<hex>"},
  "backscatter_correction": {"mode": "empirical", "curve_sha256": "<hex>"}
}
```

Fields:

- **`schema_version`** (int): bumped on any incompatible schema change; a mismatch
  forces a full regen (same "regenerate is the migration" rule as the survey index).
- **`tool_version`** (string): the value of `<version>` in `package.xml`, so a code
  change that updates the version automatically invalidates incremental baselines.
- **`cell_size_m`** (float): the `--resolution` value used; a different resolution
  invalidates the store entirely.
- **`bags`** (array): one entry per bag, in the order they were supplied, each with
  the absolute path and a SHA-256 of the bag's metadata YAML file (not the full bag
  data, for speed). Hashing the metadata YAML captures path + start time + duration +
  topic list, which changes whenever the bag is modified or replaced.
- **`reference_store`** (object or null): the path and SHA-256 of the reference
  store's `registry.json` if `--reference-store` was given; null otherwise.
- **`backscatter_correction`** (object or null): mode + SHA-256 of the explicit
  curve CSV if `--backscatter-correction empirical --backscatter-curve-file <f>` was
  given; `{"mode": "none", "curve_sha256": null}` for no correction. `auto` mode is
  rejected by `batch_regen` (ADR-0007 addendum) so it never appears here.

### Staleness rules (evaluated at incremental run start)

| Condition | Action |
|---|---|
| `build_fingerprint.json` absent or unreadable | Full regen (write fingerprint on success) |
| `schema_version` mismatch | Full regen |
| `tool_version`, `cell_size_m`, `reference_store`, or `backscatter_correction` changed | Full regen |
| Any bag in the fingerprint has a different `sha256` or is absent from the current list | Full regen |
| Current bag list is a superset (new bags appended) | Incremental (dirty tiles from new bags only) |
| Current bag list == fingerprint bag list (no change) | No-op (log and exit) |

Ordering: if the current bag list contains bags that appear after the fingerprint's
last entry (i.e., strictly new trailing bags), and all existing bags match their
hashes, it is an incremental run. Any reordering or gap triggers a full regen.

### Atomic write

```
write build_fingerprint.json.tmp
fsync
rename build_fingerprint.json.tmp -> build_fingerprint.json
```

A partial write leaves `.tmp` in place and does not corrupt the previous fingerprint.
On the next run, `.tmp` is silently removed and the (possibly stale) fingerprint is
re-evaluated. An interrupted full regen leaves no fingerprint (the old one was not
overwritten until success) — correct: the next run cannot safely do an incremental
from an incomplete store.

## Rationale

Hashing the metadata YAML rather than the full bag body is a deliberate trade-off:
it is O(milliseconds) per bag rather than O(minutes for a large bag), and metadata
changes whenever the bag changes in any way that would affect replay (start time,
topics, duration). A bag whose data changes without a metadata update (unlikely with
standard ROS 2 bag writers) would not be detected — acceptable given that replacing
bag content without updating metadata is not a normal operation.

`tool_version` as the staleness key rather than a code hash avoids rebuilding on
every development commit; version bumps are the intended signal for "outputs may differ".

## Consequences

- `build_fingerprint.h/cpp` implements the struct, JSON I/O, hash computation, and
  staleness evaluation. It has no dependency on ROS 2 or SQLite.
- `batch_regen_main.cpp` reads the fingerprint before opening any bags (early exit
  if no-op) and writes it atomically after `finalize()` returns.
- The fingerprint is a regenerable sidecar — it must never be committed to the bag
  repository or the store's git history. `.gitignore` entries should be added where
  stores are checked in.
- If `unh_echoboats_project11/scripts/build_bathy_store.sh` is run with `--fresh`,
  it passes `--no-incremental` (or omits `--incremental`) so `batch_regen` skips
  the fingerprint check and does a clean full rebuild, then writes a fresh fingerprint.

## Amendment 2026-09-14 — schema version 2: `tiling` replaces `cell_size_m` ([cube_bathymetry#143](https://github.com/rolker/cube_bathymetry/issues/143))

A store may now hold native tiles at several GGGS levels
([ADR-0002 amendment](0002-dirty-tile-footprint-math.md)), which a scalar
`cell_size_m` cannot describe. Schema version 2 replaces it with a `tiling`
object; `schema_version` is bumped so any version-1 file reads as stale (the
"regenerate is the migration" rule above). No migration code is needed: no
version-1 file was ever written — this amendment lands **with the first
implementation** of the fingerprint.

```json
{
  "schema_version": 2,
  "tiling": {
    "mode": "fixed" | "depth_adaptive",
    "cell_size_m": 0.906,
    "iho_order": "order1a",
    "policy": {
      "capture_distance_scale": 0.05,
      "capture_spacing_scale": 0.71,
      "depth_adaptive_scale": 0.05,
      "coarsest_level": 8,
      "finest_level": 14,
      "count_level": 14,
      "min_obs_per_node": 5,
      "blunder_allowance": 0.2,
      "decision_depth_percentile": 0.02,
      "achieved_percentile": 0.95
    },
    "levels_used": [8, 9, 10]
  }
}
```

- **`mode`**: `fixed` (one sheet at `cell_size_m`, the requested `-r`) or
  `depth_adaptive` (a level plan).
- **`policy`** and **`iho_order`** are written in **both** modes: the capture
  gate (ADR-0002 amendment, decision 4) and the IHO order's error model change
  fixed-level output too, so a fixed store's fingerprint must carry them to be
  told stale when either changes — the job `cell_size_m` did alone in version 1.
  The depth-adaptive-only keys are `null` in fixed mode.
- **The depth-adaptive keys are every input that decides the plan**, not only
  the level bounds: the uma#369 ladder scale (`--depth-adaptive-scale`,
  recorded as `depth_adaptive_scale` — distinct from the capture gate's
  `capture_distance_scale`, which shares a field name in `LevelPlanPolicy`) and
  the two percentiles (`--decision-depth-percentile`, `--achieved-percentile`)
  move tiles between levels exactly as `coarsest_level`/`finest_level` do. A
  fingerprint that omitted them would call a differently-tiled store fresh.
- **`capture_distance_scale` has no CLI setter today** — it is a compile-time
  default — so that one key is inert until one exists. It is recorded anyway:
  the day it becomes settable, stores built before the change are correctly
  told stale, and a written-but-inert key costs nothing.
- **`levels_used`**: every level holding native tiles from this build.
  Informational; it never makes a store stale.
- **No level-plan hash.** Overlapping native levels are the normal state
  (ADR-0002 amendment, decision 6), so a later import at other levels is not a
  staleness event.

### Staleness rules, amended

| Condition | Action |
|---|---|
| `schema_version` != 2 | Full regen |
| `tiling.mode` changed | Full regen |
| fixed mode: `cell_size_m` changed | Full regen |
| `iho_order` changed | Full regen |
| any `policy` field changed (the depth-adaptive keys only in that mode) | Full regen |
| `levels_used` changed | No effect |

### Implementation status

`build_fingerprint.h/cpp` implements read/write (atomic temp-file + rename, as
specified) and `isStale` over the `tiling` object and the
`backscatter_correction` object; `import_bag` writes the file after every
successful import (fixed and depth-adaptive). The other keys (`tool_version`,
`bags`, `reference_store`) and `batch_regen --incremental`'s consumer remain
unimplemented; adding them is additive within schema 2.

### `backscatter_correction` (cube_bathymetry#172)

Written beside `tiling`, always, by `import_bag`:

```json
"backscatter_correction": {
  "mode": "none" | "empirical" | "auto",
  "kind": "none" | "curve" | "table" | "table-unresolved",
  "source": "<curve/table path, or sonar_info:<topic>>",
  "sha256": "<hex>" | null,
  "fallback": "identity" | "exclude" | null,
  "min_pings": 0 | null
}
```

`sha256` is of the curve or table file's bytes, or of a canonical rendering
of a SonarInfo-delivered curve (there is no file to hash); `fallback` and
`min_pings` are the per-pulse-mode table's policy and are `null` for a curve
or none (they are written for `table-unresolved` too). `kind:
"table-unresolved"` records a table run in which not one ping resolved to a
table entry — a wrong `--sonar-info-topic`, no SonarInfo in the bags, or pings
without a frequency: the table was loaded and hashed, but the backscatter
layer carries none of its correction, so the store must never pass for a
table-corrected one. `import_bag` exits 4 then. Exit 4 means a table was
given and no ping resolved a pulse mode; on a full run the store and its
`table-unresolved` fingerprint were still written, and a `--level-plan-out`
preflight, which writes neither, exits 4 too. `isStale` compares `mode`, `kind`, `sha256`, `fallback` and
`min_pings` — not `source`, so the same table under another path is the same
correction. A fingerprint without the key (written before #172) is stale
against one that has it: what correction built its backscatter layer cannot
be told. This replaces the schema-1 sketch above (`{"mode", "curve_sha256"}`)
and keeps schema version 2 (additive).

`tool_version` as specified reads `package.xml`'s
`<version>`, which is `0.0.0` and has never been bumped, so that key would be
inert until a versioning practice exists — a follow-up, not this amendment.

## Amendment 2026-11-09 — schema version 3: `gain_declaration` ([cube_bathymetry#176](https://github.com/rolker/cube_bathymetry/issues/176))

A sonar's receiver gain can step without the sonar recording it, and the
operator declares the steps in a `gain-state-declaration v1` file (ADR-0007,
gain-state addendum). Backscatter intensities are shifted by the declared
offset before any correction, so the declaration decides the backscatter
layer's values the way the curve or table does and belongs in the fingerprint.

```json
"gain_declaration": {
  "reference_state": "<the declaration's reference-state label>",
  "sha256": "<hex of the declaration's CANONICAL form>",
  "source": "<file path>"
}
```

or `null` (no declaration), written beside `tiling` and `backscatter_correction`.
`schema_version` goes 2 → 3, so **every store built before #176 reads stale**:
the gain state it was built in is unknowable, and a store built across a step
without a declaration is exactly the store this amendment exists to flag.
`fromJson` already returns a non-current schema as readable-but-stale without
interpreting its fields, so no migration code is needed ("regenerate is the
migration").

- **What the digest hashes.** The canonical form of the declaration, not the
  file bytes: the format version, the reference state, and each segment's
  `start_ns`, `end_ns` and `offset_db` (`%.6f`, negative zero as zero) in
  sorted order. Ids, provenance, notes, `# sonar:`, comments, whitespace and
  field order are excluded, so rewording a note does not make every store and
  every derived table stale; anything that changes the correction does. The
  comparison is strict equality: there is no "the declaration still covers the
  data" reasoning. Both the C++ and the Python loader compute it and
  `test/data/gain_declaration_vectors.json` pins the canonical text and the
  digest for both.
- **`isStale`** compares `reference_state` and `sha256`, not `source`, and
  treats present against absent as different. A zero-segment declaration is
  present (it names a reference state), so a store imported with one is stale
  against one imported with none; that is intended: it is in a named
  reference state.
- **The derive tool's tables and curves carry the same two values**
  (`# gain_reference_state:`, `# gain_declaration_sha256:`), and `import_bag`
  and `batch_regen_bag` refuse a curve or table whose record differs from the
  declaration they are given. That check is not a staleness check on a store; it
  is the same identity applied to the correction's input.
- **`batch_regen_bag` still writes no fingerprint.** It reads the `-o` store's
  (never `--reference-store`'s, which only seeds the predicted depth surface)
  and refuses what would leave tiles in two gain states in one store: a
  declaration against a store built under another one, under none, or under an
  older schema; or no declaration against a store built under one. A missing
  fingerprint file, or an older schema with no declaration given, proceeds.
  Writing a fingerprint from `batch_regen` stays the follow-up it was.
- **The live node writes no fingerprint**, so a node started with a different
  declaration than the one that built the store it resumes from or primes from
  is not detected. Not a double application (the sums hold already-shifted
  intensities), but an unguarded mixing; the README says so.

### Staleness rules, schema 3

| Condition | Action |
|---|---|
| `schema_version` != 3 | Full regen |
| one has a `gain_declaration` and the other has none | Full regen |
| `gain_declaration.reference_state` or `.sha256` differs | Full regen |
| `gain_declaration.source` differs | No effect |

All the schema-2 rules above stand with `schema_version` read as 3.

## Note 2026-11-12 — slope-run stores carry `derived_tiles.json` ([cube_bathymetry#173](https://github.com/rolker/cube_bathymetry/issues/173))

A backscatter store written by the slope run of `import_bag` carries `derived_tiles.json` (per-tile contributors and depth-tile hashes, ADR-0007 incidence addendum) instead of a build fingerprint, and the slope run writes no `build_fingerprint.json`. No key of this schema changed, and the fingerprint of every other run is byte for byte what it was.
