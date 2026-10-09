#!/usr/bin/env bash
# Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
# Hydrographic Center, University of New Hampshire. MIT licence (see LICENSE).
#
# Start a fresh CUBE sheet on running cube_bathymetry nodes without restarting
# them (cube_bathymetry#164): walk each node through
# deactivate -> cleanup -> configure -> activate. See "Starting a fresh sheet
# without restarting" in the top-level README.
#
# Usage:
#   reset_cube_nodes.sh [--allow-persistence] [--consumer NODE]... NODE...
#
#   NODE                  a cube_bathymetry lifecycle node, e.g. /boat1/cube_bathymetry
#   --consumer NODE       a lifecycle node that accumulates state derived from the
#                         CUBE `grid` topic (the coverage assessor). It is reset
#                         while every CUBE node is quiesced, so it cannot absorb a
#                         stale window from a node that has not been reset yet.
#                         May be repeated.
#   --allow-persistence   reset even though a node has a non-empty `draft_dir`.
#                         Saved draft tiles are RELOADED on configure, so the sheet
#                         is then NOT fresh; without this flag the script refuses.
#
# Order: (0) read every CUBE node's draft_dir / prior_store_dir and refuse if any
# draft_dir is set; (1) every CUBE node to unconfigured; (2) every consumer to
# unconfigured and back to active; (3) every CUBE node to active. Fails fast,
# naming the node and the transition. An already-inactive or already-unconfigured
# node is not an error, so re-running after a failure is safe.
#
# Exit status: 0 reset done; 1 a transition or read failed, or refused because of
# draft_dir; 2 usage.

set -u -o pipefail

prog=$(basename "$0")

usage() {
  sed -n '/^# Usage:/,/^# Exit status/p' "$0" | sed 's/^# \{0,1\}//' >&2
}

die() {
  echo "$prog: $*" >&2
  exit 1
}

allow_persistence=0
consumers=()
nodes=()
while [ $# -gt 0 ]; do
  case "$1" in
    --allow-persistence) allow_persistence=1 ;;
    --consumer)
      [ $# -ge 2 ] || { echo "$prog: --consumer needs a node name" >&2; usage; exit 2; }
      consumers+=("$2")
      shift
      ;;
    -h | --help) usage; exit 0 ;;
    -*) echo "$prog: unknown option $1" >&2; usage; exit 2 ;;
    *) nodes+=("$1") ;;
  esac
  shift
done
[ ${#nodes[@]} -gt 0 ] || { echo "$prog: no cube_bathymetry node given" >&2; usage; exit 2; }

# First word of `ros2 lifecycle get`, e.g. "active [3]" -> active.
node_state() {
  local out
  out=$(ros2 lifecycle get "$1" 2>&1) || { echo "unreadable"; return 1; }
  echo "${out%% *}"
}

# transition NODE NAME EXPECTED_STATE: `ros2 lifecycle set` reports a refused
# transition without a reliable exit status, so the resulting state is re-read
# and compared instead.
transition() {
  local node=$1 name=$2 expected=$3 out state
  out=$(ros2 lifecycle set "$node" "$name" 2>&1)
  local rc=$?
  state=$(node_state "$node")
  if [ "$rc" -ne 0 ] || [ "$state" != "$expected" ]; then
    echo "$prog: $node: transition '$name' failed (state is '$state', expected '$expected')" >&2
    [ -n "$out" ] && echo "$prog: $node: ros2 said: $out" >&2
    print_states >&2
    exit 1
  fi
  echo "$node: $name -> $state"
}

to_unconfigured() {
  local node=$1 state
  while true; do
    state=$(node_state "$node")
    case "$state" in
      active) transition "$node" deactivate inactive ;;
      inactive) transition "$node" cleanup unconfigured ;;
      unconfigured) return 0 ;;
      *) die "$node: cannot read or walk from state '$state'" ;;
    esac
  done
}

to_active() {
  local node=$1 state
  while true; do
    state=$(node_state "$node")
    case "$state" in
      unconfigured) transition "$node" configure inactive ;;
      inactive) transition "$node" activate active ;;
      active) return 0 ;;
      *) die "$node: cannot read or walk from state '$state'" ;;
    esac
  done
}

# String parameter value of a node, or failure if it cannot be read.
string_param() {
  local out
  out=$(ros2 param get "$1" "$2" 2>&1) || return 1
  case "$out" in
    "String value is:"*) printf '%s\n' "${out#String value is:}" | sed 's/^ //' ;;
    *) return 1 ;;
  esac
}

print_states() {
  local node
  echo "$prog: states:"
  for node in "${nodes[@]}" "${consumers[@]}"; do
    echo "  $node: $(node_state "$node")"
  done
}

# 0. Refuse before changing anything if persistence would reload old tiles. The
# parameters are declared at configure, so a never-configured node is configured
# (harmless, it is about to be cycled) to make them readable.
for node in "${nodes[@]}"; do
  [ "$(node_state "$node")" = "unconfigured" ] && transition "$node" configure inactive
  draft_dir=$(string_param "$node" draft_dir) ||
    die "$node: cannot read parameter draft_dir (is the node running?)"
  prior_dir=$(string_param "$node" prior_store_dir) ||
    die "$node: cannot read parameter prior_store_dir"
  if [ -n "$draft_dir" ] && [ "$allow_persistence" -eq 0 ]; then
    die "$node: draft_dir is '$draft_dir'; saved draft tiles reload on configure, so the sheet would not be fresh. Nothing was reset. Clear it (ros2 param set $node draft_dir '') or pass --allow-persistence."
  fi
  [ -n "$draft_dir" ] && echo "$prog: note: $node draft_dir='$draft_dir': saved tiles will reload (--allow-persistence)"
  [ -n "$prior_dir" ] &&
    echo "$prog: note: $node prior_store_dir='$prior_dir' seeds a predicted surface, not coverage; it is kept"
done

# 1. Quiesce every CUBE node first.
for node in "${nodes[@]}"; do to_unconfigured "$node"; done

# 2. Consumers while no CUBE node is publishing.
for node in "${consumers[@]}"; do
  to_unconfigured "$node"
  to_active "$node"
done

# 3. Bring CUBE back up with a fresh sheet.
for node in "${nodes[@]}"; do to_active "$node"; done

print_states
