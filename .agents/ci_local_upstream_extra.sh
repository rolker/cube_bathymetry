#!/usr/bin/env bash
# ci_local.sh hook: mirrors AFTER_SETUP_UPSTREAM_WORKSPACE in .github/workflows/ci.yml.
# Prune the unh_marine_autonomy packages that are outside cube_bathymetry's build
# graph. Runs inside the container after the upstream clones, cwd /ci/upstream_ws.
#
# Hosted CI also sets ROSDEP_SKIP_KEYS="marine_nav_interfaces marine_nav_tasks
# marine_ais_msgs" for the pruned packages' unresolvable keys. It is not mirrored
# in .agents/ci_local_rosdep_skip_keys.txt on purpose: ci_local.sh passes the keys
# to `rosdep install` through an unquoted heredoc that drops the quotes around a
# multi-key list, so rosdep reads the second key as a path. `rosdep install -r`
# reports the unresolved keys and carries on, and colcon never builds the pruned
# packages, so the result is the same.
for p in mission_manager mission_manager_interfaces marine_autonomy_integration_tests marine_web_view; do
  find . -type d -name "$p" -exec touch {}/COLCON_IGNORE \;
done
