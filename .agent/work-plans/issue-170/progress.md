---
issue: 170
---

# Issue #170 — derive_angular_response.py: seawater absorption (full Francois-Garrison), explicit alpha override, and source-bag provenance

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-30 10:49 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-170 at `5984ab4`
**Mode**: pre-push
**Depth**: Deep (reason: 1373 changed lines across 8 files)
**Must-fix**: 2 | **Suggestions**: 11
**Round**: 1 | **Ship**: continue — round 1, and both must-fixes are a correctness/design change to the --box TF positioning (shared buffer across bags, guard-then-exact lookup) that needs a re-read

Specialists: static analysis (ament_flake8, pep257, cpplint, uncrustify, xmllint, lint_cmake, copyright: all clean); governance; plan drift skipped (no plan.md); Claude Adversarial Lens A + Lens B; cross-model gemini + codex (both EXIT=0); local model off. The 322 new pytest cases pass. Francois-Garrison coefficients were checked against the 1982 form, and S=0 is bit-identical to the pre-#170 function. Heikkinen ECEF and the C++ and marine_tools header contracts (exact-key match) were verified correct. Co-Authored-By trailers are irrelevant to every hook and CI check: check_pr_authors.py ignores co-authors, and the repo has no trailer checks.

### Findings
- [x] (must-fix) A new PingLocator/tf2 buffer per bag loses /tf_static across split files given one by one. Later splits drop every ping as no_tf, which import_bag keeps (one shared buffer). Share the buffer or carry the statics forward, and add a two-split test. Confirmed by codex, Lens A and Lens B. — `cube_bathymetry/scripts/derive_angular_response.py:486`
- [x] (must-fix) Pings are located on arrival with a latest-within-1 s fallback, unlike import_bag (3 s TF-frontier guard, then exact interpolated lookup). At a 1 Hz position rate about 27% of pings drop as stale_tf despite a bracketing transform being in the bag. The docstring and README "the way import_bag georeferences" claims are wrong. Buffer pings behind a guard, as C++ does, or correct the docs. Raised by codex, Lens A and Lens B. — `cube_bathymetry/scripts/derive_angular_response.py:246` (docs :69, :212, `README.md:742`)
- [x] (suggestion) A zero header.stamp becomes tf2 "latest" and bypasses the stale check (reproduced); reject unset stamps. Raised by codex, gemini and Lens B. — `cube_bathymetry/scripts/derive_angular_response.py:246`
- [x] (suggestion) ecef_to_geodetic raises ValueError on a non-geocentric earth translation such as (1,1,1) or (100,100,100), aborting the run, while (0,0,0) silently maps to the North Pole. Catch it or bound the height, and count the ping as dropped. Raised by codex, gemini and Lens A. — `cube_bathymetry/scripts/derive_angular_response.py:258`
- [x] (suggestion) The e2e test's ping 2 is bracketed by two OUTSIDE transforms, so it cannot tell an exact lookup from latest; there is also no test at the 1 s tolerance boundary. — `cube_bathymetry/test/test_derive_angular_response.py:566`
- [x] (suggestion) No bag storage plugin is declared. Add test_depend rosbag2_storage_mcap (the test writes mcap) and exec_depend rosbag2_storage_default_plugins. — `cube_bathymetry/package.xml:56`
- [x] (suggestion) Comment typo "with / with the box filter". — `cube_bathymetry/package.xml:55`
- [x] (suggestion) The ADR-0007 item 4 still says in the present tense "`--salinity > 0` is rejected by the tool" after the superseded note; mark it as historical. — `cube_bathymetry/docs/decisions/0007-mbes-backscatter-store-addendum-phase-b-transition.md:164`
- [x] (suggestion) Without --remove-tl, --salinity and --ph warn, but --depth-m and --water-temp-c are ignored silently; make this consistent. — `cube_bathymetry/scripts/derive_angular_response.py:344`
- [x] (suggestion) The median ping_info.frequency has no plausibility bound: kHz-in-Hz gives a near-zero alpha silently, and it now drives the salt-water alpha. Bound it to roughly 1 kHz..2 MHz. — `cube_bathymetry/scripts/derive_angular_response.py:657`
- [x] (suggestion) --box only refuses kept==0. A box keeping a handful of pings writes a curve from thin bins (cube_lab uses kMinPerBin=200); warn or refuse below a minimum. — `cube_bathymetry/scripts/derive_angular_response.py:700`
- [x] (suggestion) The CSV write is not atomic: open('w') truncates an existing good curve before writing, and a late encode error leaves a header-only file. Write to a temp file, then os.replace. — `cube_bathymetry/scripts/derive_angular_response.py:638`
- [x] (suggestion) Record the compatibility deltas in the PR body: new header lines, water_temp_c %.3g to %.6g, newly refused inputs (temperature outside -2..40, NaN, negative depth, a comma or newline in a bag name), and the override accepting --water-temp-c (record-only), which differs from the issue text. Also document that all */tf topics merge into one buffer (multi-vehicle bags). — `README.md:742` (done: list in the Implementation entry below, "Changes for users"; multi-vehicle */tf note in README, 39bbcfe)

## Implementation
**Status**: complete
**When**: 2026-10-30 11:11 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-170 at `6d69025`
**Addressed**: Local Review (Pre-Push), 2026-10-30 10:49 -04:00, branch at `5984ab4` (entry committed in `3381b4d`): 2 must-fix, 11 suggestions
**Commits**: b3d99e3, 39bbcfe, d27315f, de8f433, 93de05f, 910ecd2, b87fec7, 358088d, b73eb7e, 2e09691, 2412e67, 6d69025 (lint)

### Actions
- [x] (must-fix) One TF buffer across bags: with `--box` every bag feeds one `PingLocator`, and the bags are k-way merged on the bag timestamp into one chronological stream (import_bag's `BagReaders`), so `/tf_static` from the first split serves later splits and argument order no longer matters. Without `--box` bags are still read one after another (the sums do not depend on order). Two-split test, run with the files in both orders; it fails on the previous code. (b3d99e3) — `cube_bathymetry/scripts/derive_angular_response.py` `merged_messages`, `accumulate`
- [x] (must-fix) Positioning now matches import_bag: pings queue until the dynamic TF frontier is 3 s (`TF_GUARD_SEC`, import_bag's `kGuardSec`) past the stamp, then an exact interpolated `earth -> frame` lookup; no latest fallback; the queue is flushed at end of run (one merged stream, so one end; flushing per bag would drop the last pings of each split, whose bracketing transform is in the next file). Cache window 60 s like import_bag. Drops are counted as `no_tf` / `extrapolation`. Module docstring and README corrected. (39bbcfe) — `derive_angular_response.py` `PingLocator`, `README.md` (`--box` section)
- [x] (suggestion) Zero (or negative) stamps are dropped as `bad_stamp` before any lookup; e2e and unit tests. (d27315f) — `derive_angular_response.py` `PingLocator.locate`
- [x] (suggestion) `earth_position_to_lat_lon` returns None for a non-finite position, one `ecef_to_geodetic` raises on, or one more than 50 km from the ellipsoid; the ping counts as `bad_position` and the run continues. (0,0,0), (1,1,1), (100,100,100), local coordinates and geostationary distance are tested. (de8f433) — `derive_angular_response.py` `earth_position_to_lat_lon`
- [x] (suggestion) e2e test: new ping bracketed by a SOUTH and a NORTH transform, both outside the box, kept only by interpolation; pings before the first and after the last transform; `PingLocator` tests exercise the guard edge to the nanosecond and the no-fallback flush. (39bbcfe) — `cube_bathymetry/test/test_derive_angular_response.py`
- [x] (suggestion) `exec_depend rosbag2_storage_default_plugins` and `test_depend rosbag2_storage_mcap`. (910ecd2) — `cube_bathymetry/package.xml`
- [x] (suggestion) "with with" typo fixed. (93de05f) — `cube_bathymetry/package.xml`
- [x] (suggestion) ADR-0007 item 4 now reads as the historical #87 decision. (b87fec7) — `cube_bathymetry/docs/decisions/0007-mbes-backscatter-store-addendum-phase-b-transition.md`
- [x] (suggestion) `--water-temp-c` and `--depth-m` now warn when ignored without `--remove-tl`, like `--salinity`/`--ph`. (358088d) — `derive_angular_response.py` `parse_args`
- [x] (suggestion) Computed alpha refuses a median frequency outside 1 kHz..2 MHz (`FREQUENCY_KHZ_RANGE`); the override path does not use the frequency and is not checked. README notes it. (b73eb7e) — `derive_angular_response.py` `resolve_absorption`
- [x] (suggestion) Thin curves WARN (decision: warn, not refuse; the curve is still written and every row carries n): when the nadir reference bin has < 200 samples or fewer than 3 bins reach 200, the cube_lab `kMinPerBin` conditions. Applies to every run; with `--box` the warning names the kept-ping count. (2e09691) — `derive_angular_response.py` `thin_curve_warning`
- [x] (suggestion) CSV written atomically: text built first, temp file in the same dir, fsync, `os.replace`; temp removed on failure; existing permission bits kept. (2412e67) — `derive_angular_response.py` `write_text_atomically`
- [x] (suggestion) Compatibility deltas recorded below under "Changes for users", ready for the PR body; the multi-vehicle note (all `*/tf` topics share one buffer) is in the README and docstring (39bbcfe). — `README.md`

### Changes for users (paste into the PR body)
Compared with `derive_angular_response.py` before #170:

- **Seawater works.** `--salinity > 0` is no longer refused; alpha is the full Francois-Garrison model (new `--ph`, default 8.0). At salinity 0 the data rows are byte-identical to before.
- **New `--absorption-db-per-m`** override. It needs `--remove-tl`, excludes `--salinity`/`--ph`/`--depth-m`, and, unlike the issue text, accepts `--water-temp-c`, which is then only recorded in the header.
- **New `--box LATMIN LONMIN LATMAX LONMAX`** site filter, positioned from the bags' TF like import_bag (one buffer across all bags, chronological merge, 3 s guard then exact interpolated lookup, no latest fallback). All `*/tf` and `*/tf_static` topics share one buffer, so a multi-vehicle bag needs distinct frame names (import_bag has the same limitation). Drops are reported by reason; a box that keeps nothing is an error.
- **Header changes.** New `# key: value` lines: `absorption_source`, `salinity_psu`, `ph`, `depth_m`, `frequency_khz` (computed path), `box` (with `--box`) and `source_bags` (always). `water_temp_c` is now `%.6g` instead of `%.3g` (15.55 now prints as 15.55, not 15.6), and with the override it is omitted unless given. The C++ loader still reads only `tl_removed` and `absorption_db_per_m`.
- **Newly refused inputs:** with `--remove-tl`, `--water-temp-c` outside -2..40 C or NaN, `--salinity` outside 0..50, `--ph` outside 6..10 and `--depth-m` outside 0..12000 (negative depth included); for a computed alpha, a median `ping_info.frequency` outside 1 kHz..2 MHz; always, a bag whose basename is empty or contains a comma or line break.
- **New warnings (stderr), run still succeeds:** absorption inputs given without `--remove-tl` (`--water-temp-c`, `--salinity`, `--ph`, `--depth-m`); a thin curve (nadir bin < 200 samples, or fewer than 3 bins with 200).
- **The output CSV is replaced atomically.** A failed run no longer truncates an existing curve.
- **Packaging:** the package now declares `rosbag2_storage_default_plugins` (run) and `rosbag2_storage_mcap` (test).

### Checks
- `./sensors_ws/build.sh cube_bathymetry`: clean.
- `./sensors_ws/test.sh cube_bathymetry`: `Summary: 1192 tests, 0 errors, 0 failures, 90 skipped` (was 1163; a first run had 4 lint failures, all one D401 on `PingLocator.pending`, fixed in 6d69025)
- `test_derive_angular_response.py`: 351 passed (was 322).
- Pre-commit hooks ran on every commit (no `--no-verify`).

### Next
review-code (re-review) on the branch at `6d69025`. Nothing pushed.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-30 11:18 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-170 at `b6fa686`
**Mode**: pre-push
**Depth**: Deep (reason: ~2000 changed lines across 9 files)
**Must-fix**: 2 | **Suggestions**: 8
**Round**: 2 | **Ship**: recommended — 2 must-fixes, the same count as round 1, each a precise mechanical fix (one guard plus a test, and one topic check or docstring); no design question remains

Specialists: static analysis (the ament lint tests inside the suite: all clean); governance; plan drift skipped (no plan.md); Claude Adversarial Lens A + Lens B; cross-model gemini + codex (both EXIT=0); local model off. `./sensors_ws/test.sh cube_bathymetry`: 1192 tests, 0 errors, 0 failures, 90 skipped. Old (origin/jazzy) and new script run end to end on the test suite's synthetic bag (tier-1; tier-2 at 24 C; tier-2 at 15.55 C and 30 m): the data rows are byte-identical in all three runs. No `.agents/README.md` and no `review-context.yaml` in this repo.

Round-1 findings: 12 of 13 resolved as claimed. The shared buffer, the time-ordered merge (also correct for out-of-order files on the command line), the 3 s guard with exact lookup and end-of-run flush, bad_stamp, the package deps, the typo, the ADR wording, the ignored-input warnings, the frequency bound, the thin-curve warning and the atomic write all check out. Round-1 item 4, the non-geocentric earth position, is only partly fixed (must-fix 1). Gemini's CI-image dependency finding is a false positive: ci.yml runs rosdep install at CI time.

### Findings
- [x] (must-fix) `earth_position_to_lat_lon` still lets a non-geocentric position abort the run. For an ECEF position about 20 to 45 km from the Earth's centre, Heikkinen's cube root goes complex and the next `math.sqrt` raises `TypeError`, which the `except (ValueError, ZeroDivisionError, OverflowError)` does not catch. That is a local map frame named `earth` with the sensor 20 to 45 km from its origin. Reproduced by fuzzing, e.g. (34721.27, 13579.63, -18942.10); confirmed by Lens A. Reject a radius outside roughly [b - 50 km, a + 50 km] before the closed form (or catch TypeError), and add a test point in that band. — `cube_bathymetry/scripts/derive_angular_response.py:222`
- [x] (must-fix) `accumulate` exits with "topic not in bag" for any bag without the detections topic, yet its docstring says "a bag holding only TF positions the pings of a bag recorded beside it" (:609-611, also :564-566). With `--box`, allow readers without the topic (error only when no bag has it) and add a test with a real TF-only bag, or drop the claim. Raised by gemini, codex and Lens B. — `cube_bathymetry/scripts/derive_angular_response.py:621`
- [x] (suggestion) FIFO head-of-line blocking: one ping with a far-future header stamp holds every later ping until the end of the run, and at the flush most of them drop as `extrapolation`. Codex reproduced it; in Lens A's run 95 of 101 dropped. Drain from a stamp-ordered heap so ready pings leave regardless of arrival order. import_bag has the same deque (`import_bag_main.cpp:2243`), so the fix belongs there too. Raised by codex (as must-fix), gemini, Lens A and Lens B. — `cube_bathymetry/scripts/derive_angular_response.py:318`
- [x] (suggestion) The pending queue is unbounded. With no dynamic /tf (frontier stays None), or once TF stops, every remaining ping is held as a deserialized message until the flush: about 36 KB per 512-beam ping in Python, against about 7 KB serialized, so roughly 2.6 GB per hour at 20 Hz. Fail fast when no bag has a dynamic tf topic, queue a compact tuple or the serialized bytes, and report the maximum pending count; give it a README sentence. Raised by Lens A and Lens B, and measured here. — `cube_bathymetry/scripts/derive_angular_response.py:288`
- [x] (suggestion) The frontier advances on a transform tf2 rejects (NaN, bad quaternion, TF_OLD_DATA) and on a future-stamped one, so waiting pings are released before their bracketing transform arrives and drop as `extrapolation`. Advance it only for accepted, plausible stamps. Raised by Lens A and Lens B. — `cube_bathymetry/scripts/derive_angular_response.py:297`
- [x] (suggestion) `main` now opens every bag reader up front even without `--box` (before the fix pass they were opened one at a time) and never releases them; hundreds of split files run into `ulimit -n`. Open lazily in the no-box path, and release each reader once it is exhausted in `merged_messages`. Raised by Lens B. — `cube_bathymetry/scripts/derive_angular_response.py:919`
- [x] (suggestion) `write_text_atomically` replaces a symlinked `-o` with a regular file, where the old `open('w')` wrote through the link, and `-o /dev/stdout` now fails (temp file in /dev). Resolve `os.path.realpath` first and write a non-regular target directly. Raised by Lens A and Lens B. — `cube_bathymetry/scripts/derive_angular_response.py:849`
- [x] (suggestion) The same bag given twice, or overlapping bags, double-counts pings and floods stderr with unthrottled tf2 TF_OLD_DATA warnings. Refuse duplicate resolved paths, and count rather than print the rejected transforms. Raised by Lens A and Lens B. — `cube_bathymetry/scripts/derive_angular_response.py:919`
- [x] (suggestion) `deserialize_message` is unguarded, so one corrupt message aborts the run with a traceback (the C++ catches and reports per message); skip and count it, or fail naming the bag. Raised by Lens B. — `cube_bathymetry/scripts/derive_angular_response.py:669`
- [x] (suggestion) `--bin-width-deg` is unvalidated: 0 raises ZeroDivisionError, and a negative or NaN value makes nonsense bins (this predates the PR; one `p.error`). Raised by Lens A. — `cube_bathymetry/scripts/derive_angular_response.py:395`

## Implementation
**Status**: complete
**When**: 2026-10-30 11:38 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-170 at `db59d65`
**Addressed**: Local Review (Pre-Push), 2026-10-30 11:18 -04:00, branch at `b6fa686` (entry committed in `bdea32d`), Round 2: 2 must-fix, 8 suggestions
**Commits**: 75361b7, 52edeae, 4c65d03, ce5bd0c, 6c9dd12 (lint), 6fbae15, 4c50117, bfe4410, 89d6493, 6c1f4ea, db59d65

### Actions
- [x] (must-fix) Non-geocentric position abort: `earth_position_to_lat_lon` now refuses a geocentric radius outside `EARTH_RADIUS_RANGE_M` = [b - 50 km, a + 50 km] before the closed form, also catches `TypeError` and refuses a non-float result; the ping counts as `bad_position`. Tests: the fuzz point (34721.27, 13579.63, -18942.10), 1 m either side of both band edges, a point inside the band but 71 km below the ellipsoid, and a seeded 20000-point sweep from the centre to 7000 km that must never raise (fails on the previous code). (75361b7) — `cube_bathymetry/scripts/derive_angular_response.py` `earth_position_to_lat_lon`
- [x] (must-fix) TF-only companion bag: with `--box` a bag without the detections topic is read for its TF; the run errors ("not in any bag") only when no bag has the topic. Without `--box` every bag must still carry it (the error now names the bag). Tests with a real TF-only bag beside a detections-only bag (kept 2 of 2), the no-box error, and the no-bag-has-it error. Docstring and README say so. (52edeae, lint 6c9dd12) — `derive_angular_response.py` `accumulate`
- [x] (suggestion) Head-of-line blocking: the pending queue is a heap on (stamp, arrival), so ready pings leave whatever arrived before them; a ping stamped more than `PING_FUTURE_LIMIT_SEC` (1 h) ahead of the TF frontier when it arrives is dropped at once as `bad_stamp` (report wording updated). Unit tests for both, failing on the FIFO. **Note for the host:** import_bag has the same first-in-first-out queue (`cube_bathymetry/src/import_bag_main.cpp:2243`, `std::deque<std::pair<int64_t, SonarDetections>> pending`) and may need the same fix; the C++ is not changed in this PR. (6c1f4ea) — `derive_angular_response.py` `PingLocator.push`/`drain`
- [x] (suggestion) Unbounded queue: `PingLocator.push(ping_ns, frame_id, item)` now queues an opaque item, and `accumulate` queues a `PingRecord` (the three per-beam float32 arrays as they are, flags as bytes, sound speed and frequency: about 13 bytes per beam, ~7 KB for 512 beams, against ~36 KB for the message). `bin_ping` reads the same values in the same order (end-to-end freshwater rows still byte-identical, below). A `--box` run whose bags have no `/tf` topic is refused before reading; the report prints "at most N waited for TF"; README sentence on the memory. With the 1 h future bound above, a TF stream that stops holds at most about an hour of pings. (89d6493) — `derive_angular_response.py` `PingRecord`, `accumulate`, `box_report`
- [x] (suggestion) Frontier on rejected/future transforms: `PingLocator.transform_reject_reason` keeps out of tf2 what it rejects (empty or self-referencing frame names, NaN/inf, a zero quaternion; jazzy's tf2 normalises any other quaternion, verified) and, for dynamic transforms, one more than 60 s older than its child frame's newest (TF_OLD_DATA) or stamped more than 60 s after its bag time (tf2 would accept it and then prune the real transforms of that frame against it, verified). Those are counted, not printed by tf2, and one warning reports them. An accepted transform advances the frontier to its stamp but never past the time the bag recorded it (a clamp rather than a refusal, so a constant clock offset between publisher and recorder delays release instead of rejecting every transform). Tests use capfd to show tf2 prints nothing. (db59d65) — `derive_angular_response.py` `PingLocator.add_tf`
- [x] (suggestion) File descriptors: topics are checked from each bag's metadata (`rosbag2_py.Info`, nothing held open). Without `--box` bags are opened and read one at a time and each is closed before the next opens. With `--box` `merged_messages` opens a bag only when the stream reaches its metadata start time and closes it once exhausted, so consecutive splits hold one reader at a time; abandoning the generator closes the rest. Unit tests with a fake reader: 300 splits given in reverse hold at most 1 open; overlapping bags interleave with ties to the first given. (4c50117) — `derive_angular_response.py` `read_bag_metadata`, `merged_messages`, `accumulate`
- [x] (suggestion) Symlinked `-o` and `/dev/stdout`: a regular (or not yet existing) output is resolved with `os.path.realpath` and replaced there, so the link stays; a device or pipe, or any path under `/dev` or `/proc` as given or resolved, is written directly as `open(path, 'w')` did (checking the given path first matters: `/dev/stdout` redirected to a file resolves to that file, which must not be replaced). Tests: symlink (link kept, target mode kept), dangling symlink, `/dev/null`, a FIFO, and `-o /dev/stdout` in a subprocess. README sentence. (6fbae15) — `derive_angular_response.py` `write_text_atomically`
- [x] (suggestion) Same bag twice: `check_distinct_bags` refuses a repeated bag after `realpath` (symlinks, `..`, trailing slash) and a storage file inside a bag directory that is also given; overlapping bags are documented in the README as counted once per bag, not deduplicated. The other half of the finding, the unthrottled tf2 warnings, is fixed by the transform checks above: rejected transforms are counted and reported once. (ce5bd0c, db59d65) — `derive_angular_response.py` `check_distinct_bags`
- [x] (suggestion) Corrupt messages: `deserialize_or_none` catches rclpy's `RMWError` (a `RuntimeError`, verified), `ValueError` and `MemoryError`; the message is skipped and counted per topic, and one warning reports the counts at the end. Corrupt pings are excluded from the ping count. Tests with a garbage detections and a garbage `/tf` message, with and without `--box`. (bfe4410) — `derive_angular_response.py` `accumulate`
- [x] (suggestion) `--bin-width-deg` must be finite, > 0 and <= 90 (`p.error`); tests for 0, -2, nan, inf, 90.5 refused and 0.1, 2, 90 accepted. (4c65d03) — `derive_angular_response.py` `parse_args`

### Changes for users (paste into the PR body)
Compared with `derive_angular_response.py` before #170:

- **Seawater works.** `--salinity > 0` is no longer refused; alpha is the full Francois-Garrison model (new `--ph`, default 8.0). At salinity 0 the data rows are byte-identical to before.
- **New `--absorption-db-per-m`** override. It needs `--remove-tl`, excludes `--salinity`/`--ph`/`--depth-m`, and, unlike the issue text, accepts `--water-temp-c`, which is then only recorded in the header.
- **New `--box LATMIN LONMIN LATMAX LONMAX`** site filter, positioned from the bags' TF like import_bag (one buffer across all bags, chronological merge, 3 s guard then exact interpolated lookup, no latest fallback). All `*/tf` and `*/tf_static` topics share one buffer, so a multi-vehicle bag needs distinct frame names (import_bag has the same limitation). A bag holding only TF may be given beside the detections bags; at least one bag must have the detections topic, and at least one must have a `/tf` topic (refused before reading otherwise). Waiting pings leave in stamp order; a ping stamped more than 1 h ahead of the TF stream is dropped as a bad stamp. Transforms tf2 would reject, or stamped more than 60 s after they were recorded, are left out and counted in one warning instead of tf2 printing each. Drops are reported by reason, with the most pings that waited for TF at once; a box that keeps nothing is an error.
- **Header changes.** New `# key: value` lines: `absorption_source`, `salinity_psu`, `ph`, `depth_m`, `frequency_khz` (computed path), `box` (with `--box`) and `source_bags` (always). `water_temp_c` is now `%.6g` instead of `%.3g` (15.55 now prints as 15.55, not 15.6), and with the override it is omitted unless given. The C++ loader still reads only `tl_removed` and `absorption_db_per_m`.
- **Newly refused inputs:** with `--remove-tl`, `--water-temp-c` outside -2..40 C or NaN, `--salinity` outside 0..50, `--ph` outside 6..10 and `--depth-m` outside 0..12000 (negative depth included); for a computed alpha, a median `ping_info.frequency` outside 1 kHz..2 MHz; always, a bag whose basename is empty or contains a comma or line break, the same bag given twice (however the path is spelled), and `--bin-width-deg` that is not finite, > 0 and <= 90 (0 used to crash).
- **New warnings (stderr), run still succeeds:** absorption inputs given without `--remove-tl` (`--water-temp-c`, `--salinity`, `--ph`, `--depth-m`); a thin curve (nadir bin < 200 samples, or fewer than 3 bins with 200); messages that would not deserialize (skipped and counted per topic; one corrupt message used to abort the run); with `--box`, transforms left out of the TF buffer.
- **The output CSV is replaced atomically.** A failed run no longer truncates an existing curve. A symlinked `-o` is written through to its target, and `-o /dev/stdout` (or another device or pipe) is written directly.
- **Many split files:** bags are opened one at a time (with `--box`, as the time-ordered stream reaches each), so hundreds of split files no longer run out of file descriptors. The "topic not in bag" error now names the bag.
- **Packaging:** the package now declares `rosbag2_storage_default_plugins` (run) and `rosbag2_storage_mcap` (test).

### Checks
- `./sensors_ws/build.sh cube_bathymetry`: clean.
- `./sensors_ws/test.sh cube_bathymetry`: `Summary: 1227 tests, 0 errors, 0 failures, 90 skipped` (was 1192)
- `test_derive_angular_response.py`: 386 passed (was 351). Each new test for a behaviour change was run against the previous script and failed there.
- End to end, origin/jazzy's script and this branch's on the test suite's synthetic bag (tier-1; tier-2 at 24 C; tier-2 at 15.55 C and 30 m): data rows byte-identical in all three.
- ament_flake8 and ament_pep257 on both files: clean. Pre-commit hooks ran on every commit (no `--no-verify`).

### Next
review-code (re-review) on the branch at `db59d65`. Nothing pushed.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-30 11:48 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-170 at `70a0c55`
**Mode**: pre-push
**Depth**: Standard, targeted (reason: round 3 reviews only the round-2 fix commits, `git diff bdea32d..db59d65`, 4 files, +933 -160)
**Must-fix**: 1 | **Suggestions**: 2
**Round**: 3 | **Ship**: recommended — 1 must-fix (down from 2), a regression this fix pass introduced, with one precise remedy and a test; a targeted check of that fix is enough, not another full round

Specialists: static analysis (the ament flake8/pep257 tests inside the suite: clean); one fresh-context Claude adversarial pass (Lens A); cross-model off (nothing needed a second opinion); local model off; plan drift skipped (no plan.md). `./sensors_ws/test.sh cube_bathymetry`: 1227 tests, 0 errors, 0 failures, 90 skipped.

End to end, the round-2 script (bdea32d) and this one (db59d65) were run on a richer synthetic bag than the suite's: 1195 pings of 64 beams, about 5% NaN intensities, nonzero flags (1, 2, 3, 255) with flag arrays up to 3 beams short, three frequencies, varying sound speed, ping stamps jittered and recorded out of stamp order. Tier-1, tier-2 at 30 psu, and tier-2 with `--box`: the data rows are byte-identical in all three, so `PingRecord` gives the binning the same beam data. Also checked: empty bags (metadata start INT64_MAX) and a bag whose first message is not on a wanted topic, given beside the detections bag, give the same rows; a bag directory together with its own storage file is refused.

All 10 round-2 findings are resolved as claimed. The heap tie-break, the 1 h future drop, the stamp-ordered flush, the radius band (Lens A fuzzed 200k valid positions, none rejected), the lazy open and close in `merged_messages`, the `sources`/`tf_topics`/`msg_types` index alignment, `deserialize_or_none`, the `/dev` direct write and `--bin-width-deg` all check out. The new frontier rules introduce one regression (must-fix below).

### Findings
- [x] (must-fix) The 60 s 'future' rule and the bag-time clamp on the frontier break bags whose recorder clock lags the publisher's (for example operator-station bags of relayed topics, or unsynchronised hosts). Reproduced: with every header stamp 90 s ahead of the bag time, round 2 kept 400 of 1195 pings and this commit keeps 0. All 240 transforms are rejected as 'future' and the run ends "no pings inside --box". At 30 s the run still works, but every ping waits about 30 s longer. Lens A found that past about 57 s the waiting ping falls out of the 60 s tf2 cache and drops as 'extrapolation'. Make both rules relative to each child frame's own stamp-minus-bag-time offset: take the offset of the frame's first accepted transform as its reference; reject as 'future' only a transform whose offset jumps more than the cache length above that reference; advance the frontier to `min(ns, bag_ns + reference offset)`. A lone glitch is still caught and a constant offset passes. Add tests at +90 s and +57 s. — `cube_bathymetry/scripts/derive_angular_response.py:384` (and :355)
- [x] (suggestion) `-o` that is (or links to) a writable file in a read-only directory now fails with a PermissionError traceback from `mkstemp`. The pre-#170 `open(path, 'w')` wrote it. Reproduced with both a plain path and a symlink. Replacing the file also resets its owner and group and breaks hard links. On a PermissionError from `mkstemp`, either fall back to a direct write of an existing writable file or exit with a clean error. — `cube_bathymetry/scripts/derive_angular_response.py:1165`
- [x] (suggestion) The frontier is the newest dynamic stamp across all child frames. A link on the earth chain that publishes less often than every 3 s (for example earth->map at 0.1 Hz beside a 10 Hz map->base) lets the faster frame release a ping before that link's next transform arrives, and the ping drops as 'extrapolation'. Lens A reproduced this with a PingLocator. The behaviour is inherited from import_bag, not new in this pass, but the docstring promises "the transforms on both sides of it are in the buffer". Either document the roughly 3 s publishing requirement in the docstring and README, or take the frontier per chain. — `cube_bathymetry/scripts/derive_angular_response.py:355`

## Implementation
**Status**: complete
**When**: 2026-10-30 12:06 -04:00
**By**: Claude Code Agent (Claude Opus)

**Branch**: feature/issue-170 at `ed40753`
**Addressed**: Local Review (Pre-Push), 2026-10-30 11:48 -04:00, branch at `70a0c55` (entry committed in `c21463e`), Round 3: 1 must-fix, 2 suggestions
**Commits**: 2d84a47, b29280b, f910878, ed40753 (lint)

### Actions
- [x] (must-fix) Recorder clock offset: each child frame learns its offset (stamp minus bag time) from its first accepted dynamic transform. A transform is 'future' only when its offset is more than 60 s above that reference, and the frontier advances to `min(stamp, bag time + offset)`. A constant offset changes nothing; a jump on one frame is still left out and cannot release pings. A frame whose very first transform is a glitch keeps it as reference (its real transforms then drop as 'old', as tf2 would refuse them anyway); docstring says so. Tests: PingLocator at +90, +57, -30 s constant offsets releases pings as at zero; a 1 h jump on one frame is rejected beside a +90 s frame and does not move the frontier; end to end, a 150 s bag (1 Hz TF, 2 Hz pings) at +90 s and +57 s writes the same CSV bytes and box report as at zero (kept 298 of 300). The +90/+57 tests failed on the previous script. Warning wording, module docstring and README updated. (2d84a47) — `cube_bathymetry/scripts/derive_angular_response.py` `PingLocator.add_tf`, `transform_reject_reason`
- [x] (suggestion) Read-only `-o` directory: when `mkstemp` fails with EACCES/EPERM and the target exists and is writable, it is written in place with a warning ("cannot create a temp file in <dir> ...; writing <path> in place instead, so a failure part-way would leave it truncated"). This also keeps owner, group and hard links. Otherwise the run exits with `error: cannot write <path>: cannot create a file in <dir> (<reason>)`. A failed write or replace after the temp file exists now also exits cleanly ("cannot write <path> (<reason>); it is unchanged") instead of a traceback. Tests: chmod 555 dir with a writable file, directly and through a symlink; a new file and a read-only file there; a missing directory; the failed-replace test now expects the clean exit. Checked by hand through the CLI too. README sentence. (b29280b) — `derive_angular_response.py` `write_text_atomically`, `_write_in_place`
- [x] (suggestion) Slow link on the earth chain: **took the frontier per chain** (the simple version was small). PingLocator keeps a frontier per child frame and the parent of every accepted transform. A ping is ready once the oldest frontier among the dynamic links from `earth` to its frame is the 3 s guard past it. Two bounds keep this from holding pings forever: a frame not yet connected to `earth` waits for the overall frontier, as before and as in import_bag; and any ping is released once the overall frontier is cache - guard (57 s) past it, since the faster links are then about to prune the transforms around it. So every dynamic link must publish at least about once a minute (docstring, module docstring, README). A static-only chain is ready at once, and a parent loop reads as "not connected". A ping held by a slow link holds back later pings of other frames for at most that bound (documented in `drain`). Why: the per-chain gate fixes the loss rather than documenting it, and the 57 s cap is never later than the old rule would have dropped the ping. import_bag keeps the single frontier and the 3 s requirement; the C++ is not changed here (README says so). Tests: 0.1 Hz earth -> map beside 10 Hz map -> base_link and an unrelated 10 Hz frame positions the ping (dropped as extrapolation before); a stalled link releases the ping at exactly 57 s; static chain and unconnected frame; cycle. (f910878) — `derive_angular_response.py` `PingLocator.chain_frontier`, `_ready`, `drain`

### Changes for users (paste into the PR body)
Compared with `derive_angular_response.py` before #170:

- **Seawater works.** `--salinity > 0` is no longer refused; alpha is the full Francois-Garrison model (new `--ph`, default 8.0). At salinity 0 the data rows are byte-identical to before.
- **New `--absorption-db-per-m`** override. It needs `--remove-tl`, excludes `--salinity`/`--ph`/`--depth-m`, and, unlike the issue text, accepts `--water-temp-c`, which is then only recorded in the header.
- **New `--box LATMIN LONMIN LATMAX LONMAX`** site filter, positioned from the bags' TF like import_bag (one buffer across all bags, chronological merge, 3 s guard then exact interpolated lookup, no latest fallback). All `*/tf` and `*/tf_static` topics share one buffer, so a multi-vehicle bag needs distinct frame names (import_bag has the same limitation). A bag holding only TF may be given beside the detections bags; at least one bag must have the detections topic, and at least one must have a `/tf` topic (refused before reading otherwise). A ping waits until every dynamic link of its `earth -> ... -> frame` chain is 3 s past it, so a slow link (such as earth -> map at 0.1 Hz) no longer loses pings (import_bag still does); a link must publish at least about once a minute. Waiting pings leave in stamp order; a ping stamped more than 1 h ahead of the TF stream is dropped as a bad stamp. A constant clock offset between the TF publisher and the bag recorder (relayed topics, unsynchronised hosts) is handled per frame. Transforms tf2 would reject, or whose stamp jumps more than 60 s further ahead of the bag time than their frame's offset, are left out and counted in one warning instead of tf2 printing each. Drops are reported by reason, with the most pings that waited for TF at once; a box that keeps nothing is an error.
- **Header changes.** New `# key: value` lines: `absorption_source`, `salinity_psu`, `ph`, `depth_m`, `frequency_khz` (computed path), `box` (with `--box`) and `source_bags` (always). `water_temp_c` is now `%.6g` instead of `%.3g` (15.55 now prints as 15.55, not 15.6), and with the override it is omitted unless given. The C++ loader still reads only `tl_removed` and `absorption_db_per_m`.
- **Newly refused inputs:** with `--remove-tl`, `--water-temp-c` outside -2..40 C or NaN, `--salinity` outside 0..50, `--ph` outside 6..10 and `--depth-m` outside 0..12000 (negative depth included); for a computed alpha, a median `ping_info.frequency` outside 1 kHz..2 MHz; always, a bag whose basename is empty or contains a comma or line break, the same bag given twice (however the path is spelled), and `--bin-width-deg` that is not finite, > 0 and <= 90 (0 used to crash).
- **New warnings (stderr), run still succeeds:** absorption inputs given without `--remove-tl` (`--water-temp-c`, `--salinity`, `--ph`, `--depth-m`); a thin curve (nadir bin < 200 samples, or fewer than 3 bins with 200); messages that would not deserialize (skipped and counted per topic; one corrupt message used to abort the run); with `--box`, transforms left out of the TF buffer; `-o` written in place because no temp file could be created beside it.
- **The output CSV is replaced atomically.** A failed run no longer truncates an existing curve. A symlinked `-o` is written through to its target, and `-o /dev/stdout` (or another device or pipe) is written directly. An existing writable `-o` in a read-only directory is written in place with a warning; a new file there, a missing directory or any other write failure is a one-line error naming the path, not a traceback.
- **Many split files:** bags are opened one at a time (with `--box`, as the time-ordered stream reaches each), so hundreds of split files no longer run out of file descriptors. The "topic not in bag" error now names the bag.
- **Packaging:** the package now declares `rosbag2_storage_default_plugins` (run) and `rosbag2_storage_mcap` (test).

### Checks
- `./sensors_ws/build.sh cube_bathymetry`: clean.
- `./sensors_ws/test.sh cube_bathymetry`: `Summary: 1241 tests, 0 errors, 0 failures, 90 skipped` (was 1227)
- `test_derive_angular_response.py`: 400 passed (was 386). Every new behaviour test was run against the script before its fix and failed there (the -30 s offset case passed before too; it guards the negative side).
- Freshwater byte-identity tests (against the pre-#170 rows) still pass.
- ament_flake8 and ament_pep257 on both files: clean. This repo has no `.pre-commit-config.yaml` and no installed hook, so no pre-commit hooks ran; nothing was skipped with `--no-verify`.

### Not done (for the host)
- import_bag (`cube_bathymetry/src/import_bag_main.cpp`, single `tf_frontier_ns`, around line 2240/2386) still has the single frontier and the bag-time-free future handling; the per-chain gate and the per-frame offset are Python-only in this PR.
- `-o` writability is checked only at the end of the run, after every bag is read; an early check would save a long run that ends in the write error. Not asked; left as is.

### Next
review-code (re-review) on the branch at `ed40753`. Nothing pushed.

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-10-30 12:10 -04:00
**By**: Claude Code Agent (Claude Opus)
**Verdict**: changes-requested

**Branch**: feature/issue-170 at `87d90e9` (code at `ed40753`)
**Mode**: pre-push
**Depth**: Standard, targeted (reason: round 4 reviews only the round-3 fix commits, `git diff c21463e..ed40753`, 4 files, +415 -56)
**Must-fix**: 2 | **Suggestions**: 1
**Round**: 4 | **Ship**: continue — 2 must-fixes, both regressions this fix pass introduced (the per-frame clock offset and the overall-frontier release bound let one bad frame cost every ping of a good one), each reproduced with a PingLocator script; both need a small design choice, not just a guard, so one more targeted check after the fix

Specialists: static analysis (ament_flake8 and ament_pep257 on both Python files: clean); one Claude adversarial pass, done inline with reproduction scripts against this commit and against the round-3 code (`c21463e`); cross-model off (as asked); local model off; plan drift skipped (no plan.md). `./sensors_ws/test.sh cube_bathymetry`: 1241 tests, 0 errors, 0 failures, 90 skipped. Freshwater byte-identity tests and the new offset, chain, release and read-only-directory tests pass (`pytest -k`, 14 passed).

The 3 round-3 findings are resolved as described: the +90/+57/-30 s offset tests pass and a constant offset common to every frame changes nothing; the slow-link gate holds a ping until `earth -> map` passes it and the 57 s bound releases a stalled one; a writable file in a read-only directory is written in place with a warning, through a symlink too, and a new file there, a missing directory or a failed replace is a one-line error. Chain discovery from accepted transforms, the all-static chain (ready at once), the unconnected frame and the parent loop (both fall back to the overall frontier) behave as documented. The heap head-of-line wait is bounded by the 57 s release and documented in `drain`.

### Findings
- [x] (must-fix) A frame's first accepted transform becomes its clock reference whatever its stamp, and a glitch there loses every ping on that chain. Reproduced: a first `earth -> base_link` stamped 0 (an unset stamp, as a node publishing before its clock is set does) makes every later transform of that frame fail the 'future' test, 29 of 29 rejected, and every `mbes` ping drops as 'extrapolation'. The round-3 code (`c21463e`) kept all of them. A first transform one day ahead is kept as reference, its real transforms are refused as 'old', and it also moves the overall frontier a day ahead, so the 57 s release fires at once and the pings of an unrelated, healthy frame (`buoy -> mbes2`) drop too; `c21463e` refused that one transform as 'future' and kept every ping. The docstring at :432-436 says the real transforms are then left out as 'old', which is wrong for a low glitch (they are counted as 'future') and does not mention the spill-over to other frames. Fix: do not adopt a reference offset outside a sane bound (for example `future_limit_sec`, 1 h, which still passes the +90/-30 s cases): reject such a transform (count it as 'future', or treat a zero or negative stamp as 'invalid', as `push` already does for pings) and leave the reference unset until a transform within the bound arrives. Add tests for a zero-stamp and a day-ahead first transform, each with a second healthy frame. Then correct the docstring and README. — `cube_bathymetry/scripts/derive_angular_response.py:390` (and :450, :432)
- [ ] (must-fix) The 57 s release bound uses the overall frontier, the newest stamp of any frame, and with per-frame offsets that frontier can run ahead of a chain by the offset difference between frames. Reproduced: `earth -> base_link` at 1 Hz with offset 0 beside `earth -> buoy` at 10 Hz whose stamps lead the bag time by 90 s (a relayed buoy or another host whose clock is ahead, which the README now says is handled per frame) drops all 40 `mbes` pings as 'extrapolation'; at a 30 s lead it drops none. Before this pass the buoy's transforms were refused as 'future' and the vessel's pings survived. The same overall frontier feeds the unconnected-frame fallback in `chain_frontier`. Fix: release against the chain's own links, since tf2 prunes per frame: release a ping once the newest frontier among its chain's dynamic links is `cache - guard` past it (the fast link that is about to prune), keeping the overall frontier only for a chain that does not reach earth. Add a test with two frames whose offsets differ by more than 57 s. — `cube_bathymetry/scripts/derive_angular_response.py:507` (and :412)
- [ ] (suggestion) The in-place fallback's error after a failed write (`_write_in_place`) says only "cannot write <path>"; when `open` succeeded and the write failed part-way the file is now truncated, and the earlier warning is the only hint. Add "it may be truncated" to the error when the fallback path was taken. — `cube_bathymetry/scripts/derive_angular_response.py` `_write_in_place`

### Next
address-findings for the 2 must-fixes, then a targeted round-5 check of `PingLocator` only. Nothing pushed.
