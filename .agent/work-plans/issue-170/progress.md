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
