#!/usr/bin/env bash
# Surface the real-bag smoke test's outcome as a named CI line
# (cube_bathymetry#143 / #162).
#
# `test_real_bag_smoke` drives the real import_bag / batch_regen_bag binaries
# over a 3,000-ping excerpt of a survey bag. The excerpt is survey data
# referenced by path, not a committed fixture, so inside a CI container it is
# normally unreachable and every case SKIPS. A skip is the correct outcome
# there -- but a skip buried in gtest output reads as a green package, and
# "the depth-adaptive path was never exercised" must not look like "it passed".
#
# Run by ci_local.sh from the workspace root after `colcon test` passes, with
# the overlay sourced (.agents/ci_local_extra.sh convention, ADR-0018).
# Set CUBE_REAL_BAG_EXCERPT to run it for real where the archive IS mounted.
set -uo pipefail

BIN="build/cube_bathymetry/test_real_bag_smoke"
if [[ ! -x "$BIN" ]]; then
  echo "ci-local extra: FAIL - $BIN is missing; the real-bag smoke test was not built"
  exit 1
fi

out="$("$BIN" 2>&1)"
rc=$?
if [[ $rc -ne 0 ]]; then
  echo "$out"
  echo "ci-local extra: FAIL - the real-bag smoke test failed (exit $rc)"
  exit $rc
fi

# Only the per-case lines (they end in a duration); the tail of gtest's output
# lists the skipped names again, and counting those doubles the tally.
skipped="$(grep -c '^\[  SKIPPED \].*ms)$' <<<"$out")"
ran="$(grep -c '^\[       OK \].*ms)$' <<<"$out")"
reason="$(grep -m1 'real-bag smoke test SKIPPED' <<<"$out" | sed 's/^ *//')"

if [[ "$skipped" -gt 0 ]]; then
  echo "ci-local extra: real-bag smoke test SKIPPED ($skipped case(s), $ran ran) - NOT a pass."
  echo "ci-local extra: reason: ${reason:-no excerpt reachable from this container}"
  echo "ci-local extra: the depth-adaptive main() path was NOT exercised in this run."
else
  echo "ci-local extra: real-bag smoke test RAN for real ($ran case(s) against the excerpt)."
fi
