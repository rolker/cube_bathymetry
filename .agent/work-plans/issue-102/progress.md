---
issue: 102
---

# Issue #102 — Read the angular-response curve from SonarInfo (auto-enable; supersedes CSV delivery from #81)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-07-16 15:47 -0400
**By**: Claude Code Agent (Claude Fable 5)
**Verdict**: changes-requested (all findings addressed in `1299508` same session)

**Branch**: feature/issue-102 (plan `3495ee1`, impl `~`, fixes `1299508`)
**Mode**: pre-push
**Depth**: Deep (reason: ~550 lines; default-behavior flip; lifecycle surface)
**Must-fix**: 5 | **Suggestions**: 5
**Round**: 1 | **Ship**: recommended — all must-fixes were diagnostics/docs/lifecycle-hygiene (no algorithm defects), applied and re-verified (459/459 tests + live-node smoke)

### Findings
- [x] (must-fix) failed explicit curve file silently swallowed in auto mode, suppressing warning AND SonarInfo fallback — node + import_bag (Lens A) → loud warning in every mode
- [x] (must-fix) import_bag usage() claimed 'default none' after the flip (Governance)
- [x] (must-fix) ADR-0007 addendum stale (default-none / CSV-only delivery claims) — amended per repo convention, decision + supersession recorded (Governance)
- [x] (must-fix) batch_regen silently accepted 'auto' as identity with stale help text — now explicitly rejected with bit-exactness guidance (Governance)
- [x] (must-fix/suggestion, cross-confirmed) sonar_info subscription not torn down on reconfigure-to-none / cleanup; ungated callback could touch stale sheet — reset unconditionally on configure + in on_cleanup (Lens A + Governance)
- [x] (suggestion) curveFromSonarInfo accepts non-finite curve points (breaks latch equality every heartbeat) — rejected + tested (Lens A)
- [x] (suggestion) none-mode never confirms curves are ignored — log-once in node, stdout note in import_bag (Lens A, plan spec)
- [x] (suggestion) rosbag2 playback is volatile → live node gets NO replayed sonar_info — documented at the subscription + guidance to import_bag (Governance)
- [x] (suggestion) late-producer mid-grid transition note (Governance)
- [x] (suggestion) duplicate import_bag target_link_libraries folded (Governance)

Cleared by review: apply_ara precedence parenthesization; latch baseline copy-before-move; reject-before-write in curveFromSonarInfo; deriveSonarInfoTopic edge cases; pre-pass filter cost (topic-filtered, first-hit early exit); single-threaded executor sequencing of the mid-run setBackscatterCorrection; bs_* reset on reconfigure; MIT headers; test matrix matches validator branches. Known gap (accepted): no node-level test of the precedence/latch branches — covered by the live smoke instead.

Verification: 459/459 package tests; live smoke — lifecycle node auto-adopts a published tier-2 curve (correct alpha in the accept log) and warns-then-ignores a differing mid-run curve.
