#!/usr/bin/env bash
# Copyright 2026 Center for Coastal and Ocean Mapping & NOAA-UNH Joint
# Hydrographic Center, University of New Hampshire. MIT licence (see LICENSE).
#
# Tests scripts/reset_cube_nodes.sh against a stub `ros2` on PATH that keeps one
# lifecycle state per node (cube_bathymetry#164). Three cases:
#   1. consumer ordering: every CUBE node is quiesced before a consumer is touched,
#      and the consumer is back up before any CUBE node is configured.
#   2. a non-empty draft_dir is refused, and nothing is changed.
#   3. a failing transition fails fast, naming the node and the transition.
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
    case "$s" in unconfigured) id=1 ;; inactive) id=2 ;; active) id=3 ;; esac
    echo "$s [$id]"
    ;;
  "lifecycle set")
    node=$3 t=$4 f="$STUB_DIR/state/$(key "$3")"
    echo "$node $t" >> "$STUB_DIR/log"
    if [ -e "$STUB_DIR/fail_$(key "$node")_$t" ]; then
      echo "Transition failed"   # exit status 0, like the real verb can
      exit 0
    fi
    s=$(cat "$f")
    case "$s:$t" in
      active:deactivate) echo inactive > "$f" ;;
      inactive:cleanup) echo unconfigured > "$f" ;;
      unconfigured:configure) echo inactive > "$f" ;;
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
export STUB_DIR="$work" PATH="$work/bin:$PATH"

failures=0
fail() { echo "FAIL: $*"; failures=$((failures + 1)); }

setup_nodes() {
  rm -f "$work"/log "$work"/fail_* "$work"/state/* "$work"/param/*
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
  [ "$(cat "$work/state/$(echo "${n//\//_}")")" = active ] || fail "case 1: $n not active at the end"
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

# --- 2. draft_dir set: refuse, change nothing --------------------------------
setup_nodes
echo /data/draft > "$work/param/_boat2_cube_bathymetry_draft_dir"
out=$("$script" "${cubes[@]}" 2>&1); rc=$?
[ "$rc" -eq 1 ] || fail "case 2: expected exit 1, got $rc"
case "$out" in *"/boat2/cube_bathymetry"*"draft_dir"*) ;; *) fail "case 2: message does not name node and parameter: $out" ;; esac
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

if [ "$failures" -ne 0 ]; then
  echo "$failures failure(s)"
  exit 1
fi
echo "test_reset_cube_nodes: all cases passed"
