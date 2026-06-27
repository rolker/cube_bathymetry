# Plan: CI: remove nlohmann_json workaround (unh_marine_autonomy#228 landed); decide marine_nav pruning

## Issue

https://github.com/rolker/cube_bathymetry/issues/76

## Context

PR #73 (#72) added two CI workarounds for latent packaging gaps in the
`unh_marine_autonomy` monorepo:

1. `nlohmann_json` was not a valid rosdep key (should be `nlohmann-json-dev`).
   CI skipped it via `ROSDEP_SKIP_KEYS` and the Dockerfile installed
   `nlohmann-json3-dev` explicitly to compensate.
2. `mission_manager*` / integration-tests / backscatter packages depend on
   `marine_nav_*` (a repo not in `upstream.repos`) and are unrelated to
   `cube_bathymetry` — they were pruned via `COLCON_IGNORE` to prevent the
   build from pulling an unrelated dependency.

`rolker/unh_marine_autonomy#228` merged 2026-06-27, fixing item 1 (`nlohmann_json`
→ `nlohmann-json-dev`). Item 2 is **not** a bug fix — it is intentional CI
scope pruning that should stay in place and be described accurately.

## Approach

### Step 1 — Drop the nlohmann_json workaround from ci.yml

In `.github/workflows/ci.yml`, remove `nlohmann_json` from `ROSDEP_SKIP_KEYS`,
leaving only `marine_nav_interfaces marine_nav_tasks`. Reframe the `ROSDEP_SKIP_KEYS`
+ `AFTER_SETUP_UPSTREAM_WORKSPACE` comment block to describe the **intentional
pruning** rationale (Option B, see below), dropping all references to "#228
pending" and "drop once #228 lands".

### Step 2 — Drop the explicit apt-get install from the Dockerfile

In `.github/ci/Dockerfile`, remove the explicit
`apt-get install -y --no-install-recommends nlohmann-json3-dev` line from the
rosdep bake `RUN` step. The Dockerfile's auto-skip pattern (`rosdep check`
→ `--skip-keys`) now detects no bad keys for `nlohmann-json-dev`, so it
resolves naturally through `rosdep install`. Update the surrounding comment to
remove the "#228-pending" language.

### Step 3 — Reframe marine_nav / COLCON_IGNORE comments as intentional pruning

Both files still reference `marine_nav_*` skips and the `COLCON_IGNORE` hook.
Update those comments explicitly to state: cube CI intentionally prunes
`mission_manager*/integration-tests/backscatter` because those packages are not
part of cube's build graph. The `marine_nav_*` rosdep skip-keys follow from that
pruning (rosdep does not honor `COLCON_IGNORE`). This is not a workaround — it
is a deliberate scope boundary.

## A-vs-B Design Fork — **Decision required at plan-review**

### Option A — Full-resolve (drop all skips + COLCON_IGNORE)

Add `unh_marine_navigation` to `upstream.repos` (or inherit it from
`unh_marine_autonomy`'s new `dependencies.repos`). Drop `marine_nav_interfaces` /
`marine_nav_tasks` from `ROSDEP_SKIP_KEYS`. Remove the `COLCON_IGNORE` hook.
The entire monorepo (including `mission_manager`, integration tests, backscatter)
builds in cube CI.

Drawback: cube CI pulls in packages it doesn't ship, increasing build time and
coupling cube's green CI to unrelated package health.

### Option B — Keep intentional pruning (recommended)

Keep `marine_nav_interfaces` / `marine_nav_tasks` in `ROSDEP_SKIP_KEYS` and keep
the `COLCON_IGNORE` hook. `unh_marine_navigation` is not cloned. cube CI stays
scoped to cube's own build graph. Reframe comments to say "intentional pruning",
not "#228 workaround".

**Recommendation: Option B.** The pruned packages are genuinely outside cube's
concern; building them provides no signal for cube correctness and adds noise.
The comment update in Step 3 makes the intent durable and auditable.

*This fork is the explicit checkpoint for plan-review — Roland confirms A or B
before implementation proceeds.*

## Files to Change

| File | Change |
|------|--------|
| `.github/workflows/ci.yml` | Drop `nlohmann_json` from `ROSDEP_SKIP_KEYS`; reframe workaround comment block to describe intentional pruning |
| `.github/ci/Dockerfile` | Drop explicit `apt-get install nlohmann-json3-dev`; update comment to remove #228-pending language |

## Principles Self-Check

| Principle | Consideration |
|---|---|
| Capture decisions, not just implementations | A-vs-B choice is documented in ci.yml comment block, not only in the PR description |
| A change includes its consequences | Dockerfile change triggers `ci-image.yml` rebuild of `ghcr.io/rolker/cube_bathymetry-ci:jazzy`; no bootstrap risk (image already exists) |
| Only what's needed | Option B keeps CI scoped to cube's subtree; no new dependency repos |
| Human control and transparency | Comments explicitly describe intentional pruning so a future reader understands the choice |

## ADR Compliance

| ADR | Triggered | How addressed |
|---|---|---|
| ADR-0001 (Adopt ADRs) | No | A-vs-B is a CI config choice; rationale in comments + PR, not an ADR |
| ADR-0013 (progress.md) | Yes | `## Plan Authored` entry will be appended after this plan is committed |

## Consequences

| If we change... | Also update... | Included in plan? |
|---|---|---|
| `.github/ci/Dockerfile` | `ci-image.yml` rebuilds image automatically on merge | Yes — noted; no action needed |
| `ROSDEP_SKIP_KEYS` in ci.yml | Dockerfile must not still install `nlohmann-json3-dev` explicitly (both changes go together) | Yes — Steps 1+2 are paired |

## Open Questions

- **A vs B**: Keep intentional pruning (Option B, recommended) or full-resolve
  by cloning `unh_marine_navigation` (Option A)? — gate for plan-review
  confirmation before implementation.

## Estimated Scope

Single PR — two small file edits, no code changes, no new tests required.
