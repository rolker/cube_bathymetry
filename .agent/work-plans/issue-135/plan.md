# Plan: live draft tile writes must not lose data (cube_bathymetry#135)

## Problem
Issue #135 (comment of 2026-10-12, also unh_marine_autonomy#256):
1. `saveTile` writes straight to the final path, so a crash/SIGKILL mid-write
   leaves a truncated tile that has already replaced the good one.
2. `marine_bathymetry_store::load()` throws on the first unreadable tile; the node
   logs WARN "starting empty" and primes nothing.
3. The next save writes each touched tile from the in-memory sheet alone, so every
   good tile the new session touches loses all earlier surveys' cells. The bad tile
   stays, so each later session repeats it.
The costmap loadWindow race (issue body) is the reader side of the same non-atomic write.

## Scope (this repo only)
The atomic write inside `marine_tiled_raster_store::saveTile` and a per-tile skip in
`load()`/`loadWindow()` belong to unh_marine_autonomy#256; no change is made there.
The cube-owned writers are made safe here, which closes the loss path today:

1. `cube::writeFileAtomically(path, fn)` (store_import.h): write to `<path>.partial`
   (extension `.partial`, so store scans ignore it), fsync, rename over `path`, fsync
   the directory; remove the temp on any error. Used for the node's `saveDirtyTiles`
   and batch_regen's processed-tile write (bathy and MBES backscatter).
2. `cube::loadStoreSkippingUnreadable(store, dir)`: try `load()`; if it throws (and the
   store is not a refused legacy `survey/` layout, which still fails configure), load
   every readable tile individually (`loadTile` + `importTiles`) and return the list of
   unreadable ones. A good tile is never lost because its neighbour is damaged.
3. Node: the configure/reset prime uses (2). Unreadable `draft/` tiles are recorded;
   `saveDirtyTiles` REFUSES to write them (never overwrite what could not be loaded),
   logging an ERROR naming the file, and keeps the rest saving normally. Operator
   action: repair or delete the named tile.

## Refinements from review (pre-push)
- A layer dir that cannot be listed fails the whole prime; the node then sets
  `draft_saves_blocked_` (no draft save at all this session).
- A refused tile stays dirty (`GeoMapSheet::clearDirtyGrids(saved)`): it is never
  evicted, and cleanup refuses rather than lose it. If the operator removes the
  damaged file, the next save writes the tile (nothing is left to erase); repairing it
  needs a reconfigure/`~/reset` to reload.
- Not covered here (offline tools, same fix as upstream #256): `marine_bathymetry_store::save`
  used by the draft-clearing step in store_import.cpp and `count_grid` scratch tiles.

## Decision taken (flag for the owner)
Refuse rather than quarantine/replace: a tile that cannot be read is left on disk
untouched and new soundings for that tile are not persisted this session (they stay in
the bag). Alternative: rename the bad file aside and write fresh.

## Tests
- Reproduction (fails before): session 1 writes tile A; a truncated neighbour is
  planted; session 2 primes via the node's load path and saves; assert A's cells survive.
- Atomic write: failure mid-write leaves the original tile and no `.partial`.
- Skipping loader: reports the unreadable tile, loads the others; legacy-refused rethrows.
- Node-level: dirty tile mapped to an unreadable file is not overwritten.
