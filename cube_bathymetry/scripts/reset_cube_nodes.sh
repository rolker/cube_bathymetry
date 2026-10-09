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
# Order: (0) preflight, changing nothing: every node and consumer must answer and
# be in a state the script can walk (active, inactive or unconfigured), and no
# CUBE node may have a `draft_dir` set (refused unless --allow-persistence); a
# CUBE node that has never been configured has no readable `draft_dir`, so it is
# configured, read and returned to unconfigured, one at a time, only after every
# other check has passed; (1) every CUBE node to unconfigured; (2) every consumer
# to unconfigured and back to active; (3) every CUBE node to active.
#
# Fails fast, naming the node and the transition. Once step 1 has begun a failure
# prints every node's state and tells you to re-run: a re-run is safe (an
# already-inactive or already-unconfigured node is not an error), and a CUBE node
# that is not `active` is NOT publishing `grid`. Every `ros2` call is bounded by
# `timeout --foreground` (so Ctrl-C still reaches `ros2`; plain `timeout` moves it
# into its own process group) (RESET_CUBE_GET_TIMEOUT, default 30 s, for reads;
# RESET_CUBE_SET_TIMEOUT, default 300 s, for transitions, because a configure with
# `draft_dir` set loads the saved tiles), so an unresponsive node fails the script
# instead of hanging it with boats half reset.
#
# SIGHUP and SIGPIPE are ignored: an ssh drop or a closed pipe must not kill the
# script between the quiesce and the bring-up. Run it under tmux or nohup anyway if
# the link is unreliable.
#
# Not for use underway: between steps 1 and 3 the named boats publish no `grid`.
#
# Exit status: 0 reset done; 1 a transition or read failed, or refused because of
# draft_dir; 2 usage.

set -u -o pipefail

prog=$(basename "$0")
get_timeout=${RESET_CUBE_GET_TIMEOUT:-30}
set_timeout=${RESET_CUBE_SET_TIMEOUT:-300}

usage() {
  sed -n '/^# Usage:/,/^# Exit status/p' "$0" | sed 's/^# \{0,1\}//' >&2
}

allow_persistence=0
consumers=()
nodes=()
while [ $# -gt 0 ]; do
  case "$1" in
    --allow-persistence) allow_persistence=1 ;;
    --consumer)
      # The value must be a node name: `--consumer --allow-persistence` would
      # otherwise swallow the flag and reset a node called "--allow-persistence".
      if [ $# -lt 2 ] || [ -z "$2" ] || [ "${2#-}" != "$2" ]; then
        echo "$prog: --consumer needs a node name, got '${2:-}'" >&2
        usage
        exit 2
      fi
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

# A dropped ssh session (SIGHUP) or a closed pipe (SIGPIPE) would otherwise kill
# the script mid-reset, leaving boats without `grid` and nothing printed.
trap '' HUP PIPE

# Set once a CUBE node of the reset may have been changed; decides what `die`
# tells the operator. The probe of a never-configured node (0c) is not a reset: it
# is tracked separately in `probing`, so a failing probe does not claim one.
changed=0
probing=""

# Read-only `ros2` call: stdout only (a stderr warning must not be parsed as
# data), bounded.
ros2_get() {
  timeout --foreground "$get_timeout" ros2 "$@" 2>/dev/null
}

# First word of `ros2 lifecycle get`, e.g. "active [3]" -> active.
node_state() {
  local out first state
  out=$(ros2_get lifecycle get "$1") || { echo "unreadable"; return 1; }
  first=${out%%$'\n'*}
  state=${first%% *}
  [ -n "$state" ] || { echo "unreadable"; return 1; }
  echo "$state"
}

print_states() {
  local node
  echo "$prog: states:"
  for node in "${nodes[@]}" "${consumers[@]}"; do
    echo "  $node: $(node_state "$node")"
  done
  # A closed stdout (SIGPIPE is ignored) makes the last echo fail; the reset is
  # done, and that must not become the script's exit status.
  return 0
}

die() {
  echo "$prog: $*" >&2
  if [ "$changed" -eq 1 ]; then
    print_states >&2
    echo "$prog: the reset did not finish. Re-run the same command to continue (a node that is already reset is simply cycled again); a CUBE node that is not 'active' is not publishing grid." >&2
  elif [ -n "$probing" ]; then
    print_states >&2
    echo "$prog: no CUBE node was reset, but $probing was being probed and may have been left 'inactive' (configured). Re-run the same command; it is read normally then." >&2
  else
    echo "$prog: nothing was changed." >&2
  fi
  exit 1
}

on_signal() {
  trap - INT TERM
  die "interrupted"
}
trap on_signal INT TERM

# transition NODE NAME EXPECTED_STATE: `ros2 lifecycle set` reports a refused
# transition without a reliable exit status, so the resulting state is re-read
# and compared instead.
transition() {
  local node=$1 name=$2 expected=$3 out rc state
  [ -n "$probing" ] || changed=1
  out=$(timeout --foreground "$set_timeout" ros2 lifecycle set "$node" "$name" 2>&1)
  rc=$?
  state=$(node_state "$node")
  if [ "$rc" -ne 0 ] || [ "$state" != "$expected" ]; then
    [ -n "$out" ] && echo "$prog: $node: ros2 said: $out" >&2
    die "$node: transition '$name' failed (state is '$state', expected '$expected'; exit $rc)"
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
  out=$(ros2_get param get "$1" "$2") || return 1
  case "$out" in
    "String value is:"*) printf '%s\n' "${out#String value is:}" | sed 's/^ //' ;;
    *) return 1 ;;
  esac
}

note_prior() {
  [ -n "$2" ] &&
    echo "$prog: note: $1 prior_store_dir='$2' seeds a predicted surface, not coverage; it is kept"
  return 0
}

refuse_draft() {
  die "$1: draft_dir is '$2'; saved draft tiles reload on configure, so the sheet would not be fresh. Point it at a NEW empty directory (ros2 param set $1 draft_dir /path/to/new/empty/dir) and pass --allow-persistence. Do not set it to '': that turns persistence and eviction off, so RAM grows on a long survey, and cleanup still flushes against the directory the node was configured with."
}

# 0a. Every node and consumer must answer and be walkable, before anything moves:
# a mistyped --consumer or a finalized node found after the CUBE nodes were
# already unconfigured would leave the boats without a grid.
for node in "${nodes[@]}" "${consumers[@]}"; do
  state=$(node_state "$node")
  case "$state" in
    active) ;;
    inactive | unconfigured)
      # A CUBE node that is not active is not publishing grid (an earlier run may
      # have stopped part way); a refusal below says "nothing was changed", which
      # is true of this run only.
      case " ${nodes[*]} " in
        *" $node "*)
          echo "$prog: warning: $node is '$state', not 'active': it is not publishing grid" >&2 ;;
      esac
      ;;
    *) die "$node: cannot read or walk from state '$state' (is the name right and the node running?)" ;;
  esac
done

# 0b. Refuse if persistence would reload old tiles. The parameters are declared
# at configure, so a node that was never configured has none to read yet: those
# are probed in 0c, after everything that can be checked without touching a node.
probe=()
for node in "${nodes[@]}"; do
  if draft_dir=$(string_param "$node" draft_dir); then
    prior_dir=$(string_param "$node" prior_store_dir) ||
      die "$node: cannot read parameter prior_store_dir"
    if [ -n "$draft_dir" ]; then
      [ "$allow_persistence" -eq 1 ] || refuse_draft "$node" "$draft_dir"
      echo "$prog: note: $node draft_dir='$draft_dir': saved tiles will reload (--allow-persistence)"
    fi
    note_prior "$node" "$prior_dir"
  elif [ "$(node_state "$node")" = "unconfigured" ]; then
    [ "$allow_persistence" -eq 1 ] || probe+=("$node")
  else
    die "$node: cannot read parameter draft_dir (is the node running?)"
  fi
done

# 0c. Never-configured nodes: configure, read, put back, one at a time. The node
# is returned to the state it was found in, so a refusal here has not reset
# anything.
for node in "${probe[@]}"; do
  probing=$node
  transition "$node" configure inactive
  draft_dir=$(string_param "$node" draft_dir)
  rc_draft=$?
  prior_dir=$(string_param "$node" prior_store_dir)
  rc_prior=$?
  transition "$node" cleanup unconfigured
  probing=""
  [ "$rc_draft" -eq 0 ] || die "$node: cannot read parameter draft_dir after configure"
  [ "$rc_prior" -eq 0 ] || die "$node: cannot read parameter prior_store_dir after configure"
  [ -z "$draft_dir" ] || refuse_draft "$node" "$draft_dir"
  note_prior "$node" "$prior_dir"
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
