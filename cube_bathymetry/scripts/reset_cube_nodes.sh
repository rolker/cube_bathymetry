#!/usr/bin/env bash
# Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
# Hydrographic Center, University of New Hampshire. MIT licence (see LICENSE).
#
# Start a fresh CUBE sheet on running cube_bathymetry nodes without restarting
# them (cube_bathymetry#164, #167). A node that serves the std_srvs/Trigger
# `<node>/reset` service is reset with ONE `ros2 service call`: its session state is
# replaced in RAM and nothing on disk changes. Any other node (an older binary, an
# unconfigured node, one that has a draft_dir and was passed --allow-persistence) is
# walked through deactivate -> cleanup -> configure -> activate, as before. See
# "Starting a fresh sheet without restarting" in the top-level README.
#
# Usage:
#   reset_cube_nodes.sh [--allow-persistence] [--consumer NODE]... NODE...
#
#   NODE                  a cube_bathymetry lifecycle node, e.g. /boat1/cube_bathymetry
#   --consumer NODE       a lifecycle node that accumulates state derived from the
#                         CUBE `grid` topic (the coverage assessor). It is reset
#                         only after every CUBE node has answered (see Order).
#                         May be repeated.
#   Each node is named once: a node given twice, or as both NODE and --consumer
#   (`/a/b`, `a/b`, `a/b/` and `//a//b` are the same node), is a usage error.
#   --allow-persistence   reset even though a node has a non-empty `draft_dir`.
#                         Saved draft tiles are RELOADED on configure, so the sheet
#                         is then NOT fresh; without this flag the script refuses.
#                         Every CUBE node then takes the lifecycle walk (the service
#                         refuses while draft_dir is set); consumers are unaffected.
#
# Order. (0) Classify, changing nothing: ONE `ros2 service list` tells which nodes
# serve `<node>/reset`. A node that is in a non-empty list is `service` class; a node
# that is not is `walk` class (older binary, never-configured node, a mistyped name,
# or discovery lag, which is safe: the node merely takes the walk). An empty list or
# a failed call is "indeterminate", retried like a read and, if it stays so, the
# script stops before any change; it is never read as "no node has a service".
# (0a-0c) The lifecycle preflight, for EVERY walk-class CUBE node and EVERY walk-class
# consumer, before the first service call: each must answer and be in a state the
# script can walk (active, inactive or unconfigured), no walk-class CUBE node may
# have a `draft_dir` set (refused unless --allow-persistence), and a walk-class CUBE
# node that has never been configured has no readable `draft_dir`, so it is
# configured, read and returned to unconfigured, one at a time, only after every
# other check has passed. A mistyped --consumer, a finalized node or a refusing
# walk-class node is therefore found before any boat is reset. (1) Every
# service-class CUBE node is reset, in the order given; a refusal (the node has a
# draft_dir) stops the script at that node. (2) Every walk-class CUBE node to
# unconfigured. (3) Every consumer: a service-class consumer by `service call`
# (if it refuses, for instance because the map frame moved, the reason is printed
# and it takes the walk, which captures the georeference again), a walk-class one to
# unconfigured and back to active. (4) Every walk-class CUBE node to active.
# Service-class CUBE nodes go first so that a draft_dir refusal, which only the
# service can report, is found before any boat has left `active`.
#
# What a refusal leaves behind. The service refuses inside the node, so a boat with
# a draft_dir set is found when the script reaches it, not in a preflight: the
# service-class boats before it in the list are already reset (a reset only replaces
# RAM state, so this is idempotent and costs nothing on disk). The script stops at the
# FIRST refusal, prints which nodes were reset and which refused, does not touch the
# walk-class nodes or the consumers, and exits 1; re-running it is safe.
#
# `ros2` calls (each is a CLI start-up, 3 to 8 s under load, which is where the time
# goes; the transitions themselves are quick):
#   classification                          1  `service list`, shared by all nodes
#   a service node (CUBE or consumer)       1  `service call`; no `lifecycle get`, no
#                                              `param get`, no final state listing:
#                                              the service refuses on draft_dir itself
#                                              and reports its own state
#   a walk node (as before)                 about 17 per CUBE node, about 14 per
#                                           consumer, plus the final state listing
#   four boats and the assessor, all on services: 6 calls, against about 80.
#
# Ordering, honestly. The walk quiesced every CUBE node before a consumer was reset,
# so no `grid` could cross the reset in either direction. With services the CUBE
# nodes keep publishing. (i) A `grid` that an old sheet published just before its
# reset, and that is still queued in the consumer's subscription (reliable, depth 10,
# at most about one per node per 5 s) when the consumer is reset, is folded into the
# empty union and stays there (unions only grow). (ii) The other direction: cells that
# a reset CUBE node accumulates before the consumer is reset are wiped from the
# union, and return only through that node's next published 200 m window (about 5 s
# later), so cells outside the window are lost. Both are the CLI gap between the CUBE
# call and the consumer call. The script relies on two things: the consumer is reset
# only after every CUBE node has answered (which bounds (i)), and the boats are NOT
# UNDERWAY (which makes (ii) harmless). A stamp guard against (i) is possible but is
# not made: it needs the sonar stamps and the consumer's clock to share a time base
# across hosts, and it does nothing for (ii). A consumer that takes the walk is
# immune: its subscription is destroyed.
#
# A node that answers "inactive" (configured, not activated: an earlier run or a
# failed probe left it so) was reset but would publish no `grid` (CUBE) or assess
# nothing (consumer): the script then ACTIVATES it, right after its reset, so every
# run ends with every node active, and re-running the same command after a failure
# recovers a node that failure left inactive. It is reported by name. (A walk always
# ends active.)
#
# Fails fast, naming the node and the step. Once a walk transition or a service call
# has changed something, a failure says what was done and tells you to re-run: a
# re-run is safe (a service reset is idempotent; an already-inactive or already-
# unconfigured node is not an error for the walk) and it converges: a node that the
# failure left `inactive` is configured, so it serves ~/reset, and the re-run resets
# it and activates it. Until then a CUBE node that is not `active` is NOT publishing
# `grid`. Every `ros2` call is bounded by
# `timeout --foreground` (so Ctrl-C still reaches `ros2`; plain `timeout` moves it
# into its own process group): RESET_CUBE_GET_TIMEOUT (default 30 s) for reads,
# RESET_CUBE_SET_TIMEOUT (default 300 s) for transitions, because a configure with
# `draft_dir` set loads the saved tiles, and RESET_CUBE_CALL_TIMEOUT for a service
# call, because the reset reads the prior store from disk: its default is the
# transition bound (300 s), so a prior that the walk's configure can load within its
# bound is not failed by the service path. An unresponsive node fails the script
# instead of hanging it with boats half reset.
#
# A READ (`ros2 service list`, `ros2 lifecycle get`, `ros2 param get`) that fails or
# times out is retried, RESET_CUBE_GET_TRIES attempts in all (default 3) with
# RESET_CUBE_RETRY_PAUSE seconds between them (default 2): under a heavily loaded
# system one service can miss its turn once and answer the next, and a cold ROS 2
# daemon answers `service list` with an empty list at first. Reads change nothing,
# so a retry is always safe. A SERVICE CALL that FAILS FAST (the CLI exits non-zero, or
# answers with no Trigger response) is retried too, unlike a transition, because a
# reset only replaces RAM state and so is idempotent. It has its own budget,
# RESET_CUBE_CALL_TRIES (default 2). A call that TIMES OUT (exit 124) is NOT retried:
# the first request is still queued in the node, and a retry would queue a second
# whole-store prior load behind it with the node's executor blocked again, so the
# script stops and says the node may have been reset. Whether a node HAS the
# service is decided only by the classification, never by the outcome of a call:
# a slow node is never sent down the walk after it already reset. A transition is
# NEVER retried: it fails fast, because a repeated configure or cleanup is not the
# same operation as the one that failed. Only after every attempt has failed does a
# read or a call fail the script, and the preflight reads do so before anything has
# been changed.
#
# SIGHUP and SIGPIPE are ignored: an ssh drop or a closed pipe must not kill the
# script between the quiesce and the bring-up. Run it under tmux or nohup anyway if
# the link is unreliable.
#
# Not for use underway: between the quiesce and the bring-up a walked boat
# publishes no `grid`, and a service reset blocks the node's executor while it
# reads the prior store (pings arriving meanwhile are dropped).
#
# Exit status: 0 reset done, every node active; 1 a transition, read or service call
# failed, or a CUBE node refused (a consumer that refuses is walked instead); 2 usage.

set -u -o pipefail

prog=$(basename "$0")
get_timeout=${RESET_CUBE_GET_TIMEOUT:-30}
set_timeout=${RESET_CUBE_SET_TIMEOUT:-300}
get_tries=${RESET_CUBE_GET_TRIES:-3}
retry_pause=${RESET_CUBE_RETRY_PAUSE:-2}
call_timeout=${RESET_CUBE_CALL_TIMEOUT:-$set_timeout}
call_tries=${RESET_CUBE_CALL_TRIES:-2}

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

# Each node once. A CUBE node also given as a consumer would be reactivated in
# step 2 and publish `grid` while the remaining consumers are still being reset,
# which is exactly what the ordering exists to prevent; a node given twice would
# be cycled twice. Compared fully qualified, as ROS names the node.
qualified() {
  local n
  n=$(printf '%s' "$1" | tr -s /)
  while [ "${#n}" -gt 1 ] && [ "${n%/}" != "$n" ]; do n=${n%/}; done
  [ "${n#/}" = "$n" ] && n="/$n"
  echo "$n"
}
declare -A listed_as=()
list_once() {
  local key
  key=$(qualified "$1")
  if [ -n "${listed_as[$key]:-}" ]; then
    echo "$prog: $key is given twice (as ${listed_as[$key]} and as $2); name each node once. Nothing was changed." >&2
    exit 2
  fi
  listed_as[$key]=$2
}
for node in "${nodes[@]}"; do list_once "$node" "a CUBE node"; done
for node in "${consumers[@]}"; do list_once "$node" "a --consumer"; done
# From here on every name is the fully qualified one, so `ros2` is given the
# spelling ROS uses whatever form was typed.
for i in "${!nodes[@]}"; do nodes[i]=$(qualified "${nodes[i]}"); done
for i in "${!consumers[@]}"; do consumers[i]=$(qualified "${consumers[i]}"); done

# A dropped ssh session (SIGHUP) or a closed pipe (SIGPIPE) would otherwise kill
# the script mid-reset, leaving boats without `grid` and nothing printed.
trap '' HUP PIPE

# Set once a CUBE node of the reset may have been changed; decides what `die`
# tells the operator. The probe of a never-configured node (0c) is not a reset: it
# is tracked separately in `probing`, so a failing probe does not claim one.
changed=0
probing=""
# Service resets done so far (node names), and what the last service call said.
reset_done=()
svc_message=""
# The node whose ~/reset call is in flight or gave no usable answer: its request may
# have reached it (a timeout says nothing about that), so `die` must not claim that
# nothing was changed. Empty once a call has been answered.
calling=""
# Nodes that take (or fell back to) the lifecycle walk: the ones whose state is
# listed at the end and after a failure. A service node's state is its own message.
walked=()

# Read-only `ros2` call: stdout only (a stderr warning must not be parsed as
# data), each attempt bounded, retried up to $get_tries attempts in all. Never used
# for a transition.
ros2_get() {
  local attempt=1 out rc
  while true; do
    out=$(timeout --foreground "$get_timeout" ros2 "$@" 2>/dev/null)
    rc=$?
    if [ "$rc" -eq 0 ]; then
      printf '%s\n' "$out"
      return 0
    fi
    [ "$attempt" -lt "$get_tries" ] || return "$rc"
    attempt=$((attempt + 1))
    sleep "$retry_pause"
  done
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
  [ "${#walked[@]}" -gt 0 ] || return 0
  echo "$prog: states:"
  for node in "${walked[@]}"; do
    echo "  $node: $(node_state "$node")"
  done
  # A closed stdout (SIGPIPE is ignored) makes the last echo fail; the reset is
  # done, and that must not become the script's exit status.
  return 0
}

die() {
  echo "$prog: $*" >&2
  if [ "${#reset_done[@]}" -gt 0 ]; then
    echo "$prog: reset before this: ${reset_done[*]}" >&2
  fi
  if [ -n "$calling" ]; then
    echo "$prog: $calling: the ~/reset request may have reached it, so it may or may not be reset." >&2
  fi
  if [ "$changed" -eq 1 ]; then
    print_states >&2
    echo "$prog: the reset did not finish. Re-run the same command to continue (a node that is already reset is simply reset or cycled again; a node left 'inactive' is reset and then activated); a CUBE node that is not 'active' is not publishing grid." >&2
  elif [ -n "$probing" ]; then
    print_states >&2
    echo "$prog: no CUBE node was reset, but $probing was being probed and may have been left 'inactive' (configured). Re-run the same command: a configured node serves ~/reset, so the re-run resets it by service and then activates it." >&2
  elif [ "${#reset_done[@]}" -gt 0 ] || [ -n "$calling" ]; then
    echo "$prog: nothing else was changed. Re-running is safe: a service reset only replaces RAM state." >&2
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

# One `ros2 service list`, retried like a read. An EMPTY answer is indeterminate
# (a cold daemon starts with an empty graph; a real graph always lists at least the
# parameter services of every node), so it is retried and, if it stays empty, fails:
# it must never be read as "no node has a reset service".
service_list=""
load_service_list() {
  local attempt=1 out rc
  while true; do
    out=$(timeout --foreground "$get_timeout" ros2 service list 2>/dev/null)
    rc=$?
    if [ "$rc" -eq 0 ] && [ -n "$out" ]; then
      service_list=$out
      return 0
    fi
    [ "$attempt" -lt "$get_tries" ] || return 1
    attempt=$((attempt + 1))
    sleep "$retry_pause"
  done
}

has_reset_service() {
  printf '%s\n' "$service_list" | grep -Fxq -- "$1/reset"
}

# service_reset NODE: one `ros2 service call NODE/reset std_srvs/srv/Trigger '{}'`.
# A call that fails fast is retried up to $call_tries attempts, because a reset is
# idempotent; a call that times out (124) is not, because its request is still queued
# in the node (see the header). Sets $svc_message from the response. Returns 0 on
# success=True, 1 on a refusal (success=False), 2 when no attempt gave a usable answer. The response is read from
# the line `std_srvs.srv.Trigger_Response(success=..., message=...)` and nothing else:
# a message may echo a user-supplied path, so a bare "success=True" substring means
# nothing.
service_reset() {
  local node=$1 attempt=1 out rc line verdict
  calling=$node
  while true; do
    out=$(timeout --foreground "$call_timeout" ros2 service call "$node/reset" \
      std_srvs/srv/Trigger '{}' 2>/dev/null)
    rc=$?
    if [ "$rc" -eq 0 ]; then
      line=$(printf '%s\n' "$out" | grep -m1 '^std_srvs\.srv\.Trigger_Response(success=')
      # The verdict is read from the START of the line only: the message after it
      # may echo a user-supplied path, such as draft_dir='/data/(success=True,x)',
      # and a match anywhere in the line would read that refusal as a success.
      verdict=""
      case "$line" in
        "std_srvs.srv.Trigger_Response(success=True,"*) verdict=0 ;;
        "std_srvs.srv.Trigger_Response(success=False,"*) verdict=1 ;;
      esac
      if [ -n "$verdict" ]; then
        svc_message=$(printf '%s\n' "$line" |
          sed -E "s/^std_srvs\.srv\.Trigger_Response\(success=(True|False), message=(.*)\)\$/\2/" |
          sed -E "s/^'(.*)'\$/\1/; s/^\"(.*)\"\$/\1/")
        calling=""
        return "$verdict"
      fi
    fi
    # Exit 124 is a timeout: the node may still be working on the first request.
    [ "$rc" -ne 124 ] || return 2
    [ "$attempt" -lt "$call_tries" ] || return 2
    attempt=$((attempt + 1))
    sleep "$retry_pause"
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

# 0. Classify. One call, only when it can matter: with --allow-persistence and no
# consumer every node takes the walk and nothing needs the list.
svc_cube=()
walk_cube=()
consumer_class=()   # parallel to consumers: service | walk
if [ "$allow_persistence" -eq 0 ] || [ "${#consumers[@]}" -gt 0 ]; then
  load_service_list ||
    die "cannot read the list of services (ros2 service list failed or came back empty $get_tries times; is ROS_DOMAIN_ID right and the daemon up?)"
fi
for node in "${nodes[@]}"; do
  if [ "$allow_persistence" -eq 0 ] && has_reset_service "$node"; then
    svc_cube+=("$node")
  else
    walk_cube+=("$node")
  fi
done
for node in "${consumers[@]}"; do
  if has_reset_service "$node"; then consumer_class+=(service); else consumer_class+=(walk); fi
done
walk_consumers=()
for i in "${!consumers[@]}"; do
  [ "${consumer_class[i]}" = walk ] && walk_consumers+=("${consumers[i]}")
done
walked=("${walk_cube[@]}" "${walk_consumers[@]}")
if [ "${#walked[@]}" -gt 0 ]; then
  echo "$prog: lifecycle walk (no ~/reset service): ${walked[*]}"
fi

# 0a. Every walk-class node and consumer must answer and be walkable, before
# anything moves: a mistyped --consumer or a finalized node found after the CUBE
# nodes were already reset would leave the boats half done.
for node in "${walked[@]}"; do
  state=$(node_state "$node")
  case "$state" in
    active) ;;
    inactive | unconfigured)
      # A CUBE node that is not active is not publishing grid (an earlier run may
      # have stopped part way); a refusal below says "nothing was changed", which
      # is true of this run only.
      case " ${walk_cube[*]} " in
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
# (A service-class node needs no read: the service refuses on draft_dir itself.)
probe=()
for node in "${walk_cube[@]}"; do
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

# Nodes that answered "inactive" and were activated after their reset.
activated_nodes=()

# service_node NODE: reset it by service, and activate it if it answered inactive.
# Returns 0 when reset, 1 when it refused ($svc_message says why); dies when no
# attempt gave a usable answer or the activation failed.
service_node() {
  local node=$1 rc
  service_reset "$node"
  rc=$?
  case "$rc" in
    0)
      reset_done+=("$node")
      echo "$node: reset: $svc_message"
      # The node reports its state in the LAST "; node is <state>" clause of the
      # message. An earlier clause can be text the node echoes (a prior_store_dir
      # path, an exception message), so match the tail, never the whole message.
      case "${svc_message##*; node is }" in
        "inactive"*)
          # Configured but not activated: reset, and now activated, so that this
          # command leaves it publishing and a re-run after a failure recovers it.
          echo "$prog: $node was inactive (not publishing): reset, now activating it" >&2
          walked+=("$node")
          to_active "$node"
          activated_nodes+=("$node")
          ;;
      esac
      return 0
      ;;
    1) return 1 ;;
    *)
      die "$node: ~/reset gave no usable answer (timed out after $call_timeout s, or $call_tries failed attempt(s)). The node may be hung, or its service may be listed only in the daemon's cache. It was not walked, and a timed-out call is not repeated: the request may still be queued in the node."
      ;;
  esac
}

# 1. Service-class CUBE nodes, in the order given. The first refusal stops the
# script: nothing after it is touched and nothing has left `active`.
for node in "${svc_cube[@]}"; do
  service_node "$node" || die "$node: ~/reset answered: $svc_message"
done

# 2. Walk-class CUBE nodes down.
for node in "${walk_cube[@]}"; do to_unconfigured "$node"; done

# 3. Consumers, once every CUBE node has answered (or is quiesced).
for i in "${!consumers[@]}"; do
  node=${consumers[i]}
  if [ "${consumer_class[i]}" = service ]; then
    if service_node "$node"; then
      continue
    fi
    # A refusal (the map frame moved, say) falls back to the walk, which captures
    # the georeference again: it is the superset.
    echo "$prog: $node: ~/reset answered: $svc_message -- taking the lifecycle walk instead" >&2
    walked+=("$node")
  fi
  to_unconfigured "$node"
  to_active "$node"
done

# 4. Walk-class CUBE nodes back up with a fresh sheet.
for node in "${walk_cube[@]}"; do to_active "$node"; done

print_states
if [ "${#activated_nodes[@]}" -gt 0 ]; then
  echo "$prog: reset done; activated after the reset (they were inactive): ${activated_nodes[*]}"
fi
