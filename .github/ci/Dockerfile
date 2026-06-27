# Prebaked CI dependency image for cube_bathymetry (Tier C, deps-only).
#
# Bakes the rosdep system deps for cube_bathymetry + its upstream source
# workspace so the per-PR industrial_ci run (DOCKER_IMAGE=this image) skips the
# apt dependency install — the slowest non-cacheable part of the warm build.
#
# It bakes DEPS ONLY: CI still clones + builds the upstream *sources* every run
# (with ccache), so the image never goes stale w.r.t. source. Only its baked
# system deps can lag, and two things cover that: the weekly rebuild, and the
# CI-time `rosdep install` (a fast no-op when a dep is already present, a real
# install when a dep was newly added since the last image build).
#
# Built + pushed to GHCR by .github/workflows/ci-image.yml.
FROM ros:jazzy-ros-base

SHELL ["/bin/bash", "-c"]

# Build tooling the industrial_ci run expects, plus ccache for the warm build.
RUN apt-get update && apt-get install -y --no-install-recommends \
        ccache \
        git \
        python3-colcon-common-extensions \
        python3-rosdep \
        python3-vcstool \
    && rm -rf /var/lib/apt/lists/*

RUN rosdep init 2>/dev/null || true
RUN rosdep update --rosdistro jazzy

# Clone the same upstream source deps CI uses (this MUST match cube's CI
# upstream.repos) plus this package's own manifest, then bake every resolvable
# rosdep system dep.
WORKDIR /tmp/dep-bake
COPY upstream.repos ./upstream.repos
COPY cube_bathymetry/package.xml ./cube_bathymetry/package.xml
RUN mkdir -p src && vcs import src < upstream.repos

# `rosdep install` is all-or-nothing on key resolution: a single unresolvable
# key aborts the whole install. This monorepo carries keys that don't resolve
# standalone — `nlohmann_json` (an upstream packaging bug,
# rolker/unh_marine_autonomy#228) and the `marine_nav_*` source deps (no
# manifest declares where to obtain them). So first compute the unresolvable
# keys via `rosdep check` and pass them as --skip-keys, so one bad key skips
# only itself instead of nuking the bake. `nlohmann-json3-dev` is then
# installed explicitly to cover the skipped (mis-named) `nlohmann_json` key
# until #228 lands and cube's upstream.repos picks up the fix.
RUN apt-get update \
    && skip_keys="$(rosdep check --from-paths src cube_bathymetry --ignore-src --rosdistro jazzy 2>&1 \
          | sed -n 's/.*Cannot locate rosdep definition for \[\([^]]*\)\].*/\1/p' \
          | sort -u | tr '\n' ' ' | sed 's/  *$//')" ; \
    echo "dep bake: skipping unresolvable keys: ${skip_keys:-<none>}" ; \
    rosdep install --from-paths src cube_bathymetry --ignore-src -y --rosdistro jazzy \
        ${skip_keys:+--skip-keys "$skip_keys"} ; \
    apt-get install -y --no-install-recommends nlohmann-json3-dev ; \
    rm -rf /tmp/dep-bake /var/lib/apt/lists/*

WORKDIR /
