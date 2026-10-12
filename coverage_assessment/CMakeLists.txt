cmake_minimum_required(VERSION 3.14.4)
project(coverage_assessment)

if(NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(grid_map_core REQUIRED)

# The core (polygon rasterising, per-source accumulation, judging) is plain
# C++ over grid_map_core: no ROS, no message types, no TF. The ROS wrapper
# (a later step of #164) converts messages and frames into the core's own
# structs, which keeps every judging rule unit-testable without a graph.
# Targets are added with the component each one builds.

if(BUILD_TESTING)
  find_package(ament_lint_auto REQUIRED)
  ament_lint_auto_find_test_dependencies()

  find_package(ament_cmake_gtest REQUIRED)
endif()

ament_package()
