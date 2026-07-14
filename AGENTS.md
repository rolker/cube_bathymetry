# AGENTS.md — cube_bathymetry

Instructions for AI agents working in this repository — including **GitHub
Copilot code review**, which reads this file when reviewing PRs. There is no
`.agents/README.md` deep guide yet (the workspace convention for project
agent guides — distinct from the `.agent/work-plans/` directory); start from
the top-level `README.md` and `docs/` (including this repo's ADRs under
`docs/decisions/`).

## Workspace Rules

This repo is developed inside a
[ROS 2 Agent Workspace](https://github.com/rolker/ros2_agent_workspace).
The workspace root `AGENTS.md` carries the full shared rules (worktree
isolation, issue-first policy, commit conventions, AI signatures). This file
**references** those rules and adds repo-specific context only — it must
never restate or fork them.

## Quality Standard

This is software for autonomous robot boats operating on open water.
Robustness is not optional.

- Fix bugs completely: add the test, handle the edge case, check the
  lifecycle transition.
- Concerns about error handling, silent failures, stale data, or missing
  validation are not nits — flag them unless the failure mode genuinely
  cannot occur. "Config is under our control" and "pathological input" are
  not blanket dismissals; field configs change under pressure.
- A change includes its consequences: tests, documentation, and dependent
  references update in the same PR.

## Reviewing PRs

- If the PR carries a work plan (`.agent/work-plans/issue-<N>/plan.md` or a
  plan in the PR body), the plan is kept **in sync with the implementation
  as it evolves** — an implementation that matches the current plan text is
  not "plan drift", even if the plan changed after the PR opened.
- Verify claims against source: parameters, topics, services, and message
  types in docs must match the code.

## Review Context — cube_bathymetry

- **This implements CUBE** (Brian Calder's bathymetric estimation
  algorithm): changes to estimator behavior need methodological
  justification against the algorithm's published semantics, not just green
  tests — algorithm fidelity is a review dimension of its own.
- **Numerical/statistical code — watch signs and units**: depths vs
  elevations, dB conventions, and transmission-loss corrections have all
  produced sign bugs before; intensity statistics ride Welford accumulators
  where order-independence and merge semantics matter.
- **Eviction that bounds memory must never lose survey data**: draft-tile
  eviction persists to the durable store *before* dropping from RAM; the
  tile store is the product of record for live coverage.
- **CI is industrial_ci with a prebaked GHCR deps image**:
  `.github/workflows/ci.yml` (build/test), `.github/workflows/ci-image.yml`
  (image publish), and `.github/ci/Dockerfile` must stay in sync —
  dependency changes usually touch more than one.
