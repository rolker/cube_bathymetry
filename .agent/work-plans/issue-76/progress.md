---
issue: 76
---

# Issue #76 — CI: remove nlohmann_json workaround (unh_marine_autonomy#228 landed); decide marine_nav pruning

## Issue Review
**Status**: complete
**When**: 2026-06-27 00:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Issue**: #76
**Comment**: (best-effort post follows this entry; not recorded inline)
**Scope verdict**: well-scoped

### Summary

Issue targets two CI config files in `cube_bathymetry` only:
- `.github/workflows/ci.yml` — drop `nlohmann_json` from `ROSDEP_SKIP_KEYS`; update comments
- `.github/ci/Dockerfile` — drop explicit `apt-get install nlohmann-json3-dev`; the auto-skip `rosdep check` pattern now resolves the correctly-named `nlohmann-json-dev` key naturally

The upstream blocker (`rolker/unh_marine_autonomy#228`) merged 2026-06-27 — no pending dependencies. One design fork remains: whether to keep `marine_nav_*` skip-keys + `COLCON_IGNORE` pruning (Option B, recommended) or go full-resolve by cloning `unh_marine_navigation` (Option A). The issue correctly defers that decision to plan-review.

### Principle Alignment

| Principle | Status | Notes |
|---|---|---|
| Capture decisions, not just implementations | Watch | The A-vs-B pruning choice should be captured in ci.yml comment block — not just in the PR description. The "Done when" already requires this; plan should make it explicit. |
| A change includes its consequences | OK | Issue correctly notes Dockerfile change triggers ci-image.yml rebuild; no additional consequences. |
| Only what's needed | OK | Option B (recommended) keeps CI focused on cube's subtree; Option A would pull in unrelated `mission_manager`/`marine_nav` build scope. |
| Human control and transparency | OK | Issue explicitly requires updating comments so remaining skips/COLCON_IGNORE reflect *intentional pruning* rather than workaround status. |
| Improve incrementally | OK | Small, focused CI maintenance change; single PR scope is appropriate. |

### ADR Applicability

| ADR | Triggered | Notes |
|---|---|---|
| ADR-0001 (Adopt ADRs) | No | A-vs-B is a CI configuration choice, not an architectural direction change. Rationale belongs in code comments + PR description, not an ADR. |
| ADR-0002 (Worktree isolation) | OK | Worktree already exists (`feature/issue-76`). |
| ADR-0013 (progress.md vocabulary) | Yes | This entry fulfills it. |
| Others | Not triggered | No ROS 2 packages, no Python packaging, no AGENTS.md, no Makefile changes. |

### Consequences

- Dockerfile change triggers a `ci-image.yml` rebuild of `ghcr.io/rolker/cube_bathymetry-ci:jazzy`. The image already exists so there is no bootstrap chicken-egg risk (per the CI ADR lesson in workspace memory). The image rebuild is automatic on merge.
- No downstream packages affected — CI-only change.

### Open Questions

- **A vs B fork** (flag for plan-review checkpoint): Issue recommends Option B (keep pruning). plan-task should explicitly present both options with recommendation, and the plan-review checkpoint is where Roland confirms the choice. No new dependency repos needed for Option B.

### Actions
- [ ] Ensure plan captures A-vs-B decision as an explicit checkpoint with recommendation rationale documented in ci.yml comments — not just in the PR.

## Plan Authored
**Status**: complete
**When**: 2026-06-27 15:00 +00:00
**By**: Claude Code Agent (Claude Sonnet)

**Plan**: `.agent/work-plans/issue-76/plan.md` at `7fc2a36`
**Branch**: feature/issue-76 at `7fc2a36`
**Phases**: single

### Open questions
- [ ] A vs B fork: keep intentional marine_nav pruning (Option B, recommended) or full-resolve by cloning unh_marine_navigation (Option A)? — gate for plan-review confirmation.
