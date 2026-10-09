#!/usr/bin/env bash
# Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
# Hydrographic Center, University of New Hampshire. MIT licence (see LICENSE).
#
# Tests scripts/reset_cube_nodes.sh against a stub `ros2` on PATH that keeps one
# lifecycle state per node (cube_bathymetry#164). Cases:
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
    v=$(cat "$STUB_DIR/param/$(key "$3")_$4" 2>/dev/null) || { echo "Parameter not set."; exit 1; }
    echo "String value is: $v"
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

failures=0
fail() { echo "FAIL: $*"; failures=$((failures + 1)); }

setup_nodes() {
  rm -f "$work"/log "$work"/fail_* "$work"/hang_* "$work"/noise "$work"/slow \
    "$work"/timeout_calls "$work"/onconfigure_* "$work"/state/* "$work"/param/*
  for n in boat1 boat2 boat3 boat4; do
    echo active > "$work/state/_${n}_cube_bathymetry"
    printf '' > "$work/param/_${n}_cube_bathymetry_draft_dir"
    printf '' > "$work/param/_${n}_cube_bathymetry_prior_store_dir"
  done
  echo active > "$work/state/_coverage_assessment"
}
cubes=(/boat1/cube_bathymetry /boat2/cube_bathymetry /boat3/cube_bathymetry /boat4/cube_bathymetry)

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

if [ "$failures" -ne 0 ]; then
  echo "$failures failure(s)"
  exit 1
fi
echo "test_reset_cube_nodes: all cases passed"
