---
issue: 61
---

# Issue #61 — Remove nlohmann_json workaround (follow-up to #57 / UMA#203)

## Local Review (Pre-Push)
**Status**: complete
**When**: 2026-06-21 12:00 -0400
**By**: Claude Code Agent (Claude Opus 4.8 (1M context))
**Verdict**: approved
**Round**: 1
**Ship**: recommended

**Branch**: feature/issue-61 at `45dd673`
**Mode**: pre-push
**Depth**: Light (reason: remove 2 workaround lines; build-config only)
**Must-fix**: 0 | **Suggestions**: 0

Host self-review. Removed find_package(nlohmann_json) + nlohmann-json-dev depend (the #57
workaround for the store's unexported dep, fixed by UMA#203). Verified cube_bathymetry +
import_bag + cube_bathymetry_store_import build clean against the fixed store export; 296
gtests pass. Proves UMA#203 unblocked the consumer.

### Findings
- [ ] No issues found. LGTM.
