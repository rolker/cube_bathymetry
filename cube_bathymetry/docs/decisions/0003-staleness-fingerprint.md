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
