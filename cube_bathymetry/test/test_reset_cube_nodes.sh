#!/usr/bin/env bash
# Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
# Hydrographic Center, University of New Hampshire. MIT licence (see LICENSE).
#
# Tests scripts/reset_cube_nodes.sh against a stub `ros2` on PATH that keeps one
# lifecycle state per node (cube_bathymetry#164, #167). The stub also serves
# `service list` and `service call <node>/reset` in the output format of a real
# jazzy `ros2 service call` of a std_srvs/Trigger service. Cases 1-12 run on the
# walk path (the stub's default `service list` holds no `/reset`); 13-22 are the
# service path of cube_bathymetry#167. Cases:
#   1. consumer ordering: every CUBE node is quiesced before a consumer is touched,
#      and the consumer is back up before any CUBE node is configured.
#   2. a non-empty draft_dir is refused, and nothing is changed.
#   3. a failing transition fails fast, naming the node and the transition, and
#      (after the first change) prints every node's state and a re-run hint.
#   4. preflight: a mistyped consumer, or a finalized CUBE node, is found before
#      any transition runs.
#   5. `--consumer` does not swallow a following flag.
#   6. a never-configured node is probed after all other checks and put back; a
#      draft_dir found only by that probe refuses without resetting anything.
#   7. an unresponsive node fails the script within the timeout, before any change.
#   8. a stderr warning from `ros2` is not parsed as data.
#   9. every `ros2` call goes through `timeout --foreground`; SIGHUP and SIGPIPE
#      do not kill the script mid-reset.
#  10. a failing probe is not reported as a failed reset; a CUBE node that is not
#      active is warned about in the preflight.
#  11. a node given twice, or as both a CUBE node and a consumer (in any spelling
#      of its name), is a usage error before anything is read or changed; a node
#      typed in another spelling is handed to `ros2` fully qualified.
#  12. a read (lifecycle get, param get) that fails twice and then answers does not
#      abort the reset; one that fails every time does, after the bounded number
#      of attempts and before any change; a transition is never retried.
#  13. all-service fleet: one `service list` plus one `service call` per node (6 calls
#      for four boats and the assessor), no lifecycle get/set, no param get, the
#      consumer after every CUBE node, every call through `timeout --foreground`.
#  14. a CUBE node that refuses (draft_dir; its message echoes "success=True"): the
#      script stops at the first refusal, prints what was reset, leaves later nodes
#      and the consumer alone, exits 1.
#  14b. a refusal that echoes "(success=True," in its message is still a refusal.
#  15. nodes with no service take the walk; a mixed fleet (two services, two walk,
#      a consumer on a service) runs in the documented order.
#  16. --allow-persistence sends every CUBE node down the walk, the consumer is
#      classified normally, and with no consumer `service list` is not even called.
#  17. a consumer that refuses falls back to the walk, after the reason is printed.
#  18. an empty, failing or hanging `service list` is retried, then stops the script
#      before any change; it never selects the walk.
#  19. a `service call` that fails fast is retried (own budget), one that times out
#      is not, and neither selects the walk; one that answers after a retry is a
#      success. The call timeout defaults to the transition timeout.
#  20. everything that can refuse runs before the first service call: a mistyped
#      consumer, a finalized walk-class node, a draft_dir on a walk-class node.
#  21. a node that answers "inactive" is reset and then activated (exit 0); a failing
#      activation is a failure with the re-run hint.
#  22. re-running the same command recovers a node that a failed walk, or a failed
#      probe of a never-configured node, left inactive.
#
# Usage: test_reset_cube_nodes.sh /path/to/reset_cube_nodes.sh

set -u

script=${1:?usage: $0 /path/to/reset_cube_nodes.sh}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

mkdir -p "$work/bin" "$work/state" "$work/param"
cat > "$work/bin/ros2" <<'STUB'
#!/usr/bin/env bash
# Stub ros2: lifecycle get/set and param get over files under $STUB_DIR.
key() { echo "${1//\//_}"; }
case "$1 $2" in
  "lifecycle get")
    echo "lifecycle get $3" >> "$STUB_DIR/reads"
    if [ -e "$STUB_DIR/flaky_state_$(key "$3")" ]; then
      n=$(cat "$STUB_DIR/flaky_state_$(key "$3")")
      if [ "$n" -gt 0 ]; then
        echo $((n - 1)) > "$STUB_DIR/flaky_state_$(key "$3")"
        echo "stub: lifecycle service did not answer" >&2
        exit 1
      fi
    fi
    s=$(cat "$STUB_DIR/state/$(key "$3")" 2>/dev/null) || { echo "Node not found"; exit 1; }
    [ -e "$STUB_DIR/hang_$(key "$3")" ] && exec sleep 30
    [ -e "$STUB_DIR/noise" ] && echo "[WARN] [rcl]: stub deprecation warning" >&2
    case "$s" in unconfigured) id=1 ;; inactive) id=2 ;; active) id=3 ;; finalized) id=4 ;; esac
    echo "$s [$id]"
    ;;
  "lifecycle set")
    node=$3 t=$4 f="$STUB_DIR/state/$(key "$3")"
    echo "$node $t" >> "$STUB_DIR/log"
    [ -e "$STUB_DIR/slow" ] && sleep 0.4
    if [ -e "$STUB_DIR/fail_$(key "$node")_$t" ]; then
      echo "Transition failed"   # exit status 0, like the real verb can
      exit 0
    fi
    s=$(cat "$f")
    case "$s:$t" in
      active:deactivate) echo inactive > "$f" ;;
      inactive:cleanup) echo unconfigured > "$f" ;;
      unconfigured:configure)
        echo inactive > "$f"
        # parameters are declared at configure: publish the ones the test staged
        for staged in "$STUB_DIR"/onconfigure_"$(key "$node")"_*; do
          [ -e "$staged" ] || continue
          cp "$staged" "$STUB_DIR/param/${staged##*/onconfigure_}"
        done
        ;;
      inactive:activate) echo active > "$f" ;;
      *) echo "Transition failed"; exit 0 ;;
    esac
    echo "Transitioning successful"
    ;;
  "param get")
    echo "param get $3 $4" >> "$STUB_DIR/reads"
    if [ -e "$STUB_DIR/flaky_param_$(key "$3")_$4" ]; then
      n=$(cat "$STUB_DIR/flaky_param_$(key "$3")_$4")
      if [ "$n" -gt 0 ]; then
        echo $((n - 1)) > "$STUB_DIR/flaky_param_$(key "$3")_$4"
        echo "stub: timed out waiting for the parameter service" >&2
        exit 1
      fi
    fi
    v=$(cat "$STUB_DIR/param/$(key "$3")_$4" 2>/dev/null) || { echo "Parameter not set."; exit 1; }
    echo "String value is: $v"
    ;;
  "service list")
    echo "service list" >> "$STUB_DIR/list_calls"
    if [ -e "$STUB_DIR/flaky_list" ]; then
      n=$(cat "$STUB_DIR/flaky_list")
      if [ "$n" -gt 0 ]; then
        echo $((n - 1)) > "$STUB_DIR/flaky_list"
        echo "stub: daemon not ready" >&2
        exit 1
      fi
    fi
    [ -e "$STUB_DIR/hang_list" ] && exec sleep 30
    [ -e "$STUB_DIR/empty_list" ] && exit 0   # a cold daemon: exit 0, nothing listed
    # A real graph always lists the parameter services of every node.
    echo /some_other_node/get_parameters
    echo /some_other_node/set_parameters
    for m in "$STUB_DIR"/svc_*; do
      if [ -e "$m" ]; then cat "$m"; fi
    done
    exit 0
    ;;
  "service call")
    node=${3%/reset}
    k=$(key "$node")
    echo "call $node" >> "$STUB_DIR/calls"
    if [ "$4" != "std_srvs/srv/Trigger" ] || [ "$5" != "{}" ]; then
      echo "stub: bad service call arguments: $*" >&2
      exit 2
    fi
    # `ros2 service call` waits for a service that is not there without bound.
    if [ ! -e "$STUB_DIR/svc_$k" ]; then echo "waiting for service to become available..."; exec sleep 30; fi
    if [ -e "$STUB_DIR/hang_call_$k" ]; then echo "waiting for service to become available..."; exec sleep 30; fi
    if [ -e "$STUB_DIR/flaky_call_$k" ]; then
      n=$(cat "$STUB_DIR/flaky_call_$k")
      if [ "$n" -gt 0 ]; then
        echo $((n - 1)) > "$STUB_DIR/flaky_call_$k"
        echo "stub: service call failed" >&2
        exit 1
      fi
    fi
    echo "waiting for service to become available..."
    echo "requester: making request: std_srvs.srv.Trigger_Request()"
    echo
    echo "response:"
    if [ -e "$STUB_DIR/refuse_$k" ]; then
      # Python's repr switches to double quotes when the text holds a single quote.
      echo "std_srvs.srv.Trigger_Response(success=False, message=\"$(cat "$STUB_DIR/refuse_$k")\")"
    else
      s=$(cat "$STUB_DIR/state/$k")
      echo "$node reset-service" >> "$STUB_DIR/log"
      if [ "$s" = active ]; then
        echo "std_srvs.srv.Trigger_Response(success=True, message='3 tile(s) dropped from RAM; prior_store_dir unset; node is active; nothing on disk was changed')"
      else
        echo "std_srvs.srv.Trigger_Response(success=True, message='3 tile(s) dropped from RAM; prior_store_dir unset; node is inactive: no grid until activated; nothing on disk was changed')"
      fi
    fi
    echo
    ;;
  *) echo "stub ros2: unsupported: $*" >&2; exit 2 ;;
esac
STUB
chmod +x "$work/bin/ros2"
# Records its arguments, then runs the real timeout, so the test can check that
# every ros2 call is bounded by `timeout --foreground`.
real_timeout=$(command -v timeout)
cat > "$work/bin/timeout" <<TIMEOUT
#!/usr/bin/env bash
echo "\$*" >> "\$STUB_DIR/timeout_calls"
exec $real_timeout "\$@"
TIMEOUT
chmod +x "$work/bin/timeout"
export STUB_DIR="$work" PATH="$work/bin:$PATH"
# Short pause between read attempts, so the retry cases stay quick.
export RESET_CUBE_RETRY_PAUSE=0.05

failures=0
fail() { echo "FAIL: $*"; failures=$((failures + 1)); }

setup_nodes() {
  rm -f "$work"/log "$work"/fail_* "$work"/hang_* "$work"/noise "$work"/slow \
    "$work"/timeout_calls "$work"/reads "$work"/flaky_* "$work"/onconfigure_* "$work"/state/* "$work"/param/* \
    "$work"/svc_* "$work"/refuse_* "$work"/calls "$work"/list_calls "$work"/empty_list
  for n in boat1 boat2 boat3 boat4; do
    echo active > "$work/state/_${n}_cube_bathymetry"
    printf '' > "$work/param/_${n}_cube_bathymetry_draft_dir"
    printf '' > "$work/param/_${n}_cube_bathymetry_prior_store_dir"
  done
  echo active > "$work/state/_coverage_assessment"
}
cubes=(/boat1/cube_bathymetry /boat2/cube_bathymetry /boat3/cube_bathymetry /boat4/cube_bathymetry)

# Make nodes serve `<node>/reset` (the stub lists them and answers the call).
nkey() { echo "${1//\//_}"; }
mark_service() { local n; for n in "$@"; do echo "$n/reset" > "$work/svc_$(nkey "$n")"; done; }

# --- 1. consumer ordering, four nodes, one of them already inactive -----------
setup_nodes
echo inactive > "$work/state/_boat3_cube_bathymetry"
out=$("$script" --consumer /coverage_assessment "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 1: exit $rc: $out"
for n in "${cubes[@]}" /coverage_assessment; do
  [ "$(cat "$work/state/${n//\//_}")" = active ] || fail "case 1: $n not active at the end"
done
line() { grep -n -m1 -x "$1" "$work/log" | cut -d: -f1; }
last_cube_cleanup=0
for n in "${cubes[@]}"; do
  l=$(line "$n cleanup"); [ -n "$l" ] || { fail "case 1: no cleanup for $n"; continue; }
  [ "$l" -gt "$last_cube_cleanup" ] && last_cube_cleanup=$l
done
cons_cleanup=$(line "/coverage_assessment cleanup")
cons_activate=$(line "/coverage_assessment activate")
first_cube_configure=$(grep -n -m1 'cube_bathymetry configure' "$work/log" | cut -d: -f1)
[ -n "$cons_cleanup" ] && [ -n "$cons_activate" ] && [ -n "$first_cube_configure" ] ||
  fail "case 1: missing log lines"
[ "$cons_cleanup" -gt "$last_cube_cleanup" ] || fail "case 1: consumer reset before every CUBE node was quiesced"
[ "$first_cube_configure" -gt "$cons_activate" ] || fail "case 1: CUBE configured before the consumer was back up"
[ -z "$(line "/boat3/cube_bathymetry deactivate")" ] || fail "case 1: already-inactive node was deactivated"
case "$out" in *"warning: /boat3/cube_bathymetry is 'inactive'"*) ;; *) fail "case 1: no preflight warning for the inactive node: $out" ;; esac
case "$out" in *"warning: /boat1/cube_bathymetry"*) fail "case 1: warned about an active node: $out" ;; esac
case "$out" in *"warning: /coverage_assessment"*) fail "case 1: warned about a consumer: $out" ;; esac

# --- 2. draft_dir set: refuse, change nothing --------------------------------
setup_nodes
echo /data/draft > "$work/param/_boat2_cube_bathymetry_draft_dir"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 2: expected exit 1, got $rc"
case "$out" in *"/boat2/cube_bathymetry"*"draft_dir"*) ;; *) fail "case 2: message does not name node and parameter: $out" ;; esac
case "$out" in *"nothing was changed"*) ;; *) fail "case 2: message does not say nothing was changed: $out" ;; esac
[ ! -s "$work/log" ] || fail "case 2: transitions ran despite the refusal: $(cat "$work/log")"
# ...and --allow-persistence lets it through
out=$("$script" --allow-persistence "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 2b: --allow-persistence should proceed, exit $rc: $out"

# --- 3. failing transition: fail fast, name node and transition -------------
setup_nodes
touch "$work/fail__boat2_cube_bathymetry_cleanup"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 3: expected exit 1, got $rc"
case "$out" in *"/boat2/cube_bathymetry"*"cleanup"*"failed"*) ;; *) fail "case 3: message does not name node and transition: $out" ;; esac
grep -q "boat3/cube_bathymetry" "$work/log" && fail "case 3: kept going after the failure (touched boat3)"
case "$out" in *"states:"*"/boat1/cube_bathymetry"*"/boat3/cube_bathymetry"*) ;; *) fail "case 3: no node states after the failure: $out" ;; esac
case "$out" in *"Re-run"*) ;; *) fail "case 3: no re-run hint: $out" ;; esac

# --- 4. preflight: nothing moves when a later node or consumer is unusable ----
setup_nodes
out=$("$script" --consumer /typo "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 4a: mistyped consumer: expected exit 1, got $rc"
case "$out" in *"/typo"*) ;; *) fail "case 4a: message does not name the consumer: $out" ;; esac
[ ! -s "$work/log" ] || fail "case 4a: transitions ran despite a mistyped consumer: $(cat "$work/log")"
setup_nodes
echo finalized > "$work/state/_boat4_cube_bathymetry"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 4b: finalized node: expected exit 1, got $rc"
case "$out" in *"/boat4/cube_bathymetry"*"finalized"*) ;; *) fail "case 4b: message does not name node and state: $out" ;; esac
[ ! -s "$work/log" ] || fail "case 4b: transitions ran despite a finalized node: $(cat "$work/log")"

# --- 5. --consumer must not swallow a flag --------------------------------------
setup_nodes
out=$("$script" --consumer --allow-persistence "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 2 ] || fail "case 5: expected usage exit 2, got $rc: $out"
[ ! -s "$work/log" ] || fail "case 5: transitions ran: $(cat "$work/log")"
out=$("$script" "${cubes[@]}" --consumer 2>&1); rc=$?
[ "$rc" -eq 2 ] || fail "case 5b: --consumer with no value: expected exit 2, got $rc"

# --- 6. a never-configured node: probe after the other checks, put it back -----
setup_nodes
echo unconfigured > "$work/state/_boat4_cube_bathymetry"
rm -f "$work/param/_boat4_cube_bathymetry_"*
printf '' > "$work/onconfigure__boat4_cube_bathymetry_draft_dir"
printf '' > "$work/onconfigure__boat4_cube_bathymetry_prior_store_dir"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 6a: exit $rc: $out"
for n in "${cubes[@]}"; do
  [ "$(cat "$work/state/${n//\//_}")" = active ] || fail "case 6a: $n not active at the end"
done
probe_configure=$(grep -n -m1 '^/boat4/cube_bathymetry configure' "$work/log" | cut -d: -f1)
first_deactivate=$(grep -n -m1 ' deactivate$' "$work/log" | cut -d: -f1)
[ -n "$probe_configure" ] && [ -n "$first_deactivate" ] && [ "$probe_configure" -lt "$first_deactivate" ] ||
  fail "case 6a: the probe configure did not come before the first quiesce"
[ "$(grep -c '^/boat4/cube_bathymetry configure' "$work/log")" -eq 2 ] ||
  fail "case 6a: expected the probe configure plus the real one"

# another node's readable draft_dir refuses BEFORE the never-configured node is
# touched. The never-configured node comes FIRST in the argument order and the node
# with the draft_dir LAST, so a script that probed inline, in argument order, would
# configure boat1 before it reached boat4's draft_dir and fail the log check.
setup_nodes
echo unconfigured > "$work/state/_boat1_cube_bathymetry"
rm -f "$work/param/_boat1_cube_bathymetry_"*
echo /data/draft > "$work/param/_boat4_cube_bathymetry_draft_dir"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 6b: expected exit 1, got $rc"
case "$out" in *"/boat4/cube_bathymetry"*"draft_dir"*) ;; *) fail "case 6b: wrong message: $out" ;; esac
[ ! -s "$work/log" ] || fail "case 6b: the never-configured node was touched before the refusal: $(cat "$work/log")"

# draft_dir that only the probe can see: refuse, node put back, nobody reset
setup_nodes
echo unconfigured > "$work/state/_boat4_cube_bathymetry"
rm -f "$work/param/_boat4_cube_bathymetry_"*
echo /data/draft > "$work/onconfigure__boat4_cube_bathymetry_draft_dir"
printf '' > "$work/onconfigure__boat4_cube_bathymetry_prior_store_dir"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 6c: expected exit 1, got $rc"
case "$out" in *"/boat4/cube_bathymetry"*"draft_dir"*"nothing was changed"*) ;; *) fail "case 6c: wrong message: $out" ;; esac
[ "$(cat "$work/state/_boat4_cube_bathymetry")" = unconfigured ] || fail "case 6c: probed node not put back"
[ "$(wc -l < "$work/log")" -eq 2 ] || fail "case 6c: expected only the probe's configure and cleanup: $(cat "$work/log")"
[ "$(cat "$work/state/_boat1_cube_bathymetry")" = active ] || fail "case 6c: another node was reset"

# --- 7. an unresponsive node fails within the timeout, before any change -------
setup_nodes
touch "$work/hang__boat2_cube_bathymetry"
start=$SECONDS
out=$(RESET_CUBE_GET_TIMEOUT=1 "$script" "${cubes[@]}" 2>&1); rc=$?
elapsed=$((SECONDS - start))
[ "$rc" -eq 1 ] || fail "case 7: expected exit 1, got $rc"
[ "$elapsed" -lt 20 ] || fail "case 7: took ${elapsed}s, the timeout did not bound the call"
case "$out" in *"/boat2/cube_bathymetry"*) ;; *) fail "case 7: message does not name the node: $out" ;; esac
[ ! -s "$work/log" ] || fail "case 7: transitions ran: $(cat "$work/log")"

# --- 8. stderr noise from ros2 is not parsed as data ----------------------------
setup_nodes
touch "$work/noise"
out=$("$script" "${cubes[@]}" 2>/dev/null); rc=$?
[ "$rc" -eq 0 ] || fail "case 8: stderr warning broke the run, exit $rc: $out"

# --- 9. timeout --foreground on every call; HUP and PIPE do not kill the script --
setup_nodes
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 9a: exit $rc: $out"
[ -s "$work/timeout_calls" ] || fail "case 9a: no ros2 call went through timeout"
bare=$(grep -vc '^--foreground ' "$work/timeout_calls")
[ "$bare" -eq 0 ] || fail "case 9a: $bare timeout call(s) without --foreground: $(cat "$work/timeout_calls")"

setup_nodes
touch "$work/slow"
"$script" "${cubes[@]}" > "$work/hup_out" 2>&1 &
pid=$!
sleep 0.6
kill -HUP "$pid"
wait "$pid"; rc=$?
[ "$rc" -eq 0 ] || fail "case 9b: SIGHUP killed the script mid-reset (exit $rc): $(cat "$work/hup_out")"
for n in "${cubes[@]}"; do
  [ "$(cat "$work/state/${n//\//_}")" = active ] || fail "case 9b: $n not active after SIGHUP"
done

setup_nodes
"$script" "${cubes[@]}" 2>&1 | head -n 1 > /dev/null
rc=${PIPESTATUS[0]}
[ "$rc" -eq 0 ] || fail "case 9c: a closed pipe killed the script (exit $rc)"
for n in "${cubes[@]}"; do
  [ "$(cat "$work/state/${n//\//_}")" = active ] || fail "case 9c: $n not active after the pipe closed"
done

# --- 10. a failing probe is not a failed reset; the preflight warns --------------
setup_nodes
echo unconfigured > "$work/state/_boat4_cube_bathymetry"
rm -f "$work/param/_boat4_cube_bathymetry_"*
printf '' > "$work/onconfigure__boat4_cube_bathymetry_draft_dir"
printf '' > "$work/onconfigure__boat4_cube_bathymetry_prior_store_dir"
touch "$work/fail__boat4_cube_bathymetry_cleanup"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 10a: expected exit 1, got $rc"
case "$out" in *"the reset did not finish"*) fail "case 10a: a failed probe was reported as a failed reset: $out" ;; esac
case "$out" in *"no CUBE node was reset"*"/boat4/cube_bathymetry"*) ;; *) fail "case 10a: no probe-specific message: $out" ;; esac
case "$out" in *"warning: /boat4/cube_bathymetry is 'unconfigured'"*) ;; *) fail "case 10a: no preflight warning: $out" ;; esac
[ "$(cat "$work/state/_boat1_cube_bathymetry")" = active ] || fail "case 10a: another node was reset"

# --- 11. each node named once -----------------------------------------------------
setup_nodes
out=$("$script" --consumer /boat2/cube_bathymetry "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 2 ] || fail "case 11a: CUBE node also a consumer: expected exit 2, got $rc: $out"
case "$out" in *"/boat2/cube_bathymetry"*"twice"*"Nothing was changed"*) ;; *) fail "case 11a: message: $out" ;; esac
[ ! -s "$work/log" ] || fail "case 11a: transitions ran: $(cat "$work/log")"
setup_nodes
out=$("$script" --consumer boat2/cube_bathymetry/ "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 2 ] || fail "case 11b: same node in another spelling: expected exit 2, got $rc: $out"
[ ! -s "$work/log" ] || fail "case 11b: transitions ran: $(cat "$work/log")"
setup_nodes
out=$("$script" --consumer //boat3//cube_bathymetry// "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 2 ] || fail "case 11e: repeated slashes: expected exit 2, got $rc: $out"
[ ! -s "$work/log" ] || fail "case 11e: transitions ran: $(cat "$work/log")"
setup_nodes
out=$("$script" "${cubes[@]}" /boat1/cube_bathymetry 2>&1); rc=$?
[ "$rc" -eq 2 ] || fail "case 11c: CUBE node given twice: expected exit 2, got $rc: $out"
case "$out" in *"/boat1/cube_bathymetry"*"twice"*) ;; *) fail "case 11c: message: $out" ;; esac
[ ! -s "$work/log" ] || fail "case 11c: transitions ran: $(cat "$work/log")"
setup_nodes
out=$("$script" --consumer /coverage_assessment --consumer coverage_assessment "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 2 ] || fail "case 11d: consumer given twice: expected exit 2, got $rc: $out"
[ ! -s "$work/log" ] || fail "case 11d: transitions ran: $(cat "$work/log")"

# 11f. a lone node in another spelling is reset under its qualified name
setup_nodes
out=$("$script" boat1/cube_bathymetry/ /boat2/cube_bathymetry 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 11f: other spelling of a single node: exit $rc: $out"
[ -n "$(line "/boat1/cube_bathymetry cleanup")" ] || fail "case 11f: not reset under /boat1/cube_bathymetry: $(cat "$work/log")"

# --- 12. reads are retried a bounded number of times; transitions never ----------
# 12a. a parameter read and a state read each fail twice, then answer: the reset
# completes.
setup_nodes
echo 2 > "$work/flaky_param__boat2_cube_bathymetry_prior_store_dir"
echo 2 > "$work/flaky_param__boat3_cube_bathymetry_draft_dir"
echo 2 > "$work/flaky_state__boat1_cube_bathymetry"
out=$("$script" --consumer /coverage_assessment "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 12a: a read that failed twice aborted the reset, exit $rc: $out"
for n in "${cubes[@]}" /coverage_assessment; do
  [ "$(cat "$work/state/${n//\//_}")" = active ] || fail "case 12a: $n not active at the end"
done
[ "$(grep -c '^param get /boat2/cube_bathymetry prior_store_dir$' "$work/reads")" -ge 3 ] ||
  fail "case 12a: prior_store_dir was not retried: $(grep boat2 "$work/reads")"

# 12b. a parameter read that fails every time: three attempts, then the script
# fails, naming the node and the parameter, before anything was changed.
setup_nodes
echo 99 > "$work/flaky_param__boat2_cube_bathymetry_prior_store_dir"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 12b: expected exit 1, got $rc: $out"
case "$out" in *"/boat2/cube_bathymetry"*"prior_store_dir"*"nothing was changed"*) ;; *) fail "case 12b: message: $out" ;; esac
attempts=$(grep -c '^param get /boat2/cube_bathymetry prior_store_dir$' "$work/reads")
[ "$attempts" -eq 3 ] || fail "case 12b: expected exactly 3 attempts, got $attempts"
[ ! -s "$work/log" ] || fail "case 12b: transitions ran: $(cat "$work/log")"

# 12c. a state read that fails every time, same bound, nothing changed.
setup_nodes
echo 99 > "$work/flaky_state__boat3_cube_bathymetry"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 12c: expected exit 1, got $rc: $out"
attempts=$(grep -c '^lifecycle get /boat3/cube_bathymetry$' "$work/reads")
[ "$attempts" -eq 3 ] || fail "case 12c: expected exactly 3 attempts, got $attempts"
[ ! -s "$work/log" ] || fail "case 12c: transitions ran: $(cat "$work/log")"

# 12d. a failing transition is run once, never retried.
setup_nodes
touch "$work/fail__boat2_cube_bathymetry_cleanup"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 12d: expected exit 1, got $rc"
[ "$(grep -c '^/boat2/cube_bathymetry cleanup$' "$work/log")" -eq 1 ] ||
  fail "case 12d: the failed transition was retried: $(cat "$work/log")"

# =============================================================================
# The service path (cube_bathymetry#167)
# =============================================================================
line_of() { grep -n -m1 -x -- "$1" "$work/log" | cut -d: -f1; }

# --- 13. all-service fleet --------------------------------------------------------
setup_nodes
mark_service "${cubes[@]}" /coverage_assessment
out=$("$script" --consumer /coverage_assessment "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 13: exit $rc: $out"
[ "$(wc -l < "$work/timeout_calls")" -eq 6 ] ||
  fail "case 13: expected 6 ros2 calls (1 list + 5 service calls), got: $(cat "$work/timeout_calls")"
[ "$(wc -l < "$work/list_calls")" -eq 1 ] || fail "case 13: expected exactly one service list"
bare=$(grep -vc '^--foreground ' "$work/timeout_calls")
[ "$bare" -eq 0 ] || fail "case 13: $bare call(s) without timeout --foreground: $(cat "$work/timeout_calls")"
[ ! -e "$work/reads" ] || fail "case 13: a lifecycle get or param get ran on the service path: $(cat "$work/reads")"
grep -qE ' (deactivate|cleanup|configure|activate)$' "$work/log" &&
  fail "case 13: a lifecycle transition ran on the service path: $(cat "$work/log")"
[ "$(wc -l < "$work/log")" -eq 5 ] || fail "case 13: expected 5 resets in the log: $(cat "$work/log")"
last_cube=0
for n in "${cubes[@]}"; do
  l=$(line_of "$n reset-service"); [ -n "$l" ] || { fail "case 13: $n was not reset"; continue; }
  [ "$l" -gt "$last_cube" ] && last_cube=$l
done
cons=$(line_of "/coverage_assessment reset-service")
[ -n "$cons" ] && [ "$cons" -gt "$last_cube" ] || fail "case 13: the consumer was reset before every CUBE node had answered"
case "$out" in *"/boat1/cube_bathymetry: reset: 3 tile(s) dropped from RAM"*) ;; *) fail "case 13: no per-node report: $out" ;; esac
case "$out" in *"NOT publishing"*) fail "case 13: reported a node as not publishing: $out" ;; esac
case "$out" in *"states:"*) fail "case 13: the service path printed a state listing: $out" ;; esac

# --- 14. a refusal stops the script at the first refusing node ---------------------
setup_nodes
mark_service "${cubes[@]}" /coverage_assessment
echo "refused: draft_dir is '/data/draft'; saved tiles would reload; has success=True inside" > "$work/refuse__boat3_cube_bathymetry"
out=$("$script" --consumer /coverage_assessment "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 14: expected exit 1, got $rc: $out"
case "$out" in *"/boat3/cube_bathymetry: ~/reset answered: refused"*"/data/draft"*) ;; *) fail "case 14: message does not name node, refusal and directory: $out" ;; esac
case "$out" in *"reset before this: /boat1/cube_bathymetry /boat2/cube_bathymetry"*) ;; *) fail "case 14: does not say which nodes were already reset: $out" ;; esac
case "$out" in *"Re-running is safe"*) ;; *) fail "case 14: no re-run hint: $out" ;; esac
[ -n "$(line_of "/boat1/cube_bathymetry reset-service")" ] && [ -n "$(line_of "/boat2/cube_bathymetry reset-service")" ] ||
  fail "case 14: the boats before the refusal were not reset"
grep -q "boat4\|coverage_assessment" "$work/log" && fail "case 14: went on past the refusal: $(cat "$work/log")"
grep -q "boat4\|coverage_assessment" "$work/calls" && fail "case 14: called a node after the refusal: $(cat "$work/calls")"
[ ! -e "$work/reads" ] || fail "case 14: reads on the service path: $(cat "$work/reads")"

# 14b. a refusal whose message contains "(success=True," (a path the operator typed)
# is still a refusal: the verdict is read from the start of the response line.
setup_nodes
mark_service "${cubes[@]}" /coverage_assessment
echo "refused: draft_dir is '/data/(success=True,old)'; saved tiles would reload" > "$work/refuse__boat2_cube_bathymetry"
out=$("$script" --consumer /coverage_assessment "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 14b: a refusal echoing '(success=True,' was read as a success, exit $rc: $out"
case "$out" in *"/boat2/cube_bathymetry: ~/reset answered: refused"*) ;; *) fail "case 14b: the refusal was not reported: $out" ;; esac
grep -q "boat3\|boat4\|coverage_assessment" "$work/log" && fail "case 14b: went on past the refusal: $(cat "$work/log")"

# --- 15. no service: the walk; mixed fleet in the documented order ------------------
setup_nodes
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 15a: exit $rc: $out"
[ ! -e "$work/calls" ] || fail "case 15a: a node without a service was called: $(cat "$work/calls")"
[ -n "$(line_of "/boat1/cube_bathymetry cleanup")" ] || fail "case 15a: the walk did not run"

setup_nodes
mark_service /boat1/cube_bathymetry /boat2/cube_bathymetry /coverage_assessment
out=$("$script" --consumer /coverage_assessment "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 15b: exit $rc: $out"
for n in "${cubes[@]}" /coverage_assessment; do
  [ "$(cat "$work/state/${n//\//_}")" = active ] || fail "case 15b: $n not active at the end"
done
b2=$(line_of "/boat2/cube_bathymetry reset-service")
b3d=$(line_of "/boat3/cube_bathymetry deactivate"); b4c=$(line_of "/boat4/cube_bathymetry cleanup")
cs=$(line_of "/coverage_assessment reset-service")
b3cfg=$(line_of "/boat3/cube_bathymetry configure"); b4a=$(line_of "/boat4/cube_bathymetry activate")
{ [ -n "$(line_of "/boat1/cube_bathymetry reset-service")" ] && [ -n "$b2" ] && [ -n "$b3d" ] && [ -n "$b4c" ] &&
  [ -n "$cs" ] && [ -n "$b3cfg" ] && [ -n "$b4a" ]; } ||
  fail "case 15b: missing log lines: $(cat "$work/log")"
[ "$b2" -lt "$b3d" ] || fail "case 15b: a service-class boat was reset after a walk-class boat left active"
[ "$cs" -gt "$b4c" ] || fail "case 15b: the consumer was reset before the walk-class CUBE nodes were quiesced"
[ "$cs" -lt "$b3cfg" ] || fail "case 15b: a walk-class CUBE node came back before the consumer was reset"
grep -q 'param get /boat1/cube_bathymetry\|lifecycle get /boat1/cube_bathymetry' "$work/reads" &&
  fail "case 15b: a service-class node was read: $(cat "$work/reads")"
grep -q 'param get /boat3/cube_bathymetry draft_dir' "$work/reads" || fail "case 15b: no preflight for the walk-class node"
case "$out" in *"states:"*"/boat3/cube_bathymetry"*) ;; *) fail "case 15b: no state listing for the walked nodes: $out" ;; esac
case "$out" in *"/boat1/cube_bathymetry: active"*) fail "case 15b: listed the state of a service node: $out" ;; esac

# --- 16. --allow-persistence --------------------------------------------------------
setup_nodes
mark_service "${cubes[@]}" /coverage_assessment
echo /data/draft > "$work/param/_boat2_cube_bathymetry_draft_dir"
out=$("$script" --allow-persistence --consumer /coverage_assessment "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 16a: exit $rc: $out"
for n in "${cubes[@]}"; do
  [ -z "$(line_of "$n reset-service")" ] || fail "case 16a: $n took the service despite --allow-persistence"
  [ -n "$(line_of "$n cleanup")" ] || fail "case 16a: $n was not walked"
done
[ -n "$(line_of "/coverage_assessment reset-service")" ] || fail "case 16a: the consumer was not classified normally"
setup_nodes
mark_service "${cubes[@]}"
out=$("$script" --allow-persistence "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 16b: exit $rc: $out"
[ ! -e "$work/list_calls" ] || fail "case 16b: service list ran although every node takes the walk"

# --- 17. a consumer that refuses falls back to the walk -----------------------------
setup_nodes
mark_service "${cubes[@]}" /coverage_assessment
echo "refused: the map frame 'map' <- 'earth' moved since the raster georeference was captured" > "$work/refuse__coverage_assessment"
out=$("$script" --consumer /coverage_assessment "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 17: exit $rc: $out"
case "$out" in *"/coverage_assessment: ~/reset answered: refused"*"map frame"*"lifecycle walk instead"*) ;; *) fail "case 17: the reason was not printed: $out" ;; esac
for t in deactivate cleanup configure activate; do
  [ -n "$(line_of "/coverage_assessment $t")" ] || fail "case 17: consumer walk is missing '$t': $(cat "$work/log")"
done
[ "$(cat "$work/state/_coverage_assessment")" = active ] || fail "case 17: consumer not active at the end"

# --- 18. service list: indeterminate is retried, then stops; never the walk ----------
setup_nodes
mark_service "${cubes[@]}"
echo 2 > "$work/flaky_list"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 18a: a service list that failed twice aborted the reset, exit $rc: $out"
[ "$(wc -l < "$work/list_calls")" -eq 3 ] || fail "case 18a: expected 3 attempts"
[ -n "$(line_of "/boat1/cube_bathymetry reset-service")" ] || fail "case 18a: not reset by service"

setup_nodes
mark_service "${cubes[@]}"
touch "$work/empty_list"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 18b: an empty service list: expected exit 1, got $rc: $out"
[ "$(wc -l < "$work/list_calls")" -eq 3 ] || fail "case 18b: an empty list must be retried (3 attempts)"
{ [ ! -s "$work/log" ] && [ ! -e "$work/calls" ] && [ ! -e "$work/reads" ]; } ||
  fail "case 18b: something ran after an indeterminate list (the walk must not be selected)"
case "$out" in *"nothing was changed"*) ;; *) fail "case 18b: message: $out" ;; esac

setup_nodes
mark_service "${cubes[@]}"
touch "$work/hang_list"
start=$SECONDS
out=$(RESET_CUBE_GET_TIMEOUT=1 RESET_CUBE_GET_TRIES=2 "$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 18c: a hanging service list: expected exit 1, got $rc"
[ $((SECONDS - start)) -lt 20 ] || fail "case 18c: the timeout did not bound the call"
{ [ ! -s "$work/log" ] && [ ! -e "$work/reads" ]; } || fail "case 18c: something ran after the failed list"

# --- 19. service call: own retry budget, never the walk --------------------------------
setup_nodes
mark_service "${cubes[@]}"
echo 1 > "$work/flaky_call__boat2_cube_bathymetry"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 19a: a call that failed once aborted the reset, exit $rc: $out"
[ "$(grep -c '^call /boat2/cube_bathymetry$' "$work/calls")" -eq 2 ] || fail "case 19a: expected 2 attempts at boat2"

setup_nodes
mark_service "${cubes[@]}" /coverage_assessment
touch "$work/hang_call__boat2_cube_bathymetry"
start=$SECONDS
out=$(RESET_CUBE_CALL_TIMEOUT=1 "$script" --consumer /coverage_assessment "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 19b: a hung call: expected exit 1, got $rc: $out"
[ $((SECONDS - start)) -lt 30 ] || fail "case 19b: the call timeout did not bound the call"
[ "$(grep -c '^call /boat2/cube_bathymetry$' "$work/calls")" -eq 1 ] ||
  fail "case 19b: a call that timed out was repeated (its request is still queued in the node): $(cat "$work/calls")"
case "$out" in *"/boat2/cube_bathymetry"*"no usable answer"*"was not walked"*"not repeated"*) ;; *) fail "case 19b: message: $out" ;; esac
case "$out" in *"reset before this: /boat1/cube_bathymetry"*) ;; *) fail "case 19b: does not say boat1 was reset: $out" ;; esac
[ ! -e "$work/reads" ] || fail "case 19b: the node was read or walked after the timeout: $(cat "$work/reads")"
grep -qE ' (deactivate|cleanup|configure|activate)$' "$work/log" && fail "case 19b: a transition ran: $(cat "$work/log")"
grep -q "boat3\|boat4\|coverage_assessment" "$work/log" && fail "case 19b: went on past the failed call"
# the retry budget is its own variable, spent on calls that fail fast
setup_nodes
mark_service "${cubes[@]}"
echo 99 > "$work/flaky_call__boat1_cube_bathymetry"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 19c: a call that fails every time: expected exit 1, got $rc: $out"
[ "$(grep -c '^call /boat1/cube_bathymetry$' "$work/calls")" -eq 2 ] || fail "case 19c: the default RESET_CUBE_CALL_TRIES (2) was not used: $(cat "$work/calls")"
setup_nodes
mark_service "${cubes[@]}"
echo 99 > "$work/flaky_call__boat1_cube_bathymetry"
out=$(RESET_CUBE_CALL_TRIES=3 "$script" "${cubes[@]}" 2>&1); rc=$?
[ "$(grep -c '^call /boat1/cube_bathymetry$' "$work/calls")" -eq 3 ] || fail "case 19c: RESET_CUBE_CALL_TRIES was not honoured"

# 19d. with no RESET_CUBE_CALL_TIMEOUT a call is bounded by the transition bound
# (the set timeout), and the one that times out is still not repeated.
setup_nodes
mark_service "${cubes[@]}"
touch "$work/hang_call__boat1_cube_bathymetry"
start=$SECONDS
out=$(RESET_CUBE_SET_TIMEOUT=1 "$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 19d: a hung call: expected exit 1, got $rc: $out"
[ $((SECONDS - start)) -lt 20 ] || fail "case 19d: the default call timeout is not the set timeout"
[ "$(grep -c '^call /boat1/cube_bathymetry$' "$work/calls")" -eq 1 ] || fail "case 19d: a timed-out call was repeated: $(cat "$work/calls")"

# --- 20. everything that can refuse runs before the first service call -----------------
setup_nodes
mark_service "${cubes[@]}"
out=$("$script" --consumer /typo "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 20a: mistyped consumer: expected exit 1, got $rc: $out"
case "$out" in *"/typo"*) ;; *) fail "case 20a: message does not name the consumer: $out" ;; esac
{ [ ! -s "$work/log" ] && [ ! -e "$work/calls" ]; } || fail "case 20a: a boat was reset before the typo was found: $(cat "$work/log")"

setup_nodes
mark_service /boat1/cube_bathymetry /boat2/cube_bathymetry /boat3/cube_bathymetry
echo finalized > "$work/state/_boat4_cube_bathymetry"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 20b: finalized walk-class node: expected exit 1, got $rc"
{ [ ! -s "$work/log" ] && [ ! -e "$work/calls" ]; } || fail "case 20b: a boat was reset before the finalized node was found"

setup_nodes
mark_service /boat1/cube_bathymetry /boat2/cube_bathymetry /boat3/cube_bathymetry
echo /data/draft > "$work/param/_boat4_cube_bathymetry_draft_dir"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 20c: draft_dir on a walk-class node: expected exit 1, got $rc"
case "$out" in *"/boat4/cube_bathymetry"*"draft_dir"*"nothing was changed"*) ;; *) fail "case 20c: message: $out" ;; esac
{ [ ! -s "$work/log" ] && [ ! -e "$work/calls" ]; } || fail "case 20c: a boat was reset before the refusal was found"

# --- 21. an inactive node is reset, then activated ----------------------------------
setup_nodes
mark_service "${cubes[@]}" /coverage_assessment
echo inactive > "$work/state/_boat2_cube_bathymetry"
echo inactive > "$work/state/_coverage_assessment"
out=$("$script" --consumer /coverage_assessment "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 21: expected exit 0, got $rc: $out"
for n in "${cubes[@]}" /coverage_assessment; do
  [ "$(cat "$work/state/${n//\//_}")" = active ] || fail "case 21: $n is not active at the end"
done
r=$(line_of "/boat2/cube_bathymetry reset-service"); a=$(line_of "/boat2/cube_bathymetry activate")
{ [ -n "$r" ] && [ -n "$a" ] && [ "$a" -gt "$r" ]; } || fail "case 21: boat2 was not reset and then activated: $(cat "$work/log")"
r=$(line_of "/coverage_assessment reset-service"); a=$(line_of "/coverage_assessment activate")
{ [ -n "$r" ] && [ -n "$a" ] && [ "$a" -gt "$r" ]; } || fail "case 21: the consumer was not reset and then activated: $(cat "$work/log")"
[ -z "$(line_of "/boat1/cube_bathymetry activate")" ] || fail "case 21: an active node was activated"
case "$out" in *"/boat2/cube_bathymetry was inactive"*) ;; *) fail "case 21: no note naming the node: $out" ;; esac
case "$out" in *"activated after the reset (they were inactive): /boat2/cube_bathymetry /coverage_assessment"*) ;; *) fail "case 21: no summary line: $out" ;; esac
case "$out" in *"NOT publishing"*) fail "case 21: reported a node as not publishing: $out" ;; esac

# 21b. an activation that fails is a failure with the re-run hint, not a quiet success
setup_nodes
mark_service "${cubes[@]}"
echo inactive > "$work/state/_boat2_cube_bathymetry"
touch "$work/fail__boat2_cube_bathymetry_activate"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 21b: expected exit 1, got $rc: $out"
case "$out" in *"/boat2/cube_bathymetry"*"activate"*"failed"*) ;; *) fail "case 21b: message: $out" ;; esac
case "$out" in *"states:"*"/boat2/cube_bathymetry: inactive"*"Re-run"*) ;; *) fail "case 21b: no state listing and re-run hint: $out" ;; esac

# --- 22. re-running the same command recovers a node that a failure left inactive ---------
# 22a. a walk failed at cleanup: boat1 is unconfigured, boat2 inactive. A configured node
# serves ~/reset, so the re-run resets boat2 by service and ACTIVATES it (exit 0).
setup_nodes
touch "$work/fail__boat2_cube_bathymetry_cleanup"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 22a: the failing first run: expected exit 1, got $rc: $out"
[ "$(cat "$work/state/_boat2_cube_bathymetry")" = inactive ] || fail "case 22a: boat2 was not left inactive"
rm -f "$work/fail__boat2_cube_bathymetry_cleanup" "$work/log"
mark_service /boat2/cube_bathymetry /boat3/cube_bathymetry /boat4/cube_bathymetry   # configured nodes
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 22a: re-running the same command did not recover: exit $rc: $out"
for n in "${cubes[@]}"; do
  [ "$(cat "$work/state/${n//\//_}")" = active ] || fail "case 22a: $n is not active after the re-run"
done
r=$(line_of "/boat2/cube_bathymetry reset-service"); a=$(line_of "/boat2/cube_bathymetry activate")
{ [ -n "$r" ] && [ -n "$a" ] && [ "$a" -gt "$r" ]; } || fail "case 22a: boat2 was not reset by service and then activated: $(cat "$work/log")"

# 22b. a probe of a never-configured node failed at cleanup and left boat4 inactive
setup_nodes
echo unconfigured > "$work/state/_boat4_cube_bathymetry"
rm -f "$work/param/_boat4_cube_bathymetry_"*
printf '' > "$work/onconfigure__boat4_cube_bathymetry_draft_dir"
printf '' > "$work/onconfigure__boat4_cube_bathymetry_prior_store_dir"
touch "$work/fail__boat4_cube_bathymetry_cleanup"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 22b: the failing probe: expected exit 1, got $rc: $out"
case "$out" in *"re-run resets it by service and then activates it"*) ;; *) fail "case 22b: the die text does not promise what the re-run does: $out" ;; esac
[ "$(cat "$work/state/_boat4_cube_bathymetry")" = inactive ] || fail "case 22b: boat4 was not left inactive"
rm -f "$work/fail__boat4_cube_bathymetry_cleanup" "$work/log"
mark_service /boat4/cube_bathymetry
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 0 ] || fail "case 22b: re-running the same command did not recover: exit $rc: $out"
for n in "${cubes[@]}"; do
  [ "$(cat "$work/state/${n//\//_}")" = active ] || fail "case 22b: $n is not active after the re-run"
done

if [ "$failures" -ne 0 ]; then
  echo "$failures failure(s)"
  exit 1
fi
echo "test_reset_cube_nodes: all cases passed"
