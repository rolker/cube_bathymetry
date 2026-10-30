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
- [ ] (suggestion) Without --remove-tl, --salinity and --ph warn, but --depth-m and --water-temp-c are ignored silently; make this consistent. — `cube_bathymetry/scripts/derive_angular_response.py:344`
- [ ] (suggestion) The median ping_info.frequency has no plausibility bound: kHz-in-Hz gives a near-zero alpha silently, and it now drives the salt-water alpha. Bound it to roughly 1 kHz..2 MHz. — `cube_bathymetry/scripts/derive_angular_response.py:657`
- [ ] (suggestion) --box only refuses kept==0. A box keeping a handful of pings writes a curve from thin bins (cube_lab uses kMinPerBin=200); warn or refuse below a minimum. — `cube_bathymetry/scripts/derive_angular_response.py:700`
- [ ] (suggestion) The CSV write is not atomic: open('w') truncates an existing good curve before writing, and a late encode error leaves a header-only file. Write to a temp file, then os.replace. — `cube_bathymetry/scripts/derive_angular_response.py:638`
- [ ] (suggestion) Record the compatibility deltas in the PR body: new header lines, water_temp_c %.3g to %.6g, newly refused inputs (temperature outside -2..40, NaN, negative depth, a comma or newline in a bag name), and the override accepting --water-temp-c (record-only), which differs from the issue text. Also document that all */tf topics merge into one buffer (multi-vehicle bags). — `README.md:742`
